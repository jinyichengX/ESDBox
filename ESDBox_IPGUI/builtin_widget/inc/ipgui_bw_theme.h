#ifndef IPGUI_BW_THEME_H
#define IPGUI_BW_THEME_H

/*
 * IPGUI 内置控件统一视觉层
 *
 * 所有 builtin_widget 控件共用同一套设计令牌与绘制工具，保证
 * 圆角、描边、阴影、渐变、字体的观感一致，避免每个控件各写一套。
 *
 * 坐标约定：控件 render 回调收到的 surf 已经是控件本地坐标系，
 * 因此这里所有绘制都使用 (0,0) - (w-1,h-1) 的本地坐标。
 *
 * 留白约定：外阴影会超出视觉盒，而控件 render 被裁剪在控件矩形内，
 * 所以每个控件的矩形都要比视觉盒大一圈，见 IPGUI_BW_PAD。
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "ipgui_widget.h"
#include "ipgui_blend.h"
#include "ipgui_draw_box_background.h"
#include "ipgui_draw_box_border.h"
#include "ipgui_draw_box_shadow.h"
#include "ipgui_draw_builtin_font.h"
#include "ipgui_draw_line.h"
#include "ipgui_draw_filled_circle.h"
#include "quicksand_medium.h"

/* 控件矩形四周预留给外阴影的像素数 */
#define IPGUI_BW_PAD            6

/* 外阴影开关：小堆（几十 KB）场景可置 0，控件会退化为无阴影，其余效果不变 */
#ifndef IPGUI_BW_SHADOW_EN
#define IPGUI_BW_SHADOW_EN      1
#endif

/* ---------------- 设计令牌 ---------------- */

/* 主色：靛蓝 -> 紫罗兰，用于主按钮 / 开关 / 进度 */
#define BW_ACCENT_TOP           ipgui_bw_color(0x75, 0x83, 0xFB, 0xFF)
#define BW_ACCENT_BOT           ipgui_bw_color(0x4F, 0x5B, 0xE8, 0xFF)
#define BW_ACCENT_SOFT          ipgui_bw_color(0xEE, 0xF1, 0xFF, 0xFF)
#define BW_ACCENT_DEEP          ipgui_bw_color(0x43, 0x4C, 0xD8, 0xFF)

/* 危险色：用于删除 / 重置等破坏性操作 */
#define BW_DANGER_TOP           ipgui_bw_color(0xF8, 0x71, 0x71, 0xFF)
#define BW_DANGER_BOT           ipgui_bw_color(0xE5, 0x48, 0x4D, 0xFF)

/* 成功色：用于打开状态的开关 */
#define BW_SUCCESS_TOP          ipgui_bw_color(0x34, 0xD3, 0x99, 0xFF)
#define BW_SUCCESS_BOT          ipgui_bw_color(0x10, 0xB9, 0x81, 0xFF)

/* 表面与描边 */
#define BW_SURFACE_TOP          ipgui_bw_color(0xFF, 0xFF, 0xFF, 0xFF)
#define BW_SURFACE_BOT          ipgui_bw_color(0xF4, 0xF6, 0xFC, 0xFF)
#define BW_SURFACE_FLAT         ipgui_bw_color(0xF7, 0xF9, 0xFD, 0xFF)
#define BW_BORDER               ipgui_bw_color(0xE1, 0xE6, 0xF1, 0xFF)
#define BW_BORDER_STRONG        ipgui_bw_color(0xCB, 0xD3, 0xE3, 0xFF)
#define BW_TRACK                ipgui_bw_color(0xE8, 0xEC, 0xF5, 0xFF)
#define BW_TRACK_BORDER         ipgui_bw_color(0xDC, 0xE2, 0xEE, 0xFF)

/* 文字 */
#define BW_TEXT                 ipgui_bw_color(0x1E, 0x24, 0x33, 0xFF)
#define BW_TEXT_MUTED           ipgui_bw_color(0x6B, 0x74, 0x88, 0xFF)
#define BW_TEXT_ON_ACCENT       ipgui_bw_color(0xFF, 0xFF, 0xFF, 0xFF)

/* 禁用态 */
#define BW_DISABLED_BG          ipgui_bw_color(0xF0, 0xF2, 0xF7, 0xFF)
#define BW_DISABLED_TEXT        ipgui_bw_color(0xA8, 0xAF, 0xBE, 0xFF)

/* ---------------- 基础工具 ---------------- */

