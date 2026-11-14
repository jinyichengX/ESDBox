#ifndef IPGUI_BW_CHECKBOX_H
#define IPGUI_BW_CHECKBOX_H

/*
 * 复选框
 *
 * - 圆角方形方块 + 右侧文字，勾选时主色渐变淡入、对勾缩放出现
 * - 取消勾选时颜色反向淡出
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "ipgui_widget.h"
#include "ipgui_color.h"

typedef void (*ipgui_bw_checkbox_cb_t)(ipgui_widget_t * widget, u8_t checked);

/* 建议高度 32 以上 */
ipgui_widget_t * ipgui_bw_checkbox_create(
    ipgui_widget_t * parent,
    ipgui_coord_t x, ipgui_coord_t y,
    ipgui_coord_t w, ipgui_coord_t h,
    const char * label,
    u8_t checked);

void ipgui_bw_checkbox_set_checked(
    ipgui_widget_t * widget, u8_t checked, u8_t animated);
u8_t ipgui_bw_checkbox_get_checked(ipgui_widget_t * widget);
void ipgui_bw_checkbox_set_label(ipgui_widget_t * widget, const char * label);
void ipgui_bw_checkbox_set_enabled(ipgui_widget_t * widget, u8_t enabled);
void ipgui_bw_checkbox_set_on_change(
    ipgui_widget_t * widget, ipgui_bw_checkbox_cb_t cb);

void ipgui_bw_checkbox_task(ipgui_widget_t * widget);

#ifdef __cplusplus
}
#endif

#endif /* IPGUI_BW_CHECKBOX_H */
