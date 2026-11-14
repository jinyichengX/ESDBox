#include "ipgui_bw_ring_progress.h"
#include "ipgui_bw_theme.h"
#include "ipgui_draw_arc.h"
#include "ipgui_memory.h"
#include "ipgui_time.h"

#define BW_RP_PAD        2
#define BW_RP_LABEL_MAX  24
#define BW_RP_DURATION   320

typedef struct {
    char            label[BW_RP_LABEL_MAX];
    u8_t            value;        /* 目标值 0..100 */
    u8_t            thickness;
    u8_t            anim_from;    /* 0..255，255 表示 100% */
    u8_t            anim_to;
    u8_t            anim_last;
    u8_t            animating;
    ipgui_tick_t    anim_t0;
    u16_t           anim_dur;

    ipgui_color_t   accent_from;
    ipgui_color_t   accent_to;
} ipgui_bw_ring_progress_t;

/* -------------------------------------------------------------------------- */

static void bw_rp_copy_label(char * dst, const char * src)
{
    u32_t i = 0;

    if (!dst) return;
    if (!src) { dst[0] = '\0'; return; }

    while (src[i] && i < BW_RP_LABEL_MAX - 1) {
        dst[i] = src[i];
        i ++;
    }
    dst[i] = '\0';
}

static void bw_rp_arc(
    ipgui_surf_t * s, ipgui_coord_t cx, ipgui_coord_t cy,
    ipgui_coord_t er, ipgui_coord_t ir,
    ipgui_arc_angle_t start, u16_t angle,
    ipgui_paint_t * paint, u8_t opacity,
    ipgui_arc_endpoint_type_t cap)
{
    ipgui_arc_t       arc;
    ipgui_arc_style_t st;

    arc.cx    = cx;
    arc.cy    = cy;
    arc.er    = er;
    arc.ir    = ir;
    arc.start = start;
    arc.dir   = IPGUI_ARC_DRAW_DIR_CW;
    arc.angle = angle;

    st.paint     = *paint;
    st.opacity   = opacity;
    st.sep_type  = cap;
    st.eep_type  = cap;
    st.blend_mode = IPGUI_BLEND_NORMAL;

    ipgui_draw_arc(s, (ipgui_aabb_t *)0, &arc, &st);
}

static void bw_rp_render(
    ipgui_widget_t * widget, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_bw_ring_progress_t * rp =
        (ipgui_bw_ring_progress_t *)widget->priv_data;
    ipgui_surf_t * s = ctx->surf;
    ipgui_coord_t  cx, cy, r_out, r_in, thick, r;
    ipgui_aabb_t   bbox;
    ipgui_paint_t  track_paint, arc_paint;
    u8_t           t;
    u8_t           v;
    char           buf[8];
    int            len = 0;

    if (!rp || !s) return;

    thick = rp->thickness;
    if (thick < 2) thick = 2;

    r = IPGUI_MIN(widget->w, widget->h) / 2 - BW_RP_PAD;
    if (r <= thick + 2) return;

    r_out = r;
    r_in  = r - thick;

    cx = widget->w / 2;
    cy = widget->h / 2;

    t = ipgui_bw_anim8(rp->anim_from, rp->anim_to,
                       rp->anim_t0, ipgui_tick_now(), rp->anim_dur);
    rp->anim_last = t;

    v = (u8_t)(((u32_t)t * 100u + 127u) / 255u);

    /* ---------------- 轨道 ---------------- */
    track_paint.type           = IPGUI_PAINT_COLOR;
    track_paint.src.color      = BW_TRACK;
    bw_rp_arc(s, cx, cy, r_out, r_in, 0, 360, &track_paint, 255,
              IPGUI_ARC_ENDPOINT_TYPE_BUTT);

    /* ---------------- 进度弧 ---------------- */
    if (t > 0) {
        ipgui_color_t c0 = rp->accent_from.a ? rp->accent_from : BW_ACCENT_TOP;
        ipgui_color_t c1 = rp->accent_to.a   ? rp->accent_to   : BW_ACCENT_BOT;

        bbox.start.x = cx - r_out;
        bbox.start.y = cy - r_out;
        bbox.end.x   = cx + r_out;
        bbox.end.y   = cy + r_out;

        arc_paint.type = IPGUI_PAINT_GRADIENT;
        arc_paint.src.grad_src.grad_type = IPGUI_GRADIENT_TYPE_LINEAR;
        ipgui_liner_gradient_init_direct(
            &arc_paint.src.grad_src.grad.liner_grad,
            bbox.start.x, bbox.start.y,
            bbox.end.x,   bbox.end.y);

        {
            ipgui_gradient_color_stop_t s0, s1;
            s0.color = c0; s0.pos = 0;
            s1.color = c1; s1.pos = 255;
            ipgui_liner_gradient_add_stop(
                &arc_paint.src.grad_src.grad.liner_grad, &s0);
            ipgui_liner_gradient_add_stop(
                &arc_paint.src.grad_src.grad.liner_grad, &s1);
        }

        bw_rp_arc(s, cx, cy, r_out, r_in, 270,
                  (u16_t)(((u32_t)t * 360u + 127u) / 255u),
                  &arc_paint, 255, IPGUI_ARC_ENDPOINT_TYPE_ROUND);
    }

    /* ---------------- 圆心文字 ---------------- */
    {
        const ipgui_font_t * vf;
        const ipgui_font_t * lf = (const ipgui_font_t *)0;
        ipgui_coord_t        vh, lh = 0, total, y0;
        ipgui_aabb_t         tb;

        if (v >= 100) {
            buf[len++] = '1'; buf[len++] = '0'; buf[len++] = '0';
        } else {
            if (v >= 10) buf[len++] = (char)('0' + v / 10);
            buf[len++] = (char)('0' + v % 10);
        }
        buf[len++] = '%';
        buf[len]   = '\0';

        if (r_out >= 60)      vf = &quicksand_medium_24px;
        else if (r_out >= 40) vf = &quicksand_medium_19px;
        else                  vf = &quicksand_medium_15px;

        vh = vf->line_height;

        if (rp->label[0] && r_out >= 42) {
            lf = &quicksand_medium_11px;
            lh = lf->line_height;
        }

        total = vh + (lf ? (lh + 2) : 0);
        y0    = cy - total / 2;

        tb.start.x = cx - r_in;
        tb.start.y = y0;
        tb.end.x   = cx + r_in;
        tb.end.y   = y0 + vh - 1;
        ipgui_bw_text_center(s, &tb, vf, buf, BW_TEXT, 255);

        if (lf) {
            tb.start.y = y0 + vh + 2;
            tb.end.y   = tb.start.y + lh - 1;
            ipgui_bw_text_center(s, &tb, lf, rp->label, BW_TEXT_MUTED, 255);
        }
    }
}

