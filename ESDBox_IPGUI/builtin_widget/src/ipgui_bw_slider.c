#include "ipgui_bw_slider.h"
#include "ipgui_bw_theme.h"
#include "ipgui_memory.h"
#include "ipgui_time.h"

#define BW_SL_PAD        6
#define BW_SL_TRACK_H    6
#define BW_SL_KNOB_R     9
#define BW_SL_GROW_MS    120

typedef struct {
    u8_t                value;      /* 0..100 */
    u8_t                disabled;
    u8_t                hovered;
    u8_t                dragging;

    u8_t                anim_from;
    u8_t                anim_to;
    u8_t                anim_last;
    u8_t                animating;
    ipgui_tick_t        anim_t0;
    u16_t               anim_dur;

    ipgui_color_t       accent_left;
    ipgui_color_t       accent_right;

    ipgui_bw_slider_cb_t on_change;
} ipgui_bw_slider_t;

static void bw_sl_track_range(
    ipgui_widget_t * widget, ipgui_coord_t knob_r,
    ipgui_coord_t * x0, ipgui_coord_t * x1)
{
    ipgui_coord_t left  = BW_SL_PAD;
    ipgui_coord_t right = widget->w - 1 - BW_SL_PAD;

    *x0 = left + knob_r;
    *x1 = right - knob_r;
    if (*x1 <= *x0) *x1 = *x0 + 1;
}

static void bw_sl_render(
    ipgui_widget_t * widget, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_bw_slider_t * sl = (ipgui_bw_slider_t *)widget->priv_data;
    ipgui_surf_t      * s  = ctx->surf;
    ipgui_aabb_t        box, track, active, knob_box;
    ipgui_coord_t       cy, x0, x1, cx;
    ipgui_coord_t       knob_r, grow;
    ipgui_color_t       ac_l, ac_r;
    u8_t                grow_t;
    u8_t                op;

    if (!sl || !s) return;

    box.start.x = BW_SL_PAD;
    box.start.y = BW_SL_PAD;
    box.end.x   = widget->w - 1 - BW_SL_PAD;
    box.end.y   = widget->h - 1 - BW_SL_PAD;

    if (ipgui_aabb_width(&box) < 24 || ipgui_aabb_height(&box) < 12) return;

    cy = (box.start.y + box.end.y) / 2;

    /* 圆钮按 hover / 按压放大 */
    grow_t = ipgui_bw_anim8(sl->anim_from, sl->anim_to,
                            sl->anim_t0, ipgui_tick_now(), sl->anim_dur);
    sl->anim_last = grow_t;
    grow = (ipgui_coord_t)(grow_t / 85);   /* 0..3 px */

    knob_r = BW_SL_KNOB_R + grow;

    /* 取值映射与圆钮绘制统一按基准半径计算，避免按下放大时位置抖动 */
    bw_sl_track_range(widget, BW_SL_KNOB_R, &x0, &x1);
    cx = x0 + (ipgui_coord_t)(((s32_t)(x1 - x0) * sl->value) / 100);

    ac_l = sl->accent_left.a  ? sl->accent_left  : BW_ACCENT_TOP;
    ac_r = sl->accent_right.a ? sl->accent_right : BW_ACCENT_BOT;
    if (sl->disabled) {
        ac_l = BW_DISABLED_TEXT;
        ac_r = BW_DISABLED_TEXT;
    }

    op = sl->disabled ? 140 : 255;

    /* ---------------- 轨道 ---------------- */
    track.start.x = box.start.x;
    track.end.x   = box.end.x;
    track.start.y = cy - BW_SL_TRACK_H / 2;
    track.end.y   = cy + BW_SL_TRACK_H / 2;

    ipgui_bw_fill_box(s, &track, BW_SL_TRACK_H / 2, BW_TRACK, op);
    ipgui_bw_stroke_box(s, &track, BW_SL_TRACK_H / 2, 1,
                        BW_TRACK_BORDER, sl->disabled ? 70 : 160);

    /* ---------------- 激活段 ---------------- */
    if (sl->value > 0) {
        ipgui_coord_t r;

        active.start.x = track.start.x;
        active.end.x   = cx;
        active.start.y = track.start.y;
        active.end.y   = track.end.y;

        if (active.end.x >= active.start.x) {
            r = BW_SL_TRACK_H / 2;
            if (ipgui_aabb_width(&active) / 2 < r)
                r = ipgui_aabb_width(&active) / 2;
            if (r < 1) r = 1;

            ipgui_bw_fill_hgrad(s, &active, r, ac_l, ac_r, op);
            ipgui_bw_top_gloss(s, &active, r,
                ipgui_bw_color(0xFF, 0xFF, 0xFF, 0xFF), 55);
        }
    }

    /* ---------------- 圆钮 ---------------- */
    knob_box.start.x = cx - knob_r;
    knob_box.start.y = cy - knob_r;
    knob_box.end.x   = cx + knob_r;
    knob_box.end.y   = cy + knob_r;

    ipgui_bw_shadow_box(s, &knob_box, knob_r,
        ipgui_bw_color(0x2A, 0x3A, 0x5F, 0xFF),
        sl->disabled ? 18 : (sl->dragging ? 64 : 48),
        sl->dragging ? 8 : 7, 0, 0, sl->dragging ? 3 : 2);

    /* 主色圆环 + 白色内芯 */
    {
        ipgui_filled_circle_style_t cs;

        cs.paint.type      = IPGUI_PAINT_COLOR;
        cs.blend_mode      = IPGUI_BLEND_NORMAL;
        cs.opacity         = op;

        cs.paint.src.color = ac_r;
        ipgui_draw_filled_circle(s, (ipgui_aabb_t *)0, cx, cy, knob_r, &cs);

        cs.paint.src.color = ipgui_bw_color(0xFF, 0xFF, 0xFF, 0xFF);
        ipgui_draw_filled_circle(s, (ipgui_aabb_t *)0, cx, cy, knob_r - 2, &cs);
    }
}

