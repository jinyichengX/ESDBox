/*
 * E-MOTION 电摩 HMI —— 独立页面
 *
 * 设计目标：
 *   1. 不是“把图元摆上去”的静态 demo，而是有状态机、有插值、有点击反馈的整页 HMI。
 *   2. 分控件脏矩形：仪表、电池、电机、行程、导航、状态栏、模式切换各自独立重绘。
 *   3. 视觉上采用深色座舱 + 电光青主色 + 模式主题色，所有图标用矢量绘制，不依赖位图资源。
 *
 * 交互：
 *   - 点击底部 ECO / SPORT / RAIN：切换骑行模式，仪表量程、主题色、滑轨指示器联动。
 *   - 点击电池卡：插枪 / 拔枪，充电时 SOC 上升、电量条呼吸。
 *   - 点击行程卡：TRIP A / TRIP B / ODO 三种视图。
 *   - 点击右下卡：导航 / 胎压两种视图。
 *   - 点击时有轻微高亮反馈。
 *
 * 说明：
 *   - 本页按 800x480 设计，主循环按 1 tick = 1ms 递增。
 *   - 页面创建后会播放约 1.4s 的启动动画，然后进入 RIDE 主界面。
 */

#include "ipgui_page_emoto.h"
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
#include <stdlib.h>

/*=============================================================================
 * 常量 / 主题
 *===========================================================================*/

#define EMOTO_W             800
#define EMOTO_H             480

#define EMOTO_MODE_ECO      0
#define EMOTO_MODE_SPORT    1
#define EMOTO_MODE_RAIN     2

#define EMOTO_BOOT_MS       900
#define EMOTO_BOOT_FADE_MS  500

typedef struct {
    u8_t r, g, b;
} emoto_rgb_t;

static const emoto_rgb_t MODE_ACCENT[3] = {
    { 155, 225,  93 },   /* ECO   lime  */
    {  53, 224, 255 },   /* SPORT cyan  */
    {  91, 140, 255 },   /* RAIN  blue  */
};

static const s32_t MODE_MAX_SPEED[3]  = { 80, 120, 60 };
static const s32_t MODE_MAJOR_STEP[3] = { 20,  20, 10 };
static const char *MODE_NAME[3]       = { "ECO", "SPORT", "RAIN" };

/*=============================================================================
 * 页面状态
 *===========================================================================*/

typedef struct {
    u8_t           inited;
    u8_t           boot_done;
    u8_t           page;
    u8_t           visible;
    u8_t           mode;
    u8_t           charging;
    u8_t           trip_view;      /* 0=A 1=B 2=ODO */
    u8_t           nav_view;       /* 0=NAV 1=TIRE */

    u8_t           left_on;
    u8_t           right_on;
    u8_t           high_beam;
    u8_t           abs_on;
    u8_t           tc_on;
    u8_t           blink;

    s32_t          speed;
    s32_t          speed_target;
    s32_t          soc_fp;         /* SOC * 16 */
    s32_t          soc;            /* 显示值 */
    s32_t          soc_target;
    s32_t          soc_ms;
    s32_t          power_x10;      /* kW * 10，可为负（回馈） */
    s32_t          torque_x10;
    s32_t          eff;
    s32_t          motor_temp;
    s32_t          volt_x10;
    s32_t          range_km;

    s32_t          trip_x100;
    s32_t          trip_b_x100;
    s32_t          odo_x100;
    s32_t          ride_ms;
    s32_t          avg_speed;
    s32_t          max_seen;

    s32_t          tab_x;          /* 指示器 x * 16 */
    s32_t          tab_target_x;
    u8_t           acc_r, acc_g, acc_b;
    u8_t           acc_tr, acc_tg, acc_tb;

    u32_t          boot_start;
    u32_t          last_tick;
    u32_t          last_minute;
    u32_t          last_motor_ms;
    u32_t          last_trip_ms;
    u32_t          last_batt_ms;

    ipgui_scr_t  * scr;
    ipgui_widget_t *w_bg;
    ipgui_widget_t *w_top;
    ipgui_widget_t *w_tell;
    ipgui_widget_t *w_center;
    ipgui_widget_t *w_batt;
    ipgui_widget_t *w_motor;
    ipgui_widget_t *w_trip;
    ipgui_widget_t *w_nav;
    ipgui_widget_t *w_tabs;
    ipgui_widget_t *w_navbtn;
    ipgui_widget_t *w_boot;
    ipgui_widget_t *w_trans;
    ipgui_widget_t *pressed;
} emoto_state_t;

static emoto_state_t g;

/*=============================================================================
 * 基础工具
 *===========================================================================*/

static ipgui_color_t emoto_rgb(u8_t r, u8_t g_, u8_t b, u8_t a)
{
    ipgui_color_t c;
    c.r = r;
    c.g = g_;
    c.b = b;
    c.a = a;
    return c;
}

static ipgui_color_t accent_color(u8_t a)
{
    return emoto_rgb(g.acc_r, g.acc_g, g.acc_b, a);
}

static s32_t clamp_s32(s32_t v, s32_t lo, s32_t hi);

static ipgui_color_t lerp_rgb(emoto_rgb_t a, emoto_rgb_t b, s32_t t)
{
    return emoto_rgb(
        (u8_t)(a.r + (b.r - a.r) * t / 255),
        (u8_t)(a.g + (b.g - a.g) * t / 255),
        (u8_t)(a.b + (b.b - a.b) * t / 255), 255);
}

/* 取模式渐变在 t=0..255 处的颜色，用于进度端点光晕 */
static ipgui_color_t progress_color(s32_t t)
{
    emoto_rgb_t c0, c1, c2;

    if (g.mode == EMOTO_MODE_ECO) {
        c0 = MODE_ACCENT[EMOTO_MODE_ECO];
        c1 = MODE_ACCENT[EMOTO_MODE_SPORT];
        c2 = MODE_ACCENT[EMOTO_MODE_SPORT];
    } else if (g.mode == EMOTO_MODE_RAIN) {
        c0 = MODE_ACCENT[EMOTO_MODE_RAIN];
        c1 = (emoto_rgb_t){ 122, 120, 255 };
        c2 = (emoto_rgb_t){ 180, 120, 255 };
    } else {
        c0 = MODE_ACCENT[EMOTO_MODE_SPORT];
        c1 = (emoto_rgb_t){ 80, 140, 255 };
        c2 = (emoto_rgb_t){ 140, 92, 255 };
    }

    t = clamp_s32(t, 0, 255);
    if (t < 128) return lerp_rgb(c0, c1, t * 2);
    return lerp_rgb(c1, c2, (t - 128) * 2);
}

static s32_t iabs(s32_t v)
{
    return (v < 0) ? -v : v;
}

