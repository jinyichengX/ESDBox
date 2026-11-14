#ifndef IPGUI_WIDGET_H
#define IPGUI_WIDGET_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ipgui_utils.h"
#include "ipgui_types.h"
#include "ipgui_conf.h"
#include "ipgui_prim.h"
#include "ipgui_widget_evt.h"
#include "ipgui_widget_tree.h"
#include "ipgui_core.h"
#include "ipgui_scroll.h"

typedef struct ipgui_scr_ctx ipgui_scr_t;

/*
 * 渲染上下文
 *
 * 作为 render 回调的参数传递给控件，包含此次渲染所需的所有环境信息。
 *
 * ===== 坐标空间：控件本地坐标系（Option C） =====
 *
 * surf、clip、parent_clip 均处于以当前控件左上角为原点 (0,0) 的本地坐标系。
 *
 * 控件绘制时从 (0,0) 开始，widget->w 和 widget->h 即为画布边界。
 * 无需调用 ipgui_widget_abs_pos()——所有坐标操作均在本地方格内完成。
 *
 * 设计原则：
 *   - surf   : 目标绘制表面（PFB 切片），surf.surf 已平移到控件本地坐标
 *   - clip   : 脏矩形区域，已平移到控件本地坐标
 *   - parent_clip : 父控件链累积的裁剪区，已平移到控件本地坐标，可空
 *   - user_data    : 预留给未来扩展（动画帧数据、主题上下文等）
 *
 * 缓冲区偏移计算公式（ipgui_surf_color_get 内部）：
 *   offset = (y - surf.surf.start.y) * stride + (x - surf.surf.start.x) * pix_size
 * 只要 x, y 与 surf.surf.start 在同一坐标空间，该公式即正确。
 */
typedef struct {
    ipgui_surf_t   * surf;        /* 目标绘制表面（控件本地坐标） */
    void           * user_data;   /* 预留扩展数据 */
} ipgui_widget_render_ctx_t;

/* 控件标志位（位掩码，控制渲染与布局行为）*/
typedef enum {
    IPGUI_WIDGET_FLAG_NONE              = 0x0000,
    IPGUI_WIDGET_FLAG_INVISIBLE         = 0x0001,  /* 不可见：跳过渲染，但保留布局空间 */
    IPGUI_WIDGET_FLAG_OVERFLOW_VISIBLE  = 0x0002,  /* 子控件可超出自身边界绘制（默认裁剪） */
    IPGUI_WIDGET_FLAG_DISABLED          = 0x0004,  /* 禁用：不响应事件 */
    IPGUI_WIDGET_FLAG_SCROLLABLE        = 0x0008,  /* 可滚动 */
} ipgui_widget_flag_t;

/* 控件在父控件中的对齐方式 */
typedef enum {
    IPGUI_WIDGET_ALIGN_LEFT_TOP,                   /* 左上对齐 */
    IPGUI_WIDGET_ALIGN_TOP_CENTER,                 /* 上中对齐 */
    IPGUI_WIDGET_ALIGN_RIGHT_TOP,                  /* 右上对齐 */
    IPGUI_WIDGET_ALIGN_LEFT_CENTER,                /* 左中对齐 */
    IPGUI_WIDGET_ALIGN_CENTER,                     /* 居中对齐 */
    IPGUI_WIDGET_ALIGN_RIGHT_CENTER,               /* 右中对齐 */
    IPGUI_WIDGET_ALIGN_LEFT_BOTTOM,                /* 左下对齐 */
    IPGUI_WIDGET_ALIGN_BOTTOM_CENTER,              /* 下中对齐 */
    IPGUI_WIDGET_ALIGN_RIGHT_BOTTOM,               /* 右下对齐 */
} ipgui_widget_align_t;

typedef enum {
    // IPGUI_SCROLL_DIR_AUTO_XY = 0, /* 自动选择滚动轴x或y */
    IPGUI_SCROLL_DIR_X,           /* 沿x轴滚动 */
    IPGUI_SCROLL_DIR_Y,           /* 沿y轴滚动 */
    IPGUI_SCROLL_DIR_GESTURE,     /* 沿手势滚动 */
} ipgui_scroll_dir_t;

/* 业务监听槽数量见 ipgui_conf.h 的 IPGUI_WIDGET_LISTENER_MAX */
typedef void (*ipgui_widget_listener_fn)(
    ipgui_widget_t * widget,
    ipgui_widget_evt_t * evt,
    void * user);

typedef struct {
    ipgui_widget_listener_fn fn;
    void * user;
} ipgui_widget_listener_slot_t;

