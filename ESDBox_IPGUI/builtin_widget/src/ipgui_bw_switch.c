#include "ipgui_bw_switch.h"
#include "ipgui_bw_theme.h"
#include "ipgui_memory.h"
#include "ipgui_time.h"

#define BW_SW_PAD       3
#define BW_SW_DURATION  180

typedef struct {
    u8_t                on;
    u8_t                disabled;
    u8_t                hovered;
    u8_t                pressed;

    u8_t                anim_from;
    u8_t                anim_to;
    u8_t                anim_last;
    u8_t                animating;
    ipgui_tick_t        anim_t0;
    u16_t               anim_dur;

    ipgui_color_t       on_top;
    ipgui_color_t       on_bottom;

    ipgui_bw_switch_cb_t on_change;
} ipgui_bw_switch_t;

/* -------------------------------------------------------------------------- */

static void bw_sw_render(
    ipgui_widget_t * widget, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_bw_switch_t * w = (ipgui_bw_switch_t *)widget->priv_data;
    ipgui_surf_t      * s = ctx->surf;
    ipgui_aabb_t        box;
    ipgui_coord_t       track_h, radius, knob_d, knob_r;
    ipgui_coord_t       off_cx, on_cx, knob_cx, cy;
    ipgui_color_t       top, bottom, border;
    u8_t                t;
    ipgui_aabb_t        knob_box;

    if (!w || !s) return;

    box.start.x = BW_SW_PAD;
    box.start.y = BW_SW_PAD;
    box.end.x   = widget->w - 1 - BW_SW_PAD;
    box.end.y   = widget->h - 1 - BW_SW_PAD;

    track_h = ipgui_aabb_height(&box);
    if (track_h < 12 || ipgui_aabb_width(&box) < 24) return;

    radius = track_h / 2;
    knob_d = track_h - 6;
    knob_r = knob_d / 2;

    t = ipgui_bw_anim8(w->anim_from, w->anim_to,
                       w->anim_t0, ipgui_tick_now(), w->anim_dur);
    w->anim_last = t;

    /* 关闭态：浅灰微渐变；打开态：主色渐变；按 t 过渡 */
    {
        ipgui_color_t on_top = w->on_top.a ? w->on_top : BW_ACCENT_TOP;
        ipgui_color_t on_bot = w->on_bottom.a ? w->on_bottom : BW_ACCENT_BOT;

        if (w->disabled) {
            on_top = BW_DISABLED_BG;
            on_bot = BW_DISABLED_BG;
        }

        top    = ipgui_bw_mix(ipgui_bw_color(0xEC, 0xEF, 0xF6, 0xFF),
                              on_top, t);
        bottom = ipgui_bw_mix(ipgui_bw_color(0xE1, 0xE6, 0xF0, 0xFF),
                              on_bot, t);
        border = ipgui_bw_mix(BW_TRACK_BORDER,
                              ipgui_bw_color(0xFF, 0xFF, 0xFF, 0x30), t);
    }

    /* 悬停时轨道外扩一圈主色柔光，提示可点击 */
    if (w->hovered && !w->disabled && !w->pressed) {
        ipgui_bw_shadow_box(s, &box, radius, top, 46, 12, 0, 0, 3);
    }

    /* 轨道 */
    ipgui_bw_fill_vgrad(s, &box, radius, top, bottom,
                        w->disabled ? 150 : 255);
    ipgui_bw_stroke_box(s, &box, radius, 1, border,
                        w->disabled ? 90 : 200);

    /* 圆钮位置 */
    cy     = (box.start.y + box.end.y) / 2;
    off_cx = box.start.x + 3 + knob_r;
    on_cx  = box.end.x   - 3 - knob_r;
    knob_cx = off_cx + (ipgui_coord_t)(((s32_t)(on_cx - off_cx) * t) / 255);

    knob_box.start.x = knob_cx - knob_r;
    knob_box.start.y = cy - knob_r;
    knob_box.end.x   = knob_cx + knob_r;
    knob_box.end.y   = cy + knob_r;

    /* 圆钮投影 */
    ipgui_bw_shadow_box(s, &knob_box, knob_r,
        ipgui_bw_color(0x1A, 0x24, 0x40, 0xFF),
        w->disabled ? 20 : (w->pressed ? 70 : 55),
        w->pressed ? 7 : 5, 0, 0, w->pressed ? 2 : 1);

    /* 圆钮本体 */
    {
        ipgui_filled_circle_style_t cs;
        ipgui_color_t white = w->disabled
            ? ipgui_bw_color(0xF7, 0xF8, 0xFB, 0xFF)
            : ipgui_bw_color(0xFF, 0xFF, 0xFF, 0xFF);

        cs.paint.type      = IPGUI_PAINT_COLOR;
        cs.paint.src.color = white;
        cs.opacity         = 255;
        cs.blend_mode      = IPGUI_BLEND_NORMAL;
        ipgui_draw_filled_circle(s, (ipgui_aabb_t *)0,
                                 knob_cx, cy, knob_r, &cs);
    }
}

/* -------------------------------------------------------------------------- */

static int bw_sw_hit(ipgui_widget_t * widget, ipgui_coord_t x, ipgui_coord_t y)
{
    return (x >= 0 && y >= 0 && x < widget->w && y < widget->h);
}

static void bw_sw_anim_start(ipgui_bw_switch_t * w, u8_t to)
{
    w->anim_from = w->anim_last;
    w->anim_to   = to;
    w->anim_t0   = ipgui_tick_now();
    w->anim_dur  = BW_SW_DURATION;
    w->animating = 1;
}