/* -------------------------------------------------------------------------- */

ipgui_widget_t * ipgui_bw_ring_progress_create(
    ipgui_widget_t * parent,
    ipgui_coord_t x, ipgui_coord_t y,
    ipgui_coord_t w, ipgui_coord_t h,
    ipgui_coord_t thickness,
    u8_t value,
    const char * label)
{
    ipgui_widget_t            * widget;
    ipgui_bw_ring_progress_t  * rp;

    widget = ipgui_widget_create(parent);
    if (!widget) return (ipgui_widget_t *)0;

    rp = (ipgui_bw_ring_progress_t *)
        ipgui_mem_alloc_def(sizeof(ipgui_bw_ring_progress_t));
    if (!rp) {
        ipgui_widget_link_detach(&widget->link);
        ipgui_mem_free_def(widget);
        return (ipgui_widget_t *)0;
    }
    ipgui_memset((void *)rp, 0, sizeof(ipgui_bw_ring_progress_t));

    widget->x = x; widget->y = y;
    widget->w = w; widget->h = h;
    widget->priv_data = rp;
    widget->render        = bw_rp_render;
    widget->event_handler = 0;
    widget->name          = "bw_ring_progress";

    if (value > 100) value = 100;
    rp->value     = value;
    rp->thickness = (thickness > 255) ? 255 : (u8_t)thickness;
    rp->anim_last = (u8_t)(((u32_t)value * 255u + 50u) / 100u);
    rp->anim_from = rp->anim_last;
    rp->anim_to   = rp->anim_last;

    bw_rp_copy_label(rp->label, label);

    return widget;
}

void ipgui_bw_ring_progress_set_value(
    ipgui_widget_t * widget, u8_t value, u8_t animated)
{
    ipgui_bw_ring_progress_t * rp;
    u8_t to;

    if (!widget || !widget->priv_data) return;
    rp = (ipgui_bw_ring_progress_t *)widget->priv_data;

    if (value > 100) value = 100;
    if (value == rp->value) return;

    rp->value = value;
    to = (u8_t)(((u32_t)value * 255u + 50u) / 100u);

    if (animated) {
        rp->anim_from = rp->anim_last;
        rp->anim_to   = to;
        rp->anim_t0   = ipgui_tick_now();
        rp->anim_dur  = BW_RP_DURATION;
        rp->animating = 1;
    } else {
        rp->anim_last = to;
        rp->anim_from = to;
        rp->anim_to   = to;
        rp->animating = 0;
    }
    ipgui_widget_mark_dirty(widget);
}

u8_t ipgui_bw_ring_progress_get_value(ipgui_widget_t * widget)
{
    ipgui_bw_ring_progress_t * rp;

    if (!widget || !widget->priv_data) return 0;
    rp = (ipgui_bw_ring_progress_t *)widget->priv_data;
    return rp->value;
}

void ipgui_bw_ring_progress_set_label(
    ipgui_widget_t * widget, const char * label)
{
    ipgui_bw_ring_progress_t * rp;

    if (!widget || !widget->priv_data) return;
    rp = (ipgui_bw_ring_progress_t *)widget->priv_data;

    bw_rp_copy_label(rp->label, label);
    ipgui_widget_mark_dirty(widget);
}

void ipgui_bw_ring_progress_set_accent(
    ipgui_widget_t * widget, ipgui_color_t from, ipgui_color_t to)
{
    ipgui_bw_ring_progress_t * rp;

    if (!widget || !widget->priv_data) return;
    rp = (ipgui_bw_ring_progress_t *)widget->priv_data;

    rp->accent_from = from;
    rp->accent_to   = to;
    ipgui_widget_mark_dirty(widget);
}

void ipgui_bw_ring_progress_task(ipgui_widget_t * widget)
{
    ipgui_bw_ring_progress_t * rp;
    u8_t t;

    if (!widget || !widget->priv_data) return;
    rp = (ipgui_bw_ring_progress_t *)widget->priv_data;

    if (!rp->animating) return;

    t = ipgui_bw_anim8(rp->anim_from, rp->anim_to,
                       rp->anim_t0, ipgui_tick_now(), rp->anim_dur);
    if (t != rp->anim_last) {
        rp->anim_last = t;
        ipgui_widget_mark_dirty(widget);
    }
    if (t == rp->anim_to)
        rp->animating = 0;
}
