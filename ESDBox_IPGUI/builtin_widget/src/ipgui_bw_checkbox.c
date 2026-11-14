#include "ipgui_bw_checkbox.h"
#include "ipgui_bw_theme.h"
#include "ipgui_memory.h"
#include "ipgui_time.h"

#define BW_CB_PAD        4
#define BW_CB_LABEL_MAX  32
#define BW_CB_DURATION   150

typedef struct {
    char                  label[BW_CB_LABEL_MAX];
    u8_t                  checked;
    u8_t                  disabled;
    u8_t                  hovered;
    u8_t                  pressed;

    u8_t                  anim_from;
    u8_t                  anim_to;
    u8_t                  anim_last;
    u8_t                  animating;
    ipgui_tick_t          anim_t0;
    u16_t                 anim_dur;

    ipgui_bw_checkbox_cb_t on_change;
} ipgui_bw_checkbox_t;

/* -------------------------------------------------------------------------- */

static void bw_cb_copy_label(char * dst, const char * src)
{
    u32_t i = 0;

    if (!dst) return;
    if (!src) { dst[0] = '\0'; return; }

    while (src[i] && i < BW_CB_LABEL_MAX - 1) {
        dst[i] = src[i];
        i ++;
    }
    dst[i] = '\0';
}

static void bw_cb_render(
    ipgui_widget_t * widget, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_bw_checkbox_t * cb = (ipgui_bw_checkbox_t *)widget->priv_data;
    ipgui_surf_t        * s  = ctx->surf;
    ipgui_aabb_t          box;
    ipgui_coord_t         side, bx, by, cy, radius;
    ipgui_color_t         accent_mid, fill, border;
    u8_t                  t;
    u8_t                  shrink;

    if (!cb || !s) return;

    side = widget->h - BW_CB_PAD * 2;
    if (side > 22) side = 22;
    if (side < 12) return;

    t = ipgui_bw_anim8(cb->anim_from, cb->anim_to,
                       cb->anim_t0, ipgui_tick_now(), cb->anim_dur);
    cb->anim_last = t;

    cy   = widget->h / 2;
    shrink = cb->pressed ? 1 : 0;

    bx = BW_CB_PAD + shrink;
    by = cy - side / 2 + shrink;

    box.start.x = bx;
    box.start.y = by;
    box.end.x   = bx + side - 1;
    box.end.y   = by + side - 1;

    radius      = side / 4;
    if (radius < 4) radius = 4;
    if (radius > 7) radius = 7;

    accent_mid = ipgui_bw_mix(BW_ACCENT_TOP, BW_ACCENT_BOT, 128);

    /* 未选中：白色底 + 深色描边；选中：主色渐变 */
    fill   = ipgui_bw_mix(BW_SURFACE_TOP, accent_mid, t);
    border = ipgui_bw_mix(BW_BORDER_STRONG,
                          ipgui_bw_color(0xFF, 0xFF, 0xFF, 0x34), t);

    /* 悬停时描边靠近主色，给出可交互反馈 */
    if (cb->hovered && !cb->disabled)
        border = ipgui_bw_mix(border, accent_mid, 120);

    if (cb->pressed && t < 128)
        fill = ipgui_bw_mix(fill, BW_TRACK, 140);

    /* 轻微投影 */
    ipgui_bw_shadow_box(s, &box, radius,
        ipgui_bw_color(0x2A, 0x3A, 0x5F, 0xFF),
        cb->disabled ? 12 : (16 + t / 8), 6, 0, 0, 2);

    ipgui_bw_fill_box(s, &box, radius, fill, cb->disabled ? 150 : 255);
    ipgui_bw_stroke_box(s, &box, radius, 1, border,
                        cb->disabled ? 90 : 200);

    if (t > 200)
        ipgui_bw_top_gloss(s, &box, radius,
            ipgui_bw_color(0xFF, 0xFF, 0xFF, 0xFF),
            (u8_t)(((u32_t)(t - 200) * 90) / 55));

    /* 对勾：以方块中心缩放出现 */
    if (t > 0) {
        ipgui_line_t       ln;
        ipgui_line_style_t st;
        float              cx = (float)(box.start.x + box.end.x) * 0.5f;
        float              cyf = (float)(box.start.y + box.end.y) * 0.5f;
        float              sc = 0.55f + 0.45f * ((float)t / 255.0f);
        ipgui_pointf_t     p[3];
        int                i;

        p[0].x = -0.22f; p[0].y =  0.02f;
        p[1].x = -0.04f; p[1].y =  0.20f;
        p[2].x =  0.24f; p[2].y = -0.18f;

        st.width           = (side / 10 > 2) ? side / 10 : 2;
        st.cap             = IPGUI_LINE_CAP_ROUND;
        st.paint.type      = IPGUI_PAINT_COLOR;
        st.paint.src.color = ipgui_bw_color(0xFF, 0xFF, 0xFF, 0xFF);
        st.opacity         = t;
        st.blend_mode      = IPGUI_BLEND_NORMAL;
        if (st.width < 2) st.width = 2;

        for (i = 0; i < 2; i ++) {
            ln.start.x = (ipgui_coord_t)(cx + p[i].x     * (float)side * sc);
            ln.start.y = (ipgui_coord_t)(cyf + p[i].y    * (float)side * sc);
            ln.end.x   = (ipgui_coord_t)(cx + p[i + 1].x * (float)side * sc);
            ln.end.y   = (ipgui_coord_t)(cyf + p[i + 1].y * (float)side * sc);
            ipgui_draw_line_generic(s, (ipgui_aabb_t *)0, &ln, &st);
        }
    }

    /* 右侧文字 */
    if (cb->label[0] && widget->w > side + BW_CB_PAD * 2 + 12) {
        ipgui_aabb_t text_box;
        const ipgui_font_t * font;

        text_box.start.x = box.end.x + 10;
        text_box.start.y = BW_CB_PAD;
        text_box.end.x   = widget->w - 1 - BW_CB_PAD;
        text_box.end.y   = widget->h - 1 - BW_CB_PAD;

        font = ipgui_bw_font_for_height(side);
        ipgui_bw_text_left(s, &text_box, font, cb->label, 0,
                           cb->disabled ? BW_DISABLED_TEXT : BW_TEXT,
                           cb->disabled ? 200 : 255);
    }
}

/* -------------------------------------------------------------------------- */

static int bw_cb_hit(ipgui_widget_t * widget, ipgui_coord_t x, ipgui_coord_t y)
{
    return (x >= 0 && y >= 0 && x < widget->w && y < widget->h);
}

static void bw_cb_anim_start(ipgui_bw_checkbox_t * cb, u8_t to)
{
    cb->anim_from = cb->anim_last;
    cb->anim_to   = to;
    cb->anim_t0   = ipgui_tick_now();
    cb->anim_dur  = BW_CB_DURATION;
    cb->animating = 1;
}

static void bw_cb_event(
    ipgui_widget_t * widget, ipgui_widget_evt_t * evt)
{
    ipgui_bw_checkbox_t * cb = (ipgui_bw_checkbox_t *)widget->priv_data;

    if (!cb || cb->disabled) return;

    switch (evt->type) {
    case IPGUI_WIDGET_EVENT_PRESSED:
        if (!cb->pressed) {
            cb->pressed = 1;
            cb->hovered = 1;
            ipgui_widget_mark_dirty(widget);
        }
        break;

    case IPGUI_WIDGET_EVENT_RELEASED:
        if (cb->pressed) {
            int inside = bw_cb_hit(widget,
                evt->evt.released_evt.x, evt->evt.released_evt.y);

            cb->pressed = 0;
            if (inside) {
                cb->checked = cb->checked ? 0 : 1;
                bw_cb_anim_start(cb, cb->checked ? 255 : 0);
                if (cb->on_change) cb->on_change(widget, cb->checked);
            }
            ipgui_widget_mark_dirty(widget);
        }
        break;

    case IPGUI_WIDGET_EVENT_HOVER: {
        u8_t hovered = (u8_t)bw_cb_hit(widget,
            evt->evt.hover_evt.x, evt->evt.hover_evt.y);

        if (hovered != cb->hovered) {
            cb->hovered = hovered;
            ipgui_widget_mark_dirty(widget);
        }
        break;
    }

    default:
        break;
    }
}

/* -------------------------------------------------------------------------- */

ipgui_widget_t * ipgui_bw_checkbox_create(
    ipgui_widget_t * parent,
    ipgui_coord_t x, ipgui_coord_t y,
    ipgui_coord_t w, ipgui_coord_t h,
    const char * label,
    u8_t checked)
{
    ipgui_widget_t       * widget;
    ipgui_bw_checkbox_t  * cb;

    widget = ipgui_widget_create(parent);
    if (!widget) return (ipgui_widget_t *)0;

    cb = (ipgui_bw_checkbox_t *)ipgui_mem_alloc_def(sizeof(ipgui_bw_checkbox_t));
    if (!cb) {
        ipgui_widget_link_detach(&widget->link);
        ipgui_mem_free_def(widget);
        return (ipgui_widget_t *)0;
    }
    ipgui_memset((void *)cb, 0, sizeof(ipgui_bw_checkbox_t));

    widget->x = x; widget->y = y;
    widget->w = w; widget->h = h;
    widget->priv_data = cb;
    widget->render        = bw_cb_render;
    widget->event_handler = bw_cb_event;
    widget->name          = "bw_checkbox";

    cb->checked   = checked ? 1 : 0;
    cb->anim_last = cb->checked ? 255 : 0;
    cb->anim_from = cb->anim_last;
    cb->anim_to   = cb->anim_last;

    bw_cb_copy_label(cb->label, label);

    return widget;
}

void ipgui_bw_checkbox_set_checked(
    ipgui_widget_t * widget, u8_t checked, u8_t animated)
{
    ipgui_bw_checkbox_t * cb;

    if (!widget || !widget->priv_data) return;
    cb = (ipgui_bw_checkbox_t *)widget->priv_data;

    cb->checked = checked ? 1 : 0;
    if (animated) {
        bw_cb_anim_start(cb, cb->checked ? 255 : 0);
    } else {
        cb->anim_last = cb->checked ? 255 : 0;
        cb->anim_from = cb->anim_last;
        cb->anim_to   = cb->anim_last;
        cb->animating = 0;
    }
    ipgui_widget_mark_dirty(widget);
}

u8_t ipgui_bw_checkbox_get_checked(ipgui_widget_t * widget)
{
    ipgui_bw_checkbox_t * cb;

    if (!widget || !widget->priv_data) return 0;
    cb = (ipgui_bw_checkbox_t *)widget->priv_data;
    return cb->checked;
}

void ipgui_bw_checkbox_set_label(ipgui_widget_t * widget, const char * label)
{
    ipgui_bw_checkbox_t * cb;

    if (!widget || !widget->priv_data) return;
    cb = (ipgui_bw_checkbox_t *)widget->priv_data;

    bw_cb_copy_label(cb->label, label);
    ipgui_widget_mark_dirty(widget);
}

void ipgui_bw_checkbox_set_enabled(ipgui_widget_t * widget, u8_t enabled)
{
    ipgui_bw_checkbox_t * cb;

    if (!widget || !widget->priv_data) return;
    cb = (ipgui_bw_checkbox_t *)widget->priv_data;

    cb->disabled = enabled ? 0 : 1;
    if (cb->disabled) {
        cb->pressed = 0;
        cb->hovered = 0;
    }
    ipgui_widget_mark_dirty(widget);
}

void ipgui_bw_checkbox_set_on_change(
    ipgui_widget_t * widget, ipgui_bw_checkbox_cb_t cb)
{
    ipgui_bw_checkbox_t * c;

    if (!widget || !widget->priv_data) return;
    c = (ipgui_bw_checkbox_t *)widget->priv_data;
    c->on_change = cb;
}

void ipgui_bw_checkbox_task(ipgui_widget_t * widget)
{
    ipgui_bw_checkbox_t * cb;
    u8_t t;

    if (!widget || !widget->priv_data) return;
    cb = (ipgui_bw_checkbox_t *)widget->priv_data;

    if (!cb->animating) return;

    t = ipgui_bw_anim8(cb->anim_from, cb->anim_to,
                       cb->anim_t0, ipgui_tick_now(), cb->anim_dur);
    if (t != cb->anim_last) {
        cb->anim_last = t;
        ipgui_widget_mark_dirty(widget);
    }
    if (t == cb->anim_to)
        cb->animating = 0;
}