static u8_t bw_sl_value_at(ipgui_widget_t * widget, ipgui_coord_t x)
{
    ipgui_coord_t x0, x1;
    s32_t         v;

    bw_sl_track_range(widget, BW_SL_KNOB_R, &x0, &x1);
    if (x1 <= x0) return 0;

    v = ((s32_t)(x - x0) * 100 + (x1 - x0) / 2) / (x1 - x0);
    if (v < 0)   v = 0;
    if (v > 100) v = 100;
    return (u8_t)v;
}

static void bw_sl_apply(ipgui_widget_t * widget, u8_t value)
{
    ipgui_bw_slider_t * sl = (ipgui_bw_slider_t *)widget->priv_data;

    if (value == sl->value) return;

    sl->value = value;
    ipgui_widget_mark_dirty(widget);
    if (sl->on_change) sl->on_change(widget, value);
}

static void bw_sl_grow(ipgui_bw_slider_t * sl, u8_t to)
{
    sl->anim_from = sl->anim_last;
    sl->anim_to   = to;
    sl->anim_t0   = ipgui_tick_now();
    sl->anim_dur  = BW_SL_GROW_MS;
    sl->animating = 1;
}

static void bw_sl_event(
    ipgui_widget_t * widget, ipgui_widget_evt_t * evt)
{
    ipgui_bw_slider_t * sl = (ipgui_bw_slider_t *)widget->priv_data;

    if (!sl || sl->disabled) return;

    switch (evt->type) {
    case IPGUI_WIDGET_EVENT_PRESSED:
        if (!sl->dragging) {
            sl->dragging = 1;
            sl->hovered  = 1;
            bw_sl_grow(sl, 255);
        }
        /* 持续按压事件用于拖动，每次都要更新取值 */
        bw_sl_apply(widget, bw_sl_value_at(widget, evt->evt.pressed_evt.x));
        ipgui_widget_mark_dirty(widget);
        break;

    case IPGUI_WIDGET_EVENT_RELEASED:
        if (sl->dragging) {
            sl->dragging = 0;
            bw_sl_grow(sl, 0);
            ipgui_widget_mark_dirty(widget);
        }
        break;

    case IPGUI_WIDGET_EVENT_HOVER: {
        u8_t hovered = (evt->evt.hover_evt.x >= 0 &&
                        evt->evt.hover_evt.x < widget->w &&
                        evt->evt.hover_evt.y >= 0 &&
                        evt->evt.hover_evt.y < widget->h) ? 1 : 0;

        if (hovered != sl->hovered) {
            sl->hovered = hovered;
            if (!sl->dragging) bw_sl_grow(sl, hovered ? 128 : 0);
            ipgui_widget_mark_dirty(widget);
        }
        break;
    }

    default:
        break;
    }
}

