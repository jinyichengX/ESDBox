#include "ipgui_bw_radio.h"
#include "ipgui_bw_theme.h"
#include "ipgui_memory.h"
#include "ipgui_time.h"

#define BW_RD_PAD        4
#define BW_RD_LABEL_MAX  32
#define BW_RD_DURATION   150

typedef struct {
    char                label[BW_RD_LABEL_MAX];
    u8_t                selected;
    u8_t                disabled;
    u8_t                hovered;
    u8_t                pressed;

    u8_t                anim_from;
    u8_t                anim_to;
    u8_t                anim_last;
    u8_t                animating;
    ipgui_tick_t        anim_t0;
    u16_t               anim_dur;

    ipgui_bw_radio_cb_t on_change;
} ipgui_bw_radio_t;

static void bw_rd_copy_label(char * dst, const char * src)
{
    u32_t i = 0;

    if (!dst) return;
    if (!src) { dst[0] = '\0'; return; }

    while (src[i] && i < BW_RD_LABEL_MAX - 1) {
        dst[i] = src[i];
        i ++;
    }
    dst[i] = '\0';
}

static void bw_rd_draw_dot(
    ipgui_surf_t * s, ipgui_coord_t cx, ipgui_coord_t cy,
    ipgui_coord_t r, u8_t opacity)
{
    ipgui_filled_circle_style_t        cs;
    ipgui_radial_gradient_color_t    * g;
    ipgui_gradient_color_stop_t        st0, st1;

    if (r <= 0) return;

    cs.opacity    = opacity;
    cs.blend_mode = IPGUI_BLEND_NORMAL;
    cs.paint.type = IPGUI_PAINT_GRADIENT;

    cs.paint.src.grad_src.grad_type = IPGUI_GRADIENT_TYPE_RADIAL;
    g = &cs.paint.src.grad_src.grad.radial_grad;
    ipgui_radial_gradient_init(g, cx, cy, r);

    st0.color = ipgui_bw_mix(BW_ACCENT_TOP, BW_SURFACE_TOP, 60);
    st0.pos   = 0;
    st1.color = BW_ACCENT_BOT;
    st1.pos   = 255;
    ipgui_radial_gradient_add_stop(g, &st0);
    ipgui_radial_gradient_add_stop(g, &st1);

    ipgui_draw_filled_circle(s, (ipgui_aabb_t *)0, cx, cy, r, &cs);
}

