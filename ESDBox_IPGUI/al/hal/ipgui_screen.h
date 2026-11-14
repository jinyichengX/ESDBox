#ifndef IPGUI_SCREEN_H
#define IPGUI_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ipgui_utils.h"
#include "ipgui_coord.h"
#include "ipgui_color.h"
#include "ipgui_prim.h"
#include "ipgui_lcd_pix_fmt.h"
#include "ipgui_widget_evt.h"
#include "ipgui_widget_tree.h"
#include "ipgui_dirty_rect.h"
#include "ipgui_core.h"
#include "ipgui_conf.h"

typedef struct ipgui_scr_ctx ipgui_scr_t;
typedef struct{
    void                  * pri_data;
    ipgui_coord_t           xreso;
    ipgui_coord_t           yreso;

    void (* put_pixel)  (
        ipgui_scr_t       * scr,
        ipgui_coord_t       x,
        ipgui_coord_t       y,
        u8_t              * pix);

    void (* fill_region)(
        ipgui_scr_t      * scr, 
        ipgui_coord_t      x1,
        ipgui_coord_t      y1,
        ipgui_coord_t      x2,
        ipgui_coord_t      y2, 
        u8_t             * pix_buf,
        s32_t              stride);

    void (* flush)      (
        ipgui_scr_t      * scr);

}ipgui_scr_drv_t;

typedef struct ipgui_scr_ctx{
    void                 * pri_data;

    ipgui_scr_drv_t      * drv;

    /* 页面控件树：每页一棵独立的树
     * 同一时刻只有 act_page 参与渲染与命中，其余页保留完整状态但不绘制
     */
    struct widget_tree_t   pages[SCREEN_PAGE_MAX];

    /* 当前页索引，恒小于 SCREEN_PAGE_MAX */
    u8_t                   act_page;

    /* Dirty rect manager */
    ipgui_dirty_rect_mgr_t dirty;

    /* 1 = 有容器等待重排，ipgui_screen_render 入口会先消费掉 */
    u8_t                   layout_pending;

    /* offline partial framebuffer */
    /* 其中的pix_fmt可以随便定义，
     * 但是在flush到屏幕时需要正确解析，
     * 一般建议与物理屏幕的pix_fmt一致
     */
    ipgui_pfb_t            pfb;

    /* Background color or render callback 
     * if render callback is NULL, then use bg_color
     */
    ipgui_color_t          bg_color;

    void (* render_bg)(ipgui_scr_t * scr, ipgui_surf_t * surf);

#if IPGUI_FPS_MONITOR_ON == 1
    /* 屏幕的实时fps */
    u16_t                  fps;
#endif
}ipgui_scr_t;

extern __IPGUI_API__ ipgui_err_t ipgui_screen_init(
                                        ipgui_scr_t        * scr, 
                                        ipgui_scr_drv_t    * drv);

/* 切换当前页
 *
 * 只改索引并把整屏置脏，不移动、不重挂任何控件，因此切换成本是 O(1)。
 * 未被切换到的页保留完整控件树与其状态，只是不再参与渲染与命中。
 *
 * 注意：无父控件创建的控件（ipgui_widget_create(NULL)）会挂到“当前页”，
 *      因此建多页时应按页切换后再创建控件。
 */
extern __IPGUI_API__ void        ipgui_screen_page_switch(
                                        ipgui_scr_t        * scr,
                                        u8_t                page);

extern __IPGUI_API__ ipgui_err_t ipgui_scr_create_pfb(
                                        ipgui_scr_t        * scr, 
                                        u8_t               * buf, 
                                        u32_t                buf_size, 
                                        ipgui_pix_fmt_t      pix_fmt);   

extern __IPGUI_API__ void        ipgui_screen_putpixel(
                                        ipgui_scr_t        * scr, 
                                        ipgui_coord_t        x, 
                                        ipgui_coord_t        y, 
                                        u8_t               * pix);

extern __IPGUI_API__ void        ipgui_screen_fill_region(
                                        ipgui_scr_t        * scr, 
                                        ipgui_coord_t        x1, 
                                        ipgui_coord_t        y1, 
                                        ipgui_coord_t        x2, 
                                        ipgui_coord_t        y2, 
                                        u8_t               * pix_buf, 
                                        s32_t                stride);

extern __IPGUI_API__ void        ipgui_screen_flush(
                                        ipgui_scr_t        * scr);

extern __IPGUI_API__ void        ipgui_screen_handle_widget_event(
                                        ipgui_scr_t        * scr, 
                                        ipgui_widget_evt_t * evt);

extern __IPGUI_API__ void        ipgui_screen_fps(ipgui_scr_t * scr);

/* 重排当前页所有 layout_dirty 的容器
 *
 * 递归顺序是"先父后子"，祖先排完再排后代，因此一次遍历即可收敛到最终几何。
 * 布局回调里只允许改后代几何，不允许增删控件。
 * ipgui_screen_render 会在入口自动调用，一般无需手动调用。
 */
extern __IPGUI_API__ void        ipgui_layout_update(ipgui_scr_t * scr);

#ifdef __cplusplus
}
#endif

#endif
