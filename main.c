/*
 * ============================================================================
 *  CAP-AGING  ·  多路电容老化测试系统 上位机 (HMI Demo)
 * ============================================================================
 *
 *  参考工程: m:\ESDBox\multi_power
 *    下位机 : STM32F407 + FreeRTOS, RS485 自定义帧 (vTaskRS485.c)
 *             帧结构: EF EF | len_lo len_hi | BoardID CMD SEQ DATA... | CRC16 | FE FE
 *             每板 128 路, 共 24 板 = 3072 路
 *             读数据: 每路 u16 最小电压(0.1V) + u16 电流(0.01mA) + 16B 内爆位图 + 64B 内爆计数
 *    配方   : step.db / Step 表 (12 道工艺步骤)
 *    参数   : SystemConfigure.xml
 *    账号   : UserSys.ini -> admin / admin
 *
 *  本文件是纯演示用的上位机界面, 数据由本地仿真器产生, 不连接真实串口。
 *  设计目标: 深色工业风 / 矢量绘制 / 脏矩形局部刷新 / 稳定 60fps。
 * ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "SDL.h"
#include "sdl_draw.h"
#include "sdl_input_event.h"

#include "ipgui_screen.h"
#include "ipgui_widget.h"
#include "ipgui_input_dispatcher.h"
#include "ipgui_blend.h"
#include "ipgui_draw_arc.h"
#include "ipgui_draw_box_background.h"
#include "ipgui_draw_box_border.h"
#include "ipgui_draw_filled_circle.h"
#include "ipgui_draw_line.h"
#include "ipgui_draw_triangle.h"
#include "ipgui_draw_builtin_font.h"
#include "ipgui_gradient_color.h"
#include "ipgui_time.h"
#include "ipgui_math.h"
#include "ipgui_image.h"

#include "open_sans.h"
#include "quicksand_medium.h"

#undef main

/* 旧版 demo 控件（widget3/4/5）仍参与编译，需要满足链接符号；
 * 这些控件在本工程中不会被创建，保留空定义即可。 */
ipgui_image_data_t wave_img;
ipgui_image_data_t main_bg_img;
ipgui_image_data_t power_img;
ipgui_image_data_t tablelamp_img;
ipgui_image_data_t tablelamp_on_img;

/* ==========================================================================
 *  屏幕 / 驱动
 * ========================================================================== */

#define SCR_W 800
#define SCR_H 480

#define TOP_H 46
#define NAV_W 58

ipgui_input_dispatcher_t dispatcher;
ipgui_input_src_t        pointer_src;
ipgui_scr_t              main_screen;
ipgui_input_src_id_t     pointer_id;
ipgui_scr_id_t           main_scr_id;

extern __IPGUI_API__ ipgui_err_t ipgui_sdl_mouse_event_poll(
        void * priv_data, ipgui_input_src_evt_t * raw_evt);

ipgui_scr_drv_t sdl_drv = {
    .xreso       = SCR_W,
    .yreso       = SCR_H,
    .pri_data    = &g_sdl_private,
    .put_pixel   = sdl_put_pixel,
    .fill_region = sdl_fill_region,
    .flush       = sdl_flush,
};

static u8_t main_screen_frame_buf[SCR_W * 4 * SCR_H];

/* ==========================================================================
 *  页面
 * ========================================================================== */

#define PAGE_OVERVIEW 0
#define PAGE_ARRAY    1
#define PAGE_TREND    2
#define PAGE_RECIPE   3
#define PAGE_LOG      4
#define PAGE_COUNT    5

/* ==========================================================================
 *  主题
 * ========================================================================== */

static ipgui_color_t rgb(u8_t r, u8_t g, u8_t b, u8_t a)
{
    ipgui_color_t c;
    c.r = r; c.g = g; c.b = b; c.a = a;
    return c;
}

#define COL_BG_TOP      rgb(216, 220, 226, 255)
#define COL_BG_BOT      rgb(204, 209, 216, 255)
#define COL_TOPBAR      rgb(38, 68, 95, 255)
#define COL_NAV         rgb(230, 233, 238, 255)
#define COL_PANEL_TOP   rgb(255, 255, 255, 255)
#define COL_PANEL_BOT   rgb(255, 255, 255, 255)
#define COL_PANEL_FLAT  rgb(246, 247, 249, 255)
#define COL_HDR         rgb(234, 238, 243, 255)
#define COL_BORDER      rgb(160, 169, 181, 255)
#define COL_BORDER_SOFT rgb(198, 205, 214, 255)
#define COL_GRID        rgb(219, 225, 232, 255)

#define COL_TXT         rgb(20, 28, 40, 255)
#define COL_TXT_2       rgb(62, 74, 92, 255)
#define COL_TXT_3       rgb(108, 121, 138, 255)

#define COL_CYAN        rgb(20, 99, 158, 255)
#define COL_CYAN_DIM    rgb(20, 99, 158, 60)
#define COL_VIOLET      rgb(176, 62, 92, 255)
#define COL_GREEN       rgb(28, 138, 68, 255)
#define COL_AMBER       rgb(190, 120, 0, 255)
#define COL_RED         rgb(190, 55, 42, 255)
#define COL_BLUE        rgb(20, 99, 158, 255)

/* 状态灯 / 单元格用的低饱和填充 */
#define COL_OK_FILL     rgb(78, 134, 176, 255)
#define COL_OFF_FILL    rgb(198, 204, 212, 255)

/* ==========================================================================
 *  绘图原语
 * ========================================================================== */

static void draw_line(ipgui_surf_t * surf,
                      ipgui_coord_t x1, ipgui_coord_t y1,
                      ipgui_coord_t x2, ipgui_coord_t y2,
                      ipgui_coord_t width, ipgui_color_t color, u8_t opacity,
                      ipgui_line_cap_t cap)
{
    ipgui_line_t line;
    ipgui_line_style_t st;

    if (x1 == x2 && y1 == y2) return;

    line.start.x = x1; line.start.y = y1;
    line.end.x   = x2; line.end.y   = y2;

    memset(&st, 0, sizeof(st));
    st.width      = width;
    st.cap        = cap;
    st.paint.type = IPGUI_PAINT_COLOR;
    st.paint.src.color = color;
    st.opacity    = opacity;
    st.blend_mode = IPGUI_BLEND_NORMAL;

    ipgui_draw_line_generic(surf, (ipgui_aabb_t *)0, &line, &st);
}

static void draw_circle(ipgui_surf_t * surf, ipgui_coord_t cx, ipgui_coord_t cy,
                        ipgui_coord_t r, ipgui_color_t color, u8_t opacity)
{
    ipgui_filled_circle_style_t st;

    if (r <= 0) return;
    memset(&st, 0, sizeof(st));
    st.paint.type = IPGUI_PAINT_COLOR;
    st.paint.src.color = color;
    st.opacity = opacity;
    st.blend_mode = IPGUI_BLEND_NORMAL;
    ipgui_draw_filled_circle(surf, (ipgui_aabb_t *)0, cx, cy, r, &st);
}

static void draw_ring(ipgui_surf_t * surf, ipgui_coord_t cx, ipgui_coord_t cy,
                      ipgui_coord_t r, ipgui_coord_t thick,
                      ipgui_color_t color, u8_t opacity)
{
    ipgui_arc_t arc;
    ipgui_arc_style_t st;

    if (r <= thick) return;

    arc.cx = cx; arc.cy = cy;
    arc.er = r;  arc.ir = r - thick;
    arc.start = 0;
    arc.dir = IPGUI_ARC_DRAW_DIR_CW;
    arc.angle = 360;

    memset(&st, 0, sizeof(st));
    st.paint.type = IPGUI_PAINT_COLOR;
    st.paint.src.color = color;
    st.opacity = opacity;
    st.sep_type = IPGUI_ARC_ENDPOINT_TYPE_BUTT;
    st.eep_type = IPGUI_ARC_ENDPOINT_TYPE_BUTT;
    st.blend_mode = IPGUI_BLEND_NORMAL;

    ipgui_draw_arc(surf, (ipgui_aabb_t *)0, &arc, &st);
}

static void fill_rect(ipgui_surf_t * surf,
                      ipgui_coord_t x, ipgui_coord_t y,
                      ipgui_coord_t w, ipgui_coord_t h,
                      ipgui_color_t color, u8_t opacity)
{
    ipgui_aabb_t box;
    ipgui_box_style_t bs;
    ipgui_box_bg_style_t bg;

    if (w <= 0 || h <= 0) return;

    box.start.x = x; box.start.y = y;
    box.end.x = x + w - 1; box.end.y = y + h - 1;

    memset(&bs, 0, sizeof(bs));
    memset(&bg, 0, sizeof(bg));
    bg.paint.type = IPGUI_PAINT_COLOR;
    bg.paint.src.color = color;
    bg.opacity = opacity;
    bg.blend_mode = IPGUI_BLEND_NORMAL;

    ipgui_draw_box_background(surf, (ipgui_aabb_t *)0, &box, &bs, &bg);
}

