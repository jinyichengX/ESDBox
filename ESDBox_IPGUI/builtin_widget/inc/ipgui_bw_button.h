#ifndef IPGUI_BW_BUTTON_H
#define IPGUI_BW_BUTTON_H

/*
 * 圆角渐变按钮
 *
 * - 四种外观：主按钮 / 次级按钮 / 幽灵按钮 / 危险按钮
 * - 按下有缩放动画，悬停提亮、按下压暗
 * - 不使用外阴影，控件矩形只留 1px 描边空间
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "ipgui_widget.h"
#include "ipgui_color.h"

typedef enum {
    IPGUI_BW_BUTTON_PRIMARY = 0,   /* 主操作，渐变实心 */
    IPGUI_BW_BUTTON_SECONDARY,     /* 次级操作，白色表面 + 细描边 */
    IPGUI_BW_BUTTON_GHOST,         /* 弱操作，无底色 */
    IPGUI_BW_BUTTON_DANGER,        /* 破坏性操作，红色渐变 */
} ipgui_bw_button_variant_t;

typedef void (*ipgui_bw_button_cb_t)(ipgui_widget_t * widget);

/* parent 为 NULL 时挂到当前页 */
ipgui_widget_t * ipgui_bw_button_create(
    ipgui_widget_t * parent,
    ipgui_coord_t x, ipgui_coord_t y,
    ipgui_coord_t w, ipgui_coord_t h,
    const char * text,
    ipgui_bw_button_variant_t variant);

void ipgui_bw_button_set_text(ipgui_widget_t * widget, const char * text);
void ipgui_bw_button_set_variant(
    ipgui_widget_t * widget, ipgui_bw_button_variant_t variant);
void ipgui_bw_button_set_enabled(ipgui_widget_t * widget, u8_t enabled);
void ipgui_bw_button_set_on_click(
    ipgui_widget_t * widget, ipgui_bw_button_cb_t cb);

/* 自定义渐变色，传 a=0 的颜色表示恢复默认 */
void ipgui_bw_button_set_accent(
    ipgui_widget_t * widget,
    ipgui_color_t top, ipgui_color_t bottom,
    ipgui_color_t text_color);

/* 每帧调用一次以驱动按压缩放动画（不调用则只有终态，没有过渡） */
void ipgui_bw_button_task(ipgui_widget_t * widget);

#ifdef __cplusplus
}
#endif

#endif /* IPGUI_BW_BUTTON_H */
