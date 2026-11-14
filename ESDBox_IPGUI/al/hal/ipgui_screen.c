#include "ipgui_screen.h"
#include "ipgui_rect_slice.h"
#include "ipgui_widget.h"
#include "ipgui_memory.h"
#include "ipgui_debug.h"
#include "ipgui_pattle.h"
#include "ipgui_blend_color.h"

__IPGUI_API__ void ipgui_scr_set_bg_color(ipgui_scr_t * scr, ipgui_color_t color)
{
    if (!scr)
        return;
    scr->bg_color = color;
}

__IPGUI_API__ ipgui_err_t ipgui_screen_init(ipgui_scr_t * scr, ipgui_scr_drv_t * drv)
{
    if (!scr || !drv)
        return IPGUI_ERR_NOK;
    ipgui_memset((void *)scr, 0, sizeof(ipgui_scr_t));
    scr->drv = drv;

    /* 初始化脏矩形管理器 */
    ipgui_dirty_rect_mgr_init(&scr->dirty);
    ipgui_dirty_rect_add_xywh(&scr->dirty, 
        0, 
        0, 
        scr->drv->xreso, 
        scr->drv->yreso);

    /* 初始化页面控件树：每页一棵独立的树，并回填所属屏幕 */
    {
        u32_t i;
        for (i = 0; i < SCREEN_PAGE_MAX; i++) {
            ipgui_widget_tree_init(&scr->pages[i]);
            scr->pages[i].scr = scr;
        }
    }
    scr->act_page = 0;

    /* set default background color: white and fully opaque */
    IPGUI_COLOR_SET(scr->bg_color, 255, IPGUI_COLOR_WHITE);

    return IPGUI_ERR_OK;
}

/* 切换显示页 */
__IPGUI_API__ void ipgui_screen_page_switch(ipgui_scr_t * scr, u8_t page)
{
    if (!scr || !scr->drv)
        return;

    if (page >= SCREEN_PAGE_MAX) {
        ipgui_dbg_error("page index is invalid\n");
        return;
    }

    if (scr->act_page == page)
        return;

    scr->act_page = page;

    /* 整屏置脏
     * 两页的控件布局无需求交才能算出差异，且旧页内容必须被背景覆盖，
     * 因此整屏重绘是唯一不会留残影的做法，代价也只是一次全屏刷新。
     */
    ipgui_dirty_rect_add_xywh(&scr->dirty,
        0,
        0,
        scr->drv->xreso,
        scr->drv->yreso);
}

/* 其中的pix_fmt可以随便定义，
 * 但是在flush到屏幕时需要正确解析，
 * 一般建议与物理屏幕的pix_fmt一致
 */
__IPGUI_API__ ipgui_err_t ipgui_scr_create_pfb(
    ipgui_scr_t   * scr, 
    u8_t          * buf, 
    u32_t           buf_size, 
    ipgui_pix_fmt_t pix_fmt)
{
    if (!scr || !buf || !buf_size) {
        ipgui_dbg_error("param is invalid\n");
        return IPGUI_ERR_PARAM;
    }

    if (pix_fmt >= PIX_FMT_MAX) {
        ipgui_dbg_error("pix_fmt is invalid\n");
        return IPGUI_ERR_PARAM;
    }

    if (scr->drv->xreso <= 0 || scr->drv->yreso <= 0) {
        ipgui_dbg_error("screen resolution is invalid\n");
        return IPGUI_ERR_PARAM;
    }

    u8_t * top_buf = buf + buf_size;
    u32_t valid_size;
    u8_t * pfb_buf = (u8_t *)IPGUI_ALIGN_U32(buf);
    
    if (pfb_buf >= top_buf) return IPGUI_ERR_NOK;

    valid_size = top_buf - pfb_buf;

    u8_t px_size = 0;
    switch ((u8_t)pix_fmt) {
        case PIX_FMT_RGB565:
        case PIX_FMT_BGR565:   px_size = 2; break;
        case PIX_FMT_RGB888:
        case PIX_FMT_BGR888:   px_size = 4; break;
        case PIX_FMT_RGBA8888:
        case PIX_FMT_BGRA8888: px_size = 4; break;
        default: return IPGUI_ERR_INVALID_PFB_PIX_FMT;
    }

    scr->pfb.num_pixs = valid_size / px_size;
    scr->pfb.color    = pfb_buf;
    scr->pfb.pix_fmt  = pix_fmt;
    scr->pfb.pix_size = px_size;

    /* clear buf */
    ipgui_memset(pfb_buf, 0, valid_size);

    return IPGUI_ERR_OK;
}

