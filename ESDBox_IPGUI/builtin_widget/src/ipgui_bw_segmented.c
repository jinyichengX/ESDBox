#include "ipgui_bw_segmented.h"
#include "ipgui_bw_theme.h"
#include "ipgui_memory.h"
#include "ipgui_time.h"

#define BW_SG_PAD        3
#define BW_SG_INSET      3
#define BW_SG_LABEL_MAX  16
#define BW_SG_DURATION   220

typedef struct {
    char                    labels[IPGUI_BW_SEGMENT_MAX][BW_SG_LABEL_MAX];
    u8_t                    count;
    u8_t                    index;
    u8_t                    disabled;
    u8_t                    pressed;
    u8_t                    animating;
    u8_t                    anim_last;
    ipgui_tick_t            anim_t0;
    u16_t                   anim_dur;
    ipgui_coord_t           ind_from_x;
    ipgui_coord_t           ind_to_x;

    ipgui_bw_segmented_cb_t on_change;
} ipgui_bw_segmented_t;

/* -------------------------------------------------------------------------- */

static void bw_sg_geom(
    ipgui_widget_t * widget, ipgui_bw_segmented_t * sg,
    ipgui_aabb_t * track, ipgui_aabb_t * inner, ipgui_coord_t * seg_w)
{
    *track = (ipgui_aabb_t){
        { BW_SG_PAD, BW_SG_PAD },
        { widget->w - 1 - BW_SG_PAD, widget->h - 1 - BW_SG_PAD }
    };

    *inner = (ipgui_aabb_t){
        { track->start.x + BW_SG_INSET, track->start.y + BW_SG_INSET },
        { track->end.x   - BW_SG_INSET, track->end.y   - BW_SG_INSET }
    };

    *seg_w = ipgui_aabb_width(inner) / (ipgui_coord_t)sg->count;
    if (*seg_w < 1) *seg_w = 1;
}

static ipgui_coord_t bw_sg_ind_x(
    ipgui_aabb_t * inner, ipgui_coord_t seg_w, u8_t index)
{
    return inner->start.x + (ipgui_coord_t)index * seg_w;
}

static void bw_sg_render(
    ipgui_widget_t * widget, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_bw_segmented_t * sg = (ipgui_bw_segmented_t *)widget->priv_data;
    ipgui_surf_t         * s  = ctx->surf;
    ipgui_aabb_t           track, inner, ind;
    ipgui_coord_t          seg_w, target_x, ind_x;
    ipgui_coord_t          radius, ind_h;
    u8_t                   t;
    u8_t                   i;
    const ipgui_font_t   * font;

    if (!sg || !s || sg->count == 0) return;
    if (widget->w < 40 || widget->h < 24) return;

    bw_sg_geom(widget, sg, &track, &inner, &seg_w);
    radius = ipgui_aabb_height(&track) / 2;
    if (radius > 14) radius = 14;
    ind_h  = ipgui_aabb_height(&inner);

    target_x = bw_sg_ind_x(&inner, seg_w, sg->index);

    if (sg->ind_to_x < 0) {
        sg->ind_from_x = target_x;
        sg->ind_to_x   = target_x;
    }

    t = 255;
    if (sg->animating) {
        t = ipgui_bw_anim8(0, 255, sg->anim_t0,
                           ipgui_tick_now(), sg->anim_dur);
        sg->anim_last = t;
    }

    ind_x = sg->ind_from_x
          + (ipgui_coord_t)(((s32_t)(target_x - sg->ind_from_x) * t) / 255);

    /* ---------------- 轨道 ---------------- */
    ipgui_bw_fill_box(s, &track, radius, BW_TRACK,
                      sg->disabled ? 140 : 255);
    ipgui_bw_stroke_box(s, &track, radius, 1, BW_TRACK_BORDER,
                        sg->disabled ? 70 : 150);

    /* ---------------- 滑块 ---------------- */
    ind.start.x = ind_x;
    ind.start.y = inner.start.y;
    ind.end.x   = ind_x + seg_w - 1;
    ind.end.y   = inner.end.y;
    if (ind.end.x > inner.end.x) ind.end.x = inner.end.x;

    if (ind.end.x >= ind.start.x) {
        ipgui_color_t top = BW_SURFACE_TOP;
        ipgui_color_t bot = ipgui_bw_color(0xFD, 0xFE, 0xFF, 0xFF);

        if (sg->pressed) {
            top = ipgui_bw_mix(top, BW_TRACK, 70);
            bot = ipgui_bw_mix(bot, BW_TRACK, 70);
        }

        ipgui_bw_shadow_box(s, &ind, ind_h / 2,
            ipgui_bw_color(0x2A, 0x3A, 0x5F, 0xFF),
            sg->disabled ? 12 : 30, 7, 0, 0, 2);
        ipgui_bw_fill_vgrad(s, &ind, ind_h / 2, top, bot,
                            sg->disabled ? 150 : 255);
        ipgui_bw_stroke_box(s, &ind, ind_h / 2, 1,
            ipgui_bw_color(0x0F, 0x17, 0x2A, 0x12), 255);
    }

    /* ---------------- 文字 ---------------- */
    font = ipgui_bw_font_for_height(ind_h);

    /* 动画期间让高亮文字跟随滑块，而不是提前跳到目标项 */
    {
        u8_t active_idx = sg->index;

        if (sg->animating) {
            s32_t vi = (s32_t)(ind_x + seg_w / 2 - inner.start.x) / seg_w;

            if (vi < 0) vi = 0;
            if (vi >= (s32_t)sg->count) vi = (s32_t)sg->count - 1;
            active_idx = (u8_t)vi;
        }

        for (i = 0; i < sg->count; i ++) {
            ipgui_aabb_t tb;

            tb.start.x = inner.start.x + (ipgui_coord_t)i * seg_w;
            tb.start.y = inner.start.y;
            tb.end.x   = tb.start.x + seg_w - 1;
            tb.end.y   = inner.end.y;

            ipgui_bw_text_center(s, &tb, font, sg->labels[i],
                (i == active_idx)
                    ? (sg->disabled ? BW_DISABLED_TEXT : BW_TEXT)
                    : (sg->disabled ? BW_DISABLED_TEXT : BW_TEXT_MUTED),
                sg->disabled ? 180 : 255);
        }
    }
}

/* -------------------------------------------------------------------------- */

static u8_t bw_sg_index_at(
    ipgui_widget_t * widget, ipgui_bw_segmented_t * sg, ipgui_coord_t x)
{
    ipgui_aabb_t  track, inner;
    ipgui_coord_t seg_w;
    s32_t         idx;

    bw_sg_geom(widget, sg, &track, &inner, &seg_w);

    idx = (s32_t)(x - inner.start.x) / seg_w;
    if (idx < 0) idx = 0;
    if (idx >= (s32_t)sg->count) idx = (s32_t)sg->count - 1;
    return (u8_t)idx;
}

static void bw_sg_anim_start(
    ipgui_bw_segmented_t * sg, ipgui_coord_t from_x, ipgui_coord_t to_x)
{
    sg->ind_from_x = from_x;
    sg->ind_to_x   = to_x;
    sg->anim_t0    = ipgui_tick_now();
    sg->anim_dur   = BW_SG_DURATION;
    sg->animating  = 1;
    sg->anim_last  = 0;
}

static void bw_sg_event(
    ipgui_widget_t * widget, ipgui_widget_evt_t * evt)
{
    ipgui_bw_segmented_t * sg = (ipgui_bw_segmented_t *)widget->priv_data;

    if (!sg || sg->disabled) return;

    switch (evt->type) {
    case IPGUI_WIDGET_EVENT_PRESSED:
        if (!sg->pressed) {
            sg->pressed = 1;
            ipgui_widget_mark_dirty(widget);
        }
        break;

    case IPGUI_WIDGET_EVENT_RELEASED: {
        u8_t inside = (evt->evt.released_evt.x >= 0 &&
                       evt->evt.released_evt.x < widget->w &&
                       evt->evt.released_evt.y >= 0 &&
                       evt->evt.released_evt.y < widget->h) ? 1 : 0;

        sg->pressed = 0;
        if (inside) {
            u8_t idx = bw_sg_index_at(widget, sg, evt->evt.released_evt.x);

            if (idx != sg->index) {
                ipgui_aabb_t  track, inner;
                ipgui_coord_t seg_w;

                bw_sg_geom(widget, sg, &track, &inner, &seg_w);
                bw_sg_anim_start(sg,
                    bw_sg_ind_x(&inner, seg_w, sg->index),
                    bw_sg_ind_x(&inner, seg_w, idx));
                sg->index = idx;

                if (sg->on_change) sg->on_change(widget, idx);
            }
        }
        ipgui_widget_mark_dirty(widget);
        break;
    }

    default:
        break;
    }
}

/* -------------------------------------------------------------------------- */

ipgui_widget_t * ipgui_bw_segmented_create(
    ipgui_widget_t * parent,
    ipgui_coord_t x, ipgui_coord_t y,
    ipgui_coord_t w, ipgui_coord_t h,
    const char * const * labels,
    u8_t count,
    u8_t index)
{
    ipgui_widget_t       * widget;
    ipgui_bw_segmented_t * sg;
    u8_t i, j;

    widget = ipgui_widget_create(parent);
    if (!widget) return (ipgui_widget_t *)0;

    sg = (ipgui_bw_segmented_t *)
        ipgui_mem_alloc_def(sizeof(ipgui_bw_segmented_t));
    if (!sg) {
        ipgui_widget_link_detach(&widget->link);
        ipgui_mem_free_def(widget);
        return (ipgui_widget_t *)0;
    }
    ipgui_memset((void *)sg, 0, sizeof(ipgui_bw_segmented_t));

    widget->x = x; widget->y = y;
    widget->w = w; widget->h = h;
    widget->priv_data = sg;
    widget->render        = bw_sg_render;
    widget->event_handler = bw_sg_event;
    widget->name          = "bw_segmented";

    if (count == 0) count = 1;
    if (count > IPGUI_BW_SEGMENT_MAX) count = IPGUI_BW_SEGMENT_MAX;
    sg->count = count;

    for (i = 0; i < count; i ++) {
        const char * src = (labels && labels[i]) ? labels[i] : "";
        for (j = 0; j < BW_SG_LABEL_MAX - 1 && src[j]; j ++)
            sg->labels[i][j] = src[j];
        sg->labels[i][j] = '\0';
    }

    if (index >= count) index = (u8_t)(count - 1);
    sg->index       = index;
    sg->ind_from_x  = -1;
    sg->ind_to_x    = -1;

    return widget;
}

void ipgui_bw_segmented_set_index(
    ipgui_widget_t * widget, u8_t index, u8_t animated)
{
    ipgui_bw_segmented_t * sg;

    if (!widget || !widget->priv_data) return;
    sg = (ipgui_bw_segmented_t *)widget->priv_data;

    if (index >= sg->count) index = (u8_t)(sg->count - 1);
    if (index == sg->index) return;

    if (animated) {
        ipgui_aabb_t  track, inner;
        ipgui_coord_t seg_w;

        bw_sg_geom(widget, sg, &track, &inner, &seg_w);
        bw_sg_anim_start(sg,
            bw_sg_ind_x(&inner, seg_w, sg->index),
            bw_sg_ind_x(&inner, seg_w, index));
    } else {
        sg->animating = 0;
        sg->ind_to_x  = -1;   /* 下一帧直接对齐到新下标 */
    }

    sg->index = index;
    ipgui_widget_mark_dirty(widget);
}

u8_t ipgui_bw_segmented_get_index(ipgui_widget_t * widget)
{
    ipgui_bw_segmented_t * sg;

    if (!widget || !widget->priv_data) return 0;
    sg = (ipgui_bw_segmented_t *)widget->priv_data;
    return sg->index;
}

void ipgui_bw_segmented_set_enabled(ipgui_widget_t * widget, u8_t enabled)
{
    ipgui_bw_segmented_t * sg;

    if (!widget || !widget->priv_data) return;
    sg = (ipgui_bw_segmented_t *)widget->priv_data;

    sg->disabled = enabled ? 0 : 1;
    ipgui_widget_mark_dirty(widget);
}

void ipgui_bw_segmented_set_on_change(
    ipgui_widget_t * widget, ipgui_bw_segmented_cb_t cb)
{
    ipgui_bw_segmented_t * sg;

    if (!widget || !widget->priv_data) return;
    sg = (ipgui_bw_segmented_t *)widget->priv_data;
    sg->on_change = cb;
}

void ipgui_bw_segmented_task(ipgui_widget_t * widget)
{
    ipgui_bw_segmented_t * sg;
    u8_t t;

    if (!widget || !widget->priv_data) return;
    sg = (ipgui_bw_segmented_t *)widget->priv_data;

    if (!sg->animating) return;

    t = ipgui_bw_anim8(0, 255, sg->anim_t0,
                       ipgui_tick_now(), sg->anim_dur);
    if (t != sg->anim_last) {
        sg->anim_last = t;
        ipgui_widget_mark_dirty(widget);
    }
    if (t == 255)
        sg->animating = 0;
}
