#ifndef IPGUI_PAGE_EMOTO_H
#define IPGUI_PAGE_EMOTO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ipgui_screen.h"

/*
 * 电摩 HMI 页面（RIDE 主界面）
 *
 * 使用方式：
 *   ipgui_screen_init(&main_screen, &drv);
 *   ipgui_scr_create_pfb(...);
 *   ipgui_page_emoto_init(&main_screen, 1);   // 切到第 1 页并创建控件树
 *
 * 主循环每帧调用：
 *   ipgui_page_emoto_tick();
 *   ipgui_screen_render(&main_screen);
 */
__IPGUI_API__ void ipgui_page_emoto_init(ipgui_scr_t * scr, u8_t page);

/* 数据模拟、动画插值、脏矩形提交 */
__IPGUI_API__ void ipgui_page_emoto_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* IPGUI_PAGE_EMOTO_H */