static void bw_sw_event(
    ipgui_widget_t * widget, ipgui_widget_evt_t * evt)
{
    ipgui_bw_switch_t * w = (ipgui_bw_switch_t *)widget->priv_data;

    if (!w || w->disabled) return;

    switch (evt->type) {
    case IPGUI_WIDGET_EVENT_PRESSED:
        if (!w->pressed) {
            w->pressed = 1;
            w->hovered = 1;
            ipgui_widget_mark_dirty(widget);
        }
        break;

    case IPGUI_WIDGET_EVENT_RELEASED:
        if (w->pressed) {
            int inside = bw_sw_hit(widget,
                evt->evt.released_evt.x, evt->evt.released_evt.y);

            w->pressed = 0;
            if (inside) {
                w->on = w->on ? 0 : 1;
                bw_sw_anim_start(w, w->on ? 255 : 0);
                if (w->on_change) w->on_change(widget, w->on);
            }
            ipgui_widget_mark_dirty(widget);
        }
        break;

    case IPGUI_WIDGET_EVENT_HOVER: {
        u8_t hovered = (u8_t)bw_sw_hit(widget,
            evt->evt.hover_evt.x, evt->evt.hover_evt.y);

        if (hovered != w->hovered) {
            w->hovered = hovered;
            ipgui_widget_mark_dirty(widget);
        }
        break;
    }

    default:
        break;
    }
}

/* -------------------------------------------------------------------------- */

ipgui_widget_t * ipgui_bw_switch_create(
    ipgui_widget_t * parent,
    ipgui_coord_t x, ipgui_coord_t y,
    ipgui_coord_t w, ipgui_coord_t h,
    u8_t on)
{
    ipgui_widget_t     * widget;
    ipgui_bw_switch_t  * sw;

    widget = ipgui_widget_create(parent);
    if (!widget) return (ipgui_widget_t *)0;

    sw = (ipgui_bw_switch_t *)ipgui_mem_alloc_def(sizeof(ipgui_bw_switch_t));
    if (!sw) {
        ipgui_widget_link_detach(&widget->link);
        ipgui_mem_free_def(widget);
        return (ipgui_widget_t *)0;
    }
    ipgui_memset((void *)sw, 0, sizeof(ipgui_bw_switch_t));

    widget->x = x; widget->y = y;
    widget->w = w; widget->h = h;
    widget->priv_data = sw;
    widget->render        = bw_sw_render;
    widget->event_handler = bw_sw_event;
    widget->name          = "bw_switch";

    sw->on         = on ? 1 : 0;
    sw->anim_last  = sw->on ? 255 : 0;
    sw->anim_from  = sw->anim_last;
    sw->anim_to    = sw->anim_last;

    return widget;
}

void ipgui_bw_switch_set_on(
    ipgui_widget_t * widget, u8_t on, u8_t animated)
{
    ipgui_bw_switch_t * w;

    if (!widget || !widget->priv_data) return;
    w = (ipgui_bw_switch_t *)widget->priv_data;

    w->on = on ? 1 : 0;
    if (animated) {
        bw_sw_anim_start(w, w->on ? 255 : 0);
    } else {
        w->anim_last = w->on ? 255 : 0;
        w->anim_from = w->anim_last;
        w->anim_to   = w->anim_last;
        w->animating = 0;
    }
    ipgui_widget_mark_dirty(widget);
}

u8_t ipgui_bw_switch_get_on(ipgui_widget_t * widget)
{
    ipgui_bw_switch_t * w;

    if (!widget || !widget->priv_data) return 0;
    w = (ipgui_bw_switch_t *)widget->priv_data;
    return w->on;
}

void ipgui_bw_switch_set_enabled(ipgui_widget_t * widget, u8_t enabled)
{
    ipgui_bw_switch_t * w;

    if (!widget || !widget->priv_data) return;
    w = (ipgui_bw_switch_t *)widget->priv_data;

    w->disabled = enabled ? 0 : 1;
    if (w->disabled) {
        w->pressed = 0;
        w->hovered = 0;
    }
    ipgui_widget_mark_dirty(widget);
}

void ipgui_bw_switch_set_on_change(
    ipgui_widget_t * widget, ipgui_bw_switch_cb_t cb)
{
    ipgui_bw_switch_t * w;

    if (!widget || !widget->priv_data) return;
    w = (ipgui_bw_switch_t *)widget->priv_data;
    w->on_change = cb;
}

void ipgui_bw_switch_set_colors(
    ipgui_widget_t * widget, ipgui_color_t top, ipgui_color_t bottom)
{
    ipgui_bw_switch_t * w;

    if (!widget || !widget->priv_data) return;
    w = (ipgui_bw_switch_t *)widget->priv_data;

    w->on_top    = top;
    w->on_bottom = bottom;
    ipgui_widget_mark_dirty(widget);
}

void ipgui_bw_switch_task(ipgui_widget_t * widget)
{
    ipgui_bw_switch_t * w;
    u8_t t;

    if (!widget || !widget->priv_data) return;
    w = (ipgui_bw_switch_t *)widget->priv_data;

    if (!w->animating) return;

    t = ipgui_bw_anim8(w->anim_from, w->anim_to,
                       w->anim_t0, ipgui_tick_now(), w->anim_dur);
    if (t != w->anim_last) {
        w->anim_last = t;
        ipgui_widget_mark_dirty(widget);
    }
    if (t == w->anim_to)
        w->animating = 0;
}
