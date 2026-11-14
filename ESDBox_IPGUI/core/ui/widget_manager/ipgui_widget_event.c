#include "ipgui_widget.h"

#if IPGUI_WIDGET_LISTENER_MAX > 0

ipgui_err_t ipgui_widget_listen(
    ipgui_widget_t * widget, ipgui_widget_listener_fn fn, void * user)
{
    u8_t i;

    if (!widget || !fn) return IPGUI_ERR_PARAM;

    for (i = 0; i < widget->listener_num; i ++) {
        if (widget->listener[i].fn == fn && widget->listener[i].user == user)
            return IPGUI_ERR_OK;
    }

    if (widget->listener_num >= IPGUI_WIDGET_LISTENER_MAX)
        return IPGUI_ERR_LISTENER_FULL;

    widget->listener[widget->listener_num].fn   = fn;
    widget->listener[widget->listener_num].user = user;
    widget->listener_num ++;

    return IPGUI_ERR_OK;
}

ipgui_err_t ipgui_widget_unlisten(
    ipgui_widget_t * widget, ipgui_widget_listener_fn fn, void * user)
{
    u8_t i, j;

    if (!widget || !fn) return IPGUI_ERR_PARAM;

    for (i = 0; i < widget->listener_num; i ++) {
        if (widget->listener[i].fn != fn || widget->listener[i].user != user)
            continue;

        for (j = i + 1; j < widget->listener_num; j ++)
            widget->listener[j - 1] = widget->listener[j];
        widget->listener_num --;

        return IPGUI_ERR_OK;
    }

    return IPGUI_ERR_LISTENER_NOT_FOUND;
}

void ipgui_widget_clear_listeners(ipgui_widget_t * widget)
{
    if (!widget) return;
    widget->listener_num = 0;
}

int ipgui_widget_listener_count(ipgui_widget_t * widget)
{
    if (!widget) return 0;
    return (int)widget->listener_num;
}

/* 先快照再调用：回调里注销自己会前移数组，
 * 直接遍历会漏掉或重复，快照保证本次按分发开始时的集合通知。 */
void ipgui_widget_dispatch_listeners(
    ipgui_widget_t * widget, ipgui_widget_evt_t * evt)
{
    ipgui_widget_listener_slot_t snap[IPGUI_WIDGET_LISTENER_MAX];
    u8_t n, i;

    if (!widget || !evt) return;

    n = widget->listener_num;
    if (n > IPGUI_WIDGET_LISTENER_MAX) n = IPGUI_WIDGET_LISTENER_MAX;
    for (i = 0; i < n; i ++)
        snap[i] = widget->listener[i];

    for (i = 0; i < n; i ++) {
        if (snap[i].fn)
            snap[i].fn(widget, evt, snap[i].user);
    }
}

#else

ipgui_err_t ipgui_widget_listen(
    ipgui_widget_t * widget, ipgui_widget_listener_fn fn, void * user)
{
    (void)widget; (void)fn; (void)user;
    return IPGUI_ERR_EVT_NOT_SUPPORTED;
}

ipgui_err_t ipgui_widget_unlisten(
    ipgui_widget_t * widget, ipgui_widget_listener_fn fn, void * user)
{
    (void)widget; (void)fn; (void)user;
    return IPGUI_ERR_EVT_NOT_SUPPORTED;
}

void ipgui_widget_clear_listeners(ipgui_widget_t * widget)
{
    (void)widget;
}

int ipgui_widget_listener_count(ipgui_widget_t * widget)
{
    (void)widget;
    return 0;
}

void ipgui_widget_dispatch_listeners(
    ipgui_widget_t * widget, ipgui_widget_evt_t * evt)
{
    (void)widget; (void)evt;
}

#endif