typedef struct ipgui_widget
{
    /* 扩展数据 */
    void                 * priv_data;

    /* 控件树节点 */
    struct widget_link_t   link;

    /* 位置和大小（父控件局部坐标系） */
    ipgui_coord_t          x, y;
    ipgui_coord_t          w, h;

    /* 滚动控制（仅 SCROLLABLE 控件有效） */
    ipgui_coord_t          scroll_x, scroll_y;
    ipgui_scroll_dir_t     scroll_dir;
    ipgui_scroll_t         x_scroll;       /* X 轴惯性滚动状态 */
    ipgui_scroll_t         y_scroll;       /* Y 轴惯性滚动状态 */

    /* 标志位 */
    u32_t                  flags;

    /* 控件名称（调试用） */
    const char           * name;

    /* 绘制回调 */
    void (*render)       (struct ipgui_widget * widget, ipgui_widget_render_ctx_t * ctx);

    /* 事件处理回调，控件作者使用 */
    void (*event_handler)(struct ipgui_widget * widget, ipgui_widget_evt_t * evt);

#if IPGUI_WIDGET_LISTENER_MAX > 0
    /* 业务监听，控件使用者使用 */
    ipgui_widget_listener_slot_t listener[IPGUI_WIDGET_LISTENER_MAX];
    u8_t                         listener_num;
#endif
} ipgui_widget_t;

/* 创建控件并挂载到 parent（parent 为 NULL 则创建游离控件，后续需手动挂载） */
extern __IPGUI_API__ ipgui_widget_t * ipgui_widget_create(ipgui_widget_t * parent);

/* 设置渲染回调 */
extern __IPGUI_API__ void ipgui_widget_set_render(
    ipgui_widget_t * widget,
    void (*render)(struct ipgui_widget * widget, ipgui_widget_render_ctx_t * ctx));

/* 设置事件处理回调 */
extern __IPGUI_API__ void ipgui_widget_set_event_handler(
    ipgui_widget_t * widget,
    void (*handler)(struct ipgui_widget * widget, ipgui_widget_evt_t * evt));

/* 获取控件在屏幕中的绝对坐标（不考虑父控件裁剪） */
extern __IPGUI_API__ void ipgui_widget_abs_pos(ipgui_widget_t * widget, ipgui_aabb_t * r);

/* 将控件置于最顶层（先拆后插到父控件子链表末尾） */
extern __IPGUI_API__ void ipgui_widget_set_top(ipgui_widget_t * widget);

/* 将控件置于最底层层（先拆后插到父控件子链表头） */
extern __IPGUI_API__ void ipgui_widget_set_bottom(ipgui_widget_t * widget);

/* 将控件置于指定控件之前（先拆后插到指定控件之前） */
extern __IPGUI_API__ void ipgui_widget_set_behind(ipgui_widget_t * widget, ipgui_widget_t * front);

/* 将控件置于指定控件之后（先拆后插到指定控件之后） */
extern __IPGUI_API__ void ipgui_widget_set_front(ipgui_widget_t * widget, ipgui_widget_t * front);

/* 标记控件为脏，触发所属屏幕的脏矩形重绘 */
extern __IPGUI_API__ void ipgui_widget_mark_dirty(ipgui_widget_t * widget);

/* 获取控件所在屏幕（通过 tree.root 反向定位） */
extern __IPGUI_API__ ipgui_scr_t * ipgui_widget_get_screen(ipgui_widget_t * widget);

/* 将父控件局部坐标转为全局坐标系的 aabb */
extern __IPGUI_API__ void ipgui_widget_local_to_global(
    ipgui_widget_t * widget, ipgui_aabb_t * out);

/* 注册一个监听；同一 (fn, user) 重复注册是幂等的 */
extern __IPGUI_API__ ipgui_err_t ipgui_widget_listen(
    ipgui_widget_t * widget, ipgui_widget_listener_fn fn, void * user);

/* 注销一个监听 */
extern __IPGUI_API__ ipgui_err_t ipgui_widget_unlisten(
    ipgui_widget_t * widget, ipgui_widget_listener_fn fn, void * user);

/* 清空该控件的全部监听 */
extern __IPGUI_API__ void ipgui_widget_clear_listeners(ipgui_widget_t * widget);

/* 当前监听数量 */
extern __IPGUI_API__ int ipgui_widget_listener_count(ipgui_widget_t * widget);

/* 通知监听者，框架内部调用 */
extern __IPGUI_API__ void ipgui_widget_dispatch_listeners(
    ipgui_widget_t * widget, ipgui_widget_evt_t * evt);

#ifdef __cplusplus
}
#endif

#endif