__IPGUI_STATIC__ __IPGUI_INLINE__ ipgui_color_t ipgui_bw_color(
    u8_t r, u8_t g, u8_t b, u8_t a)
{
    ipgui_color_t c;
    c.r = r; c.g = g; c.b = b; c.a = a;
    return c;
}

/* t: 0 -> a, 255 -> b */
ipgui_color_t ipgui_bw_mix(ipgui_color_t a, ipgui_color_t b, u8_t t);

/* 两套视觉色，按 variant 组装时用 */
typedef struct {
    ipgui_color_t top;
    ipgui_color_t bottom;
    ipgui_color_t border;
    ipgui_color_t text;
} ipgui_bw_palette_t;

ipgui_bw_palette_t ipgui_bw_palette_mix(
    ipgui_bw_palette_t a, ipgui_bw_palette_t b, u8_t t);

/* ---------------- 动画 ---------------- */

/* 返回缓动后的 0..255 插值，dur_ms 为 0 时直接返回 to */
u8_t ipgui_bw_anim8(
    u8_t from, u8_t to,
    ipgui_tick_t t0, ipgui_tick_t now, u16_t dur_ms);

/* 简单缓出曲线，输入 0..255 */
u8_t ipgui_bw_ease_out(u8_t t);

/* ---------------- 绘制工具 ---------------- */

void ipgui_bw_box_style_init(
    ipgui_box_style_t * style, ipgui_coord_t radius);

/* 圆角矩形填充（纯色） */
void ipgui_bw_fill_box(
    ipgui_surf_t * surf, ipgui_aabb_t * box, ipgui_coord_t radius,
    ipgui_color_t color, u8_t opacity);

/* 圆角矩形填充（垂直线性渐变，top -> bottom） */
void ipgui_bw_fill_vgrad(
    ipgui_surf_t * surf, ipgui_aabb_t * box, ipgui_coord_t radius,
    ipgui_color_t top, ipgui_color_t bottom, u8_t opacity);

/* 圆角矩形填充（水平线性渐变，left -> right） */
void ipgui_bw_fill_hgrad(
    ipgui_surf_t * surf, ipgui_aabb_t * box, ipgui_coord_t radius,
    ipgui_color_t left, ipgui_color_t right, u8_t opacity);

/* 圆角描边：描边落在 box 外圈（box 内侧保持原样） */
void ipgui_bw_stroke_box(
    ipgui_surf_t * surf, ipgui_aabb_t * box, ipgui_coord_t radius,
    ipgui_coord_t width, ipgui_color_t color, u8_t opacity);

/* 外阴影：围绕 box 向外扩散 */
void ipgui_bw_shadow_box(
    ipgui_surf_t * surf, ipgui_aabb_t * box, ipgui_coord_t radius,
    ipgui_color_t color, u8_t opacity,
    ipgui_coord_t blur, ipgui_coord_t spread,
    ipgui_coord_t dx, ipgui_coord_t dy);

/* 顶部 1px 高光，营造玻璃质感；圆角处自动内缩 */
void ipgui_bw_top_gloss(
    ipgui_surf_t * surf, ipgui_aabb_t * box, ipgui_coord_t radius,
    ipgui_color_t color, u8_t opacity);

/* 水平 1px 高光线，用于进度条 / 滑块填充 */
void ipgui_bw_hairline(
    ipgui_surf_t * surf,
    ipgui_coord_t x1, ipgui_coord_t y1,
    ipgui_coord_t x2, ipgui_coord_t y2,
    ipgui_color_t color, u8_t opacity);

/* 文本：按视觉盒水平垂直居中 */
void ipgui_bw_text_center(
    ipgui_surf_t * surf, ipgui_aabb_t * box,
    const ipgui_font_t * font, const char * text,
    ipgui_color_t color, u8_t opacity);

/* 文本：左对齐、垂直居中 */
void ipgui_bw_text_left(
    ipgui_surf_t * surf, ipgui_aabb_t * box,
    const ipgui_font_t * font, const char * text,
    ipgui_coord_t indent,
    ipgui_color_t color, u8_t opacity);

/* 按视觉高度挑选字号（13 / 15 / 17 / 19px） */
const ipgui_font_t * ipgui_bw_font_for_height(ipgui_coord_t visual_h);

/* 按视觉高度挑选小字号（10 / 11 / 12px） */
const ipgui_font_t * ipgui_bw_font_small_for_height(ipgui_coord_t visual_h);

#ifdef __cplusplus
}
#endif

#endif /* IPGUI_BW_THEME_H */