static s32_t clamp_s32(s32_t v, s32_t lo, s32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* 一阶趋近：diff 很大时也不会过冲 */
static s32_t ease_s32(s32_t cur, s32_t target, s32_t dt, s32_t tau_ms)
{
    s32_t diff, step;

    if (dt <= 0 || cur == target) return target;

    diff = target - cur;
    step = diff * dt / tau_ms;
    if (step == 0) step = (diff > 0) ? 1 : -1;

    if ((diff > 0 && cur + step > target) ||
        (diff < 0 && cur + step < target)) {
        return target;
    }
    return cur + step;
}

static ipgui_coord_t pt_x(ipgui_coord_t cx, ipgui_coord_t r, s32_t angle)
{
    return cx + (ipgui_coord_t)(((s64_t)ipgui_cos(angle) * r) >> 15);
}

static ipgui_coord_t pt_y(ipgui_coord_t cy, ipgui_coord_t r, s32_t angle)
{
    return cy + (ipgui_coord_t)(((s64_t)ipgui_sin(angle) * r) >> 15);
}

/*=============================================================================
 * 绘图原语
 *===========================================================================*/

static void draw_line(
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

    ipgui_draw_line_generic(surf, (ipgui_aabb_t *)0, &line, &style);
}

static void draw_arc(
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
    /* 屏幕坐标 y 向下，CCW 才是视觉上的“从左下沿左侧扫到顶部” */
    arc.dir   = IPGUI_ARC_DRAW_DIR_CCW;
    arc.angle = (u16_t)angle;

    memset(&style, 0, sizeof(style));
    style.paint.type = IPGUI_PAINT_COLOR;
    style.paint.src.color = color;
    style.opacity = opacity;
    style.blend_mode = IPGUI_BLEND_NORMAL;
    style.sep_type = cap;
    style.eep_type = cap;

    ipgui_draw_arc(surf, (ipgui_aabb_t *)0, &arc, &style);
}

static void draw_circle(
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

    ipgui_draw_filled_circle(surf, (ipgui_aabb_t *)0, cx, cy, r, &style);
}

static void draw_tri(
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

    ipgui_draw_triangle(surf, (ipgui_aabb_t *)0, &p1, &p2, &p3, &style);
}

static void fill_round(
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

    ipgui_draw_box_background(surf, (ipgui_aabb_t *)0, &box, &bs, &bg);
}

static void stroke_round(
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

    ipgui_draw_box_border(surf, (ipgui_aabb_t *)0, &box, &bs, &st);
}

static void draw_panel(
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

    ipgui_draw_box_background(surf, (ipgui_aabb_t *)0, &box, &bs, &bg);

    if (border_opacity >= 3) {
        stroke_round(surf, x, y, w, h, r, 1, border, border_opacity);
    }

    /* 顶部 1px 高光，做出“玻璃边缘”的层次 */
    draw_line(surf, x + r, y + 1, x + w - 1 - r, y + 1, 1,
              emoto_rgb(255, 255, 255, 255), 16, IPGUI_LINE_CAP_BUTT);
}

static void draw_text(
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

    ipgui_draw_builtin_text(surf, (ipgui_aabb_t *)0, &style,
                            (const s8_t *)text, x, y);
}

static ipgui_coord_t text_w(const ipgui_font_t * font, const char * text)
{
    if (!font || !text) return 0;
    return ipgui_builtin_text_width(font, (const s8_t *)text);
}

static void draw_text_center(
    ipgui_surf_t * surf, const ipgui_font_t * font, const char * text,
    ipgui_coord_t cx, ipgui_coord_t y, ipgui_color_t color, u8_t opacity)
{
    ipgui_coord_t w = text_w(font, text);
    draw_text(surf, font, text, cx - w / 2, y, color, opacity);
}

static void draw_text_right(
    ipgui_surf_t * surf, const ipgui_font_t * font, const char * text,
    ipgui_coord_t right, ipgui_coord_t y, ipgui_color_t color, u8_t opacity)
{
    ipgui_coord_t w = text_w(font, text);
    draw_text(surf, font, text, right - w, y, color, opacity);
}

static void draw_text_gradient_shadowed(
    ipgui_surf_t * surf, const ipgui_font_t * font, const char * text,
    ipgui_coord_t x, ipgui_coord_t y,
    ipgui_color_t top, ipgui_color_t bottom,
    ipgui_color_t shadow_color, u8_t shadow_opacity, ipgui_coord_t blur)
{
    ipgui_font_style_t style;
    ipgui_text_shadow_style_t shadow;
    ipgui_liner_gradient_color_t grad;
    ipgui_gradient_color_stop_t s0, s1;

    ipgui_liner_gradient_init_direct(&grad, 0, y, 0, y + font->line_height);
    s0.pos = 0;   s0.color = top;
    s1.pos = 255; s1.color = bottom;
    ipgui_liner_gradient_add_stop(&grad, &s0);
    ipgui_liner_gradient_add_stop(&grad, &s1);

    memset(&style, 0, sizeof(style));
    style.font       = font;
    style.opacity    = 255;
    style.paint.type = IPGUI_PAINT_GRADIENT;
    style.paint.src.grad_src.grad_type = IPGUI_GRADIENT_TYPE_LINEAR;
    style.paint.src.grad_src.grad.liner_grad = grad;
    style.blend_mode = IPGUI_BLEND_NORMAL;

    memset(&shadow, 0, sizeof(shadow));
    shadow.color      = shadow_color;
    shadow.opacity    = shadow_opacity;
    shadow.blur       = blur;
    shadow.offset_x   = 0;
    shadow.offset_y   = 0;
    shadow.quality    = IPGUI_TEXT_SHADOW_QUALITY_LOW;
    shadow.blend_mode = IPGUI_BLEND_NORMAL;

    ipgui_draw_builtin_text_shadowed(surf, (ipgui_aabb_t *)0, &style, &shadow,
                                     (const s8_t *)text, x, y);
}

static void draw_press_overlay(
    ipgui_surf_t * surf, ipgui_widget_t * widget,
    ipgui_coord_t w, ipgui_coord_t h, ipgui_coord_t r)
{
    if (g.pressed != widget) return;

    fill_round(surf, 0, 0, w, h, r, emoto_rgb(255, 255, 255, 255), 12);
    stroke_round(surf, 0, 0, w, h, r, 1, accent_color(255), 90);
}

/*=============================================================================
 * 矢量图标
 *===========================================================================*/

static void icon_bluetooth(ipgui_surf_t * s, int x, int y,
                           ipgui_color_t c, u8_t op)
{
    draw_line(s, x + 5, y + 1,  x + 5,  y + 15, 1, c, op, IPGUI_LINE_CAP_ROUND);
    draw_line(s, x + 5, y + 1,  x + 11, y + 6,  1, c, op, IPGUI_LINE_CAP_ROUND);
    draw_line(s, x + 11, y + 6, x + 2,  y + 12, 1, c, op, IPGUI_LINE_CAP_ROUND);
    draw_line(s, x + 2,  y + 3, x + 11, y + 9,  1, c, op, IPGUI_LINE_CAP_ROUND);
    draw_line(s, x + 11, y + 9, x + 5,  y + 15, 1, c, op, IPGUI_LINE_CAP_ROUND);
}

static void icon_gps(ipgui_surf_t * s, int x, int y,
                     ipgui_color_t c, u8_t op)
{
    draw_arc(s, x + 7, y + 7, 6, 4, 0, 360, c, op,
             IPGUI_ARC_ENDPOINT_TYPE_BUTT);
    draw_tri(s, x + 1, y + 7, x + 13, y + 7, x + 7, y + 16, c, op);
    draw_circle(s, x + 7, y + 7, 2, c, op);
}

static void icon_signal(ipgui_surf_t * s, int x, int y,
                        ipgui_color_t c, u8_t op)
{
    int i;
    for (i = 0; i < 4; i ++) {
        int h = 3 + i * 3;
        draw_line(s, x + i * 4, y + 15 - h, x + i * 4, y + 15, 2, c,
                  (u8_t)((i == 3) ? (op / 3) : op), IPGUI_LINE_CAP_BUTT);
    }
}

static void icon_headlight(ipgui_surf_t * s, int x, int y,
                           ipgui_color_t c, u8_t op)
{
    draw_circle(s, x + 5, y + 7, 4, c, op);
    draw_line(s, x + 10, y + 3,  x + 15, y + 1,  1, c, op, IPGUI_LINE_CAP_ROUND);
    draw_line(s, x + 11, y + 7,  x + 16, y + 7,  1, c, op, IPGUI_LINE_CAP_ROUND);
    draw_line(s, x + 10, y + 11, x + 15, y + 13, 1, c, op, IPGUI_LINE_CAP_ROUND);
}

static void icon_turn(ipgui_surf_t * s, int x, int y, int dir,
                      ipgui_color_t c, u8_t op)
{
    if (dir < 0) {
        draw_tri(s, x + 11, y + 2, x + 2, y + 8, x + 11, y + 14, c, op);
        draw_line(s, x + 8, y + 8, x + 14, y + 8, 1, c, op, IPGUI_LINE_CAP_ROUND);
    } else {
        draw_tri(s, x + 3, y + 2, x + 12, y + 8, x + 3, y + 14, c, op);
        draw_line(s, x + 0, y + 8, x + 6, y + 8, 1, c, op, IPGUI_LINE_CAP_ROUND);
    }
}

static void icon_bolt(ipgui_surf_t * s, int x, int y,
                      ipgui_color_t c, u8_t op)
{
    draw_tri(s, x + 8, y + 0, x + 2, y + 9, x + 7, y + 9, c, op);
    draw_tri(s, x + 7, y + 9, x + 4, y + 16, x + 12, y + 5, c, op);
}

static void icon_battery(ipgui_surf_t * s, int x, int y, int w, int h,
                         int soc, ipgui_color_t c, u8_t op)
{
    int fill_w;

    stroke_round(s, x, y, w, h, 3, 1, c, (u8_t)(op / 2));
    fill_round(s, x + w, y + h / 3, 2, h / 3, 1, c, (u8_t)(op / 2));

    fill_w = (w - 4) * clamp_s32(soc, 0, 100) / 100;
    if (fill_w > 0) {
        fill_round(s, x + 2, y + 2, fill_w, h - 4, 2, c, op);
    }
}

static void icon_motor(ipgui_surf_t * s, int x, int y,
                       ipgui_color_t c, u8_t op)
{
    draw_arc(s, x + 8, y + 8, 8, 7, 0, 360, c, op,
             IPGUI_ARC_ENDPOINT_TYPE_BUTT);
    draw_text_center(s, &open_sans_10px, "M", x + 8, y + 2, c, op);
}

static void icon_tire(ipgui_surf_t * s, int x, int y,
                      ipgui_color_t c, u8_t op)
{
    draw_arc(s, x + 7, y + 8, 7, 6, 0, 360, c, op,
             IPGUI_ARC_ENDPOINT_TYPE_BUTT);
    draw_line(s, x + 7, y + 2,  x + 7, y + 5,  1, c, op, IPGUI_LINE_CAP_ROUND);
    draw_line(s, x + 7, y + 11, x + 7, y + 14, 1, c, op, IPGUI_LINE_CAP_ROUND);
}

static void icon_sun(ipgui_surf_t * s, int x, int y,
                     ipgui_color_t c, u8_t op)
{
    int i;
    draw_circle(s, x + 8, y + 8, 4, c, op);
    for (i = 0; i < 8; i ++) {
        s32_t a = i * 45;
        draw_line(s,
                  pt_x(x + 8, 7, a),  pt_y(y + 8, 7, a),
                  pt_x(x + 8, 10, a), pt_y(y + 8, 10, a),
                  1, c, op, IPGUI_LINE_CAP_ROUND);
    }
}

static void icon_chevron(ipgui_surf_t * s, int x, int y,
                         ipgui_color_t c, u8_t op)
{
    draw_line(s, x + 1, y + 4, x + 5, y + 8, 1, c, op, IPGUI_LINE_CAP_ROUND);
    draw_line(s, x + 5, y + 8, x + 1, y + 12, 1, c, op, IPGUI_LINE_CAP_ROUND);
}

/*=============================================================================
 * 背景页：静态层
 *===========================================================================*/

static void radial_glow(
    ipgui_surf_t * surf, ipgui_coord_t cx, ipgui_coord_t cy,
    ipgui_coord_t r, ipgui_color_t color, u8_t center_alpha)
{
    ipgui_radial_gradient_color_t grad;
    ipgui_gradient_color_stop_t s0, s1;
    ipgui_filled_circle_style_t style;

    if (r <= 0 || center_alpha < 3) return;

    ipgui_radial_gradient_init(&grad, cx, cy, r);

    color.a = center_alpha;
    s0.pos = 0;
    s0.color = color;

    color.a = 0;
    s1.pos = 255;
    s1.color = color;

    ipgui_radial_gradient_add_stop(&grad, &s0);
    ipgui_radial_gradient_add_stop(&grad, &s1);

    memset(&style, 0, sizeof(style));
    style.paint.type = IPGUI_PAINT_GRADIENT;
    style.paint.src.grad_src.grad_type = IPGUI_GRADIENT_TYPE_RADIAL;
    style.paint.src.grad_src.grad.radial_grad = grad;
    style.opacity = 255;
    style.blend_mode = IPGUI_BLEND_NORMAL;

    ipgui_draw_filled_circle(surf, (ipgui_aabb_t *)0, cx, cy, r, &style);
}

static void draw_gauge_ticks(ipgui_surf_t * surf)
{
    ipgui_coord_t cx = 400;
    ipgui_coord_t cy = 252;
    s32_t max = MODE_MAX_SPEED[g.mode];
    s32_t step = MODE_MAJOR_STEP[g.mode];
    s32_t v;
    int i;

    /* 细刻度 */
    for (i = 0; i <= 24; i ++) {
        s32_t a = 135 + (270 * i) / 24;
        draw_line(surf,
                  pt_x(cx, 116, a), pt_y(cy, 116, a),
                  pt_x(cx, 122, a), pt_y(cy, 122, a),
                  1, emoto_rgb(0x8A, 0x9F, 0xB6, 255), 110,
                  IPGUI_LINE_CAP_ROUND);
    }

    /* 主刻度 + 数字 */
    for (v = 0; v <= max; v += step) {
        s32_t a = 135 + (270 * v) / max;
        char buf[8];

        draw_line(surf,
                  pt_x(cx, 113, a), pt_y(cy, 113, a),
                  pt_x(cx, 124, a), pt_y(cy, 124, a),
                  2, emoto_rgb(0x9A, 0xAF, 0xC6, 255), 210,
                  IPGUI_LINE_CAP_ROUND);

        snprintf(buf, sizeof(buf), "%d", (int)v);
        draw_text_center(surf, &quicksand_medium_10px, buf,
                         pt_x(cx, 151, a), pt_y(cy, 151, a) - 6,
                         emoto_rgb(0x9A, 0xAF, 0xC6, 255), 220);
    }
}

static void bg_render(ipgui_widget_t * widget, ipgui_widget_render_ctx_t * ctx)
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
    box.end.x   = EMOTO_W - 1;
    box.end.y   = EMOTO_H - 1;

    memset(&bs, 0, sizeof(bs));

    ipgui_liner_gradient_init_direct(&grad, 0, 0, 0, EMOTO_H - 1);
    s0.pos = 0;   s0.color = emoto_rgb(0x05, 0x08, 0x0D, 255);
    s1.pos = 255; s1.color = emoto_rgb(0x0A, 0x12, 0x1D, 255);
    ipgui_liner_gradient_add_stop(&grad, &s0);
    ipgui_liner_gradient_add_stop(&grad, &s1);

    memset(&bg, 0, sizeof(bg));
    bg.paint.type = IPGUI_PAINT_GRADIENT;
    bg.paint.src.grad_src.grad_type = IPGUI_GRADIENT_TYPE_LINEAR;
    bg.paint.src.grad_src.grad.liner_grad = grad;
    bg.opacity = 255;
    bg.blend_mode = IPGUI_BLEND_NORMAL;
    ipgui_draw_box_background(surf, (ipgui_aabb_t *)0, &box, &bs, &bg);

    /* 座舱氛围光 */
    radial_glow(surf, 400, 252, 270, accent_color(255), 36);
    radial_glow(surf, 400, 500, 240, emoto_rgb(0x2B, 0x4E, 0xFF, 255), 18);

    /* 外围 HUD 环 */
    draw_arc(surf, 400, 252, 196, 195, 0, 360, accent_color(255), 18,
             IPGUI_ARC_ENDPOINT_TYPE_BUTT);
    draw_arc(surf, 400, 252, 220, 219, 0, 360, accent_color(255), 10,
             IPGUI_ARC_ENDPOINT_TYPE_BUTT);

    for (i = 0; i < 4; i ++) {
        s32_t a = 45 + i * 90;
        draw_circle(surf, pt_x(400, 220, a), pt_y(252, 220, a), 2,
                    accent_color(255), 45);
    }

    /* 仪表底环 */
    draw_arc(surf, 400, 252, 150, 149, 135, 270, accent_color(255), 18,
             IPGUI_ARC_ENDPOINT_TYPE_BUTT);
    draw_arc(surf, 400, 252, 138, 126, 135, 270,
             emoto_rgb(0x18, 0x26, 0x36, 255), 255,
             IPGUI_ARC_ENDPOINT_TYPE_BUTT);
    draw_arc(surf, 400, 252, 124, 123, 135, 270,
             emoto_rgb(0x24, 0x38, 0x4E, 255), 160,
             IPGUI_ARC_ENDPOINT_TYPE_BUTT);

    draw_gauge_ticks(surf);

    /* 顶栏 / 底栏分隔线 */
    draw_line(surf, 0, 57, EMOTO_W - 1, 57, 1,
              emoto_rgb(0x22, 0x32, 0x46, 255), 120, IPGUI_LINE_CAP_BUTT);
    draw_line(surf, 24, 456, EMOTO_W - 25, 456, 1,
              emoto_rgb(0x22, 0x32, 0x46, 255), 90, IPGUI_LINE_CAP_BUTT);
}

