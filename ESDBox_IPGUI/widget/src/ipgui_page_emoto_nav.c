/*
 * E-MOTION 电摩 HMI —— NAV 导航页
 *
 * 与 RIDE 页共用车辆状态快照，视觉上换成“地图 + 路线 + 转向 + ETA”的信息布局。
 * 所有图形均为矢量绘制：地图道路、街区、河流、路线、位置脉冲、罗盘、车道提示。
 * 页面切换使用 ipgui_emoto_shared 的幕布转场，底层是内置 ipgui_anim。
 */

#include "ipgui_page_emoto_nav.h"
#include "ipgui_emoto_shared.h"
#include "ipgui_widget.h"
#include "ipgui_draw_arc.h"
#include "ipgui_draw_box_background.h"
#include "ipgui_draw_box_border.h"
#include "ipgui_draw_filled_circle.h"
#include "ipgui_draw_line.h"
#include "ipgui_draw_triangle.h"
#include "ipgui_draw_builtin_font.h"
#include "ipgui_gradient_color.h"
#include "ipgui_blend.h"
#include "ipgui_math.h"
#include "ipgui_time.h"
#include "ipgui_dirty_rect.h"
#include "quicksand_medium.h"
#include "open_sans.h"

#include <stdio.h>
#include <string.h>

#define NAV_W               800
#define NAV_H               480
#define NAV_MAP_W           500
#define NAV_MAP_H           344
#define NAV_MAP_CX          250
#define NAV_MAP_CY          172

typedef struct {
    u8_t r, g, b;
} nav_rgb_t;

static const nav_rgb_t NAV_MODE_ACCENT[3] = {
    { 155, 225,  93 },   /* ECO   */
    {  53, 224, 255 },   /* SPORT */
    {  91, 140, 255 },   /* RAIN  */
};

static const char * NAV_MODE_NAME[3] = { "ECO", "SPORT", "RAIN" };

typedef struct {
    u8_t  inited;
    u8_t  page;
    u8_t  visible;
    s32_t zoom;             /* 80..170 */

    u8_t  acc_r, acc_g, acc_b;
    u8_t  acc_tr, acc_tg, acc_tb;

    u32_t last_tick;
    u32_t last_pulse;
    u32_t last_speed_ms;
    u32_t last_batt_ms;
    u32_t last_top_ms;
    s32_t last_speed;

    ipgui_scr_t * scr;
    ipgui_widget_t * w_bg;
    ipgui_widget_t * w_top;
    ipgui_widget_t * w_map;
    ipgui_widget_t * w_lane;
    ipgui_widget_t * w_zoom;
    ipgui_widget_t * w_turn;
    ipgui_widget_t * w_eta;
    ipgui_widget_t * w_batt;
    ipgui_widget_t * w_stats;
    ipgui_widget_t * w_speed;
    ipgui_widget_t * w_gps;
    ipgui_widget_t * w_back;
    ipgui_widget_t * w_trans;
    ipgui_widget_t * pressed;
} nav_state_t;

static nav_state_t g;

/* 地图内容贴到圆角面板内部，避免缩放后从圆角处溢出 */
static ipgui_aabb_t  g_map_clip;
static ipgui_aabb_t * g_clip = (ipgui_aabb_t *)0;

/*=============================================================================
 * 基础工具
 *===========================================================================*/

static ipgui_color_t nav_rgb(u8_t r, u8_t g_, u8_t b, u8_t a)
{
    ipgui_color_t c;
    c.r = r; c.g = g_; c.b = b; c.a = a;
    return c;
}

static ipgui_color_t nav_accent(u8_t a)
{
    return nav_rgb(g.acc_r, g.acc_g, g.acc_b, a);
}

static s32_t nav_clamp(s32_t v, s32_t lo, s32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static s32_t nav_ease(s32_t cur, s32_t target, s32_t dt, s32_t tau_ms)
{
    s32_t diff, step;

    if (dt <= 0 || cur == target) return target;
    diff = target - cur;
    step = diff * dt / tau_ms;
    if (step == 0) step = (diff > 0) ? 1 : -1;
    if ((diff > 0 && cur + step > target) ||
        (diff < 0 && cur + step < target)) return target;
    return cur + step;
}

static s32_t nav_isqrt(s32_t v)
{
    s32_t x, y;
    if (v <= 0) return 0;
    x = v;
    y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + v / x) / 2;
    }
    return x;
}

static ipgui_coord_t nav_pt_x(ipgui_coord_t cx, ipgui_coord_t r, s32_t angle)
{
    return cx + (ipgui_coord_t)(((s64_t)ipgui_cos(angle) * r) >> 15);
}

static ipgui_coord_t nav_pt_y(ipgui_coord_t cy, ipgui_coord_t r, s32_t angle)
{
    return cy + (ipgui_coord_t)(((s64_t)ipgui_sin(angle) * r) >> 15);
}

/*=============================================================================
 * 绘图原语
 *===========================================================================*/

static void nav_draw_line(
    ipgui_surf_t * surf, ipgui_coord_t x1, ipgui_coord_t y1,
    ipgui_coord_t x2, ipgui_coord_t y2,
    ipgui_coord_t width, ipgui_color_t color, u8_t opacity,
    ipgui_line_cap_t cap)
{
    ipgui_line_t line;
    ipgui_line_style_t style;

    if (x1 == x2 && y1 == y2) return;

    line.start.x = x1;
    line.start.y = y1;
    line.end.x   = x2;
    line.end.y   = y2;

    memset(&style, 0, sizeof(style));
    style.width      = width;
    style.cap        = cap;
    style.paint.type = IPGUI_PAINT_COLOR;
    style.paint.src.color = color;
    style.opacity    = opacity;
    style.blend_mode = IPGUI_BLEND_NORMAL;

    ipgui_draw_line_generic(surf, g_clip, &line, &style);
}

static void nav_draw_arc(
    ipgui_surf_t * surf, ipgui_coord_t cx, ipgui_coord_t cy,
    ipgui_coord_t er, ipgui_coord_t ir,
    s32_t start, s32_t angle, ipgui_color_t color, u8_t opacity,
    ipgui_arc_endpoint_type_t cap)
{
    ipgui_arc_t arc;
    ipgui_arc_style_t style;

    if (angle <= 0 || er <= ir) return;

    arc.cx    = cx;
    arc.cy    = cy;
    arc.er    = er;
    arc.ir    = ir;
    arc.start = start;
    arc.dir   = IPGUI_ARC_DRAW_DIR_CCW;
    arc.angle = (u16_t)angle;

    memset(&style, 0, sizeof(style));
    style.paint.type = IPGUI_PAINT_COLOR;
    style.paint.src.color = color;
    style.opacity = opacity;
    style.blend_mode = IPGUI_BLEND_NORMAL;
    style.sep_type = cap;
    style.eep_type = cap;

    ipgui_draw_arc(surf, g_clip, &arc, &style);
}

static void nav_draw_circle(
    ipgui_surf_t * surf, ipgui_coord_t cx, ipgui_coord_t cy,
    ipgui_coord_t r, ipgui_color_t color, u8_t opacity)
{
    ipgui_filled_circle_style_t style;

    if (r <= 0) return;

    memset(&style, 0, sizeof(style));
    style.paint.type = IPGUI_PAINT_COLOR;
    style.paint.src.color = color;
    style.opacity = opacity;
    style.blend_mode = IPGUI_BLEND_NORMAL;

    ipgui_draw_filled_circle(surf, g_clip, cx, cy, r, &style);
}

static void nav_draw_tri(
    ipgui_surf_t * surf,
    ipgui_coord_t x1, ipgui_coord_t y1,
    ipgui_coord_t x2, ipgui_coord_t y2,
    ipgui_coord_t x3, ipgui_coord_t y3,
    ipgui_color_t color, u8_t opacity)
{
    ipgui_point_t p1, p2, p3;
    ipgui_triangle_style_t style;

    p1.x = x1; p1.y = y1;
    p2.x = x2; p2.y = y2;
    p3.x = x3; p3.y = y3;

    memset(&style, 0, sizeof(style));
    style.paint.type = IPGUI_PAINT_COLOR;
    style.paint.src.color = color;
    style.opacity = opacity;
    style.blend_mode = IPGUI_BLEND_NORMAL;

    ipgui_draw_triangle(surf, g_clip, &p1, &p2, &p3, &style);
}