static void fill_round(ipgui_surf_t * surf,
                       ipgui_coord_t x, ipgui_coord_t y,
                       ipgui_coord_t w, ipgui_coord_t h, ipgui_coord_t r,
                       ipgui_color_t color, u8_t opacity)
{
    ipgui_aabb_t box;
    ipgui_box_style_t bs;
    ipgui_box_bg_style_t bg;

    if (w <= 0 || h <= 0) return;

    box.start.x = x; box.start.y = y;
    box.end.x = x + w - 1; box.end.y = y + h - 1;

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

static void stroke_round(ipgui_surf_t * surf,
                         ipgui_coord_t x, ipgui_coord_t y,
                         ipgui_coord_t w, ipgui_coord_t h, ipgui_coord_t r,
                         ipgui_coord_t width, ipgui_color_t color, u8_t opacity)
{
    ipgui_aabb_t box;
    ipgui_box_style_t bs;
    ipgui_box_border_style_t st;

    if (w <= 0 || h <= 0 || width <= 0) return;

    box.start.x = x; box.start.y = y;
    box.end.x = x + w - 1; box.end.y = y + h - 1;

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

/* 工控面板：纯白、1px 实边框、直角，不做渐变和阴影 */
static void draw_panel(ipgui_surf_t * surf,
                       ipgui_coord_t x, ipgui_coord_t y,
                       ipgui_coord_t w, ipgui_coord_t h, ipgui_coord_t r,
                       ipgui_color_t top, ipgui_color_t bottom,
                       ipgui_color_t border, u8_t border_opacity)
{
    (void)r; (void)top; (void)bottom;

    if (w <= 0 || h <= 0) return;

    fill_rect(surf, x, y, w, h, COL_PANEL_TOP, 255);
    stroke_round(surf, x, y, w, h, 0, 1, border, border_opacity);
}

/* 面板标题栏：浅灰底 + 下横线，组态软件的标准分组框样式 */
static void draw_text(ipgui_surf_t * surf, const ipgui_font_t * font,
                      const char * text, ipgui_coord_t x, ipgui_coord_t y,
                      ipgui_color_t color, u8_t opacity);

static void panel_header(ipgui_surf_t * surf, ipgui_coord_t w,
                         const char * title, const char * sub)
{
    fill_rect(surf, 1, 1, w - 2, 33, COL_HDR, 255);
    draw_line(surf, 0, 34, w - 1, 34, 1, COL_BORDER_SOFT, 255,
              IPGUI_LINE_CAP_BUTT);

    if (sub) {
        draw_text(surf, &open_sans_8px, title, 10, 6, COL_TXT_3, 255);
        draw_text(surf, &quicksand_medium_12px, sub, 10, 16, COL_TXT, 255);
    } else {
        draw_text(surf, &quicksand_medium_12px, title, 10, 11, COL_TXT, 255);
    }
}

/* 方形状态灯：工控盘面最典型的指示元件 */
static void draw_lamp(ipgui_surf_t * surf, ipgui_coord_t x, ipgui_coord_t y,
                      ipgui_coord_t size, ipgui_color_t color, int on)
{
    fill_rect(surf, x, y, size, size, color, on ? 255 : 55);
    stroke_round(surf, x, y, size, size, 0, 1, rgb(120, 130, 146, 255),
                 on ? 255 : 160);
    if (on) {
        fill_rect(surf, x + 2, y + 2, size - 4, 2, rgb(255, 255, 255, 255), 70);
    }
}

/* ==========================================================================
 *  文字
 * ========================================================================== */

static void draw_text(ipgui_surf_t * surf, const ipgui_font_t * font,
                      const char * text, ipgui_coord_t x, ipgui_coord_t y,
                      ipgui_color_t color, u8_t opacity)
{
    ipgui_font_style_t st;

    if (!font || !text || opacity < 3) return;

    memset(&st, 0, sizeof(st));
    st.font       = font;
    st.opacity    = opacity;
    st.paint.type = IPGUI_PAINT_COLOR;
    st.paint.src.color = color;
    st.blend_mode = IPGUI_BLEND_NORMAL;

    ipgui_draw_builtin_text(surf, (ipgui_aabb_t *)0, &st,
                            (const s8_t *)text, x, y);
}

static ipgui_coord_t text_w(const ipgui_font_t * font, const char * text)
{
    if (!font || !text) return 0;
    return ipgui_builtin_text_width(font, (const s8_t *)text);
}

static void draw_text_center(ipgui_surf_t * surf, const ipgui_font_t * font,
                             const char * text, ipgui_coord_t cx, ipgui_coord_t y,
                             ipgui_color_t color, u8_t opacity)
{
    draw_text(surf, font, text, cx - text_w(font, text) / 2, y, color, opacity);
}

static void draw_text_right(ipgui_surf_t * surf, const ipgui_font_t * font,
                            const char * text, ipgui_coord_t right, ipgui_coord_t y,
                            ipgui_color_t color, u8_t opacity)
{
    draw_text(surf, font, text, right - text_w(font, text), y, color, opacity);
}

/* 数字格式化：不依赖浮点 printf，避免拖慢每帧 */
static void fmt_int(char * b, s32_t v)
{
    char t[16];
    int i = 0, n = 0;
    u32_t u;

    if (v < 0) { b[n++] = '-'; u = (u32_t)(-v); } else { u = (u32_t)v; }
    if (u == 0) { b[n++] = '0'; b[n] = 0; return; }

    while (u) { t[i++] = (char)('0' + (u % 10)); u /= 10; }
    while (i) b[n++] = t[--i];
    b[n] = 0;
}

/* v 为放大 10 倍的值，输出 "280.4" */
static void fmt_dec1(char * b, s32_t v)
{
    char t[16];
    s32_t ip, fp;

    if (v < 0) { *b++ = '-'; v = -v; }
    ip = v / 10;
    fp = v % 10;
    fmt_int(t, ip);
    strcpy(b, t);
    strcat(b, ".");
    t[0] = (char)('0' + fp);
    t[1] = 0;
    strcat(b, t);
}

/* v 为放大 100 倍的值，输出 "1.82" */
static void fmt_dec2(char * b, s32_t v)
{
    char t[16];
    s32_t ip, fp;

    if (v < 0) { *b++ = '-'; v = -v; }
    ip = v / 100;
    fp = v % 100;
    fmt_int(t, ip);
    strcpy(b, t);
    strcat(b, ".");
    t[0] = (char)('0' + fp / 10);
    t[1] = (char)('0' + fp % 10);
    t[2] = 0;
    strcat(b, t);
}

static void fmt_hms(char * b, u32_t sec)
{
    char t[8];
    u32_t h = sec / 3600, m = (sec / 60) % 60, s = sec % 60;

    t[0] = (char)('0' + (h / 10) % 10);
    t[1] = (char)('0' + h % 10);
    t[2] = ':';
    t[3] = (char)('0' + m / 10);
    t[4] = (char)('0' + m % 10);
    t[5] = ':';
    t[6] = (char)('0' + s / 10);
    t[7] = 0;
    strcpy(b, t);

    {
        char s1[4];
        s1[0] = (char)('0' + s % 10);
        s1[1] = 0;
        strcat(b, s1);
    }
}

static void fmt_pad2(char * b, s32_t v)
{
    b[0] = (char)('0' + (v / 10) % 10);
    b[1] = (char)('0' + v % 10);
    b[2] = 0;
}

static s32_t clamp(s32_t v, s32_t lo, s32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

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

/* ==========================================================================
 *  数据模型
 * ========================================================================== */

#define NBOARD   24
#define NCH      128
#define NTOT     (NBOARD * NCH)
#define TREND_N  96

#define CH_OFF   0
#define CH_OK    1
#define CH_WARN  2
#define CH_BAD   3

typedef struct {
    u8_t  min;
    s16_t v;
    s16_t cur;
    u8_t  heat;
    s16_t temp;
    u8_t  raise;
    s16_t low;
} step_t;

/* 来自 step.db / Step 表 */
static const step_t STEPS[12] = {
    { 15,  30, 8, 0,  0, 0,   0 },
    { 70, 130, 8, 1, 55, 0,   0 },
    { 55, 180, 8, 1, 55, 0,   0 },
    { 50, 220, 8, 1, 55, 0,   0 },
    { 22, 240, 8, 1, 55, 0,   0 },
    { 25, 260, 8, 1, 55, 0,   0 },
    { 30, 280, 8, 1, 55, 0,   0 },
    { 15, 280, 8, 1, 55, 0,   0 },
    {  5, 280, 8, 1, 55, 1, 260 },
    { 10, 250, 5, 1, 70, 1, 240 },
    { 90, 250, 5, 1, 85, 1, 240 },
    { 20, 250, 5, 0,  0, 0,   0 },
};

static step_t RCP[12];

static u16_t s_v[NTOT];      /* 0.1 V */
static u16_t s_i[NTOT];      /* 0.01 mA */
static u8_t  s_imp[NTOT];
static u8_t  s_st[NTOT];

static struct {
    u8_t   page;
    u8_t   active_step;
    u8_t   sel_step;
    u8_t   nav_hover;
    u8_t   nav_press;
    u8_t   head;
    u8_t   running;
    u8_t   log_filter;

    u32_t  boot_ms;
    u32_t  run_ms;
    u32_t  step_ms;
    u32_t  last_sim_ms;
    u32_t  last_slow_ms;
    u32_t  last_top_ms;

    s32_t  bus_x10;
    s32_t  leak_x100;
    s32_t  oven_x10;
    s32_t  charge_x10;

    u16_t  online;
    u16_t  imp_n;
    u16_t  warn_n;

    s32_t  curve_v[TREND_N];
    s32_t  curve_i[TREND_N];

    s32_t  hist[16];
    u16_t  worst[5];

    u16_t  sel_ch;
    u16_t  hov_ch;
} A;

static u32_t hash32(u32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static u8_t ch_state_of(u16_t idx, u16_t v, u16_t i)
{
    if (s_imp[idx]) return CH_BAD;
    if (v + 32 < (u16_t)A.bus_x10) return CH_WARN;
    if (i > 235) return CH_WARN;
    return CH_OK;
}

/* ==========================================================================
 *  仿真
 * ========================================================================== */

static void sim_init(void)
{
    u16_t i;

    memcpy(RCP, STEPS, sizeof(STEPS));

    A.page        = PAGE_OVERVIEW;
    A.active_step = 6;
    A.sel_step    = 6;
    A.sel_ch      = 7 * NCH + 42;
    A.hov_ch      = 0xFFFF;
    A.running     = 1;
    A.bus_x10     = 2800;
    A.leak_x100   = 180;
    A.oven_x10    = 552;
    A.charge_x10  = 80;
    A.run_ms      = (4u * 3600u + 12u * 60u + 36u) * 1000u;
    A.step_ms     = (u32_t)RCP[6].min * 60u * 1000u * 45u / 100u;

    for (i = 0; i < NTOT; i ++) {
        u32_t h = hash32((u32_t)i * 2654435761u + 0x9e3779b9u);
        s_imp[i] = ((h % 421u) == 0u) ? (u8_t)(1 + (h >> 8) % 3) : 0;
        s_v[i] = 2800;
        s_i[i] = 180;
        s_st[i] = CH_OK;
    }

    /* 预置一段上升沿，避免首屏曲线是一条直线 */
    for (i = 0; i < TREND_N; i ++) {
        s32_t t = (s32_t)i * 100 / (TREND_N - 1);
        A.curve_v[i] = 2400 + t * 4;
        A.curve_i[i] = 120 + t * 35 / 100;
    }
    A.head = 0;
}

static void sim_channel_fill(void)
{
    u16_t idx;
    u16_t online = 0, warn = 0, imp = 0;
    s32_t hist[16];
    s32_t bus = A.bus_x10;
    u64_t sum_i = 0;

    memset(hist, 0, sizeof(hist));

    for (idx = 0; idx < NTOT; idx ++) {
        u32_t h = hash32((u32_t)idx * 2654435761u + 0x9e3779b9u);
        /* 三角分布近似正态，直方图才是钟形而不是一堵墙 */
        s32_t dev = (s32_t)(h % 21u) + (s32_t)((h >> 8) % 21u)
                  + (s32_t)((h >> 16) % 21u) - 30;
        s32_t v   = bus + dev;
        s32_t cur = 96 + (s32_t)(h % 100u) + (s32_t)((h >> 6) % 10u) + (s32_t)(idx % 5u);
        s32_t b;

        if ((h % 97u) == 0u) {
            v -= 55;                                 /* 偶发偏低通道 */
        }

        if (v < 0) v = 0;

        s_v[idx] = (u16_t)v;
        s_i[idx] = (u16_t)cur;
        s_st[idx] = ch_state_of(idx, (u16_t)v, (u16_t)cur);

        if (s_st[idx] != CH_OFF) online ++;
        if (s_st[idx] == CH_WARN) warn ++;
        if (s_imp[idx]) imp ++;

        sum_i += s_i[idx];

        b = (v - (bus - 40)) / 5;
        if (b < 0) b = 0;
        if (b > 15) b = 15;
        hist[b] ++;
    }

    A.online = online;
    A.warn_n = warn;
    A.imp_n  = imp;
    A.leak_x100 = (s32_t)(sum_i / NTOT);
    memcpy(A.hist, hist, sizeof(hist));
}

static void sim_refresh_worst(void)
{
    u16_t idx;
    u16_t w[5];
    u8_t k;

    for (k = 0; k < 5; k ++) w[k] = 0;

    for (idx = 0; idx < NTOT; idx ++) {
        for (k = 0; k < 5; k ++) {
            if (s_i[idx] > s_i[w[k]]) {
                u8_t m;
                for (m = 4; m > k; m --) w[m] = w[m - 1];
                w[k] = idx;
                break;
            }
        }
    }

    memcpy(A.worst, w, sizeof(w));
}

static void sim_tick(u32_t now)
{
    u32_t dt = now - A.last_sim_ms;
    s32_t step_min_ms;
    s32_t target_v;
    s32_t target_t;

    if (dt < 40) return;
    A.last_sim_ms = now;

    if (dt > 400) dt = 400;

    /* 工艺推进：1 秒真实时间 = 1 分钟工艺时间（演示加速） */
    if (A.running) {
        A.step_ms += dt * 60u;
        A.run_ms  += dt;

        step_min_ms = (s32_t)RCP[A.active_step].min * 60 * 1000;
        if (step_min_ms <= 0 || A.step_ms >= (u32_t)step_min_ms) {
            A.step_ms = 0;
            A.active_step = (u8_t)((A.active_step + 1) % 12);
            A.sel_step = A.active_step;
        }
    }

    target_v = (s32_t)RCP[A.active_step].v * 10;
    A.bus_x10  = ease_s32(A.bus_x10, target_v, (s32_t)dt, 700);

    target_t = RCP[A.active_step].heat ? (s32_t)RCP[A.active_step].temp * 10 : 250;
    A.oven_x10 = ease_s32(A.oven_x10, target_t, (s32_t)dt, 2200);

    A.charge_x10 = ease_s32(A.charge_x10, (s32_t)RCP[A.active_step].cur * 10,
                            (s32_t)dt, 600);

    /* 通道扫描 + 曲线：10Hz */
    if (now - A.last_slow_ms >= 100) {
        A.last_slow_ms = now;

        sim_channel_fill();
        sim_refresh_worst();

        A.curve_v[A.head] = A.bus_x10;
        A.curve_i[A.head] = A.leak_x100;
        A.head = (u8_t)((A.head + 1) % TREND_N);
    }
}

/* ==========================================================================
 *  页面通用：顶栏
 * ========================================================================== */

static void draw_logo(ipgui_surf_t * s, int x, int y, int size)
{
    stroke_round(s, x, y, size, size, 0, 1, rgb(255, 255, 255, 255), 210);
    fill_rect(s, x + 4,  y + 5, 3, 14, rgb(255, 255, 255, 255), 235);
    fill_rect(s, x + 9,  y + 5, 3, 9,  rgb(255, 255, 255, 255), 235);
    fill_rect(s, x + 14, y + 5, 3, 12, rgb(255, 255, 255, 255), 235);
}

static void status_block(ipgui_surf_t * s, int x,
                         const char * label, const char * value,
                         ipgui_color_t lamp_color, int on)
{
    draw_lamp(s, x, 19, 9, lamp_color, on);
    draw_text(s, &open_sans_7px, label, x + 14, 7, rgb(158, 184, 208, 255), 255);
    draw_text(s, &quicksand_medium_11px, value, x + 14, 17,
              rgb(255, 255, 255, 255), 255);
}

static void render_top(ipgui_widget_t * w, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_surf_t * s = ctx->surf;
    time_t tt;
    struct tm tmv;
    char buf[32];

    fill_rect(s, 0, 0, w->w, w->h, COL_TOPBAR, 255);
    fill_rect(s, 0, w->h - 2, w->w, 2, rgb(22, 45, 66, 255), 255);

    /* 左侧：标识 */
    draw_logo(s, 10, 11, 24);

    draw_text(s, &quicksand_medium_17px, "CAP-AGING", 44, 6,
              rgb(255, 255, 255, 255), 255);
    draw_text(s, &open_sans_7px, "MULTI-CHANNEL BURN-IN TEST SYSTEM", 45, 28,
              rgb(160, 186, 210, 255), 255);

    draw_line(s, 210, 9, 210, 37, 1, rgb(74, 106, 134, 255), 255,
              IPGUI_LINE_CAP_BUTT);

    /* 中部：盘面状态灯 */
    status_block(s, 222, "MODE", "AUTO",
                 COL_GREEN, 1);
    status_block(s, 318, "COMM", "RS485",
                 A.online ? COL_GREEN : COL_RED, A.online ? 1 : 0);
    status_block(s, 414, "ALARM", A.imp_n ? "IMPLOSION" : "NORMAL",
                 (A.imp_n || A.warn_n) ? COL_RED : COL_GREEN,
                 (A.imp_n || A.warn_n) ? 1 : 0);

    {
        char sb[12];
        fmt_pad2(sb, A.active_step + 1);
        strcat(sb, "/12");
        status_block(s, 510, "STEP", sb, COL_BLUE, 1);
    }

    draw_line(s, 612, 9, 612, 37, 1, rgb(74, 106, 134, 255), 255,
              IPGUI_LINE_CAP_BUTT);

    /* 右侧：时间 / 用户 */
    tt = time(NULL);
    {
        struct tm * ptm = localtime(&tt);
        if (ptm) tmv = *ptm;
        else memset(&tmv, 0, sizeof(tmv));
    }

    buf[0] = (char)('0' + tmv.tm_hour / 10);
    buf[1] = (char)('0' + tmv.tm_hour % 10);
    buf[2] = ':';
    buf[3] = (char)('0' + tmv.tm_min / 10);
    buf[4] = (char)('0' + tmv.tm_min % 10);
    buf[5] = ':';
    buf[6] = (char)('0' + tmv.tm_sec / 10);
    buf[7] = (char)('0' + tmv.tm_sec % 10);
    buf[8] = 0;
    draw_text_right(s, &quicksand_medium_16px, buf, 704, 6,
                    rgb(255, 255, 255, 255), 255);

    {
        char db[16];
        db[0] = (char)('0' + (tmv.tm_year + 1900) / 1000 % 10);
        db[1] = (char)('0' + (tmv.tm_year + 1900) / 100 % 10);
        db[2] = (char)('0' + (tmv.tm_year + 1900) / 10 % 10);
        db[3] = (char)('0' + (tmv.tm_year + 1900) % 10);
        db[4] = '-';
        db[5] = (char)('0' + (tmv.tm_mon + 1) / 10);
        db[6] = (char)('0' + (tmv.tm_mon + 1) % 10);
        db[7] = '-';
        db[8] = (char)('0' + tmv.tm_mday / 10);
        db[9] = (char)('0' + tmv.tm_mday % 10);
        db[10] = 0;
        draw_text_right(s, &open_sans_8px, db, 704, 28,
                        rgb(160, 186, 210, 255), 255);
    }

    fill_rect(s, 716, 11, 72, 24, rgb(30, 56, 80, 255), 255);
    stroke_round(s, 716, 11, 72, 24, 0, 1, rgb(86, 118, 146, 255), 255);
    draw_text(s, &open_sans_8px, "OPERATOR", 724, 15,
              rgb(148, 176, 202, 255), 255);
    draw_text(s, &open_sans_10px, "admin", 724, 24,
              rgb(255, 255, 255, 255), 255);
}

/* ==========================================================================
 *  页面通用：左侧导航
 * ========================================================================== */

#define NAV_ITEM_Y0 14
#define NAV_ITEM_H  74
#define NAV_BTN_X   8
#define NAV_BTN_S   42

static void nav_icon(ipgui_surf_t * s, int idx, int x, int y,
                     ipgui_color_t c, u8_t op)
{
    if (idx == PAGE_OVERVIEW) {
        draw_ring(s, x + 10, y + 10, 9, 2, c, (u8_t)(op / 2));
        draw_line(s, x + 10, y + 10, x + 15, y + 5, 2, c, op,
                  IPGUI_LINE_CAP_ROUND);
        draw_circle(s, x + 10, y + 10, 2, c, op);
    } else if (idx == PAGE_ARRAY) {
        int i, j;
        for (j = 0; j < 3; j ++)
            for (i = 0; i < 3; i ++)
                fill_round(s, x + 1 + i * 7, y + 1 + j * 7, 5, 5, 1, c,
                           (u8_t)(((i + j) & 1) ? op : op / 2));
    } else if (idx == PAGE_TREND) {
        draw_line(s, x + 1, y + 16, x + 6, y + 9, 2, c, op, IPGUI_LINE_CAP_ROUND);
        draw_line(s, x + 6, y + 9, x + 11, y + 12, 2, c, op, IPGUI_LINE_CAP_ROUND);
        draw_line(s, x + 11, y + 12, x + 18, y + 3, 2, c, op, IPGUI_LINE_CAP_ROUND);
        draw_circle(s, x + 6, y + 9, 2, c, op);
        draw_circle(s, x + 11, y + 12, 2, c, op);
    } else if (idx == PAGE_RECIPE) {
        int i;
        for (i = 0; i < 3; i ++) {
            int yy = y + 3 + i * 6;
            draw_line(s, x + 1, yy, x + 18, yy, 1, c, (u8_t)(op / 2),
                      IPGUI_LINE_CAP_ROUND);
            draw_circle(s, x + 4 + i * 5, yy, 2, c, op);
        }
    } else {
        int i;
        for (i = 0; i < 3; i ++) {
            int yy = y + 3 + i * 7;
            fill_round(s, x + 1, yy, 3, 3, 1, c, op);
            draw_line(s, x + 7, yy + 1, x + 18, yy + 1, 1, c, (u8_t)(op / 2),
                      IPGUI_LINE_CAP_BUTT);
        }
    }
}

static void render_nav(ipgui_widget_t * w, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_surf_t * s = ctx->surf;
    int i;

    fill_rect(s, 0, 0, w->w, w->h, COL_NAV, 255);
    draw_line(s, w->w - 1, 0, w->w - 1, w->h - 1, 1, COL_BORDER, 255,
              IPGUI_LINE_CAP_BUTT);

    for (i = 0; i < PAGE_COUNT; i ++) {
        int by = NAV_ITEM_Y0 + i * NAV_ITEM_H;
        int active = (i == A.page);

        if (active) {
            fill_rect(s, NAV_BTN_X, by, NAV_BTN_S, NAV_BTN_S, COL_CYAN, 255);
            nav_icon(s, i, NAV_BTN_X + 11, by + 11, rgb(255, 255, 255, 255), 255);
        } else {
            if (A.nav_hover == (u8_t)i) {
                fill_rect(s, NAV_BTN_X, by, NAV_BTN_S, NAV_BTN_S,
                          rgb(211, 217, 224, 255), 255);
            }
            nav_icon(s, i, NAV_BTN_X + 11, by + 11, COL_TXT_2,
                     (A.nav_hover == (u8_t)i) ? 255 : 195);
        }
    }

    draw_line(s, 8, w->h - 26, w->w - 8, w->h - 26, 1, COL_BORDER_SOFT, 255,
              IPGUI_LINE_CAP_BUTT);
    draw_text_center(s, &open_sans_7px, "V1.10", w->w / 2, w->h - 18,
                     COL_TXT_3, 220);
}

/* ==========================================================================
 *  页面 0：总览
 * ========================================================================== */

#define KPI_X 68
#define KPI_Y 56
#define KPI_W 722
#define KPI_H 92
#define KPI_PAD 10
#define KPI_GAP 10
#define KPI_CW ((KPI_W - KPI_PAD * 2 - KPI_GAP * 3) / 4)
#define KPI_CH (KPI_H - KPI_PAD * 2)

static void kpi_card(ipgui_surf_t * s, int i,
                     const char * label, const char * value, const char * unit,
                     ipgui_color_t accent, s32_t level_pct)
{
    int x = KPI_PAD + i * (KPI_CW + KPI_GAP);
    int y = KPI_PAD;
    int tw;
    s32_t p = clamp(level_pct, 0, 100);

    draw_panel(s, x, y, KPI_CW, KPI_CH, 0,
               COL_PANEL_TOP, COL_PANEL_BOT, COL_BORDER, 255);

    /* 左侧状态色条 */
    fill_rect(s, x + 1, y + 1, 3, KPI_CH - 2, accent, 255);

    draw_text(s, &open_sans_8px, label, x + 12, y + 8, COL_TXT_3, 255);

    draw_text(s, &quicksand_medium_28px, value, x + 11, y + 19, COL_TXT, 255);
    tw = text_w(&quicksand_medium_28px, value);
    draw_text(s, &open_sans_9px, unit, x + 15 + tw, y + 33, COL_TXT_3, 255);

    /* 量程条 */
    fill_rect(s, x + 12, y + KPI_CH - 9, KPI_CW - 24, 4, COL_GRID, 255);
    if (p > 0) {
        fill_rect(s, x + 12, y + KPI_CH - 9, (KPI_CW - 24) * p / 100, 4,
                  accent, 255);
    }
}

static void render_kpi(ipgui_widget_t * w, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_surf_t * s = ctx->surf;
    char b[24];

    (void)w;

    fmt_dec1(b, A.bus_x10);
    kpi_card(s, 0, "BUS VOLTAGE", b, "V", COL_CYAN, A.bus_x10 * 100 / 3200);

    fmt_dec2(b, A.leak_x100);
    kpi_card(s, 1, "AVERAGE LEAKAGE", b, "mA", rgb(90, 107, 127, 255),
             A.leak_x100 * 100 / 320);

    fmt_int(b, A.imp_n);
    kpi_card(s, 2, "IMPLOSION EVENTS", b, "EVT",
             A.imp_n ? COL_RED : COL_GREEN, A.imp_n * 100 / 50);

    fmt_dec1(b, A.oven_x10);
    kpi_card(s, 3, "OVEN TEMPERATURE", b, "C", COL_AMBER, A.oven_x10 * 100 / 1000);
}

/* --- 总览：实时曲线 --- */

#define TR_X 68
#define TR_Y 156
#define TR_W 430
#define TR_H 204

static void chart_grid(ipgui_surf_t * s, int x, int y, int w, int h, int rows)
{
    int i;
    for (i = 0; i <= rows; i ++) {
        int yy = y + h * i / rows;
        draw_line(s, x, yy, x + w, yy, 1, COL_GRID, 255, IPGUI_LINE_CAP_BUTT);
    }
    for (i = 0; i <= 4; i ++) {
        int xx = x + w * i / 4;
        draw_line(s, xx, y, xx, y + h, 1, COL_GRID, 255, IPGUI_LINE_CAP_BUTT);
    }
}

static void polyline_from(ipgui_surf_t * s, int x, int y, int w, int h,
                          const s32_t * data, int count, int head,
                          s32_t lo, s32_t hi, ipgui_color_t color,
                          ipgui_coord_t thick)
{
    int i;
    int px = 0, py = 0;
    int started = 0;

    for (i = 0; i < count; i ++) {
        s32_t v = data[(head + i) % count];
        int xx = x + w * i / (count - 1);
        int yy;

        v = clamp(v, lo, hi);
        yy = y + h - (int)((v - lo) * h / (hi - lo));

        if (started) {
            draw_line(s, px, py, xx, yy, thick, color, 235, IPGUI_LINE_CAP_ROUND);
        }
        px = xx; py = yy;
        started = 1;
    }
}

static void render_trend_small(ipgui_widget_t * w, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_surf_t * s = ctx->surf;
    int px = 12, py = 46, pw = w->w - 24, ph = w->h - 64;
    char b[24];

    draw_panel(s, 0, 0, w->w, w->h, 0,
               COL_PANEL_TOP, COL_PANEL_BOT, COL_BORDER, 255);
    panel_header(s, w->w, "LIVE TREND", "BUS VOLTAGE / LEAKAGE");

    /* 图例放在标题栏右侧 */
    fill_rect(s, w->w - 118, 12, 8, 8, COL_CYAN, 255);
    draw_text(s, &open_sans_8px, "V", w->w - 106, 11, COL_TXT_3, 255);
    fill_rect(s, w->w - 82, 12, 8, 8, COL_VIOLET, 255);
    draw_text(s, &open_sans_8px, "mA", w->w - 70, 11, COL_TXT_3, 255);

    chart_grid(s, px, py, pw, ph, 4);

    polyline_from(s, px, py, pw, ph, A.curve_v, TREND_N, A.head,
                  0, 3200, COL_CYAN, 2);
    polyline_from(s, px, py, pw, ph, A.curve_i, TREND_N, A.head,
                  0, 320, COL_VIOLET, 1);

    /* 末端值标记 */
    {
        int yy = py + ph - (int)(A.bus_x10 * ph / 3200);
        yy = clamp(yy, py, py + ph);
        fill_rect(s, px + pw - 3, yy - 3, 6, 6, COL_CYAN, 255);
    }

    fmt_dec1(b, A.bus_x10);
    draw_text(s, &open_sans_8px, b, px + 2, py - 11, COL_CYAN, 255);

    draw_text(s, &open_sans_8px, "-24s", px, w->h - 14, COL_TXT_3, 255);
    draw_text_right(s, &open_sans_8px, "NOW", px + pw, w->h - 14, COL_TXT_3, 255);
}

/* --- 总览：板卡健康 --- */

#define BD_X 506
#define BD_Y 156
#define BD_W 284
#define BD_H 204

static int board_scan(int b, ipgui_color_t * out)
{
    u16_t idx = (u16_t)(b * NCH);
    u16_t k;
    int warn = 0, bad = 0;

    for (k = 0; k < NCH; k ++) {
        if (s_st[idx + k] == CH_BAD) bad ++;
        else if (s_st[idx + k] == CH_WARN) warn ++;
    }

    if (bad)       *out = COL_RED;
    else if (warn) *out = COL_AMBER;
    else           *out = COL_GREEN;

    return bad + warn;
}

static void render_boards(ipgui_widget_t * w, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_surf_t * s = ctx->surf;
    int i;
    char b[16];

    draw_panel(s, 0, 0, w->w, w->h, 0,
               COL_PANEL_TOP, COL_PANEL_BOT, COL_BORDER, 255);
    panel_header(s, w->w, "BOARD STATUS", "24 UNITS / 128 CH EACH");

    for (i = 0; i < NBOARD; i ++) {
        int col = i % 4;
        int row = i / 4;
        int bx = 8 + col * 68;
        int by = 44 + row * 24;
        ipgui_color_t c;
        int abn = board_scan(i, &c);

        fill_rect(s, bx, by, 62, 20, COL_PANEL_FLAT, 255);
        stroke_round(s, bx, by, 62, 20, 0, 1, COL_BORDER_SOFT, 255);
        fill_rect(s, bx, by, 4, 20, c, 255);

        b[0] = 'B';
        b[1] = (char)('0' + (i + 1) / 10);
        b[2] = (char)('0' + (i + 1) % 10);
        b[3] = 0;
        draw_text(s, &open_sans_8px, b, bx + 9, by + 6, COL_TXT_2, 255);

        if (abn) {
            fmt_int(b, abn);
            draw_text_right(s, &open_sans_8px, b, bx + 56, by + 6, c, 255);
        } else {
            fill_rect(s, bx + 48, by + 8, 6, 6, COL_GREEN, 255);
        }
    }

    draw_line(s, 8, w->h - 26, w->w - 8, w->h - 26, 1, COL_BORDER_SOFT, 255,
              IPGUI_LINE_CAP_BUTT);
    draw_text(s, &open_sans_8px, "ONLINE  24 / 24", 8, w->h - 19, COL_TXT_3, 255);
    draw_text_right(s, &open_sans_8px, "ABNORMAL CH", w->w - 8, w->h - 19,
                    COL_TXT_3, 255);
}

/* --- 总览：运行条 --- */

#define RB_X 68
#define RB_Y 368
#define RB_W 722
#define RB_H 98

static void render_runbar(ipgui_widget_t * w, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_surf_t * s = ctx->surf;
    char b[32], c[32];
    s32_t total_min = 0, done_min = 0;
    s32_t pct;
    int i;
    int bar_x = 226, bar_w = 276, bar_y = 34;

    draw_panel(s, 0, 0, w->w, w->h, 0,
               COL_PANEL_TOP, COL_PANEL_BOT, COL_BORDER, 255);

    for (i = 0; i < 12; i ++) total_min += RCP[i].min;
    for (i = 0; i < A.active_step; i ++) done_min += RCP[i].min;
    done_min += (s32_t)(A.step_ms / 60000u);
    pct = total_min ? done_min * 100 / total_min : 0;

    draw_line(s, 210, 10, 210, w->h - 10, 1, COL_BORDER_SOFT, 255,
              IPGUI_LINE_CAP_BUTT);
    draw_line(s, 540, 10, 540, w->h - 10, 1, COL_BORDER_SOFT, 255,
              IPGUI_LINE_CAP_BUTT);

    /* 左：当前工艺步 */
    draw_text(s, &open_sans_8px, "ACTIVE STEP", 16, 12, COL_TXT_3, 255);

    fmt_pad2(b, A.active_step + 1);
    strcpy(c, "STEP ");
    strcat(c, b);
    strcat(c, " / 12");
    draw_text(s, &quicksand_medium_15px, c, 16, 22, COL_TXT, 255);

    fmt_int(b, RCP[A.active_step].v);
    strcat(b, " V  /  ");
    fmt_int(c, RCP[A.active_step].cur);
    strcat(b, c);
    strcat(b, ".0 A");
    draw_text(s, &open_sans_9px, b, 16, 46, COL_TXT_2, 255);

    fmt_hms(b, A.run_ms / 1000u);
    strcpy(c, "ELAPSED  ");
    strcat(c, b);
    draw_text(s, &open_sans_9px, c, 16, 62, COL_TXT_3, 255);

    fmt_int(b, RCP[A.active_step].min);
    strcat(b, " min / step");
    draw_text(s, &open_sans_9px, b, 16, 78, COL_TXT_3, 255);

    /* 中：总进度 */
    draw_text(s, &open_sans_8px, "AGING PROGRESS", bar_x, 12, COL_TXT_3, 255);

    fill_rect(s, bar_x, bar_y, bar_w, 10, COL_GRID, 255);
    if (pct > 0) {
        fill_rect(s, bar_x, bar_y, bar_w * pct / 100, 10, COL_CYAN, 255);
    }
    stroke_round(s, bar_x, bar_y, bar_w, 10, 0, 1, COL_BORDER, 255);

    fmt_int(b, pct);
    strcat(b, "%");
    draw_text(s, &quicksand_medium_12px, b, bar_x + bar_w + 12, bar_y - 3,
              COL_TXT, 255);

    fmt_int(b, done_min);
    strcat(b, " / ");
    fmt_int(c, total_min);
    strcat(b, c);
    strcat(b, " min");
    draw_text(s, &open_sans_9px, b, bar_x, bar_y + 20, COL_TXT_2, 255);

    draw_text(s, &open_sans_9px, "3 072 CHANNELS   /   24 BOARDS   /   RS485",
              bar_x, bar_y + 38, COL_TXT_3, 255);

    /* 右：运行状态 */
    {
        int bx = 556, by = 22;
        int run = A.running;

        fill_rect(s, bx, by, 150, 34, run ? rgb(235, 245, 237, 255)
                                          : rgb(250, 236, 235, 255), 255);
        stroke_round(s, bx, by, 150, 34, 0, 1,
                     run ? COL_GREEN : COL_RED, 255);
        draw_lamp(s, bx + 12, by + 12, 10, run ? COL_GREEN : COL_RED, 1);
        draw_text(s, &quicksand_medium_15px, run ? "RUNNING" : "STOPPED",
                  bx + 30, by + 8, run ? COL_GREEN : COL_RED, 255);

        fmt_int(b, A.warn_n);
        strcat(b, " CHANNEL WARNINGS");
        draw_text(s, &open_sans_9px, b, bx, by + 46,
                  A.warn_n ? COL_AMBER : COL_TXT_3, 255);

        fmt_int(b, A.imp_n);
        strcat(b, " IMPLOSION EVENTS");
        draw_text(s, &open_sans_9px, b, bx, by + 62,
                  A.imp_n ? COL_RED : COL_TXT_3, 255);
    }
}

/* ==========================================================================
 *  页面 1：阵列图
 * ========================================================================== */

#define MAP_X 68
#define MAP_Y 56
#define MAP_W 556
#define MAP_H 402

#define MAP_PAD_X 10
#define MAP_PAD_Y 34
#define MAP_LBL_W 24
#define MAP_CW 3
#define MAP_CH 9
#define MAP_GX 1
#define MAP_GY 4
#define MAP_CELL_X0 (MAP_PAD_X + MAP_LBL_W)
#define MAP_CELL_Y0 (MAP_PAD_Y + 6)

#define DET_X 636
#define DET_Y 56
#define DET_W 154
#define DET_H 402

static ipgui_color_t ch_color(u8_t st)
{
    if (st == CH_BAD)  return COL_RED;
    if (st == CH_WARN) return COL_AMBER;
    if (st == CH_OFF)  return COL_OFF_FILL;
    return COL_OK_FILL;
}

static void render_map(ipgui_widget_t * w, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_surf_t * s = ctx->surf;
    int b, c;
    char t[8];

    draw_panel(s, 0, 0, w->w, w->h, 0,
               COL_PANEL_TOP, COL_PANEL_BOT, COL_BORDER, 255);
    panel_header(s, w->w, "CHANNEL ARRAY", "3072 CHANNELS / 24 BOARDS");

    for (b = 0; b < NBOARD; b ++) {
        int ry = MAP_CELL_Y0 + b * (MAP_CH + MAP_GY);

        t[0] = 'B';
        t[1] = (char)('0' + (b + 1) / 10);
        t[2] = (char)('0' + (b + 1) % 10);
        t[3] = 0;
        draw_text(s, &open_sans_7px, t, MAP_PAD_X, ry + 1, COL_TXT_3, 255);

        for (c = 0; c < NCH; c ++) {
            u16_t idx = (u16_t)(b * NCH + c);
            int cx = MAP_CELL_X0 + c * (MAP_CW + MAP_GX);

            fill_rect(s, cx, ry, MAP_CW, MAP_CH, ch_color(s_st[idx]), 255);
        }
    }

    /* 选中 / 悬停 */
    {
        u16_t hl[2];
        int k;

        hl[0] = A.sel_ch;
        hl[1] = A.hov_ch;

        for (k = 0; k < 2; k ++) {
            int hb, hc, hx, hy;

            if (hl[k] >= NTOT) continue;

            hb = hl[k] / NCH;
            hc = hl[k] % NCH;
            hx = MAP_CELL_X0 + hc * (MAP_CW + MAP_GX);
            hy = MAP_CELL_Y0 + hb * (MAP_CH + MAP_GY);

            if (k == 0) {
                stroke_round(s, hx - 2, hy - 2, MAP_CW + 4, MAP_CH + 4, 0, 1,
                             COL_TXT, 255);
            } else if (hl[1] != A.sel_ch) {
                stroke_round(s, hx - 1, hy - 1, MAP_CW + 2, MAP_CH + 2, 0, 1,
                             COL_CYAN, 255);
            }
        }
    }

    /* 图例 */
    {
        int lx = 10, ly = w->h - 22;
        const char * names[3] = { "NORMAL", "WARNING", "IMPLOSION" };
        ipgui_color_t cols[3];
        int i;

        cols[0] = COL_OK_FILL;
        cols[1] = COL_AMBER;
        cols[2] = COL_RED;

        for (i = 0; i < 3; i ++) {
            fill_rect(s, lx, ly + 2, 8, 8, cols[i], 255);
            stroke_round(s, lx, ly + 2, 8, 8, 0, 1, COL_BORDER_SOFT, 255);
            draw_text(s, &open_sans_8px, names[i], lx + 12, ly,
                      COL_TXT_3, 255);
            lx += 18 + text_w(&open_sans_8px, names[i]) + 16;
        }
    }
}

static void render_detail(ipgui_widget_t * w, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_surf_t * s = ctx->surf;
    u16_t idx = A.sel_ch;
    char b[24], id[24];
    int tw;
    const char * nm = "NORMAL";
    ipgui_color_t sc = COL_GREEN;

    fmt_pad2(b, idx / NCH + 1);
    strcpy(id, "B");
    strcat(id, b);
    strcat(id, " / C");
    fmt_pad2(b, idx % NCH + 1);
    strcat(id, b);

    draw_panel(s, 0, 0, w->w, w->h, 0,
               COL_PANEL_TOP, COL_PANEL_BOT, COL_BORDER, 255);
    panel_header(s, w->w, "CHANNEL DETAIL", id);

    /* 电压 */
    draw_text(s, &open_sans_8px, "VOLTAGE", 12, 44, COL_TXT_3, 255);
    fmt_dec1(b, s_v[idx]);
    draw_text(s, &quicksand_medium_26px, b, 11, 54, COL_TXT, 255);
    tw = text_w(&quicksand_medium_26px, b);
    draw_text(s, &open_sans_9px, "V", 15 + tw, 68, COL_TXT_3, 255);
    draw_line(s, 12, 92, w->w - 12, 92, 1, COL_BORDER_SOFT, 255,
              IPGUI_LINE_CAP_BUTT);

    /* 电流 */
    draw_text(s, &open_sans_8px, "LEAKAGE", 12, 100, COL_TXT_3, 255);
    fmt_dec2(b, s_i[idx]);
    draw_text(s, &quicksand_medium_18px, b, 11, 110, COL_TXT, 255);
    tw = text_w(&quicksand_medium_18px, b);
    draw_text(s, &open_sans_9px, "mA", 15 + tw, 118, COL_TXT_3, 255);
    draw_line(s, 12, 138, w->w - 12, 138, 1, COL_BORDER_SOFT, 255,
              IPGUI_LINE_CAP_BUTT);

    /* 内爆 */
    draw_text(s, &open_sans_8px, "IMPLOSIONS", 12, 146, COL_TXT_3, 255);
    fmt_int(b, s_imp[idx]);
    draw_text(s, &quicksand_medium_18px, b, 11, 156,
              s_imp[idx] ? COL_RED : COL_TXT, 255);
    tw = text_w(&quicksand_medium_18px, b);
    draw_text(s, &open_sans_9px, "/ 4", 15 + tw, 164, COL_TXT_3, 255);
    draw_line(s, 12, 184, w->w - 12, 184, 1, COL_BORDER_SOFT, 255,
              IPGUI_LINE_CAP_BUTT);

    /* 状态 */
    if (s_st[idx] == CH_WARN) { nm = "WARNING"; sc = COL_AMBER; }
    if (s_st[idx] == CH_BAD)  { nm = "FAILED";  sc = COL_RED; }
    if (s_st[idx] == CH_OFF)  { nm = "DISABLED"; sc = COL_TXT_3; }

    draw_text(s, &open_sans_8px, "STATE", 12, 192, COL_TXT_3, 255);
    fill_rect(s, 12, 204, 80, 20, sc, 22);
    stroke_round(s, 12, 204, 80, 20, 0, 1, sc, 255);
    draw_lamp(s, 17, 210, 8, sc, 1);
    draw_text(s, &open_sans_9px, nm, 30, 209, sc, 255);

    draw_line(s, 12, 236, w->w - 12, 236, 1, COL_BORDER_SOFT, 255,
              IPGUI_LINE_CAP_BUTT);

    /* 板卡信息 */
    {
        int bno = idx / NCH;
        fmt_pad2(b, bno + 1);
        {
            char t[24];
            strcpy(t, "BOARD B");
            strcat(t, b);
            draw_text(s, &open_sans_8px, t, 12, 244, COL_TXT_3, 255);
        }

        draw_text(s, &open_sans_9px, "ENABLED", 12, 258, COL_GREEN, 255);

        fmt_int(b, A.warn_n);
        strcat(b, " WARN (TOTAL)");
        draw_text(s, &open_sans_8px, b, 12, 278, COL_TXT_3, 255);

        fmt_int(b, A.imp_n);
        strcat(b, " IMPLOSION");
        draw_text(s, &open_sans_8px, b, 12, 292, COL_TXT_3, 255);
    }

    /* 底部提示 */
    draw_line(s, 12, w->h - 32, w->w - 12, w->h - 32, 1, COL_BORDER_SOFT, 255,
              IPGUI_LINE_CAP_BUTT);
    draw_text(s, &open_sans_8px, "CLICK A CELL TO", 12, w->h - 24,
              COL_TXT_3, 255);
    draw_text(s, &open_sans_8px, "INSPECT CHANNEL", 12, w->h - 14,
              COL_TXT_3, 255);
}

/* ==========================================================================
 *  页面 2：趋势
 * ========================================================================== */

#define TC_X 68
#define TC_Y 56
#define TC_W 468
#define TC_H 402

#define HI_X 548
#define HI_Y 56
#define HI_W 242
#define HI_H 190

#define OF_X 548
#define OF_Y 254
#define OF_W 242
#define OF_H 204

static void render_trend_big(ipgui_widget_t * w, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_surf_t * s = ctx->surf;
    int px = 46, py = 58, pw = w->w - 68, ph = w->h - 122;
    char b[24];

    draw_panel(s, 0, 0, w->w, w->h, 0,
               COL_PANEL_TOP, COL_PANEL_BOT, COL_BORDER, 255);
    panel_header(s, w->w, "AGING CURVE", "VOLTAGE / LEAKAGE HISTORY");

    fill_rect(s, w->w - 176, 12, 8, 8, COL_CYAN, 255);
    draw_text(s, &open_sans_8px, "BUS VOLTAGE", w->w - 164, 11, COL_TXT_3, 255);
    fill_rect(s, w->w - 84, 12, 8, 8, COL_VIOLET, 255);
    draw_text(s, &open_sans_8px, "LEAKAGE", w->w - 72, 11, COL_TXT_3, 255);

    chart_grid(s, px, py, pw, ph, 5);

    /* Y 轴刻度 */
    {
        int i;
        for (i = 0; i <= 5; i ++) {
            s32_t v = 3200 - 3200 * i / 5;
            fmt_int(b, v / 10);
            draw_text_right(s, &open_sans_8px, b, px - 6, py + ph * i / 5 - 5,
                            COL_TXT_3, 255);
        }
    }

    polyline_from(s, px, py, pw, ph, A.curve_v, TREND_N, A.head,
                  0, 3200, COL_CYAN, 2);
    polyline_from(s, px, py, pw, ph, A.curve_i, TREND_N, A.head,
                  0, 320, COL_VIOLET, 1);

    /* 末端标记 */
    {
        int yy = py + ph - (int)(A.bus_x10 * ph / 3200);
        yy = clamp(yy, py, py + ph);
        fill_rect(s, px + pw - 3, yy - 3, 6, 6, COL_CYAN, 255);
        stroke_round(s, px + pw - 3, yy - 3, 6, 6, 0, 1, COL_TXT, 255);
    }

    draw_text(s, &open_sans_8px, "-24 s", px, py + ph + 8, COL_TXT_3, 255);
    draw_text_right(s, &open_sans_8px, "NOW", px + pw, py + ph + 8,
                    COL_TXT_3, 255);

    /* 底部读数 */
    {
        int by = w->h - 34;
        draw_line(s, 12, by - 8, w->w - 12, by - 8, 1, COL_BORDER_SOFT, 255,
                  IPGUI_LINE_CAP_BUTT);

        draw_text(s, &open_sans_8px, "BUS", 14, by, COL_TXT_3, 255);
        fmt_dec1(b, A.bus_x10);
        strcat(b, " V");
        draw_text(s, &quicksand_medium_14px, b, 14, by + 10, COL_CYAN, 255);

        draw_text(s, &open_sans_8px, "LEAKAGE", 128, by, COL_TXT_3, 255);
        fmt_dec2(b, A.leak_x100);
        strcat(b, " mA");
        draw_text(s, &quicksand_medium_14px, b, 128, by + 10, COL_VIOLET, 255);

        draw_text(s, &open_sans_8px, "STEP", 250, by, COL_TXT_3, 255);
        fmt_pad2(b, A.active_step + 1);
        strcat(b, " / 12");
        draw_text(s, &quicksand_medium_14px, b, 250, by + 10, COL_TXT, 255);
    }
}

static void render_hist(ipgui_widget_t * w, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_surf_t * s = ctx->surf;
    int i;
    s32_t maxv = 1;
    char b[24];
    int px = 14, py = 50, pw = w->w - 28, ph = w->h - 78;
    int bw = pw / 16;

    draw_panel(s, 0, 0, w->w, w->h, 0,
               COL_PANEL_TOP, COL_PANEL_BOT, COL_BORDER, 255);
    panel_header(s, w->w, "DISTRIBUTION", "CHANNEL VOLTAGE");

    for (i = 0; i < 16; i ++) if (A.hist[i] > maxv) maxv = A.hist[i];

    for (i = 0; i < 16; i ++) {
        int h = (int)(A.hist[i] * ph / maxv);
        int x = px + i * bw;
        int y = py + ph - h;
        ipgui_color_t c = COL_CYAN;

        if (h < 1) h = 1;
        if (i < 2) c = COL_AMBER;
        if (i > 13) c = COL_VIOLET;

        fill_rect(s, x + 1, y, bw - 2, h, c, 235);
        fill_rect(s, x + 1, y, bw - 2, 1, rgb(255, 255, 255, 255), 90);
    }

    draw_line(s, px, py + ph, px + pw, py + ph, 1, COL_BORDER, 255,
              IPGUI_LINE_CAP_BUTT);

    fmt_dec1(b, (A.bus_x10 - 40) / 10);
    draw_text(s, &open_sans_8px, b, px, py + ph + 6, COL_TXT_3, 255);
    fmt_dec1(b, (A.bus_x10 + 40) / 10);
    draw_text_right(s, &open_sans_8px, b, px + pw, py + ph + 6, COL_TXT_3, 255);
}

static void render_offenders(ipgui_widget_t * w, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_surf_t * s = ctx->surf;
    int i;
    char b[24];

    draw_panel(s, 0, 0, w->w, w->h, 0,
               COL_PANEL_TOP, COL_PANEL_BOT, COL_BORDER, 255);
    panel_header(s, w->w, "TOP OFFENDERS", "HIGHEST LEAKAGE");

    for (i = 0; i < 5; i ++) {
        u16_t idx = A.worst[i];
        int ry = 44 + i * 30;

        draw_line(s, 10, ry + 28, w->w - 10, ry + 28, 1, COL_BORDER_SOFT, 255,
                  IPGUI_LINE_CAP_BUTT);

        draw_text(s, &open_sans_9px, "B", 12, ry + 7, COL_TXT_3, 255);
        fmt_pad2(b, idx / NCH + 1);
        draw_text(s, &open_sans_9px, b, 21, ry + 7, COL_TXT_2, 255);
        draw_text(s, &open_sans_9px, "C", 40, ry + 7, COL_TXT_3, 255);
        fmt_pad2(b, idx % NCH + 1);
        draw_text(s, &open_sans_9px, b, 49, ry + 7, COL_TXT_2, 255);

        fmt_dec2(b, s_i[idx]);
        draw_text_right(s, &quicksand_medium_12px, b, w->w - 40, ry + 5,
                        i == 0 ? COL_AMBER : COL_TXT, 255);
        draw_text(s, &open_sans_8px, "mA", w->w - 36, ry + 8, COL_TXT_3, 255);
    }
}

/* ==========================================================================
 *  页面 3：配方
 * ========================================================================== */

#define PR_X 68
#define PR_Y 56
#define PR_W 350
#define PR_H 190

#define PC_X 68
#define PC_Y 254
#define PC_W 350
#define PC_H 204

#define ST_X 430
#define ST_Y 56
#define ST_W 358
#define ST_H 402

#define ST_ROW_H 28
#define ST_ROW_Y0 54

static void render_profile(ipgui_widget_t * w, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_surf_t * s = ctx->surf;
    int px = 34, py = 48, pw = w->w - 50, ph = w->h - 78;
    int i;
    int acc = 0;
    s32_t total = 0;
    int px_prev = 0, py_prev = 0;
    char b[24];

    draw_panel(s, 0, 0, w->w, w->h, 0,
               COL_PANEL_TOP, COL_PANEL_BOT, COL_BORDER, 255);
    panel_header(s, w->w, "PROFILE", "VOLTAGE VS TIME");

    for (i = 0; i < 12; i ++) total += RCP[i].min;

    chart_grid(s, px, py, pw, ph, 4);

    for (i = 0; i < 12; i ++) {
        int x0 = px + pw * acc / total;
        int x1 = px + pw * (acc + RCP[i].min) / total;
        int y  = py + ph - (int)(RCP[i].v * ph / 320);

        if (i == 0) {
            px_prev = x0;
            py_prev = py + ph - (int)(RCP[0].v * ph / 320);
        }

        if (i == A.active_step) {
            fill_rect(s, x0, py, x1 - x0, ph, COL_CYAN, 26);
        }

        draw_line(s, px_prev, py_prev, x0, y, 1, COL_CYAN, 160,
                  IPGUI_LINE_CAP_BUTT);
        draw_line(s, x0, y, x1, y, 2,
                  (i == A.active_step) ? COL_CYAN : rgb(96, 126, 152, 255),
                  255, IPGUI_LINE_CAP_BUTT);

        draw_line(s, x1, y, x1, py + ph, 1, COL_GRID, 255,
                  IPGUI_LINE_CAP_BUTT);

        px_prev = x1;
        py_prev = y;
        acc += RCP[i].min;
    }

    draw_line(s, px, py + ph, px + pw, py + ph, 1, COL_BORDER, 255,
              IPGUI_LINE_CAP_BUTT);

    fmt_int(b, 320);
    draw_text(s, &open_sans_8px, b, 6, py - 4, COL_TXT_3, 255);
    draw_text(s, &open_sans_8px, "0", 20, py + ph - 10, COL_TXT_3, 255);

    fmt_int(b, total);
    strcat(b, " min");
    draw_text_right(s, &open_sans_8px, b, w->w - 12, w->h - 15, COL_TXT_3, 255);
}

static void btn_box(ipgui_surf_t * s, int x, int y, int w, int h, const char * t)
{
    fill_rect(s, x, y, w, h, rgb(244, 246, 249, 255), 255);
    stroke_round(s, x, y, w, h, 0, 1, COL_BORDER, 255);
    draw_text_center(s, &quicksand_medium_14px, t, x + w / 2, y + (h - 17) / 2,
                     COL_TXT_2, 255);
}

#define PB_L_X 196
#define PB_R_X 292
#define PB_W   44
#define PB_H   24
#define PB_V_Y 40
#define PB_M_Y 78

static void render_params(ipgui_widget_t * w, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_surf_t * s = ctx->surf;
    char b[24];

    draw_panel(s, 0, 0, w->w, w->h, 0,
               COL_PANEL_TOP, COL_PANEL_BOT, COL_BORDER, 255);
    panel_header(s, w->w, "STEP PARAMETERS", "STEP EDITOR");

    /* 电压 */
    draw_text(s, &open_sans_9px, "TARGET VOLTAGE", 14, PB_V_Y + 5, COL_TXT_2, 255);
    fmt_int(b, RCP[A.sel_step].v);
    strcat(b, " V");
    draw_text_right(s, &quicksand_medium_14px, b, PB_R_X - 8, PB_V_Y + 1,
                    COL_CYAN, 255);
    btn_box(s, PB_L_X, PB_V_Y, PB_W, PB_H, "-");
    btn_box(s, PB_R_X, PB_V_Y, PB_W, PB_H, "+");
    draw_line(s, 14, PB_V_Y + 32, w->w - 14, PB_V_Y + 32, 1,
              COL_BORDER_SOFT, 255, IPGUI_LINE_CAP_BUTT);

    /* 时长 */
    draw_text(s, &open_sans_9px, "DURATION", 14, PB_M_Y + 5, COL_TXT_2, 255);
    fmt_int(b, RCP[A.sel_step].min);
    strcat(b, " min");
    draw_text_right(s, &quicksand_medium_14px, b, PB_R_X - 8, PB_M_Y + 1,
                    COL_TXT, 255);
    btn_box(s, PB_L_X, PB_M_Y, PB_W, PB_H, "-");
    btn_box(s, PB_R_X, PB_M_Y, PB_W, PB_H, "+");
    draw_line(s, 14, PB_M_Y + 32, w->w - 14, PB_M_Y + 32, 1,
              COL_BORDER_SOFT, 255, IPGUI_LINE_CAP_BUTT);

    /* 系统参数（来自 SystemConfigure.xml） */
    {
        const char * k[4] = { "CHARGE RATE", "DISCHARGE", "IMPLOSION LIMIT", "TEMP TOLERANCE" };
        const char * v[4] = { "70 V/s", "3 s", "4 times", "5.0 C" };
        int i;

        for (i = 0; i < 4; i ++) {
            int ry = 120 + i * 20;
            draw_text(s, &open_sans_8px, k[i], 14, ry, COL_TXT_3, 255);
            draw_text_right(s, &open_sans_9px, v[i], w->w - 14, ry - 1,
                            COL_TXT_2, 255);
        }
    }
}

static void render_steps(ipgui_widget_t * w, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_surf_t * s = ctx->surf;
    int i;
    char b[16];

    draw_panel(s, 0, 0, w->w, w->h, 0,
               COL_PANEL_TOP, COL_PANEL_BOT, COL_BORDER, 255);
    panel_header(s, w->w, "RECIPE", "AGING PROGRAM (12 STEPS)");

    /* 表头 */
    fill_rect(s, 1, 35, w->w - 2, 18, COL_PANEL_FLAT, 255);
    draw_text(s, &open_sans_7px, "#", 14, 41, COL_TXT_3, 255);
    draw_text(s, &open_sans_7px, "VOLTAGE", 60, 41, COL_TXT_3, 255);
    draw_text(s, &open_sans_7px, "MIN", 130, 41, COL_TXT_3, 255);
    draw_text(s, &open_sans_7px, "HEAT", 172, 41, COL_TXT_3, 255);
    draw_text(s, &open_sans_7px, "PROGRESS", 226, 41, COL_TXT_3, 255);
    draw_line(s, 1, 53, w->w - 2, 53, 1, COL_BORDER_SOFT, 255,
              IPGUI_LINE_CAP_BUTT);

    for (i = 0; i < 12; i ++) {
        int ry = ST_ROW_Y0 + i * ST_ROW_H;
        int active = (i == A.active_step);
        int sel = (i == A.sel_step);

        if (sel) {
            fill_rect(s, 1, ry, w->w - 2, ST_ROW_H, rgb(233, 241, 248, 255), 255);
        }
        if (active) {
            fill_rect(s, 1, ry, 3, ST_ROW_H, COL_CYAN, 255);
        }

        draw_line(s, 1, ry + ST_ROW_H, w->w - 2, ry + ST_ROW_H, 1,
                  COL_GRID, 255, IPGUI_LINE_CAP_BUTT);

        fmt_pad2(b, i + 1);
        draw_text(s, &open_sans_9px, b, 20, ry + 8,
                  active ? COL_CYAN : COL_TXT_3, 255);

        fmt_int(b, RCP[i].v);
        strcat(b, " V");
        draw_text(s, &open_sans_9px, b, 60, ry + 8, COL_TXT_2, 255);

        fmt_int(b, RCP[i].min);
        draw_text(s, &open_sans_9px, b, 134, ry + 8, COL_TXT_2, 255);

        if (RCP[i].heat) {
            fmt_int(b, RCP[i].temp);
            strcat(b, "C");
            draw_text(s, &open_sans_9px, b, 172, ry + 8, COL_AMBER, 255);
        } else {
            draw_text(s, &open_sans_9px, "-", 176, ry + 8, COL_TXT_3, 255);
        }

        /* 进度 */
        {
            int bx = 226, bw = 92;
            s32_t p = 0;

            if (i < A.active_step) p = 100;
            else if (i == A.active_step) {
                s32_t lm = (s32_t)RCP[i].min * 60 * 1000;
                p = lm ? (s32_t)(A.step_ms * 100 / (u32_t)lm) : 0;
                p = clamp(p, 0, 100);
            }

            fill_rect(s, bx, ry + 11, bw, 6, COL_GRID, 255);
            if (p > 0) {
                fill_rect(s, bx, ry + 11, (bw * p) / 100, 6,
                          active ? COL_CYAN : COL_GREEN, 255);
            }
        }
    }
}

/* ==========================================================================
 *  页面 4：事件日志
 * ========================================================================== */

#define LG_X 68
#define LG_Y 56
#define LG_W 722
#define LG_H 402

typedef struct {
    const char * t;
    const char * lv;
    const char * msg;
    const char * ch;
} log_t;

#define LOG_N 12

static const log_t LOGS[LOG_N] = {
    { "14:32:08", "INFO", "STEP 07 START  VOLTAGE 280V",        "--"      },
    { "14:31:52", "WARN", "CHANNEL VOLTAGE LOW  276.4V",        "B07/C042" },
    { "14:29:11", "FAIL", "IMPLOSION DETECTED  COUNT 2",        "B12/C018" },
    { "14:27:45", "INFO", "OVEN TEMP REACHED  55.0C",           "--"      },
    { "14:21:30", "WARN", "LEAKAGE OVER LIMIT  2.48 mA",        "B19/C103" },
    { "14:15:02", "INFO", "STEP 06 START  VOLTAGE 260V",        "--"      },
    { "14:09:58", "FAIL", "IMPLOSION DETECTED  COUNT 1",        "B03/C077" },
    { "13:58:20", "INFO", "RS485 LINK ESTABLISHED  24 BOARDS",  "--"      },
    { "13:44:07", "WARN", "BOARD 21 TEMP DEVIATION  6.2C",      "B21"     },
    { "13:12:55", "INFO", "RECIPE LOADED  12 STEPS",            "--"      },
    { "13:02:11", "FAIL", "CHANNEL OPEN CIRCUIT",               "B16/C090" },
    { "12:58:30", "INFO", "SYSTEM POWER ON  SELF TEST PASS",    "--"      },
};

#define LOG_ROW_H 34
#define LOG_ROW_Y0 58

#define LOG_CHIP_W 52
#define LOG_CHIP_H 20
#define LOG_CHIP_GAP 6
#define LOG_CHIP_Y 7

static int log_chip_x(int i, int w)
{
    return w - (3 * LOG_CHIP_W + 2 * LOG_CHIP_GAP) - 12 + i * (LOG_CHIP_W + LOG_CHIP_GAP);
}

static void render_log(ipgui_widget_t * w, ipgui_widget_render_ctx_t * ctx)
{
    ipgui_surf_t * s = ctx->surf;
    int i, shown = 0;
    char nb[24];
    const char * chip[3] = { "ALL", "WARN", "FAIL" };

    draw_panel(s, 0, 0, w->w, w->h, 0,
               COL_PANEL_TOP, COL_PANEL_BOT, COL_BORDER, 255);
    panel_header(s, w->w, "EVENT LOG", "SYSTEM EVENTS");

    /* 过滤按钮 */
    for (i = 0; i < 3; i ++) {
        int cx = log_chip_x(i, w->w);
        int on = (A.log_filter == i);
        ipgui_color_t c = (i == 1) ? COL_AMBER : (i == 2) ? COL_RED : COL_CYAN;

        fill_rect(s, cx, LOG_CHIP_Y, LOG_CHIP_W, LOG_CHIP_H,
                  on ? c : rgb(246, 247, 249, 255), 255);
        stroke_round(s, cx, LOG_CHIP_Y, LOG_CHIP_W, LOG_CHIP_H, 0, 1,
                     on ? c : COL_BORDER, 255);
        draw_text_center(s, &open_sans_8px, chip[i], cx + LOG_CHIP_W / 2,
                         LOG_CHIP_Y + 6, on ? rgb(255, 255, 255, 255)
                                            : COL_TXT_3, 255);
    }

    /* 表头 */
    fill_rect(s, 1, 35, w->w - 2, 18, COL_PANEL_FLAT, 255);
    draw_text(s, &open_sans_7px, "TIME", 18, 41, COL_TXT_3, 255);
    draw_text(s, &open_sans_7px, "LEVEL", 92, 41, COL_TXT_3, 255);
    draw_text(s, &open_sans_7px, "CHANNEL", 168, 41, COL_TXT_3, 255);
    draw_text(s, &open_sans_7px, "MESSAGE", 254, 41, COL_TXT_3, 255);
    draw_line(s, 1, 53, w->w - 2, 53, 1, COL_BORDER_SOFT, 255,
              IPGUI_LINE_CAP_BUTT);

    for (i = 0; i < LOG_N && shown < 9; i ++) {
        int ry;
        ipgui_color_t c = COL_GREEN;
        int lw = 46;

        if (A.log_filter == 1 && strcmp(LOGS[i].lv, "WARN") != 0) continue;
        if (A.log_filter == 2 && strcmp(LOGS[i].lv, "FAIL") != 0) continue;

        ry = LOG_ROW_Y0 + shown * LOG_ROW_H;

        if (strcmp(LOGS[i].lv, "WARN") == 0) { c = COL_AMBER; lw = 46; }
        if (strcmp(LOGS[i].lv, "FAIL") == 0) { c = COL_RED;   lw = 40; }

        if (shown & 1) {
            fill_rect(s, 1, ry, w->w - 2, LOG_ROW_H, rgb(249, 250, 252, 255), 255);
        }
        draw_line(s, 10, ry + LOG_ROW_H - 1, w->w - 10, ry + LOG_ROW_H - 1,
                  1, COL_GRID, 255, IPGUI_LINE_CAP_BUTT);

        draw_text(s, &open_sans_10px, LOGS[i].t, 18, ry + 10, COL_TXT_2, 255);

        fill_rect(s, 92, ry + 7, lw, 18, c, 28);
        stroke_round(s, 92, ry + 7, lw, 18, 0, 1, c, 180);
        draw_text_center(s, &open_sans_8px, LOGS[i].lv, 92 + lw / 2, ry + 12,
                         c, 255);

        draw_text(s, &open_sans_9px, LOGS[i].ch, 168, ry + 10, COL_TXT_3, 255);
        draw_text(s, &open_sans_10px, LOGS[i].msg, 254, ry + 10, COL_TXT, 255);

        shown ++;
    }

    draw_line(s, 10, w->h - 24, w->w - 10, w->h - 24, 1, COL_BORDER_SOFT, 255,
              IPGUI_LINE_CAP_BUTT);
    fmt_int(nb, shown);
    strcat(nb, " EVENTS SHOWN");
    draw_text_right(s, &open_sans_8px, nb, w->w - 12, w->h - 17,
                    COL_TXT_3, 255);
}

static void log_evt(ipgui_widget_t * w, ipgui_widget_evt_t * e)
{
    int i;

    if (e->type != IPGUI_WIDGET_EVENT_RELEASED) return;

    if (e->evt.released_evt.y < LOG_CHIP_Y ||
        e->evt.released_evt.y > LOG_CHIP_Y + LOG_CHIP_H) return;

    for (i = 0; i < 3; i ++) {
        int cx = log_chip_x(i, w->w);
        if (e->evt.released_evt.x >= cx &&
            e->evt.released_evt.x <= cx + LOG_CHIP_W) {
            if (A.log_filter != (u8_t)i) {
                A.log_filter = (u8_t)i;
                ipgui_widget_mark_dirty(w);
            }
            return;
        }
    }
}

/* ==========================================================================
 *  事件处理
 * ========================================================================== */

static int nav_at(ipgui_coord_t y)
{
    int i = (y - NAV_ITEM_Y0) / NAV_ITEM_H;
    if (i < 0 || i >= PAGE_COUNT) return -1;
    return i;
}

static void nav_evt(ipgui_widget_t * w, ipgui_widget_evt_t * e)
{
    int i;

    if (e->type == IPGUI_WIDGET_EVENT_HOVER) {
        i = nav_at(e->evt.hover_evt.y);
        if (i >= 0 && A.nav_hover != (u8_t)i) {
            A.nav_hover = (u8_t)i;
            ipgui_widget_mark_dirty(w);
        }
    } else if (e->type == IPGUI_WIDGET_EVENT_RELEASED) {
        i = nav_at(e->evt.released_evt.y);
        if (i >= 0 && i != (int)A.page) {
            A.page = (u8_t)i;
            ipgui_screen_page_switch(&main_screen, (u8_t)i);
        }
    } else if (e->type == IPGUI_WIDGET_EVENT_PRESSED) {
        ipgui_widget_mark_dirty(w);
    }
}

static ipgui_widget_t * g_map;
static ipgui_widget_t * g_detail;
static ipgui_widget_t * g_steps;
static ipgui_widget_t * g_params;

static u16_t map_hit(ipgui_coord_t x, ipgui_coord_t y)
{
    int col, row;

    if (x < MAP_CELL_X0 || y < MAP_CELL_Y0) return 0xFFFF;

    col = (x - MAP_CELL_X0) / (MAP_CW + MAP_GX);
    row = (y - MAP_CELL_Y0) / (MAP_CH + MAP_GY);

    if (col < 0 || col >= NCH) return 0xFFFF;
    if (row < 0 || row >= NBOARD) return 0xFFFF;

    return (u16_t)(row * NCH + col);
}

static void map_evt(ipgui_widget_t * w, ipgui_widget_evt_t * e)
{
    u16_t idx;

    if (e->type == IPGUI_WIDGET_EVENT_HOVER) {
        idx = map_hit(e->evt.hover_evt.x, e->evt.hover_evt.y);
        if (idx != A.hov_ch) {
            A.hov_ch = idx;
            ipgui_widget_mark_dirty(w);
        }
    } else if (e->type == IPGUI_WIDGET_EVENT_RELEASED) {
        idx = map_hit(e->evt.released_evt.x, e->evt.released_evt.y);
        if (idx < NTOT) {
            A.sel_ch = idx;
            ipgui_widget_mark_dirty(w);
            if (g_detail) ipgui_widget_mark_dirty(g_detail);
        }
    }
}

static void steps_evt(ipgui_widget_t * w, ipgui_widget_evt_t * e)
{
    if (e->type == IPGUI_WIDGET_EVENT_RELEASED) {
        int y = e->evt.released_evt.y;

        if (y >= ST_ROW_Y0) {
            int row = (y - ST_ROW_Y0) / ST_ROW_H;
            if (row >= 0 && row < 12 && row != (int)A.sel_step) {
                A.sel_step = (u8_t)row;
                ipgui_widget_mark_dirty(w);
                if (g_params) ipgui_widget_mark_dirty(g_params);
                if (w->priv_data) ipgui_widget_mark_dirty((ipgui_widget_t *)w->priv_data);
            }
        }
    }
}

/* 配方参数按钮：局部命中，避免为两个按钮单独建控件 */
static void params_evt(ipgui_widget_t * w, ipgui_widget_evt_t * e)
{
    int x, y;
    int hit_v = 0, m = 0;

    if (e->type != IPGUI_WIDGET_EVENT_RELEASED) return;

    x = e->evt.released_evt.x;
    y = e->evt.released_evt.y;

    if (y >= PB_V_Y && y <= PB_V_Y + PB_H) hit_v = 1;
    else if (y >= PB_M_Y && y <= PB_M_Y + PB_H) hit_v = 0;
    else return;

    if (x >= PB_L_X && x <= PB_L_X + PB_W) m = -1;
    else if (x >= PB_R_X && x <= PB_R_X + PB_W) m = 1;
    else return;

    if (hit_v) {
        s32_t v = (s32_t)RCP[A.sel_step].v + m * 10;
        RCP[A.sel_step].v = (s16_t)clamp(v, 0, 800);
    } else {
        s32_t v = (s32_t)RCP[A.sel_step].min + m * 5;
        RCP[A.sel_step].min = (u8_t)clamp(v, 0, 255);
    }

    ipgui_widget_mark_dirty(w);
    if (g_steps) ipgui_widget_mark_dirty(g_steps);
}

/* ==========================================================================
 *  构建
 * ========================================================================== */

/* 每页需要单独刷新的控件（用于提交脏矩形） */
static ipgui_widget_t * W_top[PAGE_COUNT];

static ipgui_widget_t * W_ov_kpi;
static ipgui_widget_t * W_ov_trend;
static ipgui_widget_t * W_ov_boards;
static ipgui_widget_t * W_ov_run;

static ipgui_widget_t * W_ar_detail;

static ipgui_widget_t * W_tr_chart;
static ipgui_widget_t * W_tr_hist;
static ipgui_widget_t * W_tr_off;

static ipgui_widget_t * W_rc_profile;

static ipgui_widget_t * mk(ipgui_widget_t * parent,
                           int x, int y, int w, int h,
                           void (*render)(ipgui_widget_t *, ipgui_widget_render_ctx_t *),
                           void (*evt)(ipgui_widget_t *, ipgui_widget_evt_t *),
                           void * priv, const char * name)
{
    ipgui_widget_t * wd = ipgui_widget_create(parent);

    wd->x = x; wd->y = y;
    wd->w = w; wd->h = h;
    wd->render = render;
    wd->event_handler = evt;
    wd->priv_data = priv;
    wd->name = name;

    return wd;
}

static void build_overview(void)
{
    W_top[PAGE_OVERVIEW] = mk(NULL, 0, 0, SCR_W, TOP_H, render_top, NULL, NULL, "top");
    mk(NULL, 0, TOP_H, NAV_W, SCR_H - TOP_H, render_nav, nav_evt,
       (void *)(intptr_t)PAGE_OVERVIEW, "nav");

    W_ov_kpi    = mk(NULL, KPI_X, KPI_Y, KPI_W, KPI_H, render_kpi, NULL, NULL, "kpi");
    W_ov_trend  = mk(NULL, TR_X, TR_Y, TR_W, TR_H, render_trend_small, NULL, NULL, "trend");
    W_ov_boards = mk(NULL, BD_X, BD_Y, BD_W, BD_H, render_boards, NULL, NULL, "boards");
    W_ov_run    = mk(NULL, RB_X, RB_Y, RB_W, RB_H, render_runbar, NULL, NULL, "runbar");
}

static void build_array(void)
{
    W_top[PAGE_ARRAY] = mk(NULL, 0, 0, SCR_W, TOP_H, render_top, NULL, NULL, "top");
    mk(NULL, 0, TOP_H, NAV_W, SCR_H - TOP_H, render_nav, nav_evt,
       (void *)(intptr_t)PAGE_ARRAY, "nav");

    g_map = mk(NULL, MAP_X, MAP_Y, MAP_W, MAP_H, render_map, map_evt,
               NULL, "map");
    g_detail = mk(NULL, DET_X, DET_Y, DET_W, DET_H, render_detail, NULL,
                  NULL, "detail");
    W_ar_detail = g_detail;
}

static void build_trend(void)
{
    W_top[PAGE_TREND] = mk(NULL, 0, 0, SCR_W, TOP_H, render_top, NULL, NULL, "top");
    mk(NULL, 0, TOP_H, NAV_W, SCR_H - TOP_H, render_nav, nav_evt,
       (void *)(intptr_t)PAGE_TREND, "nav");

    W_tr_chart = mk(NULL, TC_X, TC_Y, TC_W, TC_H, render_trend_big, NULL, NULL, "chart");
    W_tr_hist  = mk(NULL, HI_X, HI_Y, HI_W, HI_H, render_hist, NULL, NULL, "hist");
    W_tr_off   = mk(NULL, OF_X, OF_Y, OF_W, OF_H, render_offenders, NULL, NULL, "offend");
}

static void build_recipe(void)
{
    W_top[PAGE_RECIPE] = mk(NULL, 0, 0, SCR_W, TOP_H, render_top, NULL, NULL, "top");
    mk(NULL, 0, TOP_H, NAV_W, SCR_H - TOP_H, render_nav, nav_evt,
       (void *)(intptr_t)PAGE_RECIPE, "nav");

    W_rc_profile = mk(NULL, PR_X, PR_Y, PR_W, PR_H, render_profile, NULL, NULL, "profile");
    g_params = mk(NULL, PC_X, PC_Y, PC_W, PC_H, render_params, params_evt,
                  NULL, "params");
    g_steps = mk(NULL, ST_X, ST_Y, ST_W, ST_H, render_steps, steps_evt,
                 NULL, "steps");
}

static void build_log(void)
{
    W_top[PAGE_LOG] = mk(NULL, 0, 0, SCR_W, TOP_H, render_top, NULL, NULL, "top");
    mk(NULL, 0, TOP_H, NAV_W, SCR_H - TOP_H, render_nav, nav_evt,
       (void *)(intptr_t)PAGE_LOG, "nav");

    mk(NULL, LG_X, LG_Y, LG_W, LG_H, render_log, log_evt, NULL, "log");
}

/* ==========================================================================
 *  背景
 * ========================================================================== */

static void render_bg(ipgui_scr_t * scr, ipgui_surf_t * surf)
{
    int i;

    fill_rect(surf, 0, 0, SCR_W, SCR_H, COL_BG_TOP, 255);

    /* 组态画面常见的淡网格底纹 */
    for (i = 0; i < SCR_W; i += 20) {
        draw_line(surf, i, 0, i, SCR_H - 1, 1, rgb(206, 212, 220, 255), 90,
                  IPGUI_LINE_CAP_BUTT);
    }
    for (i = 0; i < SCR_H; i += 20) {
        draw_line(surf, 0, i, SCR_W - 1, i, 1, rgb(206, 212, 220, 255), 90,
                  IPGUI_LINE_CAP_BUTT);
    }

    (void)scr;
}

/* ==========================================================================
 *  数据刷新 -> 脏矩形
 * ========================================================================== */

static void tick_pages(u32_t now)
{
    static u32_t last_fast = 0;
    static u32_t last_slow = 0;
    u8_t p = main_screen.act_page;

    /* 顶栏：时钟与在线脉冲 */
    if (now - A.last_top_ms >= 400) {
        A.last_top_ms = now;
        if (p < PAGE_COUNT && W_top[p]) ipgui_widget_mark_dirty(W_top[p]);
    }

    /* 10Hz：读数类 */
    if (now - last_fast >= 100) {
        last_fast = now;

        if (p == PAGE_OVERVIEW) {
            if (W_ov_kpi)   ipgui_widget_mark_dirty(W_ov_kpi);
            if (W_ov_trend) ipgui_widget_mark_dirty(W_ov_trend);
            if (W_ov_run)   ipgui_widget_mark_dirty(W_ov_run);
        } else if (p == PAGE_ARRAY) {
            if (W_ar_detail) ipgui_widget_mark_dirty(W_ar_detail);
        } else if (p == PAGE_TREND) {
            if (W_tr_chart) ipgui_widget_mark_dirty(W_tr_chart);
        } else if (p == PAGE_RECIPE) {
            if (g_steps) ipgui_widget_mark_dirty(g_steps);
            if (W_rc_profile) ipgui_widget_mark_dirty(W_rc_profile);
        }
    }

    /* 2.5Hz：阵列图与分布图，避免 3072 个小块高频重绘 */
    if (now - last_slow >= 400) {
        last_slow = now;

        if (p == PAGE_OVERVIEW) {
            if (W_ov_boards) ipgui_widget_mark_dirty(W_ov_boards);
        } else if (p == PAGE_ARRAY) {
            if (g_map) ipgui_widget_mark_dirty(g_map);
        } else if (p == PAGE_TREND) {
            if (W_tr_hist) ipgui_widget_mark_dirty(W_tr_hist);
            if (W_tr_off)  ipgui_widget_mark_dirty(W_tr_off);
        }
    }
}

/* ==========================================================================
 *  main
 * ========================================================================== */

int main(void)
{
    ipgui_input_src_evt_t raw_evt;
    u32_t last_tick_ms;
    int p;

    ipgui_input_dispatcher_init(&dispatcher);

    pointer_src.priv_data = (void *)0;
    pointer_src.input_src_event_read_cb = ipgui_sdl_mouse_event_poll;

    ipgui_screen_init(&main_screen, &sdl_drv);
    ipgui_sdl_screen_init(&main_screen);

    if (g_sdl_private.window) {
        SDL_SetWindowTitle(g_sdl_private.window,
                           "CAP-AGING  |  Multi-Channel Burn-In System");
    }

    pointer_id  = ipgui_dispatcher_register_input_src(&dispatcher, &pointer_src);
    main_scr_id = ipgui_dispatcher_register_screen(&dispatcher, &main_screen);

    ipgui_bind_input_src_with_screen(&dispatcher, pointer_id, main_scr_id);

    ipgui_scr_create_pfb(&main_screen, main_screen_frame_buf,
                         sizeof(main_screen_frame_buf), PIX_FMT_RGBA8888);

    if (ipgui_init() != IPGUI_ERR_OK) {
        printf("ipgui_init_err\n");
        return 0;
    }

    sim_init();

    main_screen.render_bg = render_bg;

    build_overview();

    ipgui_screen_page_switch(&main_screen, PAGE_ARRAY);
    build_array();

    ipgui_screen_page_switch(&main_screen, PAGE_TREND);
    build_trend();

    ipgui_screen_page_switch(&main_screen, PAGE_RECIPE);
    build_recipe();

    ipgui_screen_page_switch(&main_screen, PAGE_LOG);
    build_log();

    ipgui_screen_page_switch(&main_screen, PAGE_OVERVIEW);

    /* 调试/演示：CAP_PAGE=0..4 可直接从指定页面启动 */
    {
        const char * e = getenv("CAP_PAGE");
        if (e) {
            int pg = atoi(e);
            if (pg >= 0 && pg < PAGE_COUNT) {
                A.page = (u8_t)pg;
                ipgui_screen_page_switch(&main_screen, (u8_t)pg);
            }
        }
    }

    last_tick_ms = SDL_GetTicks();

    for (p = 0; ; p ++) {
        u32_t now;

        while (pointer_src.input_src_event_read_cb(&pointer_src, &raw_evt)
               == IPGUI_ERR_READ_INPUT_SRC_EVT_OK) {
            ipgui_norm_queue_post(&dispatcher.evt_queue, &raw_evt, sizeof(raw_evt));
        }

        ipgui_dispatch_input_event(&dispatcher);
        ipgui_anim_update_all();

        {
            u32_t ms = ipgui_tick2millis(ipgui_tick_now());

            sim_tick(ms);
            tick_pages(ms);
        }

        ipgui_screen_render(&main_screen);

        now = SDL_GetTicks();
        {
            u32_t delta = now - last_tick_ms;
            last_tick_ms = now;
            if (delta > 50) delta = 50;
            while (delta --) ipgui_tick_inc();
        }

        SDL_Delay(1);
    }

    return 0;
}