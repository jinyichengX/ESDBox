#ifndef IPGUI_BW_RADIO_H
#define IPGUI_BW_RADIO_H

/*
 * 单选按钮
 *
 * - 白色圆环 + 渐变圆点，选中时圆点缩放出现
 * - 悬停圆环靠近主色，按下整体内缩
 * - 右侧文字随控件高度自动选字号
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "ipgui_widget.h"
#include "ipgui_color.h"

typedef void (*ipgui_bw_radio_cb_t)(ipgui_widget_t * widget, u8_t selected);

/* 建议高度 32 以上 */
ipgui_widget_t * ipgui_bw_radio_create(
    ipgui_widget_t * parent,
    ipgui_coord_t x, ipgui_coord_t y,
    ipgui_coord_t w, ipgui_coord_t h,
    const char * label,
    u8_t selected);

void ipgui_bw_radio_set_selected(
    ipgui_widget_t * widget, u8_t selected, u8_t animated);
u8_t ipgui_bw_radio_get_selected(ipgui_widget_t * widget);
void ipgui_bw_radio_set_label(ipgui_widget_t * widget, const char * label);
void ipgui_bw_radio_set_enabled(ipgui_widget_t * widget, u8_t enabled);
void ipgui_bw_radio_set_on_change(
    ipgui_widget_t * widget, ipgui_bw_radio_cb_t cb);

void ipgui_bw_radio_task(ipgui_widget_t * widget);

#ifdef __cplusplus
}
#endif

#endif /* IPGUI_BW_RADIO_H */