/*=============================================================================
 * 顶栏
 *===========================================================================*/

static void top_render(ipgui_widget_t * widget, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_surf_t * surf = ctx->surf;
    u32_t minutes = 9 * 60 + 42 + ipgui_tick_now() / 60000;
    int hour = (int)((minutes / 60) % 24);
    int minute = (int)(minutes % 60);
    char time_buf[16];

    (void)widget;

    snprintf(time_buf, sizeof(time_buf), "%02d:%02d", hour, minute);

    draw_text(surf, &quicksand_medium_24px, time_buf,
              22, 6, emoto_rgb(0xE6, 0xF0, 0xFB, 255), 255);
    draw_text(surf, &open_sans_10px, "MON 02 NOV",
              23, 34, emoto_rgb(0x9A, 0xAF, 0xC6, 255), 220);

    icon_bluetooth(surf, 350, 14, accent_color(255), 235);
    icon_gps(surf, 376, 13, accent_color(255), 235);
    icon_signal(surf, 404, 13, emoto_rgb(0x9A, 0xAF, 0xC6, 255), 220);
    draw_text(surf, &open_sans_9px, "LTE", 426, 17,
              emoto_rgb(0x8A, 0x9F, 0xB6, 255), 255);

    icon_sun(surf, 612, 13, emoto_rgb(0xFF, 0xB8, 0x4D, 255), 235);
    draw_text(surf, &quicksand_medium_14px, "24C", 634, 13,
              emoto_rgb(0xD8, 0xE4, 0xF2, 255), 240);

    icon_headlight(surf, 692, 13,
                   g.high_beam ? emoto_rgb(0x5B, 0x8C, 0xFF, 255)
                               : emoto_rgb(0x8A, 0x9F, 0xB6, 255),
                   g.high_beam ? 255 : 80);
}

/*=============================================================================
 * 指示灯
 *===========================================================================*/

