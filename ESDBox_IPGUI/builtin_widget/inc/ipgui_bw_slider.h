#ifndef IPGUI_BW_SLIDER_H
#define IPGUI_BW_SLIDER_H

/*
 * 拖动滑块
 *
 * - 胶囊轨道，激活段为主色渐变
 * - 白色圆钮 + 主色描边 + 投影，按下时放大
 * - 值域 0..100
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "ipgui_widget.h"
#include "ipgui_color.h"

typedef void (*ipgui_bw_slider_cb_t)(ipgui_widget_t * widget, u8_t value);

/* 建议高度 36 以上，控件矩形已包含圆钮与阴影留白 */
ipgui_widget_t * ipgui_bw_slider_create(
    ipgui_widget_t * parent,
    ipgui_coord_t x, ipgui_coord_t y,
    ipgui_coord_t w, ipgui_coord_t h,
    u8_t value);

void ipgui_bw_slider_set_value(ipgui_widget_t * widget, u8_t value);
u8_t ipgui_bw_slider_get_value(ipgui_widget_t * widget);
void ipgui_bw_slider_set_enabled(ipgui_widget_t * widget, u8_t enabled);
void ipgui_bw_slider_set_on_change(
    ipgui_widget_t * widget, ipgui_bw_slider_cb_t cb);

/* 自定义激活段渐变，传 a=0 的颜色表示恢复默认 */
void ipgui_bw_slider_set_accent(
    ipgui_widget_t * widget, ipgui_color_t left, ipgui_color_t right);

void ipgui_bw_slider_task(ipgui_widget_t * widget);

#ifdef __cplusplus
}
#endif

#endif /* IPGUI_BW_SLIDER_H */