static void bw_rd_render(
    ipgui_widget_t * widget, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_bw_radio_t * rd = (ipgui_bw_radio_t *)widget->priv_data;
    ipgui_surf_t     * s  = ctx->surf;
    ipgui_coord_t      side, radius, cx, cy;
    ipgui_aabb_t       circle_box, text_box;
    u8_t               t;
    ipgui_color_t      ring;
    ipgui_filled_circle_style_t cs;

    if (!rd || !s) return;

    side = widget->h - BW_RD_PAD * 2;
    if (side > 22) side = 22;
    if (side < 14) return;

    radius = side / 2;
    cx     = BW_RD_PAD + radius;
    cy     = widget->h / 2;

    if (rd->pressed) radius --;
    if (radius < 5) radius = 5;

    t = ipgui_bw_anim8(rd->anim_from, rd->anim_to,
                       rd->anim_t0, ipgui_tick_now(), rd->anim_dur);
    rd->anim_last = t;

    circle_box.start.x = cx - radius;
    circle_box.start.y = cy - radius;
    circle_box.end.x   = cx + radius;
    circle_box.end.y   = cy + radius;

    /* 极轻的投影，避免圆环贴在背景上 */
    ipgui_bw_shadow_box(s, &circle_box, radius,
        ipgui_bw_color(0x2A, 0x3A, 0x5F, 0xFF),
        rd->disabled ? 10 : 16, 6, 0, 0, 2);

    /* 圆环：外圈着色，内圈白色 */
    ring = ipgui_bw_mix(BW_BORDER_STRONG, BW_ACCENT_BOT, t);
    if (rd->hovered && !rd->disabled)
        ring = ipgui_bw_mix(ring, BW_ACCENT_BOT, 90);

    cs.opacity    = rd->disabled ? 150 : 255;
    cs.blend_mode = IPGUI_BLEND_NORMAL;
    cs.paint.type = IPGUI_PAINT_COLOR;

    cs.paint.src.color = ring;
    ipgui_draw_filled_circle(s, (ipgui_aabb_t *)0, cx, cy, radius, &cs);

    cs.paint.src.color = rd->disabled
        ? ipgui_bw_color(0xF7, 0xF8, 0xFB, 0xFF)
        : ipgui_bw_color(0xFF, 0xFF, 0xFF, 0xFF);
    ipgui_draw_filled_circle(s, (ipgui_aabb_t *)0, cx, cy, radius - 2, &cs);

    /* 选中圆点：半径随动画缩放 */
    if (t > 0) {
        ipgui_coord_t dot_r = (radius - 5) * t / 255;

        if (dot_r < 1) dot_r = 1;
        bw_rd_draw_dot(s, cx, cy, dot_r, rd->disabled ? 150 : 255);
    }

    /* 右侧文字 */
    if (rd->label[0] && widget->w > side + BW_RD_PAD * 2 + 12) {
        text_box.start.x = circle_box.end.x + 10;
        text_box.start.y = BW_RD_PAD;
        text_box.end.x   = widget->w - 1 - BW_RD_PAD;
        text_box.end.y   = widget->h - 1 - BW_RD_PAD;

        ipgui_bw_text_left(s, &text_box,
            ipgui_bw_font_for_height(side), rd->label, 0,
            rd->disabled ? BW_DISABLED_TEXT : BW_TEXT,
            rd->disabled ? 200 : 255);
    }
}

static int bw_rd_hit(ipgui_widget_t * widget, ipgui_coord_t x, ipgui_coord_t y)
{
    return (x >= 0 && y >= 0 && x < widget->w && y < widget->h);
}

static void bw_rd_anim_start(ipgui_bw_radio_t * rd, u8_t to)
{
    rd->anim_from = rd->anim_last;
    rd->anim_to   = to;
    rd->anim_t0   = ipgui_tick_now();
    rd->anim_dur  = BW_RD_DURATION;
    rd->animating = 1;
}

static void bw_rd_event(
    ipgui_widget_t * widget, ipgui_widget_evt_t * evt)
{
    ipgui_bw_radio_t * rd = (ipgui_bw_radio_t *)widget->priv_data;

    if (!rd || rd->disabled) return;

    switch (evt->type) {
    case IPGUI_WIDGET_EVENT_PRESSED:
        if (!rd->pressed) {
            rd->pressed = 1;
            rd->hovered = 1;
            ipgui_widget_mark_dirty(widget);
        }
        break;

    case IPGUI_WIDGET_EVENT_RELEASED:
        if (rd->pressed) {
            int inside = bw_rd_hit(widget,
                evt->evt.released_evt.x, evt->evt.released_evt.y);

            rd->pressed = 0;
            if (inside && !rd->selected) {
                rd->selected = 1;
                bw_rd_anim_start(rd, 255);
                if (rd->on_change) rd->on_change(widget, 1);
            }
            ipgui_widget_mark_dirty(widget);
        }
        break;

    case IPGUI_WIDGET_EVENT_HOVER: {
        u8_t hovered = (u8_t)bw_rd_hit(widget,
            evt->evt.hover_evt.x, evt->evt.hover_evt.y);

        if (hovered != rd->hovered) {
            rd->hovered = hovered;
            ipgui_widget_mark_dirty(widget);
        }
        break;
    }

    default:
        break;
    }
}