static void tell_render(ipgui_widget_t * widget, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_surf_t * surf = ctx->surf;
    int on;

    (void)widget;

    /* 左转向 */
    on = g.left_on && g.blink;
    icon_turn(surf, 10, 3, -1,
              on ? emoto_rgb(0xFF, 0xB8, 0x4D, 255)
                 : emoto_rgb(0x8A, 0x9F, 0xB6, 255),
              on ? 255 : 50);

    /* 远光 */
    icon_headlight(surf, 66, 3,
                   g.high_beam ? emoto_rgb(0x5B, 0x8C, 0xFF, 255)
                               : emoto_rgb(0x8A, 0x9F, 0xB6, 255),
                   g.high_beam ? 255 : 50);

    /* ABS */
    draw_text(surf, &open_sans_9px, "ABS", 116, 7,
              g.abs_on ? emoto_rgb(0xFF, 0xB8, 0x4D, 255)
                       : emoto_rgb(0x8A, 0x9F, 0xB6, 255),
              g.abs_on ? 220 : 50);

    /* TC */
    draw_text(surf, &open_sans_9px, "TC", 174, 7,
              g.tc_on ? emoto_rgb(0x6E, 0xF2, 0xA0, 255)
                      : emoto_rgb(0x8A, 0x9F, 0xB6, 255),
              g.tc_on ? 220 : 50);

    /* 充电 */
    icon_bolt(surf, 229, 3,
              g.charging ? emoto_rgb(0x6E, 0xF2, 0xA0, 255)
                         : emoto_rgb(0x8A, 0x9F, 0xB6, 255),
              g.charging ? 230 : 45);

    /* READY */
    draw_circle(surf, 288, 11, 2,
                emoto_rgb(0x6E, 0xF2, 0xA0, 255), 230);
    draw_text(surf, &open_sans_9px, "READY", 294, 7,
              emoto_rgb(0x6E, 0xF2, 0xA0, 255), 200);
}

/*=============================================================================
 * 中央仪表
 *===========================================================================*/

static void arc_gradient(
    ipgui_surf_t * surf, ipgui_coord_t cx, ipgui_coord_t cy,
    ipgui_coord_t er, ipgui_coord_t ir,
    s32_t start, s32_t angle, u8_t opacity)
{
    ipgui_liner_gradient_color_t grad;
    ipgui_gradient_color_stop_t s0, s1, s2;
    ipgui_arc_t arc;
    ipgui_arc_style_t style;

    if (angle <= 0 || er <= ir) return;

    ipgui_liner_gradient_init_direct(&grad, 0, 0, 336, 0);

    if (g.mode == EMOTO_MODE_ECO) {
        s0.color = emoto_rgb(0x9B, 0xE1, 0x5D, 255);
        s1.color = emoto_rgb(0x35, 0xE0, 0xFF, 255);
        s2.color = emoto_rgb(0x35, 0xE0, 0xFF, 255);
    } else if (g.mode == EMOTO_MODE_RAIN) {
        s0.color = emoto_rgb(0x5B, 0x8C, 0xFF, 255);
        s1.color = emoto_rgb(0x7A, 0x78, 0xFF, 255);
        s2.color = emoto_rgb(0xB4, 0x78, 0xFF, 255);
    } else {
        s0.color = emoto_rgb(0x35, 0xE0, 0xFF, 255);
        s1.color = emoto_rgb(0x50, 0x8C, 0xFF, 255);
        s2.color = emoto_rgb(0x8C, 0x5C, 0xFF, 255);
    }
    s0.pos = 0;
    s1.pos = 128;
    s2.pos = 255;
    ipgui_liner_gradient_add_stop(&grad, &s0);
    ipgui_liner_gradient_add_stop(&grad, &s1);
    ipgui_liner_gradient_add_stop(&grad, &s2);

    arc.cx    = cx;
    arc.cy    = cy;
    arc.er    = er;
    arc.ir    = ir;
    arc.start = start;
    arc.dir   = IPGUI_ARC_DRAW_DIR_CCW;
    arc.angle = (u16_t)angle;

    memset(&style, 0, sizeof(style));
    style.paint.type = IPGUI_PAINT_GRADIENT;
    style.paint.src.grad_src.grad_type = IPGUI_GRADIENT_TYPE_LINEAR;
    style.paint.src.grad_src.grad.liner_grad = grad;
    style.opacity = opacity;
    style.blend_mode = IPGUI_BLEND_NORMAL;
    style.sep_type = IPGUI_ARC_ENDPOINT_TYPE_ROUND;
    style.eep_type = IPGUI_ARC_ENDPOINT_TYPE_ROUND;

    ipgui_draw_arc(surf, (ipgui_aabb_t *)0, &arc, &style);
}

static void center_render(ipgui_widget_t * widget, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_surf_t * surf = ctx->surf;
    ipgui_coord_t cx = 168;
    ipgui_coord_t cy = 166;
    s32_t max = MODE_MAX_SPEED[g.mode];
    s32_t speed = clamp_s32(g.speed, 0, max);
    s32_t angle = (270 * speed) / max;
    s32_t end_a = 135 + angle;
    char buf[32];
    int p;

    (void)widget;

    /* 进度弧：先画外发光，再画主弧 */
    if (angle > 0) {
        arc_gradient(surf, cx, cy, 144, 120, 135, angle, 42);
        arc_gradient(surf, cx, cy, 138, 126, 135, angle, 255);
    }

    /* 起点 */
    draw_circle(surf, pt_x(cx, 132, 135), pt_y(cy, 132, 135), 3,
                accent_color(255), 110);

    /* 终点光点 */
    if (angle > 0) {
        ipgui_color_t end_color = progress_color(angle * 255 / 270);
        draw_circle(surf, pt_x(cx, 132, end_a), pt_y(cy, 132, end_a), 11,
                    end_color, 55);
        draw_circle(surf, pt_x(cx, 132, end_a), pt_y(cy, 132, end_a), 6,
                    end_color, 255);
        draw_circle(surf, pt_x(cx, 132, end_a), pt_y(cy, 132, end_a), 2,
                    emoto_rgb(255, 255, 255, 255), 255);
    }

    /* 模式胶囊 */
    fill_round(surf, cx - 42, 48, 84, 22, 11, accent_color(255), 22);
    stroke_round(surf, cx - 42, 48, 84, 22, 11, 1, accent_color(255), 100);
    draw_text_center(surf, &quicksand_medium_10px, MODE_NAME[g.mode],
                     cx, 53, accent_color(255), 255);

    /* 速度 */
    snprintf(buf, sizeof(buf), "%d", (int)speed);
    {
        ipgui_coord_t w = text_w(&quicksand_medium_50px, buf);
        draw_text_gradient_shadowed(surf, &quicksand_medium_50px, buf,
                                    cx - w / 2, 76,
                                    emoto_rgb(0xFF, 0xFF, 0xFF, 255),
                                    emoto_rgb(0xC8, 0xEC, 0xFF, 255),
                                    accent_color(255), 70, 10);
    }
    draw_text_center(surf, &open_sans_13px, "km/h", cx, 132,
                     emoto_rgb(0x9A, 0xAF, 0xC6, 255), 240);

    draw_line(surf, cx - 44, 154, cx + 44, 154, 1,
              emoto_rgb(0x2A, 0x3D, 0x52, 255), 160, IPGUI_LINE_CAP_BUTT);

    /* 功率 / 回馈 */
    draw_text_center(surf, &open_sans_9px, "POWER", cx, 160,
                     emoto_rgb(0x8A, 0x9F, 0xB6, 255), 255);

    fill_round(surf, cx - 95, 170, 190, 6, 3,
               emoto_rgb(0x14, 0x1E, 0x2B, 255), 255);

    p = clamp_s32(g.power_x10, -30, 90);
    if (p >= 0) {
        int fw = (p * 95) / 90;
        if (fw > 0) {
            if (fw < 6) fw = 6;
            fill_round(surf, cx, 170, fw, 6, 3, accent_color(255), 255);
        }
    } else {
        int fw = (-p * 95) / 30;
        if (fw > 0) {
            if (fw < 6) fw = 6;
            fill_round(surf, cx - fw, 170, fw, 6, 3,
                       emoto_rgb(0x6E, 0xF2, 0xA0, 255), 255);
        }
    }
    draw_line(surf, cx, 168, cx, 177, 1,
              emoto_rgb(0x8A, 0x9F, 0xB6, 255), 180, IPGUI_LINE_CAP_BUTT);

    draw_text(surf, &open_sans_9px, "REGEN", cx - 95, 180,
              emoto_rgb(0x8A, 0x9F, 0xB6, 255), 255);
    draw_text_right(surf, &open_sans_9px, "POWER", cx + 95, 180,
                    emoto_rgb(0x8A, 0x9F, 0xB6, 255), 255);

    p = g.power_x10;
    snprintf(buf, sizeof(buf), "%s%d.%d kW", (p < 0) ? "-" : "",
             (int)(iabs(p) / 10), (int)(iabs(p) % 10));
    draw_text_center(surf, &quicksand_medium_16px, buf, cx, 194,
                     accent_color(255), 255);

    if (g.mode == EMOTO_MODE_ECO) {
        draw_text_center(surf, &open_sans_9px, "ECO RANGE +12%", cx, 220,
                         emoto_rgb(0x8A, 0x9F, 0xB6, 255), 255);
    } else if (g.mode == EMOTO_MODE_RAIN) {
        draw_text_center(surf, &open_sans_9px, "WET  TORQUE LIMITED", cx, 220,
                         emoto_rgb(0x8A, 0x9F, 0xB6, 255), 255);
    } else {
        draw_text_center(surf, &open_sans_9px, "0-50 KM/H  3.2 S", cx, 220,
                         emoto_rgb(0x8A, 0x9F, 0xB6, 255), 255);
    }

    snprintf(buf, sizeof(buf), "RANGE %d KM", (int)g.range_km);
    draw_text(surf, &open_sans_9px, buf, cx - 95, 240,
              emoto_rgb(0x8A, 0x9F, 0xB6, 255), 220);
    snprintf(buf, sizeof(buf), "TRIP %d.%d KM",
             (int)(g.trip_x100 / 100), (int)((g.trip_x100 % 100) / 10));
    draw_text_right(surf, &open_sans_9px, buf, cx + 95, 240,
                    emoto_rgb(0x8A, 0x9F, 0xB6, 255), 220);
}

