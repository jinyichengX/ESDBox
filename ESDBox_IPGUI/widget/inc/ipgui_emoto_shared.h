#ifndef IPGUI_EMOTO_SHARED_H
#define IPGUI_EMOTO_SHARED_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ipgui_screen.h"
#include "ipgui_widget.h"

/* E-MOTION HMI 两页共用的尺寸与页号 */
#define IPGUI_EMOTO_W          800
#define IPGUI_EMOTO_H          480
#define IPGUI_EMOTO_PAGE_RIDE  4
#define IPGUI_EMOTO_PAGE_NAV   5

/* RIDE 页向 NAV 页发布的车辆快照 */
typedef struct {
    u8_t  valid;
    u8_t  mode;          /* 0=ECO 1=SPORT 2=RAIN */
    u8_t  charging;
    u8_t  left_on;
    u8_t  right_on;
    u8_t  high_beam;
    u8_t  abs_on;
    u8_t  tc_on;

    s32_t speed;
    s32_t soc;
    s32_t range_km;
    s32_t power_x10;
    s32_t motor_temp;
    s32_t volt_x10;
    s32_t trip_x100;
    s32_t odo_x100;
    s32_t avg_speed;
} ipgui_emoto_snapshot_t;

__IPGUI_API__ void ipgui_emoto_shared_publish(const ipgui_emoto_snapshot_t * s);
__IPGUI_API__ const ipgui_emoto_snapshot_t * ipgui_emoto_shared_get(void);

/*
 * 两页互切动画
 *
 * 不修改 ipgui_screen 的渲染逻辑：在每一页最上层放一块全屏遮罩，
 * 用内置 ipgui_anim 驱动遮罩扫过屏幕，扫满时调用 ipgui_screen_page_switch，
 * 再让新页的遮罩反向扫出，形成一次完整的“幕布擦除”转场。
 */
__IPGUI_API__ void ipgui_emoto_transition_setup(
                    ipgui_scr_t * scr, u8_t ride_page, u8_t nav_page);
__IPGUI_API__ void ipgui_emoto_transition_register(
                    u8_t page, ipgui_widget_t * overlay);
__IPGUI_API__ ipgui_yes_no_t ipgui_emoto_transition_to(u8_t target_page);
__IPGUI_API__ u8_t ipgui_emoto_transition_active(void);

/* 遮罩控件的 render 回调，两页共用 */
__IPGUI_API__ void ipgui_emoto_transition_render(
                    ipgui_widget_t * widget,
                    ipgui_widget_render_ctx_t * ctx);

#ifdef __cplusplus
}
#endif

#endif /* IPGUI_EMOTO_SHARED_H */