__IPGUI_STATIC__ void ipgui_screen_render_widget_dfs(
    struct widget_link_t * link,
    ipgui_surf_t         * surf,
    ipgui_aabb_t         * clip,
    ipgui_aabb_t         * parent_clip,
    void                 * user_data)
{
    if (!link) return;

    ipgui_widget_t * widget;
    struct widget_link_t ** child = &link->first_child;

    while (* child) {
        widget = ipgui_container_of(* child, ipgui_widget_t, link);

        /* 跳过不可见控件 */
        if (widget->flags & IPGUI_WIDGET_FLAG_INVISIBLE)
            goto _next;

        /* 控件全局 AABB */
        ipgui_aabb_t global_aabb;
        ipgui_widget_abs_pos(widget, &global_aabb);

        /* 与脏矩形求交 */
        ipgui_aabb_t intersect;
        if (0 != ipgui_aabb_overlap(&intersect, &global_aabb, clip))
            goto _next;

        /* 与父裁剪区求交 */
        if (parent_clip) {
            ipgui_aabb_t clipped;
            if (0 != ipgui_aabb_overlap(&clipped, &intersect, parent_clip))
                goto _next;
            intersect = clipped;
        }

        /* 计算子控件累积裁剪区 */
        ipgui_aabb_t child_clip;
        ipgui_aabb_t parent_global;
        ipgui_widget_local_to_global(widget, &parent_global);

        if (widget->flags & IPGUI_WIDGET_FLAG_OVERFLOW_VISIBLE) {
            child_clip = global_aabb;
        } else {
            child_clip = parent_global;
            if (parent_clip) {
                if (0 != ipgui_aabb_overlap(&child_clip, &child_clip, parent_clip))
                    child_clip = parent_global;
            }
        }

        /* intersect 转控件本地坐标，surf 指针偏移后调 render */
        {
            ipgui_surf_t              local_surf;
            ipgui_widget_render_ctx_t render_ctx;

            local_surf              = * surf;
            local_surf.surf.start.x = intersect.start.x - global_aabb.start.x;
            local_surf.surf.start.y = intersect.start.y - global_aabb.start.y;
            local_surf.surf.end.x   = intersect.end.x   - global_aabb.start.x;
            local_surf.surf.end.y   = intersect.end.y   - global_aabb.start.y;
            {
                u32_t col = (u32_t)(intersect.start.x - surf->surf.start.x) * surf->pix_size;
                u32_t row = (u32_t)(intersect.start.y - surf->surf.start.y) * surf->stride;
                local_surf.color = (u8_t*)surf->color + row + col;
            }

            render_ctx.surf      = &local_surf;
            render_ctx.user_data = user_data;
            if (widget->render) widget->render(widget, &render_ctx);
        }

        /* 递归子控件 */
        if ((* child)->first_child) {
            ipgui_aabb_t * sub_parent_clip =
                (widget->flags & IPGUI_WIDGET_FLAG_OVERFLOW_VISIBLE)
                ? parent_clip : &child_clip;
            ipgui_screen_render_widget_dfs(* child, surf, clip, sub_parent_clip, user_data);
        }

    _next:
        child = &((* child)->sib_next);
        if (* child == link->first_child) break;
    }
}

