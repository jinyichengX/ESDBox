/*
 * 盒子外阴影
 *
 * 设计思路：
 *   1. 阴影mask用整个圆角矩形mask三次均值卷积而来，所以整张mask可以复用，不用每帧重新卷积。
 *   2. 用LRU池缓存整个阴影mask，命中后每个控件只剩一次clip求交 + blend。
 *   3. 池子不设上限，能申请到多少内存就存多少；申请失败时淘汰最久未用的项，
 *      还失败就放弃这次阴影，不破坏已有缓存。（需要优化一下，之后谁要解码大图、渲染大号文字、申请大 mask，会先 OOM）
 *   4. 模糊用居中对称核（_centered版本），保证四边扩散对称。
 */

#include "ipgui_draw_box_shadow.h"
#include "ipgui_blur.h"
#include "ipgui_ring_mask.h"
#include "ipgui_memory.h"
#include "ipgui_debug.h"

/* 3 趟 box blur 近似高斯 */
#define BOX_SHADOW_BLUR_PASSES     3

/* 对齐 mask 起始地址需要的额外字节 */
#define BOX_SHADOW_ALIGN_PAD       8U

/* 模糊半径上限：再大在屏上也看不出来，同时防止 pad / 尺寸计算溢出 s32 */
#define BOX_SHADOW_BLUR_MAX        1024

/* 单张 mask 的边长上限：保证 mw * mh 不会溢出 s32 / u32 */
#define BOX_SHADOW_SIDE_MAX        16384

/*-----------------------------------------------------------------------------
 * 缓存项：结构体 + mask 数据同一块内存，mask 指向结构体之后的地址
 *-----------------------------------------------------------------------------*/
typedef struct {
    struct list_head  node;

    /* ---- key：只和几何相关，颜色 / 透明度在 blend 阶段才生效 ---- */
    ipgui_coord_t     core_w;
    ipgui_coord_t     core_h;
    ipgui_coord_t     r_lt;
    ipgui_coord_t     r_rt;
    ipgui_coord_t     r_lb;
    ipgui_coord_t     r_br;
    ipgui_coord_t     kn;

    /* ---- payload ---- */
    ipgui_coord_t     pad;
    ipgui_coord_t     w;
    ipgui_coord_t     h;
    u8_t            * mask;
    u32_t             last_used_tick;
    u16_t             refcnt;
} shadow_mask_t;

static LIST_HEAD(g_shadow_cache);
static u32_t g_shadow_tick = 0;

/*-----------------------------------------------------------------------------
 * 时间戳规整：防止 tick 溢出后比较失效
 *-----------------------------------------------------------------------------*/
static void shadow_cache_normalize_tick(void)
{
    u32_t prev_tick = 0;
    u32_t new_tick  = 1;

    while (1) {
        struct list_head * iter;
        shadow_mask_t    * best      = (shadow_mask_t *)0;
        u32_t              best_tick = 0xffffffffU;

        list_for_each(iter, &g_shadow_cache) {
            shadow_mask_t * item = list_entry(iter, shadow_mask_t, node);
            if ((item->last_used_tick > prev_tick) &&
                (item->last_used_tick < best_tick)) {
                best_tick = item->last_used_tick;
                best      = item;
            }
        }

        if (!best) break;

        prev_tick = best_tick;
        best->last_used_tick = new_tick ++;
    }

    g_shadow_tick = new_tick - 1;
}

/* 命中后刷新时间戳，并挪到链表头部加快下次查找 */
static void shadow_cache_touch(shadow_mask_t * item)
{
    list_del(&item->node);
    list_add(&item->node, &g_shadow_cache);

    if (g_shadow_tick >= 0xffffff00U) shadow_cache_normalize_tick();
    item->last_used_tick = ++ g_shadow_tick;
}

/* 淘汰一个未被引用的最老项，返回 1 表示淘汰成功 */
static int shadow_cache_evict_one(void)
{
    struct list_head * iter;
    shadow_mask_t    * oldest = (shadow_mask_t *)0;

    list_for_each(iter, &g_shadow_cache) {
        shadow_mask_t * item = list_entry(iter, shadow_mask_t, node);
        if (item->refcnt) continue;
        if (!oldest || item->last_used_tick < oldest->last_used_tick) {
            oldest = item;
        }
    }

    if (!oldest) return 0;

    list_del(&oldest->node);
    ipgui_mem_free_def(oldest);
    return 1;
}