static void nav_fill_round(
    ipgui_surf_t * surf, ipgui_coord_t x, ipgui_coord_t y,
    ipgui_coord_t w, ipgui_coord_t h, ipgui_coord_t r,
    ipgui_color_t color, u8_t opacity)
{
    ipgui_aabb_t box;
    ipgui_box_style_t bs;
    ipgui_box_bg_style_t bg;

    if (w <= 0 || h <= 0) return;

    box.start.x = x;
    box.start.y = y;
    box.end.x   = x + w - 1;
    box.end.y   = y + h - 1;

    memset(&bs, 0, sizeof(bs));
    bs.left_top_radius     = r;
    bs.right_top_radius    = r;
    bs.left_bottom_radius  = r;
    bs.right_bottom_radius = r;

    memset(&bg, 0, sizeof(bg));
    bg.paint.type = IPGUI_PAINT_COLOR;
    bg.paint.src.color = color;
    bg.opacity = opacity;
    bg.blend_mode = IPGUI_BLEND_NORMAL;

    ipgui_draw_box_background(surf, g_clip, &box, &bs, &bg);
}

static void nav_stroke_round(
    ipgui_surf_t * surf, ipgui_coord_t x, ipgui_coord_t y,
    ipgui_coord_t w, ipgui_coord_t h, ipgui_coord_t r,
    ipgui_coord_t width, ipgui_color_t color, u8_t opacity)
{
    ipgui_aabb_t box;
    ipgui_box_style_t bs;
    ipgui_box_border_style_t st;

    if (w <= 0 || h <= 0 || width <= 0) return;

    box.start.x = x;
    box.start.y = y;
    box.end.x   = x + w - 1;
    box.end.y   = y + h - 1;

    memset(&bs, 0, sizeof(bs));
    bs.left_top_radius     = r;
    bs.right_top_radius    = r;
    bs.left_bottom_radius  = r;
    bs.right_bottom_radius = r;

    memset(&st, 0, sizeof(st));
    st.paint.type = IPGUI_PAINT_COLOR;
    st.paint.src.color = color;
    st.opacity = opacity;
    st.width = width;
    st.blend_mode = IPGUI_BLEND_NORMAL;

    ipgui_draw_box_border(surf, g_clip, &box, &bs, &st);
}

static void nav_draw_panel(
    ipgui_surf_t * surf, ipgui_coord_t x, ipgui_coord_t y,
    ipgui_coord_t w, ipgui_coord_t h, ipgui_coord_t r,
    ipgui_color_t top, ipgui_color_t bottom,
    ipgui_color_t border, u8_t border_opacity)
{
    ipgui_aabb_t box;
    ipgui_box_style_t bs;
    ipgui_box_bg_style_t bg;
    ipgui_liner_gradient_color_t grad;
    ipgui_gradient_color_stop_t s0, s1;

    if (w <= 0 || h <= 0) return;

    box.start.x = x;
    box.start.y = y;
    box.end.x   = x + w - 1;
    box.end.y   = y + h - 1;

    memset(&bs, 0, sizeof(bs));
    bs.left_top_radius     = r;
    bs.right_top_radius    = r;
    bs.left_bottom_radius  = r;
    bs.right_bottom_radius = r;

    ipgui_liner_gradient_init_direct(&grad, x, y, x, y + h - 1);
    s0.pos = 0;   s0.color = top;
    s1.pos = 255; s1.color = bottom;
    ipgui_liner_gradient_add_stop(&grad, &s0);
    ipgui_liner_gradient_add_stop(&grad, &s1);

    memset(&bg, 0, sizeof(bg));
    bg.paint.type = IPGUI_PAINT_GRADIENT;
    bg.paint.src.grad_src.grad_type = IPGUI_GRADIENT_TYPE_LINEAR;
    bg.paint.src.grad_src.grad.liner_grad = grad;
    bg.opacity = 255;
    bg.blend_mode = IPGUI_BLEND_NORMAL;

    ipgui_draw_box_background(surf, g_clip, &box, &bs, &bg);

    if (border_opacity >= 3) {
        nav_stroke_round(surf, x, y, w, h, r, 1, border, border_opacity);
    }

    nav_draw_line(surf, x + r, y + 1, x + w - 1 - r, y + 1, 1,
                  nav_rgb(255, 255, 255, 255), 16, IPGUI_LINE_CAP_BUTT);
}

static void nav_draw_text(
    ipgui_surf_t * surf, const ipgui_font_t * font, const char * text,
    ipgui_coord_t x, ipgui_coord_t y, ipgui_color_t color, u8_t opacity)
{
    ipgui_font_style_t style;

    if (!font || !text || opacity < 3) return;

    memset(&style, 0, sizeof(style));
    style.font       = font;
    style.opacity    = opacity;
    style.paint.type = IPGUI_PAINT_COLOR;
    style.paint.src.color = color;
    style.blend_mode = IPGUI_BLEND_NORMAL;

    ipgui_draw_builtin_text(surf, g_clip, &style,
                            (const s8_t *)text, x, y);
}

static ipgui_coord_t nav_text_w(const ipgui_font_t * font, const char * text)
{
    if (!font || !text) return 0;
    return ipgui_builtin_text_width(font, (const s8_t *)text);
}

static void nav_draw_text_center(
    ipgui_surf_t * surf, const ipgui_font_t * font, const char * text,
    ipgui_coord_t cx, ipgui_coord_t y, ipgui_color_t color, u8_t opacity)
{
    nav_draw_text(surf, font, text, cx - nav_text_w(font, text) / 2,
                  y, color, opacity);
}

static void nav_draw_text_right(
    ipgui_surf_t * surf, const ipgui_font_t * font, const char * text,
    ipgui_coord_t right, ipgui_coord_t y, ipgui_color_t color, u8_t opacity)
{
    nav_draw_text(surf, font, text, right - nav_text_w(font, text),
                  y, color, opacity);
}

static void nav_radial_glow(
    ipgui_surf_t * surf, ipgui_coord_t cx, ipgui_coord_t cy,
    ipgui_coord_t r, ipgui_color_t color, u8_t center_alpha)
{
    ipgui_radial_gradient_color_t grad;
    ipgui_gradient_color_stop_t s0, s1;
    ipgui_filled_circle_style_t style;

    if (r <= 0 || center_alpha < 3) return;

    ipgui_radial_gradient_init(&grad, cx, cy, r);

    color.a = center_alpha;
    s0.pos = 0;   s0.color = color;
    color.a = 0;
    s1.pos = 255; s1.color = color;

    ipgui_radial_gradient_add_stop(&grad, &s0);
    ipgui_radial_gradient_add_stop(&grad, &s1);

    memset(&style, 0, sizeof(style));
    style.paint.type = IPGUI_PAINT_GRADIENT;
    style.paint.src.grad_src.grad_type = IPGUI_GRADIENT_TYPE_RADIAL;
    style.paint.src.grad_src.grad.radial_grad = grad;
    style.opacity = 255;
    style.blend_mode = IPGUI_BLEND_NORMAL;

    ipgui_draw_filled_circle(surf, g_clip, cx, cy, r, &style);
}

static void nav_press_overlay(
    ipgui_surf_t * surf, ipgui_widget_t * widget,
    ipgui_coord_t w, ipgui_coord_t h, ipgui_coord_t r)
{
    if (g.pressed != widget) return;
    nav_fill_round(surf, 0, 0, w, h, r,
                   nav_rgb(255, 255, 255, 255), 12);
    nav_stroke_round(surf, 0, 0, w, h, r, 1, nav_accent(255), 90);
}

