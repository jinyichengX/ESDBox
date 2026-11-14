#include "ipgui_bw_theme.h"
#include <string.h>

/* ==========================================================================
 * 通用工具
 * ========================================================================== */

ipgui_color_t ipgui_bw_mix(ipgui_color_t a, ipgui_color_t b, u8_t t)
{
    ipgui_color_t r;
    u32_t         k = t;

    r.r = (u8_t)((a.r * (255u - k) + b.r * k) / 255u);
    r.g = (u8_t)((a.g * (255u - k) + b.g * k) / 255u);
    r.b = (u8_t)((a.b * (255u - k) + b.b * k) / 255u);
    r.a = (u8_t)((a.a * (255u - k) + b.a * k) / 255u);
    return r;
}

ipgui_bw_palette_t ipgui_bw_palette_mix(
    ipgui_bw_palette_t a, ipgui_bw_palette_t b, u8_t t)
{
    ipgui_bw_palette_t r;
    r.top    = ipgui_bw_mix(a.top,    b.top,    t);
    r.bottom = ipgui_bw_mix(a.bottom, b.bottom, t);
    r.border = ipgui_bw_mix(a.border, b.border, t);
    r.text   = ipgui_bw_mix(a.text,   b.text,   t);
    return r;
}

u8_t ipgui_bw_ease_out(u8_t t)
{
    /* 1 - (1-p)^2，用整数实现 */
    u32_t x = t;
    u32_t v = (x * (512u - x)) >> 8;
    return (v > 255u) ? 255u : (u8_t)v;
}

u8_t ipgui_bw_anim8(
    u8_t from, u8_t to,
    ipgui_tick_t t0, ipgui_tick_t now, u16_t dur_ms)
{
    u32_t elapsed;
    u8_t  p;

    if (dur_ms == 0) return to;

    elapsed = ipgui_tick2millis((u32_t)(now - t0));
    if (elapsed >= dur_ms) return to;

    p = (u8_t)((elapsed * 255u) / dur_ms);
    p = ipgui_bw_ease_out(p);

    if (from <= to)
        return (u8_t)(from + ((u32_t)(to - from) * p) / 255u);
    return (u8_t)(from - ((u32_t)(from - to) * p) / 255u);
}

/* ==========================================================================
 * 盒子绘制
 * ========================================================================== */

void ipgui_bw_box_style_init(ipgui_box_style_t * style, ipgui_coord_t radius)
{
    memset(style, 0, sizeof(*style));
    style->left_top_radius     = radius;
    style->right_top_radius    = radius;
    style->left_bottom_radius  = radius;
    style->right_bottom_radius = radius;
}

void ipgui_bw_fill_box(
    ipgui_surf_t * surf, ipgui_aabb_t * box, ipgui_coord_t radius,
    ipgui_color_t color, u8_t opacity)
{
    ipgui_box_style_t    bs;
    ipgui_box_bg_style_t bg;

    if (!surf || !box) return;

    ipgui_bw_box_style_init(&bs, radius);
    memset(&bg, 0, sizeof(bg));
    bg.paint.type      = IPGUI_PAINT_COLOR;
    bg.paint.src.color = color;
    bg.opacity         = opacity;
    bg.blend_mode      = IPGUI_BLEND_NORMAL;

    ipgui_draw_box_background(surf, (ipgui_aabb_t *)0, box, &bs, &bg);
}