/*=============================================================================
 * 电池卡
 *===========================================================================*/

static void batt_render(ipgui_widget_t * widget, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_surf_t * surf = ctx->surf;
    char buf[32];
    int bar_w;
    int pulse = 0;

    if (g.charging) {
        pulse = 12 + (int)((ipgui_tick_now() / 40) % 12);
    }

    draw_panel(surf, 0, 0, 196, 150, 18,
               emoto_rgb(0x16, 0x22, 0x32, 255),
               emoto_rgb(0x0D, 0x16, 0x22, 255),
               emoto_rgb(0x22, 0x32, 0x46, 255), 190);

    draw_text(surf, &open_sans_10px, "BATTERY", 16, 12,
              emoto_rgb(0x9A, 0xAF, 0xC6, 255), 230);
    icon_battery(surf, 152, 12, 26, 12, g.soc,
                 g.soc < 20 ? emoto_rgb(0xFF, 0x5C, 0x72, 255)
                            : emoto_rgb(0x6E, 0xF2, 0xA0, 255), 220);

    snprintf(buf, sizeof(buf), "%d", (int)g.soc);
    draw_text(surf, &quicksand_medium_34px, buf, 16, 28,
              emoto_rgb(0xF2, 0xF7, 0xFF, 255), 255);
    draw_text(surf, &quicksand_medium_14px, "%",
              18 + text_w(&quicksand_medium_34px, buf), 48,
              emoto_rgb(0x9A, 0xAF, 0xC6, 255), 255);

    if (g.charging) {
        icon_bolt(surf, 116, 34, emoto_rgb(0x6E, 0xF2, 0xA0, 255),
                  150 + pulse);
        draw_text(surf, &open_sans_9px, "CHARGING", 16, 70,
                  emoto_rgb(0x6E, 0xF2, 0xA0, 255), 220);
    } else {
        snprintf(buf, sizeof(buf), "RANGE %d KM", (int)g.range_km);
        draw_text(surf, &open_sans_10px, buf, 16, 70,
                  emoto_rgb(0x9A, 0xAF, 0xC6, 255), 230);
    }

    fill_round(surf, 16, 90, 164, 8, 4,
               emoto_rgb(0x18, 0x24, 0x33, 255), 255);
    bar_w = 164 * clamp_s32(g.soc, 0, 100) / 100;
    if (bar_w > 0) {
        fill_round(surf, 16, 90, bar_w, 8, 4,
                   g.soc < 20 ? emoto_rgb(0xFF, 0x5C, 0x72, 255)
                              : emoto_rgb(0x6E, 0xF2, 0xA0, 255),
                   (u8_t)(220 + pulse));
    }

    draw_text(surf, &open_sans_9px, "TEMP", 16, 110,
              emoto_rgb(0x8A, 0x9F, 0xB6, 255), 255);
    snprintf(buf, sizeof(buf), "%dC", (int)(28 + g.speed / 18));
    draw_text(surf, &quicksand_medium_12px, buf, 16, 121,
              emoto_rgb(0xD8, 0xE4, 0xF2, 255), 240);

    draw_text(surf, &open_sans_9px, "VOLT", 104, 110,
              emoto_rgb(0x8A, 0x9F, 0xB6, 255), 255);
    snprintf(buf, sizeof(buf), "%d.%dV", (int)(g.volt_x10 / 10),
             (int)(g.volt_x10 % 10));
    draw_text(surf, &quicksand_medium_12px, buf, 104, 121,
              emoto_rgb(0xD8, 0xE4, 0xF2, 255), 240);

    draw_press_overlay(surf, widget, 196, 150, 18);
}

/*=============================================================================
 * 电机卡
 *===========================================================================*/

static void motor_cell(
    ipgui_surf_t * surf, int x, int y,
    const char * label, const char * value)
{
    draw_text(surf, &open_sans_9px, label, x, y,
              emoto_rgb(0x8A, 0x9F, 0xB6, 255), 255);
    draw_text(surf, &quicksand_medium_20px, value, x, y + 11,
              emoto_rgb(0xD8, 0xE4, 0xF2, 255), 245);
}

static void motor_render(ipgui_widget_t * widget, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_surf_t * surf = ctx->surf;
    char buf[32];

    draw_panel(surf, 0, 0, 196, 150, 18,
               emoto_rgb(0x16, 0x22, 0x32, 255),
               emoto_rgb(0x0D, 0x16, 0x22, 255),
               emoto_rgb(0x22, 0x32, 0x46, 255), 190);

    draw_text(surf, &open_sans_10px, "MOTOR", 16, 12,
              emoto_rgb(0x9A, 0xAF, 0xC6, 255), 230);
    icon_motor(surf, 164, 8, accent_color(255), 220);

    snprintf(buf, sizeof(buf), "%s%d.%d kW", (g.power_x10 < 0) ? "-" : "",
             (int)(iabs(g.power_x10) / 10), (int)(iabs(g.power_x10) % 10));
    motor_cell(surf, 16, 34, "POWER", buf);

    snprintf(buf, sizeof(buf), "%d.%d Nm", (int)(g.torque_x10 / 10),
             (int)(g.torque_x10 % 10));
    motor_cell(surf, 104, 34, "TORQUE", buf);

    draw_line(surf, 16, 78, 180, 78, 1,
              emoto_rgb(0x22, 0x32, 0x46, 255), 120, IPGUI_LINE_CAP_BUTT);

    snprintf(buf, sizeof(buf), "%d%%", (int)g.eff);
    motor_cell(surf, 16, 84, "EFFICIENCY", buf);

    snprintf(buf, sizeof(buf), "%dC", (int)g.motor_temp);
    motor_cell(surf, 104, 84, "TEMP", buf);

    draw_text(surf, &open_sans_9px, "PMAC  48V", 16, 132,
              emoto_rgb(0x8A, 0x9F, 0xB6, 255), 220);

    draw_press_overlay(surf, widget, 196, 150, 18);
}

/*=============================================================================
 * 行程卡
 *===========================================================================*/

static void trip_big_text(
    ipgui_surf_t * surf, const char * value, const char * unit)
{
    ipgui_coord_t w;

    draw_text(surf, &quicksand_medium_30px, value, 16, 32,
              emoto_rgb(0xF2, 0xF7, 0xFF, 255), 255);
    w = text_w(&quicksand_medium_30px, value);
    draw_text(surf, &quicksand_medium_12px, unit, 22 + w, 48,
              emoto_rgb(0x9A, 0xAF, 0xC6, 255), 240);
}

static void trip_row(
    ipgui_surf_t * surf, int y, const char * label, const char * value)
{
    draw_text(surf, &open_sans_9px, label, 16, y,
              emoto_rgb(0x8A, 0x9F, 0xB6, 255), 255);
    draw_text_right(surf, &quicksand_medium_12px, value, 180, y - 2,
                    emoto_rgb(0xD8, 0xE4, 0xF2, 255), 240);
}