/*=============================================================================
 * 车辆快照
 *===========================================================================*/

static const ipgui_emoto_snapshot_t * nav_snap(void)
{
    return ipgui_emoto_shared_get();
}

static u8_t nav_mode(void)
{
    const ipgui_emoto_snapshot_t * s = nav_snap();
    return (s && s->valid && s->mode < 3) ? s->mode : 1;
}

static s32_t nav_speed(void)
{
    const ipgui_emoto_snapshot_t * s = nav_snap();
    return (s && s->valid) ? s->speed : 0;
}

static s32_t nav_soc(void)
{
    const ipgui_emoto_snapshot_t * s = nav_snap();
    return (s && s->valid) ? s->soc : 78;
}

static s32_t nav_range(void)
{
    const ipgui_emoto_snapshot_t * s = nav_snap();
    return (s && s->valid) ? s->range_km : 97;
}

static s32_t nav_trip(void)
{
    const ipgui_emoto_snapshot_t * s = nav_snap();
    return (s && s->valid) ? s->trip_x100 : 4260;
}

static s32_t nav_avg_speed(void)
{
    const ipgui_emoto_snapshot_t * s = nav_snap();
    return (s && s->valid) ? s->avg_speed : 25;
}

/*=============================================================================
 * 小图标
 *===========================================================================*/

static void icon_nav_gps(ipgui_surf_t * s, int x, int y,
                         ipgui_color_t c, u8_t op)
{
    nav_draw_arc(s, x + 7, y + 7, 6, 4, 0, 360, c, op,
                 IPGUI_ARC_ENDPOINT_TYPE_BUTT);
    nav_draw_tri(s, x + 1, y + 7, x + 13, y + 7, x + 7, y + 16, c, op);
    nav_draw_circle(s, x + 7, y + 7, 2, c, op);
}

static void icon_nav_bt(ipgui_surf_t * s, int x, int y,
                        ipgui_color_t c, u8_t op)
{
    nav_draw_line(s, x + 5, y + 1,  x + 5,  y + 15, 1, c, op, IPGUI_LINE_CAP_ROUND);
    nav_draw_line(s, x + 5, y + 1,  x + 11, y + 6,  1, c, op, IPGUI_LINE_CAP_ROUND);
    nav_draw_line(s, x + 11, y + 6, x + 2,  y + 12, 1, c, op, IPGUI_LINE_CAP_ROUND);
    nav_draw_line(s, x + 2,  y + 3, x + 11, y + 9,  1, c, op, IPGUI_LINE_CAP_ROUND);
    nav_draw_line(s, x + 11, y + 9, x + 5,  y + 15, 1, c, op, IPGUI_LINE_CAP_ROUND);
}

static void icon_nav_signal(ipgui_surf_t * s, int x, int y,
                            ipgui_color_t c, u8_t op)
{
    int i;
    for (i = 0; i < 4; i ++) {
        int h = 3 + i * 3;
        nav_draw_line(s, x + i * 4, y + 15 - h, x + i * 4, y + 15, 2, c,
                      (u8_t)((i == 3) ? (op / 3) : op), IPGUI_LINE_CAP_BUTT);
    }
}

static void icon_nav_battery(ipgui_surf_t * s, int x, int y, int w, int h,
                             int soc, ipgui_color_t c, u8_t op)
{
    int fill_w;

    nav_stroke_round(s, x, y, w, h, 3, 1, c, (u8_t)(op / 2));
    nav_fill_round(s, x + w, y + h / 3, 2, h / 3, 1, c, (u8_t)(op / 2));

    fill_w = (w - 4) * nav_clamp(soc, 0, 100) / 100;
    if (fill_w > 0) {
        nav_fill_round(s, x + 2, y + 2, fill_w, h - 4, 2, c, op);
    }
}

static void icon_nav_satellite(ipgui_surf_t * s, int x, int y,
                               ipgui_color_t c, u8_t op)
{
    nav_draw_line(s, x + 4, y + 11, x + 10, y + 5, 2, c, op, IPGUI_LINE_CAP_ROUND);
    nav_draw_line(s, x + 2, y + 5,  x + 11, y + 14, 1, c, (u8_t)(op / 2), IPGUI_LINE_CAP_ROUND);
    nav_draw_line(s, x + 5, y + 2,  x + 14, y + 11, 1, c, (u8_t)(op / 2), IPGUI_LINE_CAP_ROUND);
    nav_fill_round(s, x + 8, y + 3, 5, 5, 1, c, op);
    nav_fill_round(s, x + 3, y + 8, 5, 5, 1, c, op);
}

static void icon_nav_bolt(ipgui_surf_t * s, int x, int y,
                          ipgui_color_t c, u8_t op)
{
    nav_draw_tri(s, x + 8, y + 0, x + 2, y + 9, x + 7, y + 9, c, op);
    nav_draw_tri(s, x + 7, y + 9, x + 4, y + 16, x + 12, y + 5, c, op);
}

/*=============================================================================
 * 地图
 *===========================================================================*/

static ipgui_coord_t nav_map_x(ipgui_coord_t x)
{
    return NAV_MAP_CX + (x - NAV_MAP_CX) * g.zoom / 100;
}

static ipgui_coord_t nav_map_y(ipgui_coord_t y)
{
    return NAV_MAP_CY + (y - NAV_MAP_CY) * g.zoom / 100;
}

static void nav_map_rect(
    ipgui_coord_t x, ipgui_coord_t y,
    ipgui_coord_t w, ipgui_coord_t h,
    ipgui_coord_t * rx, ipgui_coord_t * ry,
    ipgui_coord_t * rw, ipgui_coord_t * rh)
{
    *rx = nav_map_x(x);
    *ry = nav_map_y(y);
    *rw = nav_map_x(x + w) - *rx;
    *rh = nav_map_y(y + h) - *ry;
}

static void nav_polyline(
    ipgui_surf_t * surf, const ipgui_point_t * p, int n,
    ipgui_coord_t width, ipgui_color_t color, u8_t opacity,
    ipgui_line_cap_t cap)
{
    int i;
    for (i = 0; i + 1 < n; i ++) {
        nav_draw_line(surf,
                      nav_map_x(p[i].x), nav_map_y(p[i].y),
                      nav_map_x(p[i + 1].x), nav_map_y(p[i + 1].y),
                      width, color, opacity, cap);
    }
}

static void nav_route_arrow(
    ipgui_surf_t * surf, ipgui_coord_t x, ipgui_coord_t y,
    ipgui_coord_t dx, ipgui_coord_t dy)
{
    ipgui_coord_t len = (ipgui_coord_t)nav_isqrt(dx * dx + dy * dy);
    ipgui_coord_t ux, uy, px, py, bx, by;

    if (len <= 0) return;

    ux = dx * 8 / len;
    uy = dy * 8 / len;
    px = -uy * 4 / 8;
    py =  ux * 4 / 8;

    bx = x - ux;
    by = y - uy;

    nav_draw_line(surf, bx + px, by + py, x, y, 2,
                  nav_rgb(255, 255, 255, 255), 190, IPGUI_LINE_CAP_ROUND);
    nav_draw_line(surf, bx - px, by - py, x, y, 2,
                  nav_rgb(255, 255, 255, 255), 190, IPGUI_LINE_CAP_ROUND);
}

static void nav_map_render(ipgui_widget_t * widget, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_surf_t * surf = ctx->surf;
    static const struct {
        ipgui_coord_t x, y, w, h, r;
    } blocks[] = {
        {  42,  46, 82, 52, 7 }, { 140,  38, 74, 66, 7 },
        { 232,  44, 92, 54, 7 }, { 342,  38, 64, 58, 7 },
        { 414, 100, 44, 78, 7 }, {  54, 120, 70, 54, 7 },
        { 140, 122, 54, 48, 7 }, { 208, 120, 86, 62, 7 },
        { 310, 116, 58, 70, 7 }, { 382, 196, 48, 60, 7 },
        {  56, 196, 76, 58, 7 }, { 150, 190, 42, 48, 7 },
        { 210, 202, 76, 52, 7 }, { 300, 204, 58, 46, 7 },
        {  60, 272, 82, 40, 7 }, { 160, 264, 72, 42, 7 },
        { 250, 272, 90, 36, 7 }, { 356, 268, 60, 40, 7 },
    };
    static const ipgui_point_t road_main[] = {
        { 24, 262 }, { 118, 232 }, { 206, 252 },
        { 318, 212 }, { 404, 232 }, { 476, 202 }
    };
    static const ipgui_point_t road_vert[] = {
        { 170, 24 }, { 190, 120 }, { 180, 220 }, { 210, 320 }
    };
    static const ipgui_point_t road_mid[] = {
        { 24, 120 }, { 118, 142 }, { 258, 110 },
        { 380, 142 }, { 476, 120 }
    };
    static const ipgui_point_t road_minor1[] = {
        { 24, 82 }, { 120, 96 }, { 250, 76 }, { 380, 92 }, { 476, 78 }
    };
    static const ipgui_point_t road_minor2[] = {
        { 110, 20 }, { 120, 120 }, { 104, 220 }, { 130, 324 }
    };
    static const ipgui_point_t river[] = {
        { 24, 300 }, { 118, 286 }, { 208, 298 },
        { 300, 270 }, { 390, 284 }, { 476, 258 }
    };
    static const ipgui_point_t route[] = {
        {  52, 300 }, { 110, 282 }, { 150, 250 }, { 198, 258 },
        { 238, 226 }, { 286, 204 }, { 326, 164 }, { 366, 132 },
        { 408,  96 }, { 438,  72 }
    };
    int i;
    ipgui_coord_t cx, cy, dx, dy, phase;

    (void)widget;

    nav_draw_panel(surf, 0, 0, NAV_MAP_W, NAV_MAP_H, 20,
                   nav_rgb(0x0B, 0x1A, 0x28, 255),
                   nav_rgb(0x07, 0x12, 0x1D, 255),
                   nav_rgb(0x1B, 0x3A, 0x52, 255), 180);

    g_map_clip.start.x = 18;
    g_map_clip.start.y = 18;
    g_map_clip.end.x   = NAV_MAP_W - 18;
    g_map_clip.end.y   = NAV_MAP_H - 18;
    g_clip = &g_map_clip;

    /* 网格 */
    for (i = 24; i < NAV_MAP_W - 20; i += 32) {
        nav_draw_line(surf, i, 22, i, NAV_MAP_H - 22, 1,
                      nav_rgb(0x12, 0x28, 0x3A, 255), 55,
                      IPGUI_LINE_CAP_BUTT);
    }
    for (i = 24; i < NAV_MAP_H - 20; i += 32) {
        nav_draw_line(surf, 22, i, NAV_MAP_W - 22, i, 1,
                      nav_rgb(0x12, 0x28, 0x3A, 255), 55,
                      IPGUI_LINE_CAP_BUTT);
    }

    /* 河流 */
    nav_polyline(surf, river, 6, 22,
                 nav_rgb(0x08, 0x2A, 0x44, 255), 100,
                 IPGUI_LINE_CAP_ROUND);
    nav_polyline(surf, river, 6, 2,
                 nav_rgb(0x2A, 0x6C, 0x8C, 255), 55,
                 IPGUI_LINE_CAP_ROUND);

    /* 街区 */
    for (i = 0; i < (int)(sizeof(blocks) / sizeof(blocks[0])); i ++) {
        ipgui_coord_t rx, ry, rw, rh;
        nav_map_rect(blocks[i].x, blocks[i].y,
                     blocks[i].w, blocks[i].h, &rx, &ry, &rw, &rh);
        nav_fill_round(surf, rx, ry, rw, rh,
                       blocks[i].r * g.zoom / 100,
                       nav_rgb(0x10, 0x23, 0x34, 255), 210);
        nav_stroke_round(surf, rx, ry, rw, rh,
                         blocks[i].r * g.zoom / 100, 1,
                         nav_rgb(0x1A, 0x34, 0x48, 255), 130);
    }

    /* 道路 */
    nav_polyline(surf, road_main, 6, 11,
                 nav_rgb(0x1C, 0x35, 0x48, 255), 235,
                 IPGUI_LINE_CAP_ROUND);
    nav_polyline(surf, road_main, 6, 2,
                 nav_rgb(0x2E, 0x5A, 0x74, 255), 120,
                 IPGUI_LINE_CAP_ROUND);
    nav_polyline(surf, road_vert, 4, 8,
                 nav_rgb(0x1B, 0x32, 0x45, 255), 220,
                 IPGUI_LINE_CAP_ROUND);
    nav_polyline(surf, road_mid, 5, 7,
                 nav_rgb(0x1A, 0x30, 0x42, 255), 210,
                 IPGUI_LINE_CAP_ROUND);
    nav_polyline(surf, road_minor1, 5, 3,
                 nav_rgb(0x17, 0x2A, 0x3A, 255), 190,
                 IPGUI_LINE_CAP_ROUND);
    nav_polyline(surf, road_minor2, 4, 3,
                 nav_rgb(0x17, 0x2A, 0x3A, 255), 190,
                 IPGUI_LINE_CAP_ROUND);

    /* 道路标签 */
    nav_draw_text(surf, &open_sans_7px, "WANFENG RD",
                  nav_map_x(54), nav_map_y(214),
                  nav_rgb(0x6F, 0x84, 0x9B, 255), 170);
    nav_draw_text(surf, &open_sans_7px, "HAIDIAN",
                  nav_map_x(300), nav_map_y(64),
                  nav_rgb(0x6F, 0x84, 0x9B, 255), 150);
    nav_draw_text(surf, &open_sans_7px, "XIZHIMEN",
                  nav_map_x(84), nav_map_y(306),
                  nav_rgb(0x6F, 0x84, 0x9B, 255), 150);

    /* 路线：辉光 + 主线 + 高光 */
    nav_polyline(surf, route, 10, 14, nav_accent(255), 34,
                 IPGUI_LINE_CAP_ROUND);
    nav_polyline(surf, route, 10, 5, nav_accent(255), 235,
                 IPGUI_LINE_CAP_ROUND);
    nav_polyline(surf, route, 10, 1,
                 nav_rgb(255, 255, 255, 255), 90,
                 IPGUI_LINE_CAP_ROUND);

    for (i = 2; i < 8; i += 3) {
        ipgui_coord_t x1 = nav_map_x(route[i].x);
        ipgui_coord_t y1 = nav_map_y(route[i].y);
        ipgui_coord_t x2 = nav_map_x(route[i + 1].x);
        ipgui_coord_t y2 = nav_map_y(route[i + 1].y);
        nav_route_arrow(surf, (x1 + x2) / 2, (y1 + y2) / 2,
                        x2 - x1, y2 - y1);
    }

    /* 终点 */
    dx = nav_map_x(route[9].x);
    dy = nav_map_y(route[9].y);
    nav_draw_circle(surf, dx, dy, 13, nav_accent(255), 28);
    nav_draw_circle(surf, dx, dy, 7, nav_accent(255), 240);
    nav_draw_tri(surf, dx - 7, dy + 2, dx + 7, dy + 2,
                 dx, dy + 14, nav_accent(255), 240);
    nav_draw_circle(surf, dx, dy, 2,
                    nav_rgb(255, 255, 255, 255), 240);
    nav_draw_text(surf, &open_sans_7px, "DEST",
                  dx + 12, dy - 4,
                  nav_accent(235), 220);

    /* 当前定位：外圈脉冲 + 实心点 + 方向箭头 */
    cx = nav_map_x(238);
    cy = nav_map_y(226);

    nav_radial_glow(surf, cx, cy, 34, nav_accent(255), 55);
    phase = (ipgui_tick_now() / 9) % 20;
    nav_draw_arc(surf, cx, cy, 17 + phase, 16 + phase,
                 0, 360, nav_accent(255),
                 (u8_t)(90 - phase * 3),
                 IPGUI_ARC_ENDPOINT_TYPE_BUTT);
    nav_draw_circle(surf, cx, cy, 8, nav_accent(255), 240);
    nav_draw_circle(surf, cx, cy, 4,
                    nav_rgb(255, 255, 255, 255), 240);

    {
        s32_t a = -39;
        nav_draw_tri(surf,
                     nav_pt_x(cx, 13, a), nav_pt_y(cy, 13, a),
                     nav_pt_x(cx, 9, a + 140), nav_pt_y(cy, 9, a + 140),
                     nav_pt_x(cx, 9, a - 140), nav_pt_y(cy, 9, a - 140),
                     nav_rgb(255, 255, 255, 255), 220);
    }

    /* 罗盘 */
    {
        ipgui_coord_t mx = 452, my = 44;
        nav_draw_circle(surf, mx, my, 18,
                        nav_rgb(0x08, 0x13, 0x1E, 255), 235);
        nav_draw_arc(surf, mx, my, 18, 17, 0, 360,
                     nav_rgb(0x2A, 0x4A, 0x64, 255), 190,
                     IPGUI_ARC_ENDPOINT_TYPE_BUTT);
        nav_draw_tri(surf, mx, my - 12, mx - 5, my + 5,
                     mx + 5, my + 5, nav_accent(255), 230);
        nav_draw_text(surf, &open_sans_7px, "N",
                      mx - 2, my - 30,
                      nav_rgb(0xD8, 0xE4, 0xF2, 255), 210);
        nav_draw_text(surf, &open_sans_7px, "2D",
                      mx - 5, my + 22,
                      nav_rgb(0x6F, 0x84, 0x9B, 255), 150);
    }

    g_clip = (ipgui_aabb_t *)0;

    /* 左上角地图信息 */
    nav_draw_text(surf, &open_sans_8px, "BEIJING / HAIDIAN",
                  24, 20, nav_rgb(0xB8, 0xCB, 0xDD, 255), 190);
}

