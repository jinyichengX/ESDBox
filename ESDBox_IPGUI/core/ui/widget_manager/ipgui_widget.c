#include "ipgui_widget.h"
#include "ipgui_memory.h"
#include "ipgui_defs.h"

#include "ipgui_screen.h"

extern ipgui_scr_t main_screen;

/* create widget */
__IPGUI_API__ ipgui_widget_t * ipgui_widget_create(ipgui_widget_t * parent)
{
    ipgui_widget_t * widget = (ipgui_widget_t *)
        ipgui_mem_alloc_def(sizeof(ipgui_widget_t));

    if (!widget) return (ipgui_widget_t *)0;

    /* init widget position and size */
    widget->x = 0;
    widget->y = 0;
    widget->w = 0;
    widget->h = 0;

    /* init scroll offset */
    widget->scroll_x = 0;
    widget->scroll_y = 0;

    /* init flags */
    widget->flags = IPGUI_WIDGET_FLAG_NONE;

    /* init widget tree link node */
    ipgui_widget_link_init(&widget->link);
    /* add to parent's child list */
    if (parent) {
        ipgui_widget_link_set_parent(&widget->link, &parent->link);
        // ipgui_widget_mark_dirty();
    } else {   
        /* 无父控件时挂到当前页的控件树根 */
        ipgui_widget_link_set_parent(
            &widget->link, 
            &main_screen.pages[main_screen.act_page].root);
    }
    
    /* init event handler and render callback */
    widget->event_handler = 0;
    widget->render        = 0;

#if IPGUI_WIDGET_LISTENER_MAX > 0
    widget->listener_num  = 0;
#endif

    return widget;
}

/* 设置渲染回调 */
__IPGUI_API__ void ipgui_widget_set_render(
    ipgui_widget_t * widget,
    void (*render)(struct ipgui_widget * widget, ipgui_widget_render_ctx_t * ctx))
{
    if (widget)
        widget->render = render;
}

/* 设置事件处理回调 */
__IPGUI_API__ void ipgui_widget_set_event_handler(
    ipgui_widget_t * widget,
    void (*handler)(struct ipgui_widget * widget, ipgui_widget_evt_t * evt))
{
    if (widget)
        widget->event_handler = handler;
}

// __IPGUI_API__ void ipgui_set_scroll_dir(ipgui_widget_t * widget, ipgui_scroll_dir_t dir)
// {
//     if (!widget) return;

//     widget->scroll_dir = dir;
//     if (dir == IPGUI_SCROLL_DIR_AUTO_XY) {
//         widget->scroll_auto_xy = 1;
//     } else {
//         widget->scroll_auto_xy = 0;
//     }
// }

/* 获取控件所在屏幕 */
__IPGUI_API__ ipgui_scr_t * ipgui_widget_get_screen(ipgui_widget_t * widget)
{
    if (!widget) return (ipgui_scr_t *)0;

    if (IPGUI_YES == ipgui_widget_link_is_detached(&widget->link))
        return (ipgui_scr_t *)0;

    struct widget_link_t * _root;
    struct widget_tree_t * _tree;
    _root = ipgui_widget_link_get_root(&widget->link);

    /* 根节点嵌入在 widget_tree_t.root 中，再由树的 scr 回指拿到屏幕
     * 一个屏幕有多棵树，不能用 ipgui_scr_t 的字段偏移直接反查
     */
    _tree = ipgui_container_of(_root, struct widget_tree_t, root);
    return _tree->scr;
}

__IPGUI_API__ void ipgui_widget_set_top(ipgui_widget_t * widget)
{
    if (!widget) return;
    ipgui_widget_link_set_last(&widget->link);
}

__IPGUI_API__ void ipgui_widget_set_bottom(ipgui_widget_t * widget)
{
    if (!widget) return;
    ipgui_widget_link_set_first(&widget->link);
}

__IPGUI_API__ void ipgui_widget_set_behind(ipgui_widget_t * widget, ipgui_widget_t * front)
{
    if (!widget || !front) return;
    ipgui_widget_link_insert_prev(&widget->link, &front->link);
}

__IPGUI_API__ void ipgui_widget_set_front(ipgui_widget_t * widget, ipgui_widget_t * behind)
{
    if (!widget || !behind) return;
    ipgui_widget_link_insert_next(&widget->link, &behind->link);
}

/**
 * @brief 计算控件在屏幕中的绝对像素坐标(AABB包围盒)，且不考虑被父控件裁剪
 * @param widget 目标控件句柄
 * @param r 输出参数：存储控件的屏幕绝对坐标包围盒
 *          -> start: 控件左上角绝对坐标
 *          -> end  : 控件右下角绝对坐标
 */
__IPGUI_API__ void ipgui_widget_abs_pos(ipgui_widget_t * widget, ipgui_aabb_t * r)
{
    if (!widget) return;

    /* if it is detached from tree, return */
    if(IPGUI_YES == ipgui_widget_link_is_detached(&widget->link))
        return;

    /* 初始化累加变量，先填入当前控件的相对坐标与大小 */
    ipgui_coord_t abs_x = widget->x;
    ipgui_coord_t abs_y = widget->y;

    /* 沿着树向上追溯，累加所有父控件的坐标偏移 */
    struct widget_link_t * _link = &widget->link;
    ipgui_widget_t * parent;
    while (_link->parent && (IPGUI_NO == ipgui_widget_link_is_root(_link->parent))) {
        parent = ipgui_container_of(_link->parent, ipgui_widget_t, link);
        
        abs_x += parent->x;
        abs_y += parent->y;

        if (parent->flags & IPGUI_WIDGET_FLAG_SCROLLABLE) {
            abs_x -= parent->scroll_x;
            abs_y -= parent->scroll_y;
        }
        
        _link = _link->parent;
    }

    r->start.x = abs_x;
    r->start.y = abs_y;
    r->end.x   = abs_x + widget->w - 1;
    r->end.y   = abs_y + widget->h - 1;
}

/* 将父控件局部坐标转为全局坐标系的 aabb */
__IPGUI_API__ void ipgui_widget_local_to_global(
    ipgui_widget_t * widget, ipgui_aabb_t * out)
{
    if (!widget || !out) return;

    out->start.x = widget->x;
    out->start.y = widget->y;
    out->end.x   = widget->x + widget->w - 1;
    out->end.y   = widget->y + widget->h - 1;

    if (IPGUI_YES == ipgui_widget_link_is_detached(&widget->link))
        return;

    struct widget_link_t * _link = &widget->link;
    ipgui_widget_t * parent;
    while (_link->parent && (IPGUI_NO == ipgui_widget_link_is_root(_link->parent))) {
        parent = ipgui_container_of(_link->parent, ipgui_widget_t, link);
        
        out->start.x += parent->x;
        out->start.y += parent->y;
        out->end.x   += parent->x;
        out->end.y   += parent->y;

        if (parent->flags & IPGUI_WIDGET_FLAG_SCROLLABLE) {
            out->start.x -= parent->scroll_x;
            out->start.y -= parent->scroll_y;
            out->end.x   -= parent->scroll_x;
            out->end.y   -= parent->scroll_y;
        }
        
        _link = _link->parent;
    }
}

__IPGUI_API__ void ipgui_widget_mark_dirty(ipgui_widget_t * widget)
{
    if (!widget) return;

    /* if it is detached from tree, return */
    if(IPGUI_YES == ipgui_widget_link_is_detached(&widget->link))
        return;

    /* 获取控件所在的树的根节点tree->link 
     * 再由根节点找到对应的屏幕
     */
    ipgui_scr_t * scr = ipgui_widget_get_screen(widget);
    if (!scr) return;

    /* 计算控件的绝对坐标 */
    ipgui_aabb_t dr;
    ipgui_widget_abs_pos(widget, &dr);

    /* clip with screen's resolution */
    ipgui_aabb_t scr_aabb;
    scr_aabb.start.x = 0;
    scr_aabb.start.y = 0;
    scr_aabb.end.x = scr->drv->xreso - 1;
    scr_aabb.end.y = scr->drv->yreso - 1;

    if(0 != ipgui_aabb_overlap(&dr, &dr, &scr_aabb))
        return;
    
    ipgui_dirty_rect_add(&scr->dirty, (ipgui_dirty_rect_t *)&dr);
}