static void trip_render(ipgui_widget_t * widget, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_surf_t * surf = ctx->surf;
    char buf[32];
    s32_t value;
    const char * unit = "km";

    draw_panel(surf, 0, 0, 196, 170, 18,
               emoto_rgb(0x16, 0x22, 0x32, 255),
               emoto_rgb(0x0D, 0x16, 0x22, 255),
               emoto_rgb(0x22, 0x32, 0x46, 255), 190);

    draw_text(surf, &open_sans_10px, "TRIP", 16, 12,
              emoto_rgb(0x9A, 0xAF, 0xC6, 255), 230);

    fill_round(surf, 148, 10, g.trip_view == 2 ? 36 : 32, 16, 8,
               accent_color(255), 22);
    draw_text_center(surf, &open_sans_9px,
                     g.trip_view == 0 ? "A" :
                     g.trip_view == 1 ? "B" : "ODO",
                     g.trip_view == 2 ? 166 : 164, 14,
                     accent_color(255), 240);

    if (g.trip_view == 0) {
        value = g.trip_x100;
        snprintf(buf, sizeof(buf), "%d.%d", (int)(value / 100),
                 (int)((value % 100) / 10));
        trip_big_text(surf, buf, unit);
        snprintf(buf, sizeof(buf), "%d km/h", (int)g.avg_speed);
        trip_row(surf, 88, "AVG SPEED", buf);
        snprintf(buf, sizeof(buf), "%dh %02dm", (int)(g.ride_ms / 3600000),
                 (int)((g.ride_ms / 60000) % 60));
        trip_row(surf, 110, "RIDE TIME", buf);
        snprintf(buf, sizeof(buf), "%d km/h", (int)g.max_seen);
        trip_row(surf, 132, "MAX SPEED", buf);
    } else if (g.trip_view == 1) {
        value = g.trip_b_x100 + g.trip_x100 / 3;
        snprintf(buf, sizeof(buf), "%d.%d", (int)(value / 100),
                 (int)((value % 100) / 10));
        trip_big_text(surf, buf, unit);
        snprintf(buf, sizeof(buf), "%d km/h", (int)(g.avg_speed * 9 / 10));
        trip_row(surf, 88, "AVG SPEED", buf);
        snprintf(buf, sizeof(buf), "%dh %02dm", (int)(g.ride_ms / 3600000 / 2),
                 (int)((g.ride_ms / 60000 / 2) % 60));
        trip_row(surf, 110, "RIDE TIME", buf);
        snprintf(buf, sizeof(buf), "%d km/h", (int)(g.max_seen * 9 / 10));
        trip_row(surf, 132, "MAX SPEED", buf);
    } else {
        value = g.odo_x100;
        snprintf(buf, sizeof(buf), "%d", (int)(value / 100));
        trip_big_text(surf, buf, unit);
        snprintf(buf, sizeof(buf), "%d.%d", (int)(g.trip_x100 / 100),
                 (int)((g.trip_x100 % 100) / 10));
        trip_row(surf, 88, "TRIP A", buf);
        snprintf(buf, sizeof(buf), "%d.%d",
                 (int)((g.trip_b_x100 + g.trip_x100 / 3) / 100),
                 (int)(((g.trip_b_x100 + g.trip_x100 / 3) % 100) / 10));
        trip_row(surf, 110, "TRIP B", buf);
        snprintf(buf, sizeof(buf), "%dh", (int)(g.ride_ms / 3600000));
        trip_row(surf, 132, "TOTAL TIME", buf);
    }

    draw_press_overlay(surf, widget, 196, 170, 18);
}

/*=============================================================================
 * 导航 / 胎压卡
 *===========================================================================*/

static void nav_map(ipgui_surf_t * surf)
{
    int i;

    fill_round(surf, 12, 32, 172, 68, 12,
               emoto_rgb(0x08, 0x10, 0x19, 255), 255);
    stroke_round(surf, 12, 32, 172, 68, 12, 1,
                 emoto_rgb(0x22, 0x32, 0x46, 255), 150);

    for (i = 1; i < 4; i ++) {
        draw_line(surf, 12 + i * 43, 34, 12 + i * 43, 97, 1,
                  emoto_rgb(0x22, 0x32, 0x46, 255), 70,
                  IPGUI_LINE_CAP_BUTT);
    }
    for (i = 1; i < 3; i ++) {
        draw_line(surf, 14, 32 + i * 22, 181, 32 + i * 22, 1,
                  emoto_rgb(0x22, 0x32, 0x46, 255), 70,
                  IPGUI_LINE_CAP_BUTT);
    }

    /* 路线 */
    draw_line(surf, 28, 88, 58, 70, 3, accent_color(255), 190,
              IPGUI_LINE_CAP_ROUND);
    draw_line(surf, 58, 70, 90, 78, 3, accent_color(255), 220,
              IPGUI_LINE_CAP_ROUND);
    draw_line(surf, 90, 78, 120, 52, 3, accent_color(255), 240,
              IPGUI_LINE_CAP_ROUND);
    draw_line(surf, 120, 52, 156, 44, 3, accent_color(255), 255,
              IPGUI_LINE_CAP_ROUND);

    draw_circle(surf, 28, 88, 3, emoto_rgb(0x6E, 0xF2, 0xA0, 255), 255);
    draw_circle(surf, 156, 44, 5, accent_color(255), 255);
    draw_circle(surf, 156, 44, 2, emoto_rgb(0xFF, 0xFF, 0xFF, 255), 255);
}

static void nav_render(ipgui_widget_t * widget, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_surf_t * surf = ctx->surf;
    char buf[32];

    draw_panel(surf, 0, 0, 196, 170, 18,
               emoto_rgb(0x16, 0x22, 0x32, 255),
               emoto_rgb(0x0D, 0x16, 0x22, 255),
               emoto_rgb(0x22, 0x32, 0x46, 255), 190);

    draw_text(surf, &open_sans_10px,
              g.nav_view == 0 ? "NAVIGATION" : "TIRE PRESSURE",
              16, 12, emoto_rgb(0x9A, 0xAF, 0xC6, 255), 230);
    icon_chevron(surf, 176, 10, emoto_rgb(0x8A, 0x9F, 0xB6, 255), 180);

    if (g.nav_view == 0) {
        u32_t minutes = 9 * 60 + 42 + 18 + ipgui_tick_now() / 60000;
        nav_map(surf);
        draw_text(surf, &open_sans_9px, "HOME", 16, 108,
                  emoto_rgb(0x8A, 0x9F, 0xB6, 255), 255);
        draw_text(surf, &quicksand_medium_20px, "8.4 km", 16, 118,
                  emoto_rgb(0xF2, 0xF7, 0xFF, 255), 255);
        draw_text(surf, &quicksand_medium_12px, "18 min", 116, 123,
                  accent_color(255), 240);
        snprintf(buf, sizeof(buf), "ARRIVE %02d:%02d",
                 (int)((minutes / 60) % 24), (int)(minutes % 60));
        draw_text(surf, &open_sans_9px, buf, 16, 148,
                  emoto_rgb(0x8A, 0x9F, 0xB6, 255), 220);
    } else {
        icon_tire(surf, 16, 36, accent_color(255), 230);
        draw_text(surf, &open_sans_9px, "FRONT", 38, 36,
                  emoto_rgb(0x8A, 0x9F, 0xB6, 255), 255);
        draw_text(surf, &quicksand_medium_20px, "2.3 bar", 38, 48,
                  emoto_rgb(0xF2, 0xF7, 0xFF, 255), 255);
        draw_text(surf, &open_sans_9px, "OK", 162, 52,
                  emoto_rgb(0x6E, 0xF2, 0xA0, 255), 230);

        icon_tire(surf, 16, 84, accent_color(255), 230);
        draw_text(surf, &open_sans_9px, "REAR", 38, 84,
                  emoto_rgb(0x8A, 0x9F, 0xB6, 255), 255);
        draw_text(surf, &quicksand_medium_20px, "2.5 bar", 38, 96,
                  emoto_rgb(0xF2, 0xF7, 0xFF, 255), 255);
        draw_text(surf, &open_sans_9px, "OK", 162, 100,
                  emoto_rgb(0x6E, 0xF2, 0xA0, 255), 230);

        fill_round(surf, 16, 126, 38, 16, 8,
                   emoto_rgb(0xFF, 0xB8, 0x4D, 255), 18);
        draw_text_center(surf, &open_sans_9px, "ABS", 35, 130,
                         emoto_rgb(0xFF, 0xB8, 0x4D, 255), 220);
        fill_round(surf, 60, 126, 32, 16, 8,
                   emoto_rgb(0x6E, 0xF2, 0xA0, 255), 18);
        draw_text_center(surf, &open_sans_9px, "TC", 76, 130,
                         emoto_rgb(0x6E, 0xF2, 0xA0, 255), 220);

        draw_text(surf, &open_sans_9px, "NEXT SERVICE 1,200 KM", 16, 150,
                  emoto_rgb(0x8A, 0x9F, 0xB6, 255), 220);
    }

    draw_press_overlay(surf, widget, 196, 170, 18);
}

/*=============================================================================
 * 模式切换
 *===========================================================================*/

static void tabs_render(ipgui_widget_t * widget, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_surf_t * surf = ctx->surf;
    int i;
    int indicator = (int)((g.tab_x + 8) >> 4);

    draw_panel(surf, 0, 0, 300, 64, 28,
               emoto_rgb(0x10, 0x18, 0x24, 255),
               emoto_rgb(0x0A, 0x10, 0x19, 255),
               emoto_rgb(0x22, 0x32, 0x46, 255), 180);

    fill_round(surf, indicator, 8, 88, 48, 24, accent_color(255), 20);
    stroke_round(surf, indicator, 8, 88, 48, 24, 1, accent_color(255), 95);

    for (i = 0; i < 3; i ++) {
        int x = 10 + i * 96;
        draw_text_center(surf, &quicksand_medium_12px, MODE_NAME[i],
                         x + 44, 24,
                         (i == g.mode) ? accent_color(255)
                                       : emoto_rgb(0x9A, 0xAF, 0xC6, 255),
                         (i == g.mode) ? 255 : 190);
    }

    draw_press_overlay(surf, widget, 300, 64, 28);
}

