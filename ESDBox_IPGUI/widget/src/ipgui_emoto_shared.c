/*
 * E-MOTION 两页共享状态 + 页面切换动画
 *
 * 切页动画不依赖 ipgui_screen 内部改动：
 *   - 每一页最上层放一块全屏遮罩控件；
 *   - 内置 ipgui_anim 驱动 0..1000 的进度；
 *   - 前半程旧页遮罩从左（或右）扫入，扫满时 ipgui_screen_page_switch；
 *   - 后半程新页遮罩继续扫出，露出新页面。
 */

#include "ipgui_emoto_shared.h"
#include "ipgui_widget.h"
#include "ipgui_animation.h"
#include "ipgui_draw_arc.h"
#include "ipgui_draw_box_background.h"
#include "ipgui_draw_line.h"
#include "ipgui_draw_builtin_font.h"
#include "ipgui_gradient_color.h"
#include "ipgui_dirty_rect.h"
#include "ipgui_membox.h"
#include "quicksand_medium.h"
#include "open_sans.h"

#include <string.h>

#define TR_MS  520

static ipgui_emoto_snapshot_t s_snap;

void ipgui_emoto_shared_publish(const ipgui_emoto_snapshot_t * s)
{
    if (!s) return;
    s_snap = *s;
    s_snap.valid = 1;
}

const ipgui_emoto_snapshot_t * ipgui_emoto_shared_get(void)
{
    return &s_snap;
}

/*=============================================================================
 * 转场状态
 *===========================================================================*/

static ipgui_scr_t * t_scr;
static u8_t          t_ride_page;
static u8_t          t_nav_page;
static u8_t          t_from;
static u8_t          t_to;
static u8_t          t_active;
static u8_t          t_switched;
static s8_t          t_dir;       /* +1: 左→右, -1: 右→左 */
static s32_t         t_cover;     /* 遮罩覆盖宽度 0..800 */

static ipgui_widget_t * t_overlay[SCREEN_PAGE_MAX];

/* 遮罩内部文字/进度只允许画在已经扫过的区域里 */
static ipgui_aabb_t * t_clip;

/*=============================================================================
 * 遮罩绘制
 *===========================================================================*/

static ipgui_color_t tr_rgb(u8_t r, u8_t g, u8_t b, u8_t a)
{
    ipgui_color_t c;
    c.r = r; c.g = g; c.b = b; c.a = a;
    return c;
}

static ipgui_color_t tr_accent(u8_t a)
{
    const ipgui_emoto_snapshot_t * s = ipgui_emoto_shared_get();
    static const u8_t MODE_ACCENT[3][3] = {
        { 155, 225,  93 },
        {  53, 224, 255 },
        {  91, 140, 255 },
    };
    u8_t m = (s && s->valid && s->mode < 3) ? s->mode : 1;
    return tr_rgb(MODE_ACCENT[m][0], MODE_ACCENT[m][1], MODE_ACCENT[m][2], a);
}

static void tr_draw_line(
    ipgui_surf_t * surf, ipgui_coord_t x1, ipgui_coord_t y1,
    ipgui_coord_t x2, ipgui_coord_t y2,
    ipgui_coord_t width, ipgui_color_t color, u8_t opacity,
    ipgui_line_cap_t cap)
{
    ipgui_line_t line;
    ipgui_line_style_t style;

    if (x1 == x2 && y1 == y2) return;

    line.start.x = x1;
    line.start.y = y1;
    line.end.x   = x2;
    line.end.y   = y2;

    memset(&style, 0, sizeof(style));
    style.width      = width;
    style.cap        = cap;
    style.paint.type = IPGUI_PAINT_COLOR;
    style.paint.src.color = color;
    style.opacity    = opacity;
    style.blend_mode = IPGUI_BLEND_NORMAL;

    ipgui_draw_line_generic(surf, t_clip, &line, &style);
}

static void tr_draw_arc(
    ipgui_surf_t * surf, ipgui_coord_t cx, ipgui_coord_t cy,
    ipgui_coord_t er, ipgui_coord_t ir,
    s32_t start, s32_t angle, ipgui_color_t color, u8_t opacity,
    ipgui_arc_endpoint_type_t cap)
{
    ipgui_arc_t arc;
    ipgui_arc_style_t style;

    if (angle <= 0 || er <= ir) return;

    arc.cx    = cx;
    arc.cy    = cy;
    arc.er    = er;
    arc.ir    = ir;
    arc.start = start;
    arc.dir   = IPGUI_ARC_DRAW_DIR_CCW;
    arc.angle = (u16_t)angle;

    memset(&style, 0, sizeof(style));
    style.paint.type = IPGUI_PAINT_COLOR;
    style.paint.src.color = color;
    style.opacity = opacity;
    style.blend_mode = IPGUI_BLEND_NORMAL;
    style.sep_type = cap;
    style.eep_type = cap;

    ipgui_draw_arc(surf, t_clip, &arc, &style);
}