/*-----------------------------------------------------------------------------
 * 几何参数
 *-----------------------------------------------------------------------------*/

/* box blur 核长：居中核只接受奇数 */
static ipgui_coord_t shadow_kernel(ipgui_coord_t blur)
{
    ipgui_coord_t kn;

    if (blur <= 0) return 1;

    kn = blur / 2 + 1;
    if (kn < 3) kn = 3;
    if ((kn & 1) == 0) kn ++;

    return kn;
}

/* 3 趟居中 blur 的扩散半径 */
static ipgui_coord_t shadow_blur_pad(ipgui_coord_t kn)
{
    return (kn <= 1) ? 0 : (3 * (kn / 2));
}

/* 圆角半径 clamp，口径与 ipgui_draw_box_background 的 get_max_radius 一致 */
static ipgui_coord_t shadow_clamp_radius(ipgui_coord_t r,
    ipgui_coord_t cw, ipgui_coord_t ch)
{
    ipgui_coord_t rmax = IPGUI_MIN(cw >> 1, ch >> 1);

    if (r > rmax) r = rmax;
    if (r < 0)    r = 0;

    return r;
}

/*-----------------------------------------------------------------------------
 * 生成圆角矩形 mask（带抗锯齿）
 *   mask 尺寸 mw × mh，核心矩形位于 (pad, pad)，尺寸 cw × ch，
 *   核心之外全部为 0（模糊时向四周扩散）
 *-----------------------------------------------------------------------------*/
static void shadow_mask_fill(
    u8_t         * mask,
    ipgui_coord_t  mw, ipgui_coord_t mh,
    ipgui_coord_t  pad,
    ipgui_coord_t  cw, ipgui_coord_t ch,
    ipgui_coord_t  r_lt, ipgui_coord_t r_rt,
    ipgui_coord_t  r_lb, ipgui_coord_t r_br)
{
    ipgui_coord_t i;

    /* 整块清零，再把核心矩形填成 255 */
    ipgui_memset(mask, 0, (u32_t)(mw * mh));
    for (i = 0; i < ch; i ++) {
        ipgui_memset(mask + (uintptr_t)(pad + i) * mw + pad, 255, (u32_t)cw);
    }

    /* 四角用 ring mask 覆盖成覆盖率，坐标系与 draw_box_background 一致
     * step = -1：逻辑 x 从 r 递减到 1（左半区）
     * step = +1：逻辑 x 从 1 递增到 r（右半区）
     */
    for (i = 0; i < r_lt; i ++) {
        u8_t * row = mask + (uintptr_t)(pad + i) * mw + pad;
        ipgui_fetch_ring_mask(r_lt, r_lt - i, 0, r_lt, -1, row, (u16_t)r_lt);
    }
    for (i = 0; i < r_rt; i ++) {
        u8_t * row = mask + (uintptr_t)(pad + i) * mw + (pad + cw - r_rt);
        ipgui_fetch_ring_mask(1, r_rt - i, 0, r_rt, 1, row, (u16_t)r_rt);
    }
    for (i = 0; i < r_lb; i ++) {
        u8_t * row = mask + (uintptr_t)(pad + ch - r_lb + i) * mw + pad;
        ipgui_fetch_ring_mask(r_lb, i + 1, 0, r_lb, -1, row, (u16_t)r_lb);
    }
    for (i = 0; i < r_br; i ++) {
        u8_t * row = mask + (uintptr_t)(pad + ch - r_br + i) * mw
                     + (pad + cw - r_br);
        ipgui_fetch_ring_mask(1, i + 1, 0, r_br, 1, row, (u16_t)r_br);
    }
}

/* 3 趟分离式居中 box blur，原地完成，不需要第二块全尺寸 buffer
 * 返回 0 成功；非 0 表示行列临时缓冲申请失败（此时 mask 只模糊了一部分，
 * 调用方必须丢弃，不能进缓存） */
static int shadow_mask_blur(ipgui_mask_surface_t * surf, ipgui_coord_t kn)
{
    int pass;

    if (kn <= 1) return 0;

    for (pass = 0; pass < BOX_SHADOW_BLUR_PASSES; pass ++) {
        if (ipgui_average_blur_hor_centered(surf, kn, surf) != 0) return -1;
        if (ipgui_average_blur_ver_centered(surf, kn, surf) != 0) return -1;
    }

    return 0;
}