static void bw_gradient_box(
    ipgui_surf_t * surf, ipgui_aabb_t * box, ipgui_coord_t radius,
    float x0, float y0, float x1, float y1,
    ipgui_color_t c0, ipgui_color_t c1, u8_t opacity)
{
    ipgui_box_style_t    bs;
    ipgui_box_bg_style_t bg;
    ipgui_liner_gradient_color_t * g;
    ipgui_gradient_color_stop_t s0, s1;

    if (!surf || !box) return;

    ipgui_bw_box_style_init(&bs, radius);

    memset(&bg, 0, sizeof(bg));
    bg.paint.type      = IPGUI_PAINT_GRADIENT;
    bg.opacity         = opacity;
    bg.blend_mode      = IPGUI_BLEND_NORMAL;

    bg.paint.src.grad_src.grad_type = IPGUI_GRADIENT_TYPE_LINEAR;
    g = &bg.paint.src.grad_src.grad.liner_grad;
    ipgui_liner_gradient_init(g, x0, y0, x1, y1);

    s0.color = c0; s0.pos = 0;
    s1.color = c1; s1.pos = 255;
    ipgui_liner_gradient_add_stop(g, &s0);
    ipgui_liner_gradient_add_stop(g, &s1);
    ipgui_liner_gradient_apply_to_aabb(g, box);

    ipgui_draw_box_background(surf, (ipgui_aabb_t *)0, box, &bs, &bg);
}

void ipgui_bw_fill_vgrad(
    ipgui_surf_t * surf, ipgui_aabb_t * box, ipgui_coord_t radius,
    ipgui_color_t top, ipgui_color_t bottom, u8_t opacity)
{
    /* 渐变端点取 0 -> 1，垂直方向：上边到 1px 下沿 */
    bw_gradient_box(surf, box, radius, 0.0f, 0.0f, 0.0f, 1.0f,
                    top, bottom, opacity);
}

void ipgui_bw_fill_hgrad(
    ipgui_surf_t * surf, ipgui_aabb_t * box, ipgui_coord_t radius,
    ipgui_color_t left, ipgui_color_t right, u8_t opacity)
{
    bw_gradient_box(surf, box, radius, 0.0f, 0.0f, 1.0f, 0.0f,
                    left, right, opacity);
}

void ipgui_bw_stroke_box(
    ipgui_surf_t * surf, ipgui_aabb_t * box, ipgui_coord_t radius,
    ipgui_coord_t width, ipgui_color_t color, u8_t opacity)
{
    ipgui_box_style_t        bs;
    ipgui_box_border_style_t bd;

    if (!surf || !box || width <= 0) return;

    ipgui_bw_box_style_init(&bs, radius);
    memset(&bd, 0, sizeof(bd));
    bd.paint.type      = IPGUI_PAINT_COLOR;
    bd.paint.src.color = color;
    bd.opacity         = opacity;
    bd.width           = width;
    bd.blend_mode      = IPGUI_BLEND_NORMAL;

    ipgui_draw_box_border(surf, (ipgui_aabb_t *)0, box, &bs, &bd);
}

void ipgui_bw_shadow_box(
    ipgui_surf_t * surf, ipgui_aabb_t * box, ipgui_coord_t radius,
    ipgui_color_t color, u8_t opacity,
    ipgui_coord_t blur, ipgui_coord_t spread,
    ipgui_coord_t dx, ipgui_coord_t dy)
{
    if (!surf || !box) return;

#if IPGUI_BW_SHADOW_EN == 0
    (void)radius; (void)color; (void)opacity;
    (void)blur; (void)spread; (void)dx; (void)dy;
    return;
#else
    ipgui_box_style_t               bs;
    ipgui_box_outer_shadow_style_t  sh;

    ipgui_bw_box_style_init(&bs, radius);
    memset(&sh, 0, sizeof(sh));
    sh.paint.type      = IPGUI_PAINT_COLOR;
    sh.paint.src.color = color;
    sh.opacity         = opacity;
    sh.blur            = blur;
    sh.spread          = spread;
    sh.offset_x        = dx;
    sh.offset_y        = dy;

    ipgui_draw_box_outer_shadow(surf, (ipgui_aabb_t *)0, box, &bs, &sh);
#endif
}

