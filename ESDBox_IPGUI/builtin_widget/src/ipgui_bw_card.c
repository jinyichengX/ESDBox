#include "ipgui_bw_card.h"
#include "ipgui_bw_theme.h"
#include "ipgui_memory.h"

#define BW_CD_PAD        6
#define BW_CD_TITLE_MAX  32
#define BW_CD_SUB_MAX    48
#define BW_CD_HEAD_PAD   16

typedef struct {
    char title[BW_CD_TITLE_MAX];
    char subtitle[BW_CD_SUB_MAX];
    u8_t divider;
} ipgui_bw_card_t;

/* -------------------------------------------------------------------------- */

static void bw_cd_copy(char * dst, u32_t cap, const char * src)
{
    u32_t i = 0;

    if (!dst) return;
    if (!src) { dst[0] = '\0'; return; }

    while (src[i] && i < cap - 1) {
        dst[i] = src[i];
        i ++;
    }
    dst[i] = '\0';
}

static void bw_cd_render(
    ipgui_widget_t * widget, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_bw_card_t * cd = (ipgui_bw_card_t *)widget->priv_data;
    ipgui_surf_t    * s  = ctx->surf;
    ipgui_aabb_t      box, tb;
    ipgui_coord_t     y;
    u8_t              has_title;

    if (!cd || !s) return;

    box.start.x = BW_CD_PAD;
    box.start.y = BW_CD_PAD;
    box.end.x   = widget->w - 1 - BW_CD_PAD;
    box.end.y   = widget->h - 1 - BW_CD_PAD;

    if (box.end.x - box.start.x < 20 || box.end.y - box.start.y < 20)
        return;

    /* 阴影受控件矩形裁剪，模糊与偏移控制在留白以内 */
    ipgui_bw_shadow_box(s, &box, 18,
        ipgui_bw_color(0x2A, 0x3A, 0x5F, 0xFF), 26, 10, 0, 0, 4);

    ipgui_bw_fill_vgrad(s, &box, 18,
        ipgui_bw_color(0xFF, 0xFF, 0xFF, 0xFF),
        ipgui_bw_color(0xFB, 0xFC, 0xFF, 0xFF), 255);
    ipgui_bw_stroke_box(s, &box, 18, 1, BW_BORDER, 235);

    has_title = cd->title[0] ? 1 : 0;
    if (!has_title) return;

    y = box.start.y + 14;

    /* 标题 */
    tb.start.x = box.start.x + BW_CD_HEAD_PAD;
    tb.start.y = y;
    tb.end.x   = box.end.x - BW_CD_HEAD_PAD;
    tb.end.y   = y + 20;
    ipgui_bw_text_left(s, &tb, &quicksand_medium_17px,
                       cd->title, 0, BW_TEXT, 255);
    y += quicksand_medium_17px.line_height;

    /* 副标题 */
    if (cd->subtitle[0]) {
        y += 2;
        tb.start.y = y;
        tb.end.y   = y + 14;
        ipgui_bw_text_left(s, &tb, &quicksand_medium_11px,
                           cd->subtitle, 0, BW_TEXT_MUTED, 255);
        y += quicksand_medium_11px.line_height;
    }

    /* 标题区下方分隔线 */
    if (cd->divider) {
        y += 12;
        ipgui_bw_hairline(s,
            box.start.x + BW_CD_HEAD_PAD, y,
            box.end.x - BW_CD_HEAD_PAD,   y,
            BW_BORDER, 200);
    }
}

/* -------------------------------------------------------------------------- */

ipgui_widget_t * ipgui_bw_card_create(
    ipgui_widget_t * parent,
    ipgui_coord_t x, ipgui_coord_t y,
    ipgui_coord_t w, ipgui_coord_t h,
    const char * title,
    const char * subtitle)
{
    ipgui_widget_t   * widget;
    ipgui_bw_card_t  * cd;

    widget = ipgui_widget_create(parent);
    if (!widget) return (ipgui_widget_t *)0;

    cd = (ipgui_bw_card_t *)ipgui_mem_alloc_def(sizeof(ipgui_bw_card_t));
    if (!cd) {
        ipgui_widget_link_detach(&widget->link);
        ipgui_mem_free_def(widget);
        return (ipgui_widget_t *)0;
    }
    ipgui_memset((void *)cd, 0, sizeof(ipgui_bw_card_t));

    widget->x = x; widget->y = y;
    widget->w = w; widget->h = h;
    widget->priv_data = cd;
    widget->render        = bw_cd_render;
    widget->event_handler = 0;
    widget->name          = "bw_card";

    cd->divider = 1;
    bw_cd_copy(cd->title,    BW_CD_TITLE_MAX, title);
    bw_cd_copy(cd->subtitle, BW_CD_SUB_MAX,   subtitle);

    return widget;
}

void ipgui_bw_card_set_title(ipgui_widget_t * widget, const char * title)
{
    ipgui_bw_card_t * cd;

    if (!widget || !widget->priv_data) return;
    cd = (ipgui_bw_card_t *)widget->priv_data;

    bw_cd_copy(cd->title, BW_CD_TITLE_MAX, title);
    ipgui_widget_mark_dirty(widget);
}

void ipgui_bw_card_set_subtitle(
    ipgui_widget_t * widget, const char * subtitle)
{
    ipgui_bw_card_t * cd;

    if (!widget || !widget->priv_data) return;
    cd = (ipgui_bw_card_t *)widget->priv_data;

    bw_cd_copy(cd->subtitle, BW_CD_SUB_MAX, subtitle);
    ipgui_widget_mark_dirty(widget);
}

void ipgui_bw_card_set_divider(ipgui_widget_t * widget, u8_t show)
{
    ipgui_bw_card_t * cd;

    if (!widget || !widget->priv_data) return;
    cd = (ipgui_bw_card_t *)widget->priv_data;

    cd->divider = show ? 1 : 0;
    ipgui_widget_mark_dirty(widget);
}