/* 当前缓存总字节，用于判断“就算全清空也放不下”的情况 */
static u32_t shadow_cache_bytes(void)
{
    struct list_head * iter;
    u32_t              total = 0;

    list_for_each(iter, &g_shadow_cache) {
        shadow_mask_t * item = list_entry(iter, shadow_mask_t, node);
        total += (u32_t)item->w * (u32_t)item->h
                 + (u32_t)sizeof(shadow_mask_t);
    }

    return total;
}

/*-----------------------------------------------------------------------------
 * 取（或生成）一块阴影 mask
 *   命中返回缓存项，refcnt 由调用方维护
 *-----------------------------------------------------------------------------*/
static shadow_mask_t * shadow_mask_acquire(
    ipgui_coord_t cw, ipgui_coord_t ch,
    ipgui_coord_t r_lt, ipgui_coord_t r_rt,
    ipgui_coord_t r_lb, ipgui_coord_t r_br,
    ipgui_coord_t kn)
{
    shadow_mask_t      * item;
    ipgui_mask_surface_t surf;
    struct list_head   * iter;
    ipgui_coord_t        pad;
    ipgui_coord_t        mw, mh;
    u32_t                need;

    /* 查缓存 */
    list_for_each(iter, &g_shadow_cache) {
        item = list_entry(iter, shadow_mask_t, node);
        if ((item->core_w == cw) && (item->core_h == ch) &&
            (item->r_lt == r_lt) && (item->r_rt == r_rt) &&
            (item->r_lb == r_lb) && (item->r_br == r_br) &&
            (item->kn   == kn)) {
            shadow_cache_touch(item);
            return item;
        }
    }

    /* 未命中：算几何、腾空间、生成 */
    pad = shadow_blur_pad(kn);
    mw  = cw + pad * 2;
    mh  = ch + pad * 2;

    /* 尺寸兜底：防止 mw * mh 溢出后才去分配，导致 buffer 比实际写入小 */
    if ((mw <= 0) || (mh <= 0) ||
        (mw > BOX_SHADOW_SIDE_MAX) || (mh > BOX_SHADOW_SIDE_MAX)) {
        return (shadow_mask_t *)0;
    }

    need = (u32_t)mw * (u32_t)mh + (u32_t)sizeof(shadow_mask_t)
           + BOX_SHADOW_ALIGN_PAD;

    /* 先直接申请；申请不到就按 LRU 淘汰未被引用的项再重试。
     * 如果连整个池子清空都不够，就直接放弃，避免为了一个画不出来的阴影
     * 把其他缓存全部拆掉 */
    item = (shadow_mask_t *)ipgui_mem_alloc_def(need);
    while (!item) {
        /* 池子全清空也放不下，或者已经没有可淘汰的项 */
        if (need > shadow_cache_bytes()) break;
        if (!shadow_cache_evict_one())      break;
        item = (shadow_mask_t *)ipgui_mem_alloc_def(need);
    }

    if (!item) {
        ipgui_dbg_error("error: box shadow mask alloc failed, %d bytes\r\n",
                        (int)need);
        return (shadow_mask_t *)0;
    }

    ipgui_memset(item, 0, (u32_t)sizeof(shadow_mask_t));

    item->mask = (u8_t *)IPGUI_ALIGN(
        (uintptr_t)item + sizeof(shadow_mask_t), sizeof(uintptr_t));

    item->core_w = cw;
    item->core_h = ch;
    item->r_lt   = r_lt;
    item->r_rt   = r_rt;
    item->r_lb   = r_lb;
    item->r_br   = r_br;
    item->kn     = kn;
    item->pad    = pad;
    item->w      = mw;
    item->h      = mh;
    item->refcnt = 0;

    shadow_mask_fill(item->mask, mw, mh, pad, cw, ch,
                     r_lt, r_rt, r_lb, r_br);

    surf.mask = item->mask;
    surf.w    = mw;
    surf.h    = mh;

    /* 模糊中途申请不到行列缓冲时，mask 只有一半被处理过，
     * 这种半成品不能进缓存，直接丢掉，下一帧再试 */
    if (0 != shadow_mask_blur(&surf, kn)) {
        ipgui_mem_free_def(item);
        return (shadow_mask_t *)0;
    }

    list_head_init(&item->node);
    list_add(&item->node, &g_shadow_cache);

    if (g_shadow_tick >= 0xffffff00U) shadow_cache_normalize_tick();
    item->last_used_tick = ++ g_shadow_tick;

    return item;
}