__IPGUI_API__ void ipgui_widget_set_align(
    ipgui_widget_t *     widget, 
    ipgui_widget_align_t align,
    ipgui_coord_t        parent_w,
    ipgui_coord_t        parent_h)
{
    if (!widget) return;

    if(IPGUI_YES == ipgui_widget_link_is_detached(&widget->link))
        return;

    switch (align) {
        case IPGUI_WIDGET_ALIGN_LEFT_TOP:
            widget->x = 0;
            widget->y = 0;
            break;
        case IPGUI_WIDGET_ALIGN_TOP_CENTER:
            widget->x = (parent_w - widget->w) / 2;
            widget->y = 0;
            break;
        case IPGUI_WIDGET_ALIGN_RIGHT_TOP:
            widget->x = parent_w - widget->w;
            widget->y = 0;
            break;
        case IPGUI_WIDGET_ALIGN_LEFT_CENTER:
            widget->x = 0;
            widget->y = (parent_h - widget->h) / 2;
            break;
        case IPGUI_WIDGET_ALIGN_CENTER:
            widget->x = (parent_w - widget->w) / 2;
            widget->y = (parent_h - widget->h) / 2;
            break;
        case IPGUI_WIDGET_ALIGN_RIGHT_CENTER:
            widget->x = parent_w - widget->w;
            widget->y = (parent_h - widget->h) / 2;
            break;
        case IPGUI_WIDGET_ALIGN_LEFT_BOTTOM:
            widget->x = 0;
            widget->y = parent_h - widget->h;
            break;
        case IPGUI_WIDGET_ALIGN_BOTTOM_CENTER:
            widget->x = (parent_w - widget->w) / 2;
            widget->y = parent_h - widget->h;
            break;
        case IPGUI_WIDGET_ALIGN_RIGHT_BOTTOM:
            widget->x = parent_w - widget->w;
            widget->y = parent_h - widget->h;
            break;
    }
}

__IPGUI_STATIC__ void ipgui_widget_drag_scroll_handler(
    ipgui_widget_t     * widget, 
    ipgui_widget_evt_t * evt)
{
    ipgui_coord_t dx, dy;
    dx = evt->evt.pressed_evt.x - evt->evt.pressed_evt.last_press_x;
    dy = evt->evt.pressed_evt.y - evt->evt.pressed_evt.last_press_y;

    if (dx == 0 && dy == 0) return;

    switch (widget->scroll_dir) {
    case IPGUI_SCROLL_DIR_X:
        widget->scroll_x -= dx;
        break;
    case IPGUI_SCROLL_DIR_Y:
        widget->scroll_y -= dy;
        break;
    case IPGUI_SCROLL_DIR_GESTURE:
        widget->scroll_x -= dx;
        widget->scroll_y -= dy;
        break;
    // case IPGUI_SCROLL_DIR_AUTO_XY:
    //     if (IPGUI_ABS(dx) > IPGUI_ABS(dy)) {
    //         widget->scroll_dir = IPGUI_SCROLL_DIR_X;
    //         widget->scroll_x -= dx;
    //     } else {
    //         widget->scroll_dir = IPGUI_SCROLL_DIR_Y;
    //         widget->scroll_y -= dy;
    //     }
    //     break;
    }

    ipgui_widget_mark_dirty(widget);
}

/* user shouldn't call this function */
void ipgui_widget_scroll_handler(
    ipgui_widget_t     * widget, 
    ipgui_widget_evt_t * evt)
{
    if (evt->type == IPGUI_WIDGET_EVENT_PRESSED) {
        /* handle the drag scroll */
        ipgui_widget_drag_scroll_handler(widget, evt);
        /* stop the inertia scroll */
        ipgui_inertia_scroll_stop(widget);
        return;
    }

    if (IPGUI_WIDGET_EVENT_RELEASED != evt->type)
        return;

    // /* 如果是自动选择滚动轴，恢复自动选择 */
    // if (widget->scroll_auto_xy) {
    //     ipgui_set_scroll_dir(widget, IPGUI_SCROLL_DIR_AUTO_XY);
    // }

    /* get delta */
    ipgui_coord_t dx, dy;
    dx = evt->evt.released_evt.x - evt->evt.released_evt.prev_press_x;
    dy = evt->evt.released_evt.y - evt->evt.released_evt.prev_press_y;

    /* 如果用户反馈滚动太灵敏或者太迟钝，只需要修改x_dv和y_dv这两个值即可 */
    s32_t x_dv = dx / 2;//这里的5只是调试用，应该改为时间差
    s32_t y_dv = dy / 2;//这里的5只是调试用，应该改为时间差

    if (x_dv == 0 && y_dv == 0)
        return;

    switch (widget->scroll_dir) {
    case IPGUI_SCROLL_DIR_X:
        ipgui_scroll_start(widget, x_dv, 0); /* x 轴 */
        break;
    case IPGUI_SCROLL_DIR_Y:
        ipgui_scroll_start(widget, y_dv, 1); /* y 轴 */
        break;
    case IPGUI_SCROLL_DIR_GESTURE:
        ipgui_scroll_start(widget, x_dv, 0); /* x 轴 */
        ipgui_scroll_start(widget, y_dv, 1); /* y 轴 */
        break;
    default:
        break;
    }
}