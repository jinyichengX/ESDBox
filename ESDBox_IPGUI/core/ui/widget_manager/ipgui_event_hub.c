#include "ipgui_event_hub.h"

#if IPGUI_EVENT_HUB_EN

#if (IPGUI_EVENT_HUB_WATCH_MAX < 1) || (IPGUI_EVENT_HUB_SUB_MAX < 1)
#error "IPGUI_EVENT_HUB_WATCH_MAX / IPGUI_EVENT_HUB_SUB_MAX must be >= 1"
#endif

typedef struct {
    ipgui_widget_t       * widget;
    ipgui_widget_event_fn  fn;
    void                 * user;
} hub_sub_t;

typedef struct {
    ipgui_widget_t * widget;
    void (*orig)(ipgui_widget_t *, ipgui_widget_evt_t *);
} hub_watch_t;

/* 静态池：不进入堆，容量在编译期确定 */
static hub_sub_t   g_hub_subs [IPGUI_EVENT_HUB_SUB_MAX];
static hub_watch_t g_hub_watch[IPGUI_EVENT_HUB_WATCH_MAX];

static void hub_bridge(ipgui_widget_t * widget, ipgui_widget_evt_t * evt);

/* -------------------------------------------------------------------------- */

static int hub_find_watch(ipgui_widget_t * widget)
{
    int i;

    for (i = 0; i < (int)IPGUI_EVENT_HUB_WATCH_MAX; i ++) {
        if (g_hub_watch[i].widget == widget)
            return i;
    }
    return -1;
}

static int hub_sub_count(ipgui_widget_t * widget)
{
    int i, n = 0;

    for (i = 0; i < (int)IPGUI_EVENT_HUB_SUB_MAX; i ++) {
        if (g_hub_subs[i].fn && g_hub_subs[i].widget == widget)
            n ++;
    }
    return n;
}

/* 释放 watch 槽；只有当前 handler 仍是转发函数时才还原，
 * 避免覆盖用户在注册之后手动替换的 handler */
static void hub_release_watch(ipgui_widget_t * widget)
{
    int i = hub_find_watch(widget);

    if (i < 0) return;

    if (widget->event_handler == hub_bridge)
        widget->event_handler = g_hub_watch[i].orig;

    g_hub_watch[i].widget = (ipgui_widget_t *)0;
    g_hub_watch[i].orig   = (void (*)(ipgui_widget_t *,
                                      ipgui_widget_evt_t *))0;
}

/* 转发：先跑控件自己的处理，再按槽位顺序通知监听者。
 * 删除监听使用清空槽位（墓碑），不做搬移，因此分发中途删除是安全的 */
static void hub_bridge(ipgui_widget_t * widget, ipgui_widget_evt_t * evt)
{
    void (*orig)(ipgui_widget_t *, ipgui_widget_evt_t *) = 0;
    int i;

    i = hub_find_watch(widget);
    if (i >= 0) orig = g_hub_watch[i].orig;

    if (orig) orig(widget, evt);

    for (i = 0; i < (int)IPGUI_EVENT_HUB_SUB_MAX; i ++) {
        /* fn 为空表示空槽或已注销 */
        if (g_hub_subs[i].fn && g_hub_subs[i].widget == widget)
            g_hub_subs[i].fn(widget, evt, g_hub_subs[i].user);
    }
}

/* -------------------------------------------------------------------------- */