/* 顶栏 NAV 入口：点击进入导航页，带幕布转场 */
static void navbtn_render(ipgui_widget_t * widget, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_surf_t * surf = ctx->surf;

    fill_round(surf, 0, 0, 62, 32, 16, accent_color(255), 20);
    stroke_round(surf, 0, 0, 62, 32, 16, 1, accent_color(255), 110);

    draw_text(surf, &open_sans_9px, "NAV", 10, 11,
              accent_color(255), 245);
    draw_line(surf, 44, 11, 49, 16, 1, accent_color(255), 220,
              IPGUI_LINE_CAP_ROUND);
    draw_line(surf, 49, 16, 44, 21, 1, accent_color(255), 220,
              IPGUI_LINE_CAP_ROUND);

    draw_press_overlay(surf, widget, 62, 32, 16);
}

/*=============================================================================
 * 启动动画
 *===========================================================================*/

static void boot_render(ipgui_widget_t * widget, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_surf_t * surf = ctx->surf;
    u32_t elapsed = ipgui_tick_now() - g.boot_start;
    u8_t alpha;
    int line_w;

    (void)widget;

    if (elapsed <= EMOTO_BOOT_MS) {
        alpha = 255;
    } else if (elapsed >= EMOTO_BOOT_MS + EMOTO_BOOT_FADE_MS) {
        alpha = 0;
    } else {
        alpha = (u8_t)(255 - (elapsed - EMOTO_BOOT_MS) * 255 / EMOTO_BOOT_FADE_MS);
    }

    fill_round(surf, 0, 0, EMOTO_W, EMOTO_H, 0,
               emoto_rgb(0x04, 0x07, 0x0B, 255), alpha);

    if (alpha < 8) return;

    draw_text_center(surf, &quicksand_medium_24px, "E-MOTION", 400, 198,
                     accent_color(255), alpha);
    draw_text_center(surf, &open_sans_10px, "ELECTRIC  PERFORMANCE", 400, 230,
                     emoto_rgb(0x8A, 0x9F, 0xB6, 255), alpha);

    draw_arc(surf, 400, 268, 20, 18, (s32_t)((ipgui_tick_now() / 3) % 360),
             90, accent_color(255), alpha, IPGUI_ARC_ENDPOINT_TYPE_ROUND);
    draw_arc(surf, 400, 268, 20, 19, 0, 360,
             emoto_rgb(0x22, 0x32, 0x46, 255), (u8_t)(alpha / 2),
             IPGUI_ARC_ENDPOINT_TYPE_BUTT);
    {
        s32_t a = (s32_t)(((ipgui_tick_now() / 3) % 360) + 90);
        draw_circle(surf, pt_x(400, 19, a), pt_y(268, 19, a), 2,
                    accent_color(255), alpha);
    }

    line_w = (int)(elapsed < EMOTO_BOOT_MS ? elapsed : EMOTO_BOOT_MS);
    line_w = line_w * 160 / EMOTO_BOOT_MS;
    fill_round(surf, 320, 300, 160, 2, 1,
               emoto_rgb(0x14, 0x1E, 0x2B, 255), alpha);
    if (line_w > 0) {
        fill_round(surf, 320, 300, line_w, 2, 1, accent_color(255), alpha);
    }

    draw_text_center(surf, &open_sans_9px, "SYSTEM CHECK", 400, 312,
                     emoto_rgb(0x8A, 0x9F, 0xB6, 255), alpha);
}

/*=============================================================================
 * 交互
 *===========================================================================*/

static void emoto_set_mode(u8_t mode)
{
    if (mode > EMOTO_MODE_RAIN || mode == g.mode) return;

    g.mode = mode;
    g.tab_target_x = (10 + mode * 96) * 16;
    g.acc_tr = MODE_ACCENT[mode].r;
    g.acc_tg = MODE_ACCENT[mode].g;
    g.acc_tb = MODE_ACCENT[mode].b;

    if (g.speed_target > MODE_MAX_SPEED[mode]) {
        g.speed_target = MODE_MAX_SPEED[mode];
    }
    if (g.speed > MODE_MAX_SPEED[mode]) {
        g.speed = MODE_MAX_SPEED[mode];
    }

    if (g.w_bg)     ipgui_widget_mark_dirty(g.w_bg);
    if (g.w_center) ipgui_widget_mark_dirty(g.w_center);
    if (g.w_tabs)   ipgui_widget_mark_dirty(g.w_tabs);
}

static void emoto_event(ipgui_widget_t * widget, ipgui_widget_evt_t * evt)
{
    ipgui_coord_t x;

    if (!widget || !evt) return;
    if (ipgui_emoto_transition_active()) return;

    if (evt->type == IPGUI_WIDGET_EVENT_PRESSED) {
        g.pressed = widget;
        ipgui_widget_mark_dirty(widget);
        return;
    }

    if (evt->type != IPGUI_WIDGET_EVENT_RELEASED) return;

    x = evt->evt.released_evt.x;

    if (widget == g.w_navbtn) {
        g.pressed = (ipgui_widget_t *)0;
        ipgui_widget_mark_dirty(widget);
        ipgui_emoto_transition_to(IPGUI_EMOTO_PAGE_NAV);
        return;
    }

    if (widget == g.w_tabs) {
        if (x < 100)      emoto_set_mode(EMOTO_MODE_ECO);
        else if (x < 196) emoto_set_mode(EMOTO_MODE_SPORT);
        else              emoto_set_mode(EMOTO_MODE_RAIN);
    } else if (widget == g.w_batt) {
        g.charging = !g.charging;
        g.soc_ms = 0;
        if (g.charging) g.speed_target = 0;
        ipgui_widget_mark_dirty(widget);
        if (g.w_tell) ipgui_widget_mark_dirty(g.w_tell);
    } else if (widget == g.w_trip) {
        g.trip_view = (u8_t)((g.trip_view + 1) % 3);
        ipgui_widget_mark_dirty(widget);
    } else if (widget == g.w_nav) {
        g.nav_view = (u8_t)((g.nav_view + 1) % 2);
        ipgui_widget_mark_dirty(widget);
    }

    g.pressed = (ipgui_widget_t *)0;
    ipgui_widget_mark_dirty(widget);
}

/*=============================================================================
 * 页面创建
 *===========================================================================*/

