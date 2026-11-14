#ifndef IPGUI_BW_PROGRESS_H
#define IPGUI_BW_PROGRESS_H

/*
 * 线性进度条
 *
 * - 胶囊轨道 + 主色渐变填充
 * - 填充顶部有一条 1px 高光，接近 Web 的玻璃质感
 * - 可选百分比文字，自动根据填充位置选择文字颜色
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "ipgui_widget.h"
#include "ipgui_color.h"

/* 建议高度 14 ~ 20 */
ipgui_widget_t * ipgui_bw_progress_create(
    ipgui_widget_t * parent,
    ipgui_coord_t x, ipgui_coord_t y,
    ipgui_coord_t w, ipgui_coord_t h,
    u8_t value,
    u8_t show_text);

void ipgui_bw_progress_set_value(ipgui_widget_t * widget, u8_t value);
u8_t ipgui_bw_progress_get_value(ipgui_widget_t * widget);
void ipgui_bw_progress_set_show_text(ipgui_widget_t * widget, u8_t show);

/* 自定义填充渐变，传 a=0 的颜色表示恢复默认 */
void ipgui_bw_progress_set_accent(
    ipgui_widget_t * widget, ipgui_color_t left, ipgui_color_t right);

#ifdef __cplusplus
}
#endif

#endif /* IPGUI_BW_PROGRESS_H */