/* 当前定位在屏幕上的脏矩形（用于脉冲动画局部刷新） */
static void nav_current_screen_rect(
    ipgui_coord_t * x, ipgui_coord_t * y, ipgui_coord_t * w, ipgui_coord_t * h)
{
    ipgui_coord_t cx = 20 + nav_map_x(238);
    ipgui_coord_t cy = 68 + nav_map_y(226);
    *x = cx - 34;
    *y = cy - 34;
    *w = 68;
    *h = 68;
}

/*=============================================================================
 * 顶栏 / 返回 / 缩放 / 车道
 *===========================================================================*/

static void nav_top_render(ipgui_widget_t * widget, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_surf_t * surf = ctx->surf;
    u32_t minutes = 9 * 60 + 42 + ipgui_tick_now() / 60000;
    char buf[16];

    (void)widget;

    snprintf(buf, sizeof(buf), "%02d:%02d",
             (int)((minutes / 60) % 24), (int)(minutes % 60));

    nav_draw_text_center(surf, &quicksand_medium_14px,
                         "NAVIGATION", 400, 10,
                         nav_rgb(0xE6, 0xF0, 0xFB, 255), 240);
    nav_draw_text_center(surf, &open_sans_8px,
                         "ROUTE  GUIDANCE", 400, 31,
                         nav_rgb(0x7F, 0x94, 0xAB, 255), 190);

    icon_nav_bt(surf, 250, 20, nav_accent(235), 220);
    icon_nav_gps(surf, 276, 19, nav_accent(235), 220);
    icon_nav_signal(surf, 304, 19, nav_rgb(0x9A, 0xAF, 0xC6, 255), 200);
    nav_draw_text(surf, &open_sans_8px, "LTE", 326, 23,
                  nav_rgb(0x8A, 0x9F, 0xB6, 255), 210);

    nav_draw_line(surf, 382, 46, 418, 46, 2, nav_accent(255), 80,
                  IPGUI_LINE_CAP_ROUND);

    nav_draw_text_right(surf, &quicksand_medium_20px, buf, 780, 6,
                        nav_rgb(0xE6, 0xF0, 0xFB, 255), 255);
    nav_draw_text_right(surf, &open_sans_8px, "MON 02 NOV", 780, 32,
                        nav_rgb(0x7F, 0x94, 0xAB, 255), 200);

    nav_draw_line(surf, 0, 57, NAV_W - 1, 57, 1,
                  nav_rgb(0x1B, 0x2C, 0x3E, 255), 120, IPGUI_LINE_CAP_BUTT);
}

static void nav_back_render(ipgui_widget_t * widget, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_surf_t * surf = ctx->surf;

    nav_fill_round(surf, 0, 0, 86, 32, 16, nav_accent(255), 18);
    nav_stroke_round(surf, 0, 0, 86, 32, 16, 1, nav_accent(255), 100);

    nav_draw_line(surf, 22, 10, 15, 16, 1, nav_accent(255), 230,
                  IPGUI_LINE_CAP_ROUND);
    nav_draw_line(surf, 15, 16, 22, 22, 1, nav_accent(255), 230,
                  IPGUI_LINE_CAP_ROUND);
    nav_draw_text(surf, &open_sans_10px, "RIDE", 30, 10,
                  nav_accent(245), 245);

    nav_press_overlay(surf, widget, 86, 32, 16);
}

static void nav_zoom_render(ipgui_widget_t * widget, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_surf_t * surf = ctx->surf;

    nav_fill_round(surf, 0, 0, 36, 33, 10,
                   nav_rgb(0x08, 0x13, 0x1E, 255), 235);
    nav_stroke_round(surf, 0, 0, 36, 33, 10, 1, nav_accent(255), 65);
    nav_draw_line(surf, 12, 16, 24, 16, 2, nav_accent(240), 220,
                  IPGUI_LINE_CAP_ROUND);
    nav_draw_line(surf, 18, 10, 18, 22, 2, nav_accent(240), 220,
                  IPGUI_LINE_CAP_ROUND);

    nav_fill_round(surf, 0, 37, 36, 33, 10,
                   nav_rgb(0x08, 0x13, 0x1E, 255), 235);
    nav_stroke_round(surf, 0, 37, 36, 33, 10, 1, nav_accent(255), 65);
    nav_draw_line(surf, 12, 53, 24, 53, 2, nav_accent(240), 220,
                  IPGUI_LINE_CAP_ROUND);

    nav_press_overlay(surf, widget, 36, 70, 10);
}

static void nav_lane_render(ipgui_widget_t * widget, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_surf_t * surf = ctx->surf;
    int i;

    (void)widget;

    nav_fill_round(surf, 0, 0, 200, 66, 14,
                   nav_rgb(0x08, 0x13, 0x1E, 255), 235);
    nav_stroke_round(surf, 0, 0, 200, 66, 14, 1, nav_accent(255), 70);

    nav_draw_text(surf, &open_sans_7px, "LANE", 14, 8,
                  nav_rgb(0x7F, 0x94, 0xAB, 255), 180);
    nav_draw_text_right(surf, &open_sans_10px, "300 m", 186, 6,
                        nav_accent(245), 240);

    for (i = 0; i < 3; i ++) {
        int x = 18 + i * 38;
        int active = (i == 1);
        ipgui_color_t c = active ? nav_accent(255)
                                 : nav_rgb(0x5F, 0x74, 0x8B, 255);

        if (active) {
            nav_fill_round(surf, x - 6, 20, 30, 26, 7,
                           nav_accent(255), 24);
            nav_stroke_round(surf, x - 6, 20, 30, 26, 7, 1,
                             nav_accent(255), 90);
        }

        if (i == 0) {
            nav_draw_line(surf, x + 8, 42, x + 8, 30, 2, c,
                          active ? 230 : 150, IPGUI_LINE_CAP_ROUND);
            nav_draw_line(surf, x + 8, 30, x + 2, 36, 2, c,
                          active ? 230 : 150, IPGUI_LINE_CAP_ROUND);
        } else if (i == 1) {
            nav_draw_line(surf, x + 8, 43, x + 8, 28, 2, c,
                          active ? 235 : 150, IPGUI_LINE_CAP_ROUND);
            nav_draw_tri(surf, x + 8, 22, x + 2, 31, x + 14, 31,
                         c, active ? 235 : 150);
        } else {
            nav_draw_line(surf, x + 8, 43, x + 8, 31, 2, c,
                          active ? 230 : 150, IPGUI_LINE_CAP_ROUND);
            nav_draw_line(surf, x + 8, 31, x + 14, 25, 2, c,
                          active ? 230 : 150, IPGUI_LINE_CAP_ROUND);
        }
    }

    nav_draw_text(surf, &open_sans_7px, "KEEP RIGHT", 14, 50,
                  nav_rgb(0x7F, 0x94, 0xAB, 255), 180);
}