static void tr_fill_round(
    ipgui_surf_t * surf, ipgui_coord_t x, ipgui_coord_t y,
    ipgui_coord_t w, ipgui_coord_t h, ipgui_coord_t r,
    ipgui_color_t color, u8_t opacity)
{
    ipgui_aabb_t box;
    ipgui_box_style_t bs;
    ipgui_box_bg_style_t bg;

    if (w <= 0 || h <= 0) return;

    box.start.x = x;
    box.start.y = y;
    box.end.x   = x + w - 1;
    box.end.y   = y + h - 1;

    memset(&bs, 0, sizeof(bs));
    bs.left_top_radius     = r;
    bs.right_top_radius    = r;
    bs.left_bottom_radius  = r;
    bs.right_bottom_radius = r;

    memset(&bg, 0, sizeof(bg));
    bg.paint.type = IPGUI_PAINT_COLOR;
    bg.paint.src.color = color;
    bg.opacity = opacity;
    bg.blend_mode = IPGUI_BLEND_NORMAL;

    ipgui_draw_box_background(surf, t_clip, &box, &bs, &bg);
}

static ipgui_coord_t tr_text_w(const ipgui_font_t * font, const char * text)
{
    if (!font || !text) return 0;
    return ipgui_builtin_text_width(font, (const s8_t *)text);
}

static void tr_draw_text(
    ipgui_surf_t * surf, const ipgui_font_t * font, const char * text,
    ipgui_coord_t x, ipgui_coord_t y, ipgui_color_t color, u8_t opacity)
{
    ipgui_font_style_t style;

    if (!font || !text || opacity < 3) return;

    memset(&style, 0, sizeof(style));
    style.font       = font;
    style.opacity    = opacity;
    style.paint.type = IPGUI_PAINT_COLOR;
    style.paint.src.color = color;
    style.blend_mode = IPGUI_BLEND_NORMAL;

    ipgui_draw_builtin_text(surf, t_clip, &style,
                            (const s8_t *)text, x, y);
}

static void tr_draw_text_center(
    ipgui_surf_t * surf, const ipgui_font_t * font, const char * text,
    ipgui_coord_t cx, ipgui_coord_t y, ipgui_color_t color, u8_t opacity)
{
    ipgui_coord_t w = tr_text_w(font, text);
    tr_draw_text(surf, font, text, cx - w / 2, y, color, opacity);
}

/*=============================================================================
 * 转场动画
 *===========================================================================*/

static ipgui_anim_value_t tr_ease(
    struct ipgui_anim_t * anim, ipgui_tick_t t, void * data)
{
    s64_t x, p, p2, v;

    (void)anim;
    (void)data;

    x = (s64_t)t;
    if (x < 0) x = 0;
    if (x > TR_MS) x = TR_MS;

    /* 归一化到 0..1000，再做 smoothstep: 3p² - 2p³ */
    p  = x * 1000 / TR_MS;
    p2 = p * p;
    v  = (3 * p2 * 1000 - 2 * p2 * p) / 1000000;

    return (ipgui_anim_value_t)v;
}

static void tr_set_visible(u8_t page, u8_t visible)
{
    ipgui_widget_t * w = t_overlay[page];

    if (!w) return;

    if (visible) {
        w->flags &= ~IPGUI_WIDGET_FLAG_INVISIBLE;
    } else {
        w->flags |= IPGUI_WIDGET_FLAG_INVISIBLE;
    }
    /* 转场期间不接受输入 */
    w->flags |= IPGUI_WIDGET_FLAG_DISABLED;
}

static void tr_mark_strip(s32_t old_cover, s32_t new_cover)
{
    s32_t band, x, w;

    if (!t_scr || old_cover == new_cover) return;

    /* 每帧重绘“当前遮罩覆盖过的整段区域”，而不是只重绘新扫过的窄条。
     * 这样遮罩内部不会出现旧页残留的竖条，颜色也始终均匀。 */
    band = (old_cover > new_cover) ? old_cover : new_cover;
    if (band <= 0) return;

    if (t_dir > 0) {
        x = 0;
        w = band;
    } else {
        x = IPGUI_EMOTO_W - band;
        w = band;
    }

    if (x < 0) { w += x; x = 0; }
    if (x + w > IPGUI_EMOTO_W) w = IPGUI_EMOTO_W - x;
    if (w <= 0) return;

    ipgui_dirty_rect_add_xywh(
        &t_scr->dirty, x, 0, w, IPGUI_EMOTO_H);
}

