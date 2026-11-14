#ifndef IPGUI_EVENT_HUB_H
#define IPGUI_EVENT_HUB_H

/*
 * 控件事件观察服务（Event Hub）
 *
 * 作用：
 *   给任意 ipgui_widget_t 挂多个事件监听，而不占用控件自己的 event_handler。
 *
 * 原理：
 *   首次给某个控件注册监听时，把这个控件当前的 event_handler 保存下来，
 *   换成一个转发函数。事件到达时先执行控件原有逻辑（按压动画、点击判定等），
 *   再依次通知所有监听者。控件源码、ipgui_widget_t、事件分发都不需要改。
 *
 * 语义：
 *   - 监听只观察，不能拦截事件，也不影响控件自身行为
 *   - 执行顺序：控件内部逻辑 -> 监听者（按槽位先后）
 *   - 分发过程中删除监听立即生效；新增监听不保证收到本次事件
 *   - 同一个 (控件, 函数, user) 重复注册是幂等的，不会重复回调
 *   - 全程静态存储，无堆分配，无动态内存碎片
 *   - 非中断安全，只能在 UI 线程 / 主循环上下文调用
 *
 * 容量（可用 -D 覆盖）：
 *   IPGUI_EVENT_HUB_WATCH_MAX  同时被监听的控件数，默认 8
 *   IPGUI_EVENT_HUB_SUB_MAX    监听项总数（所有控件共享），默认 16
 *   表满时返回 IPGUI_EVENT_HUB_ERR_FULL，不影响控件正常工作。
 *
 * 生命周期：
 *   控件销毁前调用 ipgui_widget_unwatch(widget)，恢复原 event_handler。
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "ipgui_widget.h"

/* 1: 编译进固件；0: 全部退化为空实现，不占 RAM */
#ifndef IPGUI_EVENT_HUB_EN
#define IPGUI_EVENT_HUB_EN      1
#endif

#ifndef IPGUI_EVENT_HUB_WATCH_MAX
#define IPGUI_EVENT_HUB_WATCH_MAX   8
#endif

#ifndef IPGUI_EVENT_HUB_SUB_MAX
#define IPGUI_EVENT_HUB_SUB_MAX     16
#endif

typedef void (*ipgui_widget_event_fn)(
    ipgui_widget_t * widget,
    ipgui_widget_evt_t * evt,
    void * user);

typedef enum {
    IPGUI_EVENT_HUB_OK          =  0,
    IPGUI_EVENT_HUB_ERR_PARAM   = -1,
    IPGUI_EVENT_HUB_ERR_FULL    = -2,
    IPGUI_EVENT_HUB_ERR_NOENT   = -3,
    IPGUI_EVENT_HUB_ERR_STATE   = -4,
    IPGUI_EVENT_HUB_ERR_DISABLED = -5,
} ipgui_event_hub_err_t;

/* 注册监听；返回 0 成功，负数为错误码 */
int ipgui_widget_listen(
    ipgui_widget_t * widget,
    ipgui_widget_event_fn fn,
    void * user);

/* 注销一个监听；未找到返回 NOENT */
int ipgui_widget_unlisten(
    ipgui_widget_t * widget,
    ipgui_widget_event_fn fn,
    void * user);

/* 注销该控件的全部监听并恢复原 event_handler；控件销毁前调用 */
int ipgui_widget_unwatch(ipgui_widget_t * widget);

/* 当前控件上的监听数量，调试/测试用 */
int ipgui_widget_listen_count(ipgui_widget_t * widget);

#ifdef __cplusplus
}
#endif

#endif /* IPGUI_EVENT_HUB_H */
