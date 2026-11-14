#ifndef IPGUI_BW_BADGE_H
#define IPGUI_BW_BADGE_H

/*
 * 状态徽章（Chip）
 *
 * - 胶囊底 + 同色调描边 + 状态圆点 + 文字
 * - 五种语义色调：中性 / 信息 / 成功 / 警告 / 危险
 * - 字体与圆点随控件高度自适应
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "ipgui_widget.h"
#include "ipgui_color.h"

typedef enum {
    IPGUI_BW_BADGE_NEUTRAL = 0,
    IPGUI_BW_BADGE_INFO,
    IPGUI_BW_BADGE_SUCCESS,
    IPGUI_BW_BADGE_WARNING,
    IPGUI_BW_BADGE_DANGER,
} ipgui_bw_badge_tone_t;

/* 建议高度 22 ~ 32 */
ipgui_widget_t * ipgui_bw_badge_create(
    ipgui_widget_t * parent,
    ipgui_coord_t x, ipgui_coord_t y,
    ipgui_coord_t w, ipgui_coord_t h,
    const char * text,
    ipgui_bw_badge_tone_t tone);

void ipgui_bw_badge_set_text(ipgui_widget_t * widget, const char * text);
void ipgui_bw_badge_set_tone(
    ipgui_widget_t * widget, ipgui_bw_badge_tone_t tone);
void ipgui_bw_badge_set_dot(ipgui_widget_t * widget, u8_t show);

#ifdef __cplusplus
}
#endif

#endif /* IPGUI_BW_BADGE_H */
