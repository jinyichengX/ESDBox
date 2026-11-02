#ifndef IPGUI_PAGE_EMOTO_NAV_H
#define IPGUI_PAGE_EMOTO_NAV_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ipgui_screen.h"

/*
 * E-MOTION 导航页（NAV）
 *
 * 与 RIDE 页共用车辆状态快照；本页负责地图、路线、转向、ETA、电池与行程信息。
 * 初始化完成后会自动切回 RIDE 页，保证启动画面不变。
 */
__IPGUI_API__ void ipgui_page_emoto_nav_init(ipgui_scr_t * scr, u8_t page);

/* 仅在 NAV 页可见时推进：主题色插值、位置脉冲、数据刷新 */
__IPGUI_API__ void ipgui_page_emoto_nav_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* IPGUI_PAGE_EMOTO_NAV_H */