void ipgui_bw_top_gloss(
    ipgui_surf_t * surf, ipgui_aabb_t * box, ipgui_coord_t radius,
    ipgui_color_t color, u8_t opacity)
{
    ipgui_coord_t inset;

    if (!surf || !box) return;
    if (ipgui_aabb_height(box) < 12) return;

    inset = radius / 2;
    ipgui_bw_hairline(surf,
        box->start.x + inset, box->start.y + 1,
        box->end.x - inset,   box->start.y + 1,
        color, opacity);
}

void ipgui_bw_hairline(
    ipgui_surf_t * surf,
    ipgui_coord_t x1, ipgui_coord_t y1,
    ipgui_coord_t x2, ipgui_coord_t y2,
    ipgui_color_t color, u8_t opacity)
{
    ipgui_line_t       line;
    ipgui_line_style_t st;

    if (!surf || x2 < x1) return;

    line.start.x = x1; line.start.y = y1;
    line.end.x   = x2; line.end.y   = y2;

    memset(&st, 0, sizeof(st));
    st.width           = 1;
    st.cap             = IPGUI_LINE_CAP_BUTT;
    st.paint.type      = IPGUI_PAINT_COLOR;
    st.paint.src.color = color;
    st.opacity         = opacity;
    st.blend_mode      = IPGUI_BLEND_NORMAL;

    ipgui_draw_line_generic(surf, (ipgui_aabb_t *)0, &line, &st);
}

/* ==========================================================================
 * 文本
 * ========================================================================== */

static void bw_text_at(
    ipgui_surf_t * surf, ipgui_coord_t x, ipgui_coord_t y,
    const ipgui_font_t * font, const char * text,
    ipgui_color_t color, u8_t opacity)
{
    ipgui_font_style_t fs;

    memset(&fs, 0, sizeof(fs));
    fs.font            = font;
    fs.paint.type      = IPGUI_PAINT_COLOR;
    fs.paint.src.color = color;
    fs.opacity         = opacity;
    fs.line_spacing    = 0;
    fs.blend_mode      = IPGUI_BLEND_NORMAL;

    ipgui_draw_builtin_text(surf, (ipgui_aabb_t *)0, &fs,
                            (const s8_t *)text, x, y);
}

void ipgui_bw_text_center(
    ipgui_surf_t * surf, ipgui_aabb_t * box,
    const ipgui_font_t * font, const char * text,
    ipgui_color_t color, u8_t opacity)
{
    ipgui_coord_t tw, x, y;

    if (!surf || !box || !font || !text) return;

    tw = ipgui_builtin_text_width(font, (const s8_t *)text);
    x  = box->start.x + (ipgui_aabb_width (box) - tw) / 2;
    y  = box->start.y + (ipgui_aabb_height(box) - font->line_height) / 2;

    bw_text_at(surf, x, y, font, text, color, opacity);
}

void ipgui_bw_text_left(
    ipgui_surf_t * surf, ipgui_aabb_t * box,
    const ipgui_font_t * font, const char * text,
    ipgui_coord_t indent,
    ipgui_color_t color, u8_t opacity)
{
    ipgui_coord_t x, y;

    if (!surf || !box || !font || !text) return;

    x = box->start.x + indent;
    y = box->start.y + (ipgui_aabb_height(box) - font->line_height) / 2;

    bw_text_at(surf, x, y, font, text, color, opacity);
}

const ipgui_font_t * ipgui_bw_font_for_height(ipgui_coord_t visual_h)
{
    if (visual_h >= 40) return &quicksand_medium_19px;
    if (visual_h >= 34) return &quicksand_medium_17px;
    if (visual_h >= 28) return &quicksand_medium_15px;
    return &quicksand_medium_13px;
}

const ipgui_font_t * ipgui_bw_font_small_for_height(ipgui_coord_t visual_h)
{
    if (visual_h >= 26) return &quicksand_medium_12px;
    if (visual_h >= 20) return &quicksand_medium_11px;
    return &quicksand_medium_10px;
}
