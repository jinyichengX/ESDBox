#ifndef IPGUI_BW_SEGMENTED_H
#define IPGUI_BW_SEGMENTED_H

/*
 * 分段控制器
 *
 * - 浅灰胶囊轨道 + 白色滑块，滑块在选项间平滑滑动
 * - 选中项文字加深，未选中项为次级文字色
 * - 最多 4 段
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "ipgui_widget.h"
#include "ipgui_color.h"

#define IPGUI_BW_SEGMENT_MAX 4

typedef void (*ipgui_bw_segmented_cb_t)(
    ipgui_widget_t * widget, u8_t index);

/* 建议高度 36 ~ 44，宽度按段数等分 */
ipgui_widget_t * ipgui_bw_segmented_create(
    ipgui_widget_t * parent,
    ipgui_coord_t x, ipgui_coord_t y,
    ipgui_coord_t w, ipgui_coord_t h,
    const char * const * labels,
    u8_t count,
    u8_t index);

void ipgui_bw_segmented_set_index(
    ipgui_widget_t * widget, u8_t index, u8_t animated);
u8_t ipgui_bw_segmented_get_index(ipgui_widget_t * widget);
void ipgui_bw_segmented_set_enabled(ipgui_widget_t * widget, u8_t enabled);
void ipgui_bw_segmented_set_on_change(
    ipgui_widget_t * widget, ipgui_bw_segmented_cb_t cb);

void ipgui_bw_segmented_task(ipgui_widget_t * widget);

#ifdef __cplusplus
}
#endif

#endif /* IPGUI_BW_SEGMENTED_H */
