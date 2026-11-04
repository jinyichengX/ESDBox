#ifndef IPGUI_DRAW_ARC_H
#define IPGUI_DRAW_ARC_H

#include "ipgui_blend.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef s32_t ipgui_arc_angle_t;                    /* 圆弧起始角度 */

typedef enum {
    IPGUI_ARC_ENDPOINT_TYPE_BUTT,                   /* 平头 */
    IPGUI_ARC_ENDPOINT_TYPE_ROUND,                  /* 圆头 */
}ipgui_arc_endpoint_type_t;

typedef enum {
    IPGUI_ARC_DRAW_DIR_CW,                          /* 顺时针 */
    IPGUI_ARC_DRAW_DIR_CCW,                         /* 逆时针 */
}ipgui_arc_draw_dir_t;

typedef struct {
    ipgui_coord_t             cx;                   /* 圆心x    */
    ipgui_coord_t             cy;                   /* 圆心y    */
    ipgui_coord_t             er;                   /* 外圆半径 */
    ipgui_coord_t             ir;                   /* 内圆半径 */
    ipgui_arc_angle_t         start;                /* 绘制起始角度 */
    ipgui_arc_draw_dir_t      dir;                  /* 绘制方向 cw：顺时针 ccw：逆时针 */
    u16_t                     angle;                /* 绘制角度 */
}ipgui_arc_t;

typedef struct {
    ipgui_paint_t             paint;                /* 填充     */
    u8_t                      opacity;              /* 不明度   */
    ipgui_arc_endpoint_type_t sep_type;             /* 起始端点类型 */
    ipgui_arc_endpoint_type_t eep_type;             /* 结束端点类型 */
    ipgui_blend_mode_t        blend_mode;           /* 混合类型 */
}ipgui_arc_style_t;

extern __IPGUI_API__ void ipgui_draw_arc(
    ipgui_surf_t      * surf,
    ipgui_aabb_t      * clip,
    ipgui_arc_t       * arc,
    ipgui_arc_style_t * style);

extern __IPGUI_API__ void ipgui_draw_arc_thin(
    ipgui_surf_t       * surf,
    ipgui_aabb_t       * clip,
    ipgui_coord_t        cx,                   /* 圆心x                         */
    ipgui_coord_t        cy,                   /* 圆心y                         */
    ipgui_coord_t        r,                    /* 半径                          */
    ipgui_arc_angle_t    start,                /* 绘制起始角度                   */
    ipgui_arc_draw_dir_t dir,                  /* 绘制方向 cw：顺时针 ccw：逆时针 */
    u16_t                angle,                /* 绘制角度                      */
    ipgui_color_t        color,
    u8_t                 opacity,
    ipgui_blend_mode_t   blend_mode);

#ifdef __cplusplus
}
#endif

#endif