/* 只解除引用，内存交给 LRU 在申请失败时回收 */
static void shadow_mask_release(shadow_mask_t * item)
{
    if (!item) return;

    if (item->refcnt) item->refcnt --;
}

/*-----------------------------------------------------------------------------
 * 对外接口
 *-----------------------------------------------------------------------------*/
__IPGUI_API__ void ipgui_draw_box_outer_shadow(
    ipgui_surf_t                    * surf,
    ipgui_aabb_t                    * clip,
    ipgui_aabb_t                    * box,
    ipgui_box_style_t               * style,
    ipgui_box_outer_shadow_style_t  * shadow_style)
{
    ipgui_aabb_t        core, mask_aabb, dest;
    shadow_mask_t     * item;
    ipgui_coord_t       blur, spread;
    ipgui_coord_t       cw, ch, pad, kn;
    ipgui_coord_t       r_lt, r_rt, r_lb, r_br;

    if (!surf || !box || !shadow_style) return;
    if (shadow_style->opacity < 3) return;

    blur   = shadow_style->blur;
    spread = shadow_style->spread;
    if (blur   < 0) blur   = 0;
    if (spread < 0) spread = 0;
    if (blur   > BOX_SHADOW_BLUR_MAX) blur = BOX_SHADOW_BLUR_MAX;

    /* 完全落在盒子背后，画了也看不见 */
    if (blur == 0 && spread == 0 &&
        shadow_style->offset_x == 0 && shadow_style->offset_y == 0) {
        return;
    }

    /* 核心矩形 = padding box + offset ± spread */
    core.start.x = box->start.x - (style ? style->left_padding : 0)
                   + shadow_style->offset_x - spread;
    core.start.y = box->start.y - (style ? style->top_padding : 0)
                   + shadow_style->offset_y - spread;
    core.end.x   = box->end.x   + (style ? style->right_padding : 0)
                   + shadow_style->offset_x + spread;
    core.end.y   = box->end.y   + (style ? style->bottom_padding : 0)
                   + shadow_style->offset_y + spread;

    cw = ipgui_aabb_width (&core);
    ch = ipgui_aabb_height(&core);
    if (cw <= 0 || ch <= 0) return;

    /* spread 会把阴影盒整体外扩，圆角必须同量外扩（CSS box-shadow 的规则），
     * 否则圆角外侧会露出方角阴影芯，在控件圆角缺口处形成一块深色三角 */
    r_lt = style ? style->left_top_radius     : 0;
    r_rt = style ? style->right_top_radius    : 0;
    r_lb = style ? style->left_bottom_radius  : 0;
    r_br = style ? style->right_bottom_radius : 0;
    if (spread > 0) {
        if (r_lt > 0) r_lt += spread;
        if (r_rt > 0) r_rt += spread;
        if (r_lb > 0) r_lb += spread;
        if (r_br > 0) r_br += spread;
    }
    r_lt = shadow_clamp_radius(r_lt, cw, ch);
    r_rt = shadow_clamp_radius(r_rt, cw, ch);
    r_lb = shadow_clamp_radius(r_lb, cw, ch);
    r_br = shadow_clamp_radius(r_br, cw, ch);

    kn  = shadow_kernel(blur);
    pad = shadow_blur_pad(kn);

    /* mask 覆盖的全局区域：核心矩形向外扩 pad */
    mask_aabb.start.x = core.start.x - pad;
    mask_aabb.start.y = core.start.y - pad;
    mask_aabb.end.x   = core.end.x   + pad;
    mask_aabb.end.y   = core.end.y   + pad;

    /* 只有和 surf / clip 的交集需要真正混合，mask 本身保持完整不被裁剪 */
    if (0 != ipgui_aabb_overlap(&dest, &mask_aabb, &surf->surf)) return;
    if (clip) {
        if (0 != ipgui_aabb_overlap(&dest, &dest, clip)) return;
    }

    item = shadow_mask_acquire(cw, ch, r_lt, r_rt, r_lb, r_br, kn);
    if (!item) return;

    item->refcnt ++;

    ipgui_blend(surf, clip, &dest, &shadow_style->paint,
                shadow_style->opacity, item->mask, &mask_aabb,
                IPGUI_BLEND_NORMAL);

    shadow_mask_release(item);
}