__IPGUI_STATIC__ void ipgui_screen_render_dirty_rect_slice(
    ipgui_scr_t * scr,
    ipgui_pfb_t * pfb,/* here pfb size is as same as dirty size */
    ipgui_aabb_t * dirty)
{
    /* clear pfb */
    ipgui_coord_t w, h;
    w = ipgui_aabb_width(dirty);
    h = ipgui_aabb_height(dirty);

    /* 构造surf：由pfb切片生成绘制表面（根级别：全局坐标 = 本地坐标） */
    ipgui_surf_t surf;
    surf.surf    = * dirty;                        /* 脏矩形区域（屏幕绝对坐标） */
    surf.color   = pfb->color;                    /* PFB 首地址 */
    surf.stride  = (u32_t)w * pfb->pix_size;      /* 每行字节跨度 */
    surf.pix_fmt = pfb->pix_fmt;
    surf.pix_size = pfb->pix_size;

    if (scr->render_bg) {
        scr->render_bg(scr, &surf);
    } else {
        /* 填充背景色 */
        ipgui_blend_color(
            &surf,
            (ipgui_aabb_t *)0,
            dirty,
            scr->bg_color,
            255,
            (u8_t *)0, /* no mask */
            (ipgui_aabb_t *)0, /* no mask buffer */
            IPGUI_BLEND_NORMAL);
    }

    ipgui_screen_render_widget_dfs(
        &scr->pages[scr->act_page].root, 
        &surf, 
        dirty, 
        (ipgui_aabb_t *)0, 
        (void *)0);

    /* 将pfb切片刷新到物理屏幕 */
    if (scr->drv && scr->drv->fill_region) {
        scr->drv->fill_region(
            scr,
            dirty->start.x,
            dirty->start.y,
            dirty->end.x,
            dirty->end.y,
            pfb->color,
            surf.stride);
    }
}

/* render dirty rect of screen */
__IPGUI_STATIC__ void ipgui_screen_render_dirty_rect(
    ipgui_scr_t * scr,
    ipgui_dirty_rect_t * dirty)
{
    ipgui_pfb_t * pfb = &scr->pfb;
    ipgui_aabb_t _dirty = {
        .start = {.x = dirty->x1, .y = dirty->y1}, 
        .end   = {.x = dirty->x2, .y = dirty->y2}
    };

    /* 将脏矩形区域切分成多个小块进行渲染
     * 直到全部遍历完为止
     */
    ipgui_rect_slice_ctx slice_ctx;
    ipgui_rect_slice_ctx_init(&slice_ctx, &_dirty, pfb->num_pixs);
    
    ipgui_aabb_t slice_rect;
    while (ipgui_get_rect_slice(&slice_ctx, &slice_rect)) {
        ipgui_screen_render_dirty_rect_slice(scr, pfb, &slice_rect);
    }
}

/* 
 * ipgui_screen_render
 * 屏幕渲染主入口——在每次帧刷新时调用。
 * 流程：
 *   1. 检查是否有脏区域，无则直接返回（零开销快速路径）
 *   2. 调用 ipgui_dirty_rect_flush() 做全局最优合并（减少渲染次数）
 *   3. 遍历合并后的脏矩形池，逐个调用 ipgui_screen_render_dirty_rect()
 *   4. 渲染完成后重置脏矩形管理器
 */
__IPGUI_API__ void ipgui_screen_render(ipgui_scr_t * scr)
{
    /* check if the screen have dirty region */
    if (scr->dirty.pool_num == 0) return;

    /* 渲染前最优合并一次 */
    ipgui_dirty_rect_flush(&scr->dirty);

    s32_t idx = 0;
    ipgui_dirty_rect_t * dirty; 
    for (; idx < scr->dirty.pool_num; idx ++) {
        dirty = &scr->dirty.pool[idx];
        ipgui_screen_render_dirty_rect(scr, dirty);
    }

    /* reset dirty rect manager */
    ipgui_dirty_rect_mgr_reset(&scr->dirty);

    ipgui_screen_flush(scr);
}