/*=============================================================================
 * 右侧信息卡
 *===========================================================================*/

static void nav_turn_render(ipgui_widget_t * widget, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_surf_t * surf = ctx->surf;

    (void)widget;

    nav_draw_panel(surf, 0, 0, 244, 128, 18,
                   nav_rgb(0x0C, 0x18, 0x26, 255),
                   nav_rgb(0x09, 0x13, 0x1F, 255),
                   nav_rgb(0x1C, 0x30, 0x47, 255), 180);

    nav_radial_glow(surf, 42, 58, 34, nav_accent(255), 46);
    nav_draw_circle(surf, 42, 58, 24, nav_accent(255), 20);
    nav_draw_arc(surf, 42, 58, 24, 23, 0, 360,
                 nav_accent(255), 85, IPGUI_ARC_ENDPOINT_TYPE_BUTT);

    nav_draw_line(surf, 34, 70, 34, 50, 4, nav_accent(255), 235,
                  IPGUI_LINE_CAP_ROUND);
    nav_draw_line(surf, 34, 50, 48, 50, 4, nav_accent(255), 235,
                  IPGUI_LINE_CAP_ROUND);
    nav_draw_tri(surf, 48, 42, 62, 50, 48, 58,
                 nav_accent(255), 245);

    nav_draw_text(surf, &open_sans_9px, "TURN RIGHT", 82, 12,
                  nav_accent(240), 230);
    nav_draw_text(surf, &quicksand_medium_26px, "300", 82, 24,
                  nav_rgb(0xE6, 0xF0, 0xFB, 255), 255);
    nav_draw_text(surf, &open_sans_10px, "m",
                  82 + nav_text_w(&quicksand_medium_26px, "300") + 5, 40,
                  nav_rgb(0x9A, 0xAF, 0xC6, 255), 220);
    nav_draw_text(surf, &open_sans_10px, "Wanfeng Rd", 82, 62,
                  nav_rgb(0x9A, 0xAF, 0xC6, 255), 225);
    nav_draw_text(surf, &open_sans_8px, "then 2.4 km", 82, 84,
                  nav_rgb(0x6F, 0x84, 0x9B, 255), 180);

    nav_draw_line(surf, 18, 108, 226, 108, 2,
                  nav_rgb(0x1A, 0x2A, 0x3A, 255), 140, IPGUI_LINE_CAP_BUTT);
    nav_draw_line(surf, 18, 108, 138, 108, 2,
                  nav_accent(255), 150, IPGUI_LINE_CAP_BUTT);
    nav_draw_circle(surf, 138, 108, 3, nav_accent(255), 240);
}

static void nav_eta_render(ipgui_widget_t * widget, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_surf_t * surf = ctx->surf;

    (void)widget;

    nav_draw_panel(surf, 0, 0, 244, 86, 16,
                   nav_rgb(0x0C, 0x18, 0x26, 255),
                   nav_rgb(0x09, 0x13, 0x1F, 255),
                   nav_rgb(0x1C, 0x30, 0x47, 255), 170);

    nav_draw_text(surf, &open_sans_8px, "ETA", 16, 10,
                  nav_rgb(0x7F, 0x94, 0xAB, 255), 190);
    nav_draw_text(surf, &quicksand_medium_22px, "12:48", 16, 20,
                  nav_rgb(0xE6, 0xF0, 0xFB, 255), 255);
    nav_draw_text(surf, &open_sans_9px, "18 min", 120, 28,
                  nav_accent(245), 235);
    nav_draw_text_right(surf, &open_sans_9px, "8.4 km", 228, 28,
                        nav_rgb(0x9A, 0xAF, 0xC6, 255), 220);

    nav_draw_line(surf, 16, 66, 228, 66, 3,
                  nav_rgb(0x1A, 0x2A, 0x3A, 255), 150, IPGUI_LINE_CAP_ROUND);
    nav_draw_line(surf, 16, 66, 111, 66, 3,
                  nav_accent(255), 190, IPGUI_LINE_CAP_ROUND);
    nav_draw_circle(surf, 111, 66, 3, nav_accent(255), 240);
    nav_draw_text(surf, &open_sans_7px, "ARRIVE 12:48", 16, 73,
                  nav_rgb(0x6F, 0x84, 0x9B, 255), 160);
}

static void nav_batt_render(ipgui_widget_t * widget, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_surf_t * surf = ctx->surf;
    const ipgui_emoto_snapshot_t * s = nav_snap();
    int soc = (int)nav_soc();
    int range = (int)nav_range();
    char buf[24];

    (void)widget;

    nav_draw_panel(surf, 0, 0, 244, 64, 16,
                   nav_rgb(0x0C, 0x18, 0x26, 255),
                   nav_rgb(0x09, 0x13, 0x1F, 255),
                   nav_rgb(0x1C, 0x30, 0x47, 255), 170);

    icon_nav_battery(surf, 16, 22, 36, 18, soc, nav_accent(255), 235);

    snprintf(buf, sizeof(buf), "%d%%", soc);
    nav_draw_text(surf, &quicksand_medium_16px, buf, 62, 16,
                  nav_rgb(0xE6, 0xF0, 0xFB, 255), 255);

    snprintf(buf, sizeof(buf), "%d km", range);
    nav_draw_text(surf, &open_sans_9px, buf, 62, 38,
                  nav_rgb(0x9A, 0xAF, 0xC6, 255), 220);

    if (s && s->valid && s->charging) {
        nav_draw_text(surf, &open_sans_7px, "CHARGING", 196, 10,
                      nav_accent(240), 220);
        icon_nav_bolt(surf, 222, 46, nav_accent(255), 220);
    } else {
        nav_draw_text(surf, &open_sans_7px, "RANGE", 196, 10,
                      nav_rgb(0x6F, 0x84, 0x9B, 255), 180);
    }

    nav_draw_line(surf, 150, 30, 226, 30, 4,
                  nav_rgb(0x1A, 0x2A, 0x3A, 255), 160, IPGUI_LINE_CAP_ROUND);
    nav_draw_line(surf, 150, 30, 150 + 76 * nav_clamp(soc, 0, 100) / 100,
                  30, 4, nav_accent(255), 210, IPGUI_LINE_CAP_ROUND);
}

static void nav_stats_render(ipgui_widget_t * widget, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_surf_t * surf = ctx->surf;
    char buf[32];

    (void)widget;

    nav_draw_panel(surf, 0, 0, 244, 42, 14,
                   nav_rgb(0x0C, 0x18, 0x26, 255),
                   nav_rgb(0x09, 0x13, 0x1F, 255),
                   nav_rgb(0x1C, 0x30, 0x47, 255), 150);

    nav_draw_text(surf, &open_sans_7px, "AVG", 14, 6,
                  nav_rgb(0x6F, 0x84, 0x9B, 255), 180);
    snprintf(buf, sizeof(buf), "%d km/h", (int)nav_avg_speed());
    nav_draw_text(surf, &open_sans_9px, buf, 14, 20,
                  nav_rgb(0xD8, 0xE4, 0xF2, 255), 230);

    nav_draw_text(surf, &open_sans_7px, "TRIP", 92, 6,
                  nav_rgb(0x6F, 0x84, 0x9B, 255), 180);
    snprintf(buf, sizeof(buf), "%d.%d km", (int)(nav_trip() / 100),
             (int)((nav_trip() % 100) / 10));
    nav_draw_text(surf, &open_sans_9px, buf, 92, 20,
                  nav_rgb(0xD8, 0xE4, 0xF2, 255), 230);

    nav_draw_text(surf, &open_sans_7px, "ELEV", 170, 6,
                  nav_rgb(0x6F, 0x84, 0x9B, 255), 180);
    nav_draw_text(surf, &open_sans_9px, "128 m", 170, 20,
                  nav_rgb(0xD8, 0xE4, 0xF2, 255), 230);
}