static void tr_path_cb(
    struct ipgui_anim_t * anim, ipgui_anim_value_t value, void * user)
{
    s32_t p = (s32_t)value;
    s32_t old_cover = t_cover;
    s32_t new_cover;

    (void)anim;
    (void)user;

    if (!t_active) return;
    if (p < 0) p = 0;
    if (p > 1000) p = 1000;

    if (p < 500) {
        new_cover = (p * 2) * IPGUI_EMOTO_W / 1000;
        t_cover = new_cover;
        tr_mark_strip(old_cover, new_cover);
        return;
    }

    if (!t_switched) {
        ipgui_screen_page_switch(t_scr, t_to);
        tr_set_visible(t_from, 0);
        tr_set_visible(t_to, 1);
        t_cover = IPGUI_EMOTO_W;
        ipgui_dirty_rect_add_xywh(
            &t_scr->dirty, 0, 0, IPGUI_EMOTO_W, IPGUI_EMOTO_H);
        t_switched = 1;
        old_cover = IPGUI_EMOTO_W;
    }

    new_cover = (1000 - p) * 2 * IPGUI_EMOTO_W / 1000;
    t_cover = new_cover;

    if (new_cover <= 0) {
        tr_set_visible(t_to, 0);
    }

    tr_mark_strip(old_cover, new_cover);
}

static void tr_finish_cb(struct ipgui_anim_t * anim, void * user)
{
    (void)anim;
    (void)user;

    tr_set_visible(t_from, 0);
    tr_set_visible(t_to, 0);

    t_active  = 0;
    t_switched = 0;
    t_cover   = 0;

    if (t_scr) {
        ipgui_dirty_rect_add_xywh(
            &t_scr->dirty, 0, 0, IPGUI_EMOTO_W, IPGUI_EMOTO_H);
    }
}

void ipgui_emoto_transition_setup(
    ipgui_scr_t * scr, u8_t ride_page, u8_t nav_page)
{
    t_scr       = scr;
    t_ride_page = ride_page;
    t_nav_page  = nav_page;
}

void ipgui_emoto_transition_register(u8_t page, ipgui_widget_t * overlay)
{
    if (page >= SCREEN_PAGE_MAX) return;
    t_overlay[page] = overlay;
}

u8_t ipgui_emoto_transition_active(void)
{
    /* 注意：本工程的 IPGUI_YES 是 0、IPGUI_NO 是 1，
     * 这里必须返回真正的布尔值 1/0，否则 if (...) 判断会反过来。 */
    return t_active ? 1 : 0;
}

ipgui_yes_no_t ipgui_emoto_transition_to(u8_t target_page)
{
    ipgui_anim_dsc_t dsc;
    ipgui_anim_t   * anim;
    ipgui_err_t      err;
    u8_t             from;

    if (!t_scr) return IPGUI_NO;
    if (target_page >= SCREEN_PAGE_MAX) return IPGUI_NO;
    if (target_page != t_ride_page && target_page != t_nav_page)
        return IPGUI_NO;
    if (t_active) return IPGUI_NO;
    if (target_page == t_scr->act_page) return IPGUI_NO;

    from = t_scr->act_page;

    /* 遮罩还没注册（异常路径）：退化为立即切页 */
    if (!t_overlay[from] || !t_overlay[target_page]) {
        ipgui_screen_page_switch(t_scr, target_page);
        return IPGUI_YES;
    }

    t_from     = from;
    t_to       = target_page;
    t_dir      = (target_page > from) ? 1 : -1;
    t_active   = 1;
    t_switched = 0;
    t_cover    = 0;

    /* 旧页遮罩入场，新页遮罩先隐藏 */
    tr_set_visible(t_from, 1);
    tr_set_visible(t_to, 0);

    memset(&dsc, 0, sizeof(dsc));
    dsc.anim_func = tr_ease;
    dsc.t1        = 0;
    dsc.t2        = TR_MS;
    dsc.loop_count = 1;
    dsc.path_cb   = tr_path_cb;
    dsc.finish_cb = tr_finish_cb;

    anim = ipgui_anim_create(&dsc);
    if (!anim) {
        tr_set_visible(t_from, 0);
        tr_set_visible(t_to, 0);
        t_active = 0;
        ipgui_screen_page_switch(t_scr, target_page);
        return IPGUI_YES;
    }

    err = ipgui_anim_start(anim);
    if (err != IPGUI_ERR_OK) {
        tr_set_visible(t_from, 0);
        tr_set_visible(t_to, 0);
        t_active = 0;
        ipgui_screen_page_switch(t_scr, target_page);
        return IPGUI_YES;
    }

    return IPGUI_YES;
}

