#ifndef IPGUI_BW_SWITCH_H
#define IPGUI_BW_SWITCH_H

/*
 * 开关（iOS 风格）
 *
 * - 轨道为胶囊形，关闭为浅灰，打开为主色渐变
 * - 白色圆钮带柔和投影，切换时圆钮滑动 + 轨道渐变淡入
 * - 控件矩形已包含阴影留白
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "ipgui_widget.h"
#include "ipgui_color.h"

typedef void (*ipgui_bw_switch_cb_t)(ipgui_widget_t * widget, u8_t on);

/* 建议尺寸 56 x 32 左右 */
ipgui_widget_t * ipgui_bw_switch_create(
    ipgui_widget_t * parent,
    ipgui_coord_t x, ipgui_coord_t y,
    ipgui_coord_t w, ipgui_coord_t h,
    u8_t on);

void ipgui_bw_switch_set_on(
    ipgui_widget_t * widget, u8_t on, u8_t animated);
u8_t ipgui_bw_switch_get_on(ipgui_widget_t * widget);

void ipgui_bw_switch_set_enabled(ipgui_widget_t * widget, u8_t enabled);
void ipgui_bw_switch_set_on_change(
    ipgui_widget_t * widget, ipgui_bw_switch_cb_t cb);

/* 自定义打开态渐变，传 a=0 的颜色表示恢复默认 */
void ipgui_bw_switch_set_colors(
    ipgui_widget_t * widget, ipgui_color_t top, ipgui_color_t bottom);

void ipgui_bw_switch_task(ipgui_widget_t * widget);

#ifdef __cplusplus
}
#endif

#endif /* IPGUI_BW_SWITCH_H */