static void nav_speed_render(ipgui_widget_t * widget, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_surf_t * surf = ctx->surf;
    int speed = (int)nav_speed();
    int soc = (int)nav_soc();
    int range = (int)nav_range();
    u8_t mode = nav_mode();
    char buf[32];
    ipgui_coord_t tw;

    (void)widget;

    nav_draw_panel(surf, 0, 0, 500, 44, 16,
                   nav_rgb(0x0C, 0x18, 0x26, 255),
                   nav_rgb(0x09, 0x13, 0x1F, 255),
                   nav_rgb(0x1C, 0x30, 0x47, 255), 170);

    snprintf(buf, sizeof(buf), "%d", speed);
    nav_draw_text(surf, &quicksand_medium_28px, buf, 18, 2,
                  nav_rgb(0xE6, 0xF0, 0xFB, 255), 255);
    tw = nav_text_w(&quicksand_medium_28px, buf);
    nav_draw_text(surf, &open_sans_9px, "km/h", 22 + tw, 20,
                  nav_rgb(0x9A, 0xAF, 0xC6, 255), 220);

    nav_fill_round(surf, 126, 9, 68, 26, 13, nav_accent(255), 20);
    nav_stroke_round(surf, 126, 9, 68, 26, 13, 1, nav_accent(255), 90);
    nav_draw_text_center(surf, &open_sans_8px,
                         (mode < 3) ? NAV_MODE_NAME[mode] : "SPORT",
                         160, 17, nav_accent(250), 240);

    nav_draw_text(surf, &open_sans_7px, "TRIP", 216, 7,
                  nav_rgb(0x6F, 0x84, 0x9B, 255), 180);
    snprintf(buf, sizeof(buf), "%d.%d km", (int)(nav_trip() / 100),
             (int)((nav_trip() % 100) / 10));
    nav_draw_text(surf, &open_sans_11px, buf, 216, 20,
                  nav_rgb(0xD8, 0xE4, 0xF2, 255), 235);

    icon_nav_battery(surf, 356, 12, 34, 18, soc, nav_accent(255), 235);
    snprintf(buf, sizeof(buf), "%d%%", soc);
    nav_draw_text(surf, &open_sans_9px, buf, 398, 11,
                  nav_rgb(0xE6, 0xF0, 0xFB, 255), 240);
    snprintf(buf, sizeof(buf), "%d km", range);
    nav_draw_text(surf, &open_sans_9px, buf, 440, 11,
                  nav_rgb(0x9A, 0xAF, 0xC6, 255), 220);
}

static void nav_gps_render(ipgui_widget_t * widget, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_surf_t * surf = ctx->surf;

    (void)widget;

    nav_draw_panel(surf, 0, 0, 244, 44, 14,
                   nav_rgb(0x0C, 0x18, 0x26, 255),
                   nav_rgb(0x09, 0x13, 0x1F, 255),
                   nav_rgb(0x1C, 0x30, 0x47, 255), 150);

    icon_nav_satellite(surf, 14, 14, nav_accent(255), 220);
    nav_draw_text(surf, &open_sans_8px, "GPS LOCK", 44, 8,
                  nav_rgb(0xD8, 0xE4, 0xF2, 255), 220);
    nav_draw_text(surf, &open_sans_8px, "12 SAT  /  HDOP 0.8", 44, 24,
                  nav_rgb(0x7F, 0x94, 0xAB, 255), 190);

    nav_draw_circle(surf, 222, 22, 3,
                    nav_rgb(0x6E, 0xF2, 0xA0, 255), 240);
}

/*=============================================================================
 * 页面背景
 *===========================================================================*/

static void nav_bg_render(ipgui_widget_t * widget, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_surf_t * surf = ctx->surf;
    ipgui_aabb_t box;
    ipgui_box_style_t bs;
    ipgui_box_bg_style_t bg;
    ipgui_liner_gradient_color_t grad;
    ipgui_gradient_color_stop_t s0, s1;
    int i;

    (void)widget;

    box.start.x = 0;
    box.start.y = 0;
    box.end.x   = NAV_W - 1;
    box.end.y   = NAV_H - 1;
    memset(&bs, 0, sizeof(bs));

    ipgui_liner_gradient_init_direct(&grad, 0, 0, 0, NAV_H - 1);
    s0.pos = 0;   s0.color = nav_rgb(0x04, 0x07, 0x0B, 255);
    s1.pos = 255; s1.color = nav_rgb(0x08, 0x12, 0x1C, 255);
    ipgui_liner_gradient_add_stop(&grad, &s0);
    ipgui_liner_gradient_add_stop(&grad, &s1);

    memset(&bg, 0, sizeof(bg));
    bg.paint.type = IPGUI_PAINT_GRADIENT;
    bg.paint.src.grad_src.grad_type = IPGUI_GRADIENT_TYPE_LINEAR;
    bg.paint.src.grad_src.grad.liner_grad = grad;
    bg.opacity = 255;
    bg.blend_mode = IPGUI_BLEND_NORMAL;
    ipgui_draw_box_background(surf, (ipgui_aabb_t *)0, &box, &bs, &bg);

    for (i = 0; i < NAV_W; i += 64) {
        nav_draw_line(surf, i, 0, i, NAV_H - 1, 1,
                      nav_rgb(0x12, 0x20, 0x2E, 255), 8,
                      IPGUI_LINE_CAP_BUTT);
    }
    for (i = 0; i < NAV_H; i += 64) {
        nav_draw_line(surf, 0, i, NAV_W - 1, i, 1,
                      nav_rgb(0x12, 0x20, 0x2E, 255), 8,
                      IPGUI_LINE_CAP_BUTT);
    }

    nav_radial_glow(surf, 706, 76, 230, nav_accent(255), 22);
    nav_radial_glow(surf, 80, 468, 210,
                    nav_rgb(0x2B, 0x4E, 0xFF, 255), 14);

    nav_draw_line(surf, 0, 472, NAV_W - 1, 472, 1,
                  nav_rgb(0x1B, 0x2C, 0x3E, 255), 90,
                  IPGUI_LINE_CAP_BUTT);
}

/*=============================================================================
 * 事件 / 每帧推进 / 初始化
 *===========================================================================*/

static void nav_event(ipgui_widget_t * widget, ipgui_widget_evt_t * evt)
{
    if (!widget || !evt) return;
    if (ipgui_emoto_transition_active()) return;

    if (evt->type == IPGUI_WIDGET_EVENT_PRESSED) {
        g.pressed = widget;
        ipgui_widget_mark_dirty(widget);
        return;
    }

    if (evt->type != IPGUI_WIDGET_EVENT_RELEASED) return;

    if (widget == g.w_back) {
        g.pressed = (ipgui_widget_t *)0;
        ipgui_widget_mark_dirty(widget);
        ipgui_emoto_transition_to(IPGUI_EMOTO_PAGE_RIDE);
        return;
    }

    if (widget == g.w_zoom) {
        ipgui_coord_t y = evt->evt.released_evt.y;
        s32_t old = g.zoom;

        if (y < 35) g.zoom += 25;
        else        g.zoom -= 25;
        g.zoom = nav_clamp(g.zoom, 80, 170);

        if (g.zoom != old && g.w_map) {
            ipgui_widget_mark_dirty(g.w_map);
        }
        g.pressed = (ipgui_widget_t *)0;
        ipgui_widget_mark_dirty(widget);
        return;
    }

    g.pressed = (ipgui_widget_t *)0;
    ipgui_widget_mark_dirty(widget);
}

