#ifndef IPGUI_BW_RING_PROGRESS_H
#define IPGUI_BW_RING_PROGRESS_H

/*
 * 环形进度
 *
 * - 浅灰整圆轨道 + 主色渐变进度弧，两端圆头
 * - 圆心显示百分比，可选下方小标签
 * - 数值变化带 320ms 缓出动画
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "ipgui_widget.h"
#include "ipgui_color.h"

/* 控件按正方形使用效果最佳，例如 160 x 160 */
ipgui_widget_t * ipgui_bw_ring_progress_create(
    ipgui_widget_t * parent,
    ipgui_coord_t x, ipgui_coord_t y,
    ipgui_coord_t w, ipgui_coord_t h,
    ipgui_coord_t thickness,
    u8_t value,
    const char * label);

void ipgui_bw_ring_progress_set_value(
    ipgui_widget_t * widget, u8_t value, u8_t animated);
u8_t ipgui_bw_ring_progress_get_value(ipgui_widget_t * widget);
void ipgui_bw_ring_progress_set_label(
    ipgui_widget_t * widget, const char * label);

/* 自定义进度弧渐变，传 a=0 的颜色表示恢复默认 */
void ipgui_bw_ring_progress_set_accent(
    ipgui_widget_t * widget, ipgui_color_t from, ipgui_color_t to);

void ipgui_bw_ring_progress_task(ipgui_widget_t * widget);

#ifdef __cplusplus
}
#endif

#endif /* IPGUI_BW_RING_PROGRESS_H */