static ipgui_widget_t * make_widget(
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

__IPGUI_API__ void ipgui_page_emoto_init(ipgui_scr_t * scr, u8_t page)
{
    u8_t mode = EMOTO_MODE_SPORT;

    if (!scr || g.inited) return;

    memset(&g, 0, sizeof(g));

    ipgui_screen_page_switch(scr, page);
    g.scr     = scr;
    g.page    = page;
    g.visible = 1;

    ipgui_emoto_transition_setup(scr, page, IPGUI_EMOTO_PAGE_NAV);

    g.mode            = mode;
    g.trip_view       = 0;
    g.nav_view        = 0;
    g.abs_on          = 1;
    g.tc_on           = 1;
    g.speed           = 0;
    g.speed_target    = 0;
    g.soc_target      = 78;
    g.soc_fp          = 78 * 16;
    g.soc             = 78;
    g.range_km        = 97;
    g.power_x10       = 0;
    g.torque_x10      = 20;
    g.eff             = 92;
    g.motor_temp      = 36;
    g.volt_x10        = 722;
    g.trip_x100       = 4260;
    g.trip_b_x100     = 1840;
    g.odo_x100        = 128400;
    g.ride_ms         = 6120000;
    g.avg_speed       = 25;
    g.max_seen        = 96;
    g.acc_r           = MODE_ACCENT[mode].r;
    g.acc_g           = MODE_ACCENT[mode].g;
    g.acc_b           = MODE_ACCENT[mode].b;
    g.acc_tr          = g.acc_r;
    g.acc_tg          = g.acc_g;
    g.acc_tb          = g.acc_b;
    g.tab_x           = (10 + mode * 96) * 16;
    g.tab_target_x    = g.tab_x;
    g.boot_start      = ipgui_tick_now();
    g.last_tick       = g.boot_start;

    g.w_bg     = make_widget(0, 0, EMOTO_W, EMOTO_H, "emoto_bg",
                             bg_render, emoto_event);
    g.w_top    = make_widget(0, 0, EMOTO_W, 58, "emoto_top",
                             top_render, emoto_event);
    g.w_tell   = make_widget(232, 62, 336, 22, "emoto_tell",
                             tell_render, emoto_event);
    g.w_batt   = make_widget(24, 92, 196, 150, "emoto_batt",
                             batt_render, emoto_event);
    g.w_motor  = make_widget(580, 92, 196, 150, "emoto_motor",
                             motor_render, emoto_event);
    g.w_trip   = make_widget(24, 260, 196, 170, "emoto_trip",
                             trip_render, emoto_event);
    g.w_nav    = make_widget(580, 260, 196, 170, "emoto_nav",
                             nav_render, emoto_event);
    g.w_center = make_widget(232, 86, 336, 290, "emoto_center",
                             center_render, emoto_event);
    g.w_tabs   = make_widget(250, 386, 300, 64, "emoto_tabs",
                             tabs_render, emoto_event);
    g.w_navbtn = make_widget(716, 10, 62, 32, "emoto_navbtn",
                             navbtn_render, emoto_event);
    g.w_boot   = make_widget(0, 0, EMOTO_W, EMOTO_H, "emoto_boot",
                             boot_render, emoto_event);
    g.w_trans  = make_widget(0, 0, EMOTO_W, EMOTO_H, "NAVIGATION",
                             ipgui_emoto_transition_render,
                             (void (*)(struct ipgui_widget *, ipgui_widget_evt_t *))0);
    if (g.w_trans) {
        g.w_trans->flags |= IPGUI_WIDGET_FLAG_INVISIBLE;
        g.w_trans->flags |= IPGUI_WIDGET_FLAG_DISABLED;
        ipgui_emoto_transition_register(page, g.w_trans);
    }

    g.inited = 1;

    ipgui_dirty_rect_add_xywh(&scr->dirty, 0, 0, EMOTO_W, EMOTO_H);
}

/*=============================================================================
 * 每帧状态推进
 *===========================================================================*/

static void emoto_simulate(s32_t dt)
{
    u32_t now = ipgui_tick_now();
    u32_t t;
    s32_t max = MODE_MAX_SPEED[g.mode];
    s32_t target;

    if (g.charging || (now - g.boot_start) < 1200) {
        g.speed_target = 0;
        return;
    }

    t = (now / 1000) % 36;

    if (t < 4) {
        target = 0;
    } else if (t < 11) {
        target = (s32_t)(t - 4) * 14;
    } else if (t < 19) {
        target = 72 + (s32_t)(((s64_t)ipgui_sin((s32_t)((t - 11) * 40)) * 10) >> 15);
    } else if (t < 25) {
        target = 96;
    } else if (t < 32) {
        target = 96 - (s32_t)(t - 25) * 12;
    } else {
        target = 0;
    }

    target += (s32_t)(((s64_t)ipgui_sin((s32_t)((now / 7) % 360)) * 3) >> 15);
    target = clamp_s32(target, 0, max);
    g.speed_target = target;

    (void)dt;
}

static void emoto_update_state(s32_t dt)
{
    u32_t now = ipgui_tick_now();
    s32_t old_speed = g.speed;
    s32_t old_power = g.power_x10;
    s32_t old_soc = g.soc;
    s32_t old_tab = g.tab_x;
    s32_t old_acc_r = g.acc_r;
    s32_t old_acc_g = g.acc_g;
    s32_t old_acc_b = g.acc_b;
    u8_t old_blink = g.blink;

    g.visible = (g.scr && g.scr->act_page == g.page) ? 1 : 0;
    if (!g.visible) g.pressed = (ipgui_widget_t *)0;

    emoto_simulate(dt);

    g.speed = ease_s32(g.speed, g.speed_target, dt, 200);

    /* 功率：速度项 + 加速度项 */
    {
        s32_t p = (g.speed * 10) / 9 + (g.speed_target - g.speed) * 2;
        g.power_x10 = clamp_s32(p, -30, 90);
    }
    g.torque_x10 = clamp_s32(20 + iabs(g.power_x10) / 2, 20, 120);
    g.eff = clamp_s32(86 + g.speed / 8, 86, 97);
    g.motor_temp = clamp_s32(34 + g.speed / 7, 34, 82);
    if (g.speed > g.max_seen) g.max_seen = g.speed;

    /* SOC */
    g.soc_ms += dt;
    if (g.charging) {
        if (g.soc_target < 100 && g.soc_ms >= 2000) {
            g.soc_target ++;
            g.soc_ms = 0;
        }
    } else if (g.speed > 0 && g.soc_target > 0 && g.soc_ms >= 12000) {
        g.soc_target --;
        g.soc_ms = 0;
    }
    g.soc_fp = ease_s32(g.soc_fp, g.soc_target * 16, dt, 500);
    g.soc = (g.soc_fp + 8) >> 4;
    g.range_km = (g.soc * 125) / 100;
    g.volt_x10 = 660 + g.soc * 8 / 10;

    /* 行程 */
    if (g.speed > 0) {
        s32_t add = (g.speed * dt) / 36000;
        g.trip_x100 += add;
        g.odo_x100  += add;
        g.ride_ms   += dt;
    }
    g.avg_speed = g.ride_ms ? (g.trip_x100 * 36000 / g.ride_ms) : 0;

    /* 主题色插值 */
    g.acc_r = (u8_t)ease_s32(g.acc_r, g.acc_tr, dt, 140);
    g.acc_g = (u8_t)ease_s32(g.acc_g, g.acc_tg, dt, 140);
    g.acc_b = (u8_t)ease_s32(g.acc_b, g.acc_tb, dt, 140);

    /* 过渡结束再刷一次整屏，保证外围光晕/环也落到最终主题色 */
    if (g.visible &&
        (g.acc_r == g.acc_tr && g.acc_g == g.acc_tg && g.acc_b == g.acc_tb) &&
        (old_acc_r != g.acc_r || old_acc_g != g.acc_g || old_acc_b != g.acc_b)) {
        if (g.w_bg) ipgui_widget_mark_dirty(g.w_bg);
    }

    /* 模式滑轨 */
    g.tab_x = ease_s32(g.tab_x, g.tab_target_x, dt, 110);

    /* 转向/远光自动演示 */
    g.left_on   = ((now / 1000) % 22) < 3;
    g.right_on  = ((now / 1000) % 22) >= 11 && ((now / 1000) % 22) < 14;
    g.high_beam = ((now / 1000) % 30) >= 24;
    g.blink     = (u8_t)(((now / 350) & 1) ? 1 : 0);

    /* 提交脏矩形：只在 RIDE 页可见时提交，避免给 NAV 页造成无效重绘 */
    if (g.visible) {
        if (g.speed != old_speed || g.power_x10 != old_power ||
            g.acc_r != old_acc_r || g.acc_g != old_acc_g || g.acc_b != old_acc_b) {
            if (g.w_center) ipgui_widget_mark_dirty(g.w_center);
        }
        if (g.soc != old_soc || g.charging) {
            if (g.w_batt) ipgui_widget_mark_dirty(g.w_batt);
        }
        if ((now - g.last_motor_ms) >= 120) {
            g.last_motor_ms = now;
            if (g.w_motor) ipgui_widget_mark_dirty(g.w_motor);
        }
        if ((now - g.last_trip_ms) >= 400) {
            g.last_trip_ms = now;
            if (g.w_trip) ipgui_widget_mark_dirty(g.w_trip);
        }
        if (g.blink != old_blink || (now - g.last_batt_ms) >= 500) {
            g.last_batt_ms = now;
            if (g.w_tell) ipgui_widget_mark_dirty(g.w_tell);
        }
        if (g.tab_x != old_tab) {
            if (g.w_tabs) ipgui_widget_mark_dirty(g.w_tabs);
        }
        if ((now / 60000) != g.last_minute) {
            g.last_minute = now / 60000;
            if (g.w_top) ipgui_widget_mark_dirty(g.w_top);
            if (g.w_nav) ipgui_widget_mark_dirty(g.w_nav);
        }
    }

    if ((now - g.boot_start) < (EMOTO_BOOT_MS + EMOTO_BOOT_FADE_MS)) {
        if (g.visible && g.w_boot) ipgui_widget_mark_dirty(g.w_boot);
    } else if (!g.boot_done) {
        g.boot_done = 1;
        if (g.w_boot) {
            g.w_boot->flags |= IPGUI_WIDGET_FLAG_INVISIBLE;
            g.w_boot->flags |= IPGUI_WIDGET_FLAG_DISABLED;
        }
        if (g.visible && g.scr) {
            ipgui_dirty_rect_add_xywh(&g.scr->dirty, 0, 0, EMOTO_W, EMOTO_H);
        }
    }

    /* 发布车辆快照，NAV 页直接读取，不复制模拟逻辑 */
    {
        ipgui_emoto_snapshot_t s;
        memset(&s, 0, sizeof(s));
        s.valid      = 1;
        s.mode       = g.mode;
        s.charging   = g.charging;
        s.left_on    = g.left_on;
        s.right_on   = g.right_on;
        s.high_beam  = g.high_beam;
        s.abs_on     = g.abs_on;
        s.tc_on      = g.tc_on;
        s.speed      = g.speed;
        s.soc        = g.soc;
        s.range_km   = g.range_km;
        s.power_x10  = g.power_x10;
        s.motor_temp = g.motor_temp;
        s.volt_x10   = g.volt_x10;
        s.trip_x100  = g.trip_x100;
        s.odo_x100   = g.odo_x100;
        s.avg_speed  = g.avg_speed;
        ipgui_emoto_shared_publish(&s);
    }
}

__IPGUI_API__ void ipgui_page_emoto_tick(void)
{
    u32_t now;
    s32_t dt;

    if (!g.inited) return;

    now = ipgui_tick_now();
    dt = (s32_t)(now - g.last_tick);
    if (dt <= 0) return;
    if (dt > 50) dt = 50;
    g.last_tick = now;

    emoto_update_state(dt);
}