int ipgui_widget_listen(
    ipgui_widget_t * widget,
    ipgui_widget_event_fn fn,
    void * user)
{
    int i, wi;
    int created = 0;

    if (!widget || !fn) return IPGUI_EVENT_HUB_ERR_PARAM;

    /* 幂等：同一组 (控件, 函数, user) 不重复登记 */
    for (i = 0; i < (int)IPGUI_EVENT_HUB_SUB_MAX; i ++) {
        if (g_hub_subs[i].fn &&
            g_hub_subs[i].widget == widget &&
            g_hub_subs[i].fn     == fn &&
            g_hub_subs[i].user   == user)
            return IPGUI_EVENT_HUB_OK;
    }

    wi = hub_find_watch(widget);
    if (wi < 0) {
        /* handler 已是转发函数却找不到 watch，说明表被外部改动过 */
        if (widget->event_handler == hub_bridge)
            return IPGUI_EVENT_HUB_ERR_STATE;

        for (i = 0; i < (int)IPGUI_EVENT_HUB_WATCH_MAX; i ++) {
            if (!g_hub_watch[i].widget) { wi = i; break; }
        }
        if (wi < 0) return IPGUI_EVENT_HUB_ERR_FULL;

        g_hub_watch[wi].widget = widget;
        g_hub_watch[wi].orig   = widget->event_handler;
        widget->event_handler  = hub_bridge;
        created = 1;
    }

    for (i = 0; i < (int)IPGUI_EVENT_HUB_SUB_MAX; i ++) {
        if (!g_hub_subs[i].fn) break;
    }
    if (i >= (int)IPGUI_EVENT_HUB_SUB_MAX) {
        /* 订阅表满：若刚才为此控件新建了 watch，回滚 */
        if (created && hub_sub_count(widget) == 0)
            hub_release_watch(widget);
        return IPGUI_EVENT_HUB_ERR_FULL;
    }

    g_hub_subs[i].widget = widget;
    g_hub_subs[i].fn     = fn;
    g_hub_subs[i].user   = user;

    return IPGUI_EVENT_HUB_OK;
}

int ipgui_widget_unlisten(
    ipgui_widget_t * widget,
    ipgui_widget_event_fn fn,
    void * user)
{
    int i, hit = 0;

    if (!widget || !fn) return IPGUI_EVENT_HUB_ERR_PARAM;

    for (i = 0; i < (int)IPGUI_EVENT_HUB_SUB_MAX; i ++) {
        if (g_hub_subs[i].fn &&
            g_hub_subs[i].widget == widget &&
            g_hub_subs[i].fn     == fn &&
            g_hub_subs[i].user   == user) {
            g_hub_subs[i].fn     = (ipgui_widget_event_fn)0;
            g_hub_subs[i].user   = (void *)0;
            g_hub_subs[i].widget = (ipgui_widget_t *)0;
            hit = 1;
        }
    }

    if (!hit) return IPGUI_EVENT_HUB_ERR_NOENT;

    if (hub_sub_count(widget) == 0)
        hub_release_watch(widget);

    return IPGUI_EVENT_HUB_OK;
}

int ipgui_widget_unwatch(ipgui_widget_t * widget)
{
    int i, hit = 0;
    int wi;

    if (!widget) return IPGUI_EVENT_HUB_ERR_PARAM;

    for (i = 0; i < (int)IPGUI_EVENT_HUB_SUB_MAX; i ++) {
        if (g_hub_subs[i].fn && g_hub_subs[i].widget == widget) {
            g_hub_subs[i].fn     = (ipgui_widget_event_fn)0;
            g_hub_subs[i].user   = (void *)0;
            g_hub_subs[i].widget = (ipgui_widget_t *)0;
            hit = 1;
        }
    }

    wi = hub_find_watch(widget);
    if (wi >= 0) {
        hub_release_watch(widget);
        hit = 1;
    }

    return hit ? IPGUI_EVENT_HUB_OK : IPGUI_EVENT_HUB_ERR_NOENT;
}

int ipgui_widget_listen_count(ipgui_widget_t * widget)
{
    if (!widget) return 0;
    return hub_sub_count(widget);
}

#else /* IPGUI_EVENT_HUB_EN == 0 */

int ipgui_widget_listen(ipgui_widget_t * widget,
                        ipgui_widget_event_fn fn, void * user)
{
    (void)widget; (void)fn; (void)user;
    return IPGUI_EVENT_HUB_ERR_DISABLED;
}

int ipgui_widget_unlisten(ipgui_widget_t * widget,
                          ipgui_widget_event_fn fn, void * user)
{
    (void)widget; (void)fn; (void)user;
    return IPGUI_EVENT_HUB_ERR_DISABLED;
}

int ipgui_widget_unwatch(ipgui_widget_t * widget)
{
    (void)widget;
    return IPGUI_EVENT_HUB_ERR_DISABLED;
}

int ipgui_widget_listen_count(ipgui_widget_t * widget)
{
    (void)widget;
    return 0;
}

#endif /* IPGUI_EVENT_HUB_EN */