/*
 * hit-test 内部递归
 *
 * 从 root 的子控件中按 Z-order（从最上层到最下层）查找点 (x,y) 落在哪个控件上。
 *
 * 参数：
 *   root       : 当前层级父节点的 link（只遍历其 children）
 *   x, y       : 屏幕全局坐标
 *   parent_clip: 祖先链累积的可见区域（全局坐标），子控件必须在此区域内方可命中
 *
 * 遮挡与裁剪机制：
 *   Q1: 同层兄弟遮挡怎么处理？
 *       遍历顺序本身就是遮挡处理——从上往下遍历，上层先被检查，
 *       命中后立即 return，下层兄弟根本不会被问到。
 *   Q2: 父控件对子控件的裁剪怎么处理？
 *       visible = 控件全局AABB ∩ parent_clip，parent_clip 沿着递归向下传递并逐步收窄。
 *       子控件的可见区域不可能超过祖先累积裁剪区。
 *   Q3: INVISIBLE 控件能点击吗？
 *       能。INVISIBLE 只控制渲染，不影响 hit-test。DISABLED 才跳过。
 *       例如：声明一个 INVISIBLE 的全屏控件即可作为透明手势热区。
 *
 * 遍历规则：
 *   1. 同级从最后一个（最上层）往前（最下层）遍历，循环链表终止条件：child == first
 *   2. 跳过 DISABLED 控件（IPGUI_WIDGET_FLAG_DISABLED）
 *   3. 优先递归子控件（子控件画在父控件之上），子控件无人认领才返回父控件自身
 */
__IPGUI_STATIC__ ipgui_widget_t * ipgui_screen_point_on_recurse(
    struct widget_link_t  * root,
    ipgui_coord_t           x,
    ipgui_coord_t           y,
    const ipgui_aabb_t    * parent_clip)
{
    struct widget_link_t * first; /* Z-order最下层（first_child） */
    struct widget_link_t * child;

    first = root->first_child;
    if (!first)
        return (ipgui_widget_t *)0;

    /* 从最上层往最下层遍历（child = child->sib_prev），碰到first时终止 */
    child = first->sib_prev;

    for (;;) {
        ipgui_widget_t * w = ipgui_container_of(child, ipgui_widget_t, link);

        /* 跳过禁用的控件（INVISIBLE状态仅控制渲染，不影响点击） */
        if (!(w->flags & IPGUI_WIDGET_FLAG_DISABLED)) {
            ipgui_aabb_t aabb;
            ipgui_aabb_t visible;

            /* 控件全局包围盒 */
            ipgui_widget_abs_pos(w, &aabb);

            /* 与祖先累积裁剪区求交 = 控件实际可见区域 */
            if (parent_clip) {
                if (0 != ipgui_aabb_overlap(&visible, &aabb, parent_clip))
                    goto _next_sibling;
            } else {
                visible = aabb;
            }

            /* 点是否在可见区域内 */
            if (ipgui_point_in_aabb(x, y, &visible)) {

                /* 优先问子控件（子控件在上层） */
                ipgui_widget_t * hit = ipgui_screen_point_on_recurse(&w->link, x, y, &visible);
                if (hit)
                    return hit;

                /* 无子控件命中，返回当前控件 */
                return w;
            }
        }

    _next_sibling:
        if (child == first)
            break;
        child = child->sib_prev;
    }

    return (ipgui_widget_t *)0;
}

/* 屏幕上的hit-test：返回点 (x,y) 处最上层的控件 */
__IPGUI_API__ ipgui_widget_t * ipgui_screen_point_on(
    ipgui_scr_t  * scr,
    ipgui_coord_t  x,
    ipgui_coord_t  y)
{
    ipgui_aabb_t screen_aabb;

    if (!scr)
        return (ipgui_widget_t *)0;

    /* 检查点是否在屏幕范围内 */
    screen_aabb.start.x = 0;
    screen_aabb.start.y = 0;
    screen_aabb.end.x   = scr->drv->xreso - 1;
    screen_aabb.end.y   = scr->drv->yreso - 1;

    if (!ipgui_point_in_aabb(x, y, &screen_aabb))
        return (ipgui_widget_t *)0;

    /* 以屏幕作为初始parent_clip，从当前页的根节点开始递归 */
    return ipgui_screen_point_on_recurse(
        &scr->pages[scr->act_page].root, x, y, &screen_aabb);
}

extern void ipgui_widget_scroll_handler(ipgui_widget_t * widget, ipgui_widget_evt_t * evt);

