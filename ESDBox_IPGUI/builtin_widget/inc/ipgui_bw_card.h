#ifndef IPGUI_BW_CARD_H
#define IPGUI_BW_CARD_H

/*
 * 卡片容器
 *
 * - 圆角白色表面 + 柔和外阴影 + 1px 描边
 * - 可选标题 / 副标题，标题区下方可画一条分隔线
 * - 作为父控件使用，子控件按本地坐标叠加在卡片上
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "ipgui_widget.h"
#include "ipgui_color.h"

/* 建议高度 >= 96 */
ipgui_widget_t * ipgui_bw_card_create(
    ipgui_widget_t * parent,
    ipgui_coord_t x, ipgui_coord_t y,
    ipgui_coord_t w, ipgui_coord_t h,
    const char * title,
    const char * subtitle);

void ipgui_bw_card_set_title(ipgui_widget_t * widget, const char * title);
void ipgui_bw_card_set_subtitle(
    ipgui_widget_t * widget, const char * subtitle);

/* 标题区下方的分隔线，默认开启；无标题时不绘制 */
void ipgui_bw_card_set_divider(ipgui_widget_t * widget, u8_t show);

#ifdef __cplusplus
}
#endif

#endif /* IPGUI_BW_CARD_H */