void ipgui_emoto_transition_render(
    ipgui_widget_t * widget, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_surf_t * surf;
    ipgui_aabb_t box;
    ipgui_box_style_t bs;
    ipgui_box_bg_style_t bg;
    ipgui_coord_t x0, x1, ex;
    ipgui_coord_t y;
    s32_t pw;

    if (!ctx || !ctx->surf || !widget) return;
    if (!t_active || t_cover <= 0) return;

    surf = ctx->surf;
    t_clip = (ipgui_aabb_t *)0;

    if (t_dir > 0) {
        x0 = 0;
        x1 = t_cover - 1;
    } else {
        x0 = IPGUI_EMOTO_W - t_cover;
        x1 = IPGUI_EMOTO_W - 1;
    }

    if (x0 < 0) x0 = 0;
    if (x1 >= IPGUI_EMOTO_W) x1 = IPGUI_EMOTO_W - 1;
    if (x1 < x0) return;

    box.start.x = x0;
    box.start.y = 0;
    box.end.x   = x1;
    box.end.y   = IPGUI_EMOTO_H - 1;

    memset(&bs, 0, sizeof(bs));

    memset(&bg, 0, sizeof(bg));
    bg.paint.type = IPGUI_PAINT_COLOR;
    bg.paint.src.color = tr_rgb(0x08, 0x10, 0x18, 255);
    bg.opacity = 255;
    bg.blend_mode = IPGUI_BLEND_NORMAL;

    ipgui_draw_box_background(
        surf, (ipgui_aabb_t *)0, &box, &bs, &bg);

    /* 很淡的科技网格 */
    for (y = 0; y < IPGUI_EMOTO_H; y += 40) {
        tr_draw_line(surf, x0, y, x1, y, 1,
                     tr_rgb(0x1A, 0x2A, 0x3A, 255), 12,
                     IPGUI_LINE_CAP_BUTT);
    }
    for (y = x0 - (x0 % 80); y <= x1; y += 80) {
        tr_draw_line(surf, y, 0, y, IPGUI_EMOTO_H - 1, 1,
                     tr_rgb(0x1A, 0x2A, 0x3A, 255), 8,
                     IPGUI_LINE_CAP_BUTT);
    }

    /* 扫描边缘：宽辉光 + 2px 实线 */
    ex = (t_dir > 0) ? x1 : x0;
    tr_draw_line(surf, ex, 0, ex, IPGUI_EMOTO_H - 1, 12,
                 tr_accent(255), 26, IPGUI_LINE_CAP_BUTT);
    tr_draw_line(surf, ex, 0, ex, IPGUI_EMOTO_H - 1, 2,
                 tr_accent(255), 235, IPGUI_LINE_CAP_BUTT);

    /* 品牌与目标页文字：只画在已经扫过的区域内，
     * 否则文字会提前出现在尚未被遮罩覆盖的旧页上 */
    {
        ipgui_aabb_t panel_clip;
        panel_clip.start.x = x0;
        panel_clip.start.y = 0;
        panel_clip.end.x   = x1;
        panel_clip.end.y   = IPGUI_EMOTO_H - 1;
        t_clip = &panel_clip;

        tr_draw_arc(surf, 400, 230, 122, 121, 0, 360,
                    tr_accent(255), 16, IPGUI_ARC_ENDPOINT_TYPE_BUTT);

        tr_draw_text_center(surf, &quicksand_medium_24px,
                            "E-MOTION", 400, 192,
                            tr_accent(255), 225);
        tr_draw_text_center(surf, &open_sans_10px,
                            widget->name ? widget->name : "IPGUI HMI",
                            400, 226,
                            tr_rgb(0xD8, 0xE4, 0xF2, 255), 180);
        tr_draw_text_center(surf, &open_sans_7px,
                            "VECTOR  HMI  /  READY",
                            400, 246,
                            tr_rgb(0x6F, 0x84, 0x9B, 255), 150);

        /* 底部进度 */
        tr_fill_round(surf, 320, 300, 160, 2, 1,
                      tr_rgb(0x1A, 0x2A, 0x3A, 255), 120);
        pw = t_cover * 160 / IPGUI_EMOTO_W;
        if (pw > 0) {
            tr_fill_round(surf, 320, 300, pw, 2, 1,
                          tr_accent(255), 190);
        }

        t_clip = (ipgui_aabb_t *)0;
    }
}