__IPGUI_API__ void ipgui_page_emoto_nav_tick(void)
{
    const ipgui_emoto_snapshot_t * s;
    u32_t now;
    s32_t dt;
    u8_t  old_r, old_g, old_b;
    u8_t  mode;
    s32_t speed;

    if (!g.inited || !g.scr) return;

    g.visible = (g.scr->act_page == g.page) ? 1 : 0;
    if (!g.visible) {
        g.pressed = (ipgui_widget_t *)0;
        return;
    }

    now = ipgui_tick_now();
    dt  = (s32_t)(now - g.last_tick);
    if (dt <= 0) return;
    if (dt > 50) dt = 50;
    g.last_tick = now;

    s = nav_snap();
    mode = (s && s->valid && s->mode < 3) ? s->mode : 1;

    old_r = g.acc_r;
    old_g = g.acc_g;
    old_b = g.acc_b;
    g.acc_r = (u8_t)nav_ease(g.acc_r, NAV_MODE_ACCENT[mode].r, dt, 140);
    g.acc_g = (u8_t)nav_ease(g.acc_g, NAV_MODE_ACCENT[mode].g, dt, 140);
    g.acc_b = (u8_t)nav_ease(g.acc_b, NAV_MODE_ACCENT[mode].b, dt, 140);

    if (old_r != g.acc_r || old_g != g.acc_g || old_b != g.acc_b) {
        ipgui_dirty_rect_add_xywh(&g.scr->dirty, 0, 0, NAV_W, NAV_H);
    }

    /* 定位脉冲局部刷新 */
    if ((now - g.last_pulse) >= 45) {
        ipgui_coord_t x, y, w, h;
        g.last_pulse = now;
        nav_current_screen_rect(&x, &y, &w, &h);
        ipgui_dirty_rect_add_xywh(&g.scr->dirty, x, y, w, h);
    }

    /* 速度条 */
    speed = (s && s->valid) ? s->speed : 0;
    if (speed != g.last_speed || (now - g.last_speed_ms) >= 250) {
        g.last_speed = speed;
        g.last_speed_ms = now;
        if (g.w_speed) ipgui_widget_mark_dirty(g.w_speed);
    }

    /* 电池 / 统计 / GPS */
    if ((now - g.last_batt_ms) >= 500) {
        g.last_batt_ms = now;
        if (g.w_batt)  ipgui_widget_mark_dirty(g.w_batt);
        if (g.w_stats) ipgui_widget_mark_dirty(g.w_stats);
        if (g.w_gps)   ipgui_widget_mark_dirty(g.w_gps);
    }

    /* 顶栏时间 */
    if ((now - g.last_top_ms) >= 60000) {
        g.last_top_ms = now;
        if (g.w_top) ipgui_widget_mark_dirty(g.w_top);
    }
}

static ipgui_widget_t * nav_make_widget(
    ipgui_coord_t x, ipgui_coord_t y, ipgui_coord_t w, ipgui_coord_t h,
    const char * name,
    void (*render)(struct ipgui_widget *, ipgui_widget_render_ctx_t *),
    void (*event)(struct ipgui_widget *, ipgui_widget_evt_t *))
{
    ipgui_widget_t * ww = ipgui_widget_create((ipgui_widget_t *)0);

    if (!ww) return (ipgui_widget_t *)0;

    ww->x = x;
    ww->y = y;
    ww->w = w;
    ww->h = h;
    ww->name = name;
    ww->render = render;
    ww->event_handler = event;

    return ww;
}

__IPGUI_API__ void ipgui_page_emoto_nav_init(ipgui_scr_t * scr, u8_t page)
{
    const ipgui_emoto_snapshot_t * s;
    u8_t mode;

    if (!scr || g.inited) return;

    memset(&g, 0, sizeof(g));

    g.scr   = scr;
    g.page  = page;
    g.zoom  = 100;
    g.last_speed = -9999;

    ipgui_screen_page_switch(scr, page);

    s = nav_snap();
    mode = (s && s->valid && s->mode < 3) ? s->mode : 1;
    g.acc_r = g.acc_tr = NAV_MODE_ACCENT[mode].r;
    g.acc_g = g.acc_tg = NAV_MODE_ACCENT[mode].g;
    g.acc_b = g.acc_tb = NAV_MODE_ACCENT[mode].b;

    g.w_bg = nav_make_widget(0, 0, NAV_W, NAV_H, "emoto_nav_bg",
                             nav_bg_render, (void (*)(struct ipgui_widget *, ipgui_widget_evt_t *))0);
    g.w_top = nav_make_widget(0, 0, NAV_W, 58, "emoto_nav_top",
                              nav_top_render, (void (*)(struct ipgui_widget *, ipgui_widget_evt_t *))0);

    g.w_map = nav_make_widget(20, 68, NAV_MAP_W, NAV_MAP_H, "emoto_nav_map",
                              nav_map_render, (void (*)(struct ipgui_widget *, ipgui_widget_evt_t *))0);

    g.w_lane = nav_make_widget(36, 330, 200, 66, "emoto_nav_lane",
                               nav_lane_render, (void (*)(struct ipgui_widget *, ipgui_widget_evt_t *))0);
    if (g.w_lane) g.w_lane->flags |= IPGUI_WIDGET_FLAG_DISABLED;

    g.w_zoom = nav_make_widget(470, 330, 36, 70, "emoto_nav_zoom",
                               nav_zoom_render, nav_event);

    g.w_turn = nav_make_widget(536, 68, 244, 128, "emoto_nav_turn",
                               nav_turn_render, (void (*)(struct ipgui_widget *, ipgui_widget_evt_t *))0);
    g.w_eta  = nav_make_widget(536, 204, 244, 86, "emoto_nav_eta",
                               nav_eta_render, (void (*)(struct ipgui_widget *, ipgui_widget_evt_t *))0);
    g.w_batt = nav_make_widget(536, 298, 244, 64, "emoto_nav_batt",
                               nav_batt_render, (void (*)(struct ipgui_widget *, ipgui_widget_evt_t *))0);
    g.w_stats = nav_make_widget(536, 370, 244, 42, "emoto_nav_stats",
                                nav_stats_render, (void (*)(struct ipgui_widget *, ipgui_widget_evt_t *))0);

    g.w_speed = nav_make_widget(20, 420, 500, 44, "emoto_nav_speed",
                                nav_speed_render, (void (*)(struct ipgui_widget *, ipgui_widget_evt_t *))0);
    g.w_gps   = nav_make_widget(536, 420, 244, 44, "emoto_nav_gps",
                                nav_gps_render, (void (*)(struct ipgui_widget *, ipgui_widget_evt_t *))0);

    g.w_back = nav_make_widget(20, 12, 86, 32, "emoto_nav_back",
                               nav_back_render, nav_event);

    g.w_trans = nav_make_widget(0, 0, NAV_W, NAV_H, "RIDE MODE",
                                ipgui_emoto_transition_render,
                                (void (*)(struct ipgui_widget *, ipgui_widget_evt_t *))0);
    if (g.w_trans) {
        g.w_trans->flags |= IPGUI_WIDGET_FLAG_INVISIBLE;
        g.w_trans->flags |= IPGUI_WIDGET_FLAG_DISABLED;
        ipgui_emoto_transition_register(page, g.w_trans);
    }

    g.inited = 1;

    /* 初始化完成后回到 RIDE 页，启动画面仍然是骑行主界面 */
    ipgui_screen_page_switch(scr, IPGUI_EMOTO_PAGE_RIDE);
    ipgui_dirty_rect_add_xywh(&scr->dirty, 0, 0, NAV_W, NAV_H);
}
