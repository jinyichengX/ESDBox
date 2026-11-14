#include "ipgui_bw_progress.h"
#include "ipgui_bw_theme.h"
#include "ipgui_memory.h"

#define BW_PG_PAD 2

typedef struct {
    u8_t          value;      /* 0..100 */
    u8_t          show_text;
    ipgui_color_t accent_left;
    ipgui_color_t accent_right;
} ipgui_bw_progress_t;

/* -------------------------------------------------------------------------- */

static void bw_pg_render(
    ipgui_widget_t * widget, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_bw_progress_t * pg = (ipgui_bw_progress_t *)widget->priv_data;
    ipgui_surf_t        * s  = ctx->surf;
    ipgui_aabb_t          track, fill;
    ipgui_coord_t         radius, fw;
    ipgui_color_t         ac_l, ac_r;

    if (!pg || !s) return;

    track.start.x = BW_PG_PAD;
    track.start.y = BW_PG_PAD;
    track.end.x   = widget->w - 1 - BW_PG_PAD;
    track.end.y   = widget->h - 1 - BW_PG_PAD;

    if (ipgui_aabb_width(&track) < 8 || ipgui_aabb_height(&track) < 4)
        return;

    radius = ipgui_aabb_height(&track) / 2;

    ac_l = pg->accent_left.a  ? pg->accent_left  : BW_ACCENT_TOP;
    ac_r = pg->accent_right.a ? pg->accent_right : BW_ACCENT_BOT;

    /* ---------------- 轨道 ---------------- */
    ipgui_bw_fill_box(s, &track, radius, BW_TRACK, 255);
    ipgui_bw_stroke_box(s, &track, radius, 1, BW_TRACK_BORDER, 150);

    /* ---------------- 填充 ---------------- */
    fw = (ipgui_coord_t)(((s32_t)ipgui_aabb_width(&track) * pg->value) / 100);
    if (fw > 0) {
        ipgui_coord_t r;

        fill.start.x = track.start.x;
        fill.start.y = track.start.y;
        fill.end.x   = track.start.x + fw - 1;
        fill.end.y   = track.end.y;
        if (fill.end.x > track.end.x) fill.end.x = track.end.x;

        r = radius;
        if (fw / 2 < r) r = fw / 2;
        if (r < 1) r = 1;

        ipgui_bw_fill_hgrad(s, &fill, r, ac_l, ac_r, 255);
        ipgui_bw_top_gloss(s, &fill, r,
            ipgui_bw_color(0xFF, 0xFF, 0xFF, 0xFF), 70);
    }

    /* ---------------- 百分比 ---------------- */
    if (pg->show_text) {
        char buf[8];
        int  len = 0;
        u8_t v = pg->value;
        ipgui_coord_t center_x = (track.start.x + track.end.x) / 2;
        ipgui_color_t color = (fill.end.x >= center_x)
                              ? BW_TEXT_ON_ACCENT
                              : BW_TEXT_MUTED;
        const ipgui_font_t * font =
            ipgui_bw_font_small_for_height(ipgui_aabb_height(&track));

        if (v >= 100) {
            buf[len++] = '1';
            buf[len++] = '0';
            buf[len++] = '0';
        } else {
            if (v >= 10) buf[len++] = (char)('0' + v / 10);
            buf[len++] = (char)('0' + v % 10);
        }
        buf[len++] = '%';
        buf[len]   = '\0';

        ipgui_bw_text_center(s, &track, font, buf, color, 255);
    }
}

/* -------------------------------------------------------------------------- */

ipgui_widget_t * ipgui_bw_progress_create(
    ipgui_widget_t * parent,
    ipgui_coord_t x, ipgui_coord_t y,
    ipgui_coord_t w, ipgui_coord_t h,
    u8_t value,
    u8_t show_text)
{
    ipgui_widget_t       * widget;
    ipgui_bw_progress_t  * pg;

    widget = ipgui_widget_create(parent);
    if (!widget) return (ipgui_widget_t *)0;

    pg = (ipgui_bw_progress_t *)ipgui_mem_alloc_def(sizeof(ipgui_bw_progress_t));
    if (!pg) {
        ipgui_widget_link_detach(&widget->link);
        ipgui_mem_free_def(widget);
        return (ipgui_widget_t *)0;
    }
    ipgui_memset((void *)pg, 0, sizeof(ipgui_bw_progress_t));

    widget->x = x; widget->y = y;
    widget->w = w; widget->h = h;
    widget->priv_data = pg;
    widget->render        = bw_pg_render;
    widget->event_handler = 0;
    widget->name          = "bw_progress";

    pg->value     = (value > 100) ? 100 : value;
    pg->show_text = show_text ? 1 : 0;

    return widget;
}

void ipgui_bw_progress_set_value(ipgui_widget_t * widget, u8_t value)
{
    ipgui_bw_progress_t * pg;

    if (!widget || !widget->priv_data) return;
    pg = (ipgui_bw_progress_t *)widget->priv_data;

    if (value > 100) value = 100;
    if (value == pg->value) return;

    pg->value = value;
    ipgui_widget_mark_dirty(widget);
}

u8_t ipgui_bw_progress_get_value(ipgui_widget_t * widget)
{
    ipgui_bw_progress_t * pg;

    if (!widget || !widget->priv_data) return 0;
    pg = (ipgui_bw_progress_t *)widget->priv_data;
    return pg->value;
}

void ipgui_bw_progress_set_show_text(ipgui_widget_t * widget, u8_t show)
{
    ipgui_bw_progress_t * pg;

    if (!widget || !widget->priv_data) return;
    pg = (ipgui_bw_progress_t *)widget->priv_data;

    pg->show_text = show ? 1 : 0;
    ipgui_widget_mark_dirty(widget);
}

void ipgui_bw_progress_set_accent(
    ipgui_widget_t * widget, ipgui_color_t left, ipgui_color_t right)
{
    ipgui_bw_progress_t * pg;

    if (!widget || !widget->priv_data) return;
    pg = (ipgui_bw_progress_t *)widget->priv_data;

    pg->accent_left  = left;
    pg->accent_right = right;
    ipgui_widget_mark_dirty(widget);
}