ipgui_widget_t * ipgui_bw_slider_create(
    ipgui_widget_t * parent,
    ipgui_coord_t x, ipgui_coord_t y,
    ipgui_coord_t w, ipgui_coord_t h,
    u8_t value)
{
    ipgui_widget_t     * widget;
    ipgui_bw_slider_t  * sl;

    widget = ipgui_widget_create(parent);
    if (!widget) return (ipgui_widget_t *)0;

    sl = (ipgui_bw_slider_t *)ipgui_mem_alloc_def(sizeof(ipgui_bw_slider_t));
    if (!sl) {
        ipgui_widget_link_detach(&widget->link);
        ipgui_mem_free_def(widget);
        return (ipgui_widget_t *)0;
    }
    ipgui_memset((void *)sl, 0, sizeof(ipgui_bw_slider_t));

    widget->x = x; widget->y = y;
    widget->w = w; widget->h = h;
    widget->priv_data = sl;
    widget->render        = bw_sl_render;
    widget->event_handler = bw_sl_event;
    widget->name          = "bw_slider";

    sl->value     = (value > 100) ? 100 : value;
    sl->anim_last = 0;
    sl->anim_from = 0;
    sl->anim_to   = 0;

    return widget;
}

void ipgui_bw_slider_set_value(ipgui_widget_t * widget, u8_t value)
{
    ipgui_bw_slider_t * sl;

    if (!widget || !widget->priv_data) return;
    sl = (ipgui_bw_slider_t *)widget->priv_data;

    if (value > 100) value = 100;
    if (value == sl->value) return;

    sl->value = value;
    ipgui_widget_mark_dirty(widget);
}

u8_t ipgui_bw_slider_get_value(ipgui_widget_t * widget)
{
    ipgui_bw_slider_t * sl;

    if (!widget || !widget->priv_data) return 0;
    sl = (ipgui_bw_slider_t *)widget->priv_data;
    return sl->value;
}

void ipgui_bw_slider_set_enabled(ipgui_widget_t * widget, u8_t enabled)
{
    ipgui_bw_slider_t * sl;

    if (!widget || !widget->priv_data) return;
    sl = (ipgui_bw_slider_t *)widget->priv_data;

    sl->disabled = enabled ? 0 : 1;
    if (sl->disabled) {
        sl->dragging = 0;
        sl->hovered  = 0;
    }
    ipgui_widget_mark_dirty(widget);
}

void ipgui_bw_slider_set_on_change(
    ipgui_widget_t * widget, ipgui_bw_slider_cb_t cb)
{
    ipgui_bw_slider_t * sl;

    if (!widget || !widget->priv_data) return;
    sl = (ipgui_bw_slider_t *)widget->priv_data;
    sl->on_change = cb;
}

void ipgui_bw_slider_set_accent(
    ipgui_widget_t * widget, ipgui_color_t left, ipgui_color_t right)
{
    ipgui_bw_slider_t * sl;

    if (!widget || !widget->priv_data) return;
    sl = (ipgui_bw_slider_t *)widget->priv_data;

    sl->accent_left  = left;
    sl->accent_right = right;
    ipgui_widget_mark_dirty(widget);
}

void ipgui_bw_slider_task(ipgui_widget_t * widget)
{
    ipgui_bw_slider_t * sl;
    u8_t t;

    if (!widget || !widget->priv_data) return;
    sl = (ipgui_bw_slider_t *)widget->priv_data;

    if (!sl->animating) return;

    t = ipgui_bw_anim8(sl->anim_from, sl->anim_to,
                       sl->anim_t0, ipgui_tick_now(), sl->anim_dur);
    if (t != sl->anim_last) {
        sl->anim_last = t;
        ipgui_widget_mark_dirty(widget);
    }
    if (t == sl->anim_to)
        sl->animating = 0;
}