ipgui_widget_t * ipgui_bw_radio_create(
    ipgui_widget_t * parent,
    ipgui_coord_t x, ipgui_coord_t y,
    ipgui_coord_t w, ipgui_coord_t h,
    const char * label,
    u8_t selected)
{
    ipgui_widget_t    * widget;
    ipgui_bw_radio_t  * rd;

    widget = ipgui_widget_create(parent);
    if (!widget) return (ipgui_widget_t *)0;

    rd = (ipgui_bw_radio_t *)ipgui_mem_alloc_def(sizeof(ipgui_bw_radio_t));
    if (!rd) {
        ipgui_widget_link_detach(&widget->link);
        ipgui_mem_free_def(widget);
        return (ipgui_widget_t *)0;
    }
    ipgui_memset((void *)rd, 0, sizeof(ipgui_bw_radio_t));

    widget->x = x; widget->y = y;
    widget->w = w; widget->h = h;
    widget->priv_data = rd;
    widget->render        = bw_rd_render;
    widget->event_handler = bw_rd_event;
    widget->name          = "bw_radio";

    rd->selected  = selected ? 1 : 0;
    rd->anim_last = rd->selected ? 255 : 0;
    rd->anim_from = rd->anim_last;
    rd->anim_to   = rd->anim_last;

    bw_rd_copy_label(rd->label, label);

    return widget;
}

void ipgui_bw_radio_set_selected(
    ipgui_widget_t * widget, u8_t selected, u8_t animated)
{
    ipgui_bw_radio_t * rd;

    if (!widget || !widget->priv_data) return;
    rd = (ipgui_bw_radio_t *)widget->priv_data;

    rd->selected = selected ? 1 : 0;
    if (animated) {
        bw_rd_anim_start(rd, rd->selected ? 255 : 0);
    } else {
        rd->anim_last = rd->selected ? 255 : 0;
        rd->anim_from = rd->anim_last;
        rd->anim_to   = rd->anim_last;
        rd->animating = 0;
    }
    ipgui_widget_mark_dirty(widget);
}

u8_t ipgui_bw_radio_get_selected(ipgui_widget_t * widget)
{
    ipgui_bw_radio_t * rd;

    if (!widget || !widget->priv_data) return 0;
    rd = (ipgui_bw_radio_t *)widget->priv_data;
    return rd->selected;
}

void ipgui_bw_radio_set_label(ipgui_widget_t * widget, const char * label)
{
    ipgui_bw_radio_t * rd;

    if (!widget || !widget->priv_data) return;
    rd = (ipgui_bw_radio_t *)widget->priv_data;

    bw_rd_copy_label(rd->label, label);
    ipgui_widget_mark_dirty(widget);
}

void ipgui_bw_radio_set_enabled(ipgui_widget_t * widget, u8_t enabled)
{
    ipgui_bw_radio_t * rd;

    if (!widget || !widget->priv_data) return;
    rd = (ipgui_bw_radio_t *)widget->priv_data;

    rd->disabled = enabled ? 0 : 1;
    if (rd->disabled) {
        rd->pressed = 0;
        rd->hovered = 0;
    }
    ipgui_widget_mark_dirty(widget);
}

void ipgui_bw_radio_set_on_change(
    ipgui_widget_t * widget, ipgui_bw_radio_cb_t cb)
{
    ipgui_bw_radio_t * rd;

    if (!widget || !widget->priv_data) return;
    rd = (ipgui_bw_radio_t *)widget->priv_data;
    rd->on_change = cb;
}

void ipgui_bw_radio_task(ipgui_widget_t * widget)
{
    ipgui_bw_radio_t * rd;
    u8_t t;

    if (!widget || !widget->priv_data) return;
    rd = (ipgui_bw_radio_t *)widget->priv_data;

    if (!rd->animating) return;

    t = ipgui_bw_anim8(rd->anim_from, rd->anim_to,
                       rd->anim_t0, ipgui_tick_now(), rd->anim_dur);
    if (t != rd->anim_last) {
        rd->anim_last = t;
        ipgui_widget_mark_dirty(widget);
    }
    if (t == rd->anim_to)
        rd->animating = 0;
}
