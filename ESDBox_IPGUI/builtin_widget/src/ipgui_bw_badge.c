#include "ipgui_bw_badge.h"
#include "ipgui_bw_theme.h"
#include "ipgui_memory.h"

#define BW_BD_PAD       1
#define BW_BD_TEXT_MAX  24

typedef struct {
    char      text[BW_BD_TEXT_MAX];
    u8_t      tone;
    u8_t      dot;
} ipgui_bw_badge_t;

static void bw_bd_copy(char * dst, const char * src)
{
    u32_t i = 0;

    if (!dst) return;
    if (!src) { dst[0] = '\0'; return; }

    while (src[i] && i < BW_BD_TEXT_MAX - 1) {
        dst[i] = src[i];
        i ++;
    }
    dst[i] = '\0';
}

static ipgui_color_t bw_bd_tone(u8_t tone)
{
    switch (tone) {
    case IPGUI_BW_BADGE_INFO:
        return ipgui_bw_color(0x3B, 0x82, 0xF6, 0xFF);
    case IPGUI_BW_BADGE_SUCCESS:
        return ipgui_bw_color(0x10, 0xB9, 0x81, 0xFF);
    case IPGUI_BW_BADGE_WARNING:
        return ipgui_bw_color(0xF5, 0x9E, 0x0B, 0xFF);
    case IPGUI_BW_BADGE_DANGER:
        return ipgui_bw_color(0xEF, 0x44, 0x44, 0xFF);
    case IPGUI_BW_BADGE_NEUTRAL:
    default:
        return ipgui_bw_color(0x6B, 0x74, 0x88, 0xFF);
    }
}

static void bw_bd_render(
    ipgui_widget_t * widget, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_bw_badge_t * bd = (ipgui_bw_badge_t *)widget->priv_data;
    ipgui_surf_t     * s  = ctx->surf;
    ipgui_aabb_t       box, tb;
    ipgui_coord_t      radius, dot_r, dot_cx, text_x;
    ipgui_color_t      tone, bg_top, bg_bot, border, text_color;

    if (!bd || !s) return;

    box.start.x = BW_BD_PAD;
    box.start.y = BW_BD_PAD;
    box.end.x   = widget->w - 1 - BW_BD_PAD;
    box.end.y   = widget->h - 1 - BW_BD_PAD;

    if (ipgui_aabb_width(&box) < 16 || ipgui_aabb_height(&box) < 12)
        return;

    radius = ipgui_aabb_height(&box) / 2;
    tone   = bw_bd_tone(bd->tone);

    /* 浅色底 + 同色调描边，文字用加深的色调 */
    bg_top     = ipgui_bw_mix(BW_SURFACE_TOP, tone, 28);
    bg_bot     = ipgui_bw_mix(BW_SURFACE_TOP, tone, 40);
    border     = ipgui_bw_mix(BW_SURFACE_TOP, tone, 70);
    text_color = ipgui_bw_mix(tone, BW_TEXT, 60);

    ipgui_bw_fill_vgrad(s, &box, radius, bg_top, bg_bot, 255);
    ipgui_bw_stroke_box(s, &box, radius, 1, border, 255);

    dot_r = ipgui_aabb_height(&box) / 8;
    if (dot_r < 2) dot_r = 2;
    if (dot_r > 4) dot_r = 4;

    dot_cx = box.start.x + 12;
    text_x = box.start.x + 12;

    if (bd->dot) {
        ipgui_filled_circle_style_t cs;

        cs.paint.type      = IPGUI_PAINT_COLOR;
        cs.paint.src.color = tone;
        cs.opacity         = 255;
        cs.blend_mode      = IPGUI_BLEND_NORMAL;
        ipgui_draw_filled_circle(s, (ipgui_aabb_t *)0,
            dot_cx, (box.start.y + box.end.y) / 2, dot_r, &cs);

        text_x = dot_cx + dot_r + 7;
    }

    if (bd->text[0]) {
        tb.start.x = text_x;
        tb.start.y = box.start.y;
        tb.end.x   = box.end.x - 10;
        tb.end.y   = box.end.y;

        ipgui_bw_text_left(s, &tb,
            ipgui_bw_font_small_for_height(ipgui_aabb_height(&box)),
            bd->text, 0, text_color, 255);
    }
}

ipgui_widget_t * ipgui_bw_badge_create(
    ipgui_widget_t * parent,
    ipgui_coord_t x, ipgui_coord_t y,
    ipgui_coord_t w, ipgui_coord_t h,
    const char * text,
    ipgui_bw_badge_tone_t tone)
{
    ipgui_widget_t    * widget;
    ipgui_bw_badge_t  * bd;

    widget = ipgui_widget_create(parent);
    if (!widget) return (ipgui_widget_t *)0;

    bd = (ipgui_bw_badge_t *)ipgui_mem_alloc_def(sizeof(ipgui_bw_badge_t));
    if (!bd) {
        ipgui_widget_link_detach(&widget->link);
        ipgui_mem_free_def(widget);
        return (ipgui_widget_t *)0;
    }
    ipgui_memset((void *)bd, 0, sizeof(ipgui_bw_badge_t));

    widget->x = x; widget->y = y;
    widget->w = w; widget->h = h;
    widget->priv_data = bd;
    widget->render        = bw_bd_render;
    widget->event_handler = 0;
    widget->name          = "bw_badge";

    bd->tone = (u8_t)tone;
    bd->dot  = 1;
    bw_bd_copy(bd->text, text);

    return widget;
}

void ipgui_bw_badge_set_text(ipgui_widget_t * widget, const char * text)
{
    ipgui_bw_badge_t * bd;

    if (!widget || !widget->priv_data) return;
    bd = (ipgui_bw_badge_t *)widget->priv_data;

    bw_bd_copy(bd->text, text);
    ipgui_widget_mark_dirty(widget);
}

void ipgui_bw_badge_set_tone(
    ipgui_widget_t * widget, ipgui_bw_badge_tone_t tone)
{
    ipgui_bw_badge_t * bd;

    if (!widget || !widget->priv_data) return;
    bd = (ipgui_bw_badge_t *)widget->priv_data;

    bd->tone = (u8_t)tone;
    ipgui_widget_mark_dirty(widget);
}

void ipgui_bw_badge_set_dot(ipgui_widget_t * widget, u8_t show)
{
    ipgui_bw_badge_t * bd;

    if (!widget || !widget->priv_data) return;
    bd = (ipgui_bw_badge_t *)widget->priv_data;

    bd->dot = show ? 1 : 0;
    ipgui_widget_mark_dirty(widget);
}
