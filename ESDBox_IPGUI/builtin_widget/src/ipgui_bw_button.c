#include "ipgui_bw_button.h"
#include "ipgui_bw_theme.h"
#include "ipgui_memory.h"
#include "ipgui_time.h"

#define BW_BTN_TEXT_MAX     28
#define BW_BTN_PAD          1      /* 仅给描边留 1px，按钮不使用外阴影 */
#define BW_BTN_PRESS_MS     110
#define BW_BTN_RELEASE_MS   160

typedef struct {
    char                  text[BW_BTN_TEXT_MAX];
    u8_t                  variant;
    u8_t                  disabled;
    u8_t                  hovered;
    u8_t                  pressed;

    /* 按压动画：0 = 常态，255 = 完全按下 */
    u8_t                  anim_from;
    u8_t                  anim_to;
    u8_t                  anim_last;
    u8_t                  animating;
    ipgui_tick_t          anim_t0;
    u16_t                 anim_dur;

    /* 自定义主色，a == 0 表示使用 variant 默认 */
    ipgui_color_t         accent_top;
    ipgui_color_t         accent_bottom;
    ipgui_color_t         accent_text;

    ipgui_bw_button_cb_t  on_click;
} ipgui_bw_button_t;

static void bw_btn_copy_text(char * dst, const char * src)
{
    u32_t i = 0;

    if (!dst) return;
    if (!src) { dst[0] = '\0'; return; }

    while (src[i] && i < BW_BTN_TEXT_MAX - 1) {
        dst[i] = src[i];
        i ++;
    }
    dst[i] = '\0';
}

static ipgui_bw_palette_t bw_btn_palette(ipgui_bw_button_t * b)
{
    ipgui_bw_palette_t p;

    switch (b->variant) {
    case IPGUI_BW_BUTTON_SECONDARY:
        p.top    = BW_SURFACE_TOP;
        p.bottom = BW_SURFACE_BOT;
        p.border = BW_BORDER;
        p.text   = BW_ACCENT_DEEP;
        break;

    case IPGUI_BW_BUTTON_GHOST:
        p.top    = ipgui_bw_color(0xFF, 0xFF, 0xFF, 0);
        p.bottom = ipgui_bw_color(0xFF, 0xFF, 0xFF, 0);
        p.border = ipgui_bw_color(0x00, 0x00, 0x00, 0);
        p.text   = BW_ACCENT_DEEP;
        break;

    case IPGUI_BW_BUTTON_DANGER:
        p.top    = BW_DANGER_TOP;
        p.bottom = BW_DANGER_BOT;
        p.border = ipgui_bw_color(0xFF, 0xFF, 0xFF, 0x24);
        p.text   = BW_TEXT_ON_ACCENT;
        break;

    case IPGUI_BW_BUTTON_PRIMARY:
    default:
        p.top    = BW_ACCENT_TOP;
        p.bottom = BW_ACCENT_BOT;
        p.border = ipgui_bw_color(0xFF, 0xFF, 0xFF, 0x24);
        p.text   = BW_TEXT_ON_ACCENT;
        break;
    }

    if (b->accent_top.a || b->accent_bottom.a) {
        p.top    = b->accent_top;
        p.bottom = b->accent_bottom;
    }
    if (b->accent_text.a)
        p.text = b->accent_text;

    return p;
}

static void bw_btn_render(
    ipgui_widget_t * widget, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_bw_button_t * b = (ipgui_bw_button_t *)widget->priv_data;
    ipgui_surf_t      * s = ctx->surf;
    ipgui_aabb_t        box;
    ipgui_bw_palette_t  p;
    ipgui_coord_t       visual_h, radius, inset;
    u8_t                anim;
    u8_t                opacity;
    u8_t                filled;

    if (!b || !s) return;

    anim = ipgui_bw_anim8(b->anim_from, b->anim_to,
                          b->anim_t0, ipgui_tick_now(), b->anim_dur);
    b->anim_last = anim;

    box.start.x = BW_BTN_PAD;
    box.start.y = BW_BTN_PAD;
    box.end.x   = widget->w - 1 - BW_BTN_PAD;
    box.end.y   = widget->h - 1 - BW_BTN_PAD;

    /* 按下时整体内缩 1~2px，模拟物理按压 */
    inset = (ipgui_coord_t)((anim * 2) / 255);
    box.start.x += inset;
    box.start.y += inset;
    box.end.x   -= inset;
    box.end.y   -= inset;

    if (box.end.x <= box.start.x || box.end.y <= box.start.y)
        return;

    visual_h = ipgui_aabb_height(&box);
    radius   = visual_h / 2;
    if (radius > 14) radius = 14;
    if (radius < 4)  radius = 4;

    p = bw_btn_palette(b);
    filled = (b->variant == IPGUI_BW_BUTTON_PRIMARY ||
              b->variant == IPGUI_BW_BUTTON_DANGER);

    if (b->disabled) {
        opacity = 120;
    } else {
        opacity = 255;

        /* hover：整体提亮 */
        if (b->hovered) {
            if (filled) {
                p.top    = ipgui_bw_mix(p.top,    BW_SURFACE_TOP, 22);
                p.bottom = ipgui_bw_mix(p.bottom, BW_SURFACE_TOP, 22);
            } else if (b->variant == IPGUI_BW_BUTTON_SECONDARY) {
                p.top    = BW_SURFACE_TOP;
                p.bottom = BW_ACCENT_SOFT;
            }
        }

        /* press：整体压暗 */
        if (b->pressed) {
            if (filled) {
                p.top    = ipgui_bw_mix(p.top,    BW_TEXT, 26);
                p.bottom = ipgui_bw_mix(p.bottom, BW_TEXT, 26);
            } else if (b->variant == IPGUI_BW_BUTTON_SECONDARY) {
                p.top    = ipgui_bw_mix(p.top,    BW_TRACK, 120);
                p.bottom = ipgui_bw_mix(p.bottom, BW_TRACK, 120);
            }
        }
    }

    /* ---------------- 底色 ---------------- */
    if (!(b->variant == IPGUI_BW_BUTTON_GHOST && !b->hovered)) {
        if (b->variant == IPGUI_BW_BUTTON_GHOST) {
            /* 幽灵按钮 hover 时给一层很淡的主色底 */
            ipgui_bw_fill_vgrad(s, &box, radius,
                ipgui_bw_color(BW_ACCENT_TOP.r, BW_ACCENT_TOP.g,
                               BW_ACCENT_TOP.b, 0x30),
                ipgui_bw_color(BW_ACCENT_TOP.r, BW_ACCENT_TOP.g,
                               BW_ACCENT_TOP.b, 0x18),
                opacity);
        } else {
            ipgui_bw_fill_vgrad(s, &box, radius, p.top, p.bottom, opacity);
        }
    }

    /* ---------------- 描边 ---------------- */
    if (p.border.a > 0) {
        ipgui_bw_stroke_box(s, &box, radius, 1, p.border,
                            b->disabled ? 90 : 255);
    }

    /* ---------------- 顶部高光 ---------------- */
    if (filled && !b->disabled && visual_h >= 20) {
        ipgui_bw_top_gloss(s, &box, radius,
            ipgui_bw_color(0xFF, 0xFF, 0xFF, 0xFF),
            b->pressed ? 48 : 80);
    }

    /* ---------------- 文字 ---------------- */
    {
        /* 按钮文字比视觉盒略小一档，避免 48px 高时 19px 字过满 */
        const ipgui_font_t * font =
            ipgui_bw_font_for_height(IPGUI_MIN(visual_h, 38));
        ipgui_color_t tc = p.text;

        if (b->disabled) tc = BW_DISABLED_TEXT;

        ipgui_bw_text_center(s, &box, font, b->text, tc,
                             b->disabled ? 200 : 255);
    }
}

static int bw_btn_hit(ipgui_widget_t * widget, ipgui_coord_t x, ipgui_coord_t y)
{
    return (x >= 0 && y >= 0 && x < widget->w && y < widget->h);
}

static void bw_btn_anim_start(ipgui_bw_button_t * b, u8_t to, u16_t dur)
{
    b->anim_from  = b->anim_last;
    b->anim_to    = to;
    b->anim_t0    = ipgui_tick_now();
    b->anim_dur   = dur;
    b->animating  = 1;
}

static void bw_btn_event(
    ipgui_widget_t * widget, ipgui_widget_evt_t * evt)
{
    ipgui_bw_button_t * b = (ipgui_bw_button_t *)widget->priv_data;

    if (!b || b->disabled) return;

    switch (evt->type) {
    case IPGUI_WIDGET_EVENT_PRESSED:
        if (!b->pressed) {
            b->pressed = 1;
            b->hovered = 1;
            bw_btn_anim_start(b, 255, BW_BTN_PRESS_MS);
            ipgui_widget_mark_dirty(widget);
        }
        break;

    case IPGUI_WIDGET_EVENT_RELEASED:
        if (b->pressed) {
            int inside = bw_btn_hit(widget,
                evt->evt.released_evt.x, evt->evt.released_evt.y);

            b->pressed = 0;
            bw_btn_anim_start(b, 0, BW_BTN_RELEASE_MS);
            ipgui_widget_mark_dirty(widget);

            if (inside && b->on_click)
                b->on_click(widget);
        }
        break;

    case IPGUI_WIDGET_EVENT_HOVER: {
        u8_t hovered = (u8_t)bw_btn_hit(widget,
            evt->evt.hover_evt.x, evt->evt.hover_evt.y);

        if (hovered != b->hovered) {
            b->hovered = hovered;
            ipgui_widget_mark_dirty(widget);
        }
        break;
    }

    default:
        break;
    }
}

static void bw_btn_setup(ipgui_widget_t * w, const char * text)
{
    w->render        = bw_btn_render;
    w->event_handler = bw_btn_event;
    w->name          = "bw_button";
    bw_btn_copy_text(((ipgui_bw_button_t *)w->priv_data)->text, text);
}

ipgui_widget_t * ipgui_bw_button_create(
    ipgui_widget_t * parent,
    ipgui_coord_t x, ipgui_coord_t y,
    ipgui_coord_t w, ipgui_coord_t h,
    const char * text,
    ipgui_bw_button_variant_t variant)
{
    ipgui_widget_t     * widget;
    ipgui_bw_button_t  * b;

    widget = ipgui_widget_create(parent);
    if (!widget) return (ipgui_widget_t *)0;

    b = (ipgui_bw_button_t *)ipgui_mem_alloc_def(sizeof(ipgui_bw_button_t));
    if (!b) {
        ipgui_widget_link_detach(&widget->link);
        ipgui_mem_free_def(widget);
        return (ipgui_widget_t *)0;
    }
    ipgui_memset((void *)b, 0, sizeof(ipgui_bw_button_t));

    widget->x = x; widget->y = y;
    widget->w = w; widget->h = h;
    widget->priv_data = b;

    b->variant = (u8_t)variant;
    b->anim_dur = 0;

    bw_btn_setup(widget, text);

    return widget;
}

void ipgui_bw_button_set_text(ipgui_widget_t * widget, const char * text)
{
    ipgui_bw_button_t * b;

    if (!widget || !widget->priv_data) return;
    b = (ipgui_bw_button_t *)widget->priv_data;

    bw_btn_copy_text(b->text, text);
    ipgui_widget_mark_dirty(widget);
}

void ipgui_bw_button_set_variant(
    ipgui_widget_t * widget, ipgui_bw_button_variant_t variant)
{
    ipgui_bw_button_t * b;

    if (!widget || !widget->priv_data) return;
    b = (ipgui_bw_button_t *)widget->priv_data;

    b->variant = (u8_t)variant;
    ipgui_widget_mark_dirty(widget);
}

void ipgui_bw_button_set_enabled(ipgui_widget_t * widget, u8_t enabled)
{
    ipgui_bw_button_t * b;

    if (!widget || !widget->priv_data) return;
    b = (ipgui_bw_button_t *)widget->priv_data;

    b->disabled = enabled ? 0 : 1;
    if (b->disabled) {
        b->pressed = 0;
        b->hovered = 0;
    }
    ipgui_widget_mark_dirty(widget);
}

void ipgui_bw_button_set_on_click(
    ipgui_widget_t * widget, ipgui_bw_button_cb_t cb)
{
    ipgui_bw_button_t * b;

    if (!widget || !widget->priv_data) return;
    b = (ipgui_bw_button_t *)widget->priv_data;
    b->on_click = cb;
}

void ipgui_bw_button_set_accent(
    ipgui_widget_t * widget,
    ipgui_color_t top, ipgui_color_t bottom,
    ipgui_color_t text_color)
{
    ipgui_bw_button_t * b;

    if (!widget || !widget->priv_data) return;
    b = (ipgui_bw_button_t *)widget->priv_data;

    b->accent_top    = top;
    b->accent_bottom = bottom;
    b->accent_text   = text_color;
    ipgui_widget_mark_dirty(widget);
}

void ipgui_bw_button_task(ipgui_widget_t * widget)
{
    ipgui_bw_button_t * b;
    u8_t v;

    if (!widget || !widget->priv_data) return;
    b = (ipgui_bw_button_t *)widget->priv_data;

    if (!b->animating) return;

    v = ipgui_bw_anim8(b->anim_from, b->anim_to,
                       b->anim_t0, ipgui_tick_now(), b->anim_dur);
    if (v != b->anim_last) {
        b->anim_last = v;
        ipgui_widget_mark_dirty(widget);
    }
    if (v == b->anim_to)
        b->animating = 0;
}