__IPGUI_API__ void ipgui_screen_handle_widget_event(ipgui_scr_t * scr, ipgui_widget_evt_t * evt)
{
    (void)scr;/* 暂时用不到 */
    if (!evt) return;

    if (!evt->target) return;

    if (evt->target->flags & IPGUI_WIDGET_FLAG_SCROLLABLE) {
        ipgui_widget_scroll_handler(evt->target, evt);
    }

    /* 下面的代码调试用，
     * 只用于pressed_evt和released_evt和hover_evt这三个事件 
     */
    
    // ipgui_aabb_t aabb;
    // ipgui_widget_abs_pos(evt->target, &aabb);

    // const char * type_str =
    //     evt->type == IPGUI_WIDGET_EVENT_PRESSED  ? "按压" :
    //     evt->type == IPGUI_WIDGET_EVENT_RELEASED ? "释放" :
    //     evt->type == IPGUI_WIDGET_EVENT_HOVER    ? "悬停" : "未知";

    // const char * name = evt->target->name ? evt->target->name : "(无名)";

    // printf("══════════════════════════════════════════\n");
    // printf("[控件事件] 类型=%s\n", type_str);
    // printf("  目标控件: %s\n", name);
    // printf("  控件位置: x=%d y=%d w=%d h=%d\n",
    //     evt->target->x, evt->target->y, evt->target->w, evt->target->h);
    // printf("  全局包围盒: (%d,%d) ~ (%d,%d)\n",
    //     aabb.start.x, aabb.start.y, aabb.end.x, aabb.end.y);

    // switch (evt->type) {
    // case IPGUI_WIDGET_EVENT_PRESSED:
    //     printf("  按压坐标: (%d,%d)\n",
    //         evt->evt.pressed_evt.x, evt->evt.pressed_evt.y);
    //     printf("  首次按压: (%d,%d)\n",
    //         evt->evt.pressed_evt.first_press_x, evt->evt.pressed_evt.first_press_y);
    //     printf("  上次按压: (%d,%d)\n",
    //         evt->evt.pressed_evt.last_press_x, evt->evt.pressed_evt.last_press_y);
    //     break;
    // case IPGUI_WIDGET_EVENT_RELEASED:
    //     printf("  释放坐标: (%d,%d)\n",
    //         evt->evt.released_evt.x, evt->evt.released_evt.y);
    //     printf("  首次按压: (%d,%d)\n",
    //         evt->evt.released_evt.first_press_x, evt->evt.released_evt.first_press_y);
    //     break;
    // case IPGUI_WIDGET_EVENT_HOVER:
    //     printf("  悬停坐标: (%d,%d)\n",
    //         evt->evt.hover_evt.x, evt->evt.hover_evt.y);
    //     break;
    // default:
    //     break;
    // }

    // printf("──────────────────────────────────────────\n");

    /* 直接调用控件的事件回调处理事件 */
    if (evt->target && evt->target->event_handler) {
        evt->target->event_handler(evt->target, evt);//event_handler的第一个参数也是多余的可以删掉
    }

    /* 控件内部逻辑执行完，再通知业务监听 */
    if (evt->target)
        ipgui_widget_dispatch_listeners(evt->target, evt);
}

__IPGUI_API__ void ipgui_screen_putpixel(ipgui_scr_t * scr, ipgui_coord_t x, ipgui_coord_t y, u8_t * pix)
{
    if (scr && scr->drv && scr->drv->put_pixel) {
        scr->drv->put_pixel(scr, x, y, pix);
    }
}

__IPGUI_API__ void ipgui_screen_flush(ipgui_scr_t * scr)
{
    if (scr && scr->drv && scr->drv->flush) {
        scr->drv->flush(scr);
    }
}

__IPGUI_API__ void ipgui_screen_fill_region(ipgui_scr_t * scr, 
        ipgui_coord_t x1, ipgui_coord_t y1, ipgui_coord_t x2, ipgui_coord_t y2, 
        u8_t * pix_buf, s32_t stride)
{
    if (scr && scr->drv && scr->drv->fill_region) {
        scr->drv->fill_region(scr, x1, y1, x2, y2, pix_buf, stride);
    }
}

#if IPGUI_FPS_MONITOR_ON == 1
/* 计算FPS */
__IPGUI_API__ void ipgui_screen_fps(ipgui_scr_t * scr)
{
  ;
}
#endif