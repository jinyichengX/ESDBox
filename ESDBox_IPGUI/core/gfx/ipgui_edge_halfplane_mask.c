#include "ipgui_edge_halfplane_mask.h"
#include "ipgui_memory.h"

__IPGUI_API__ ipgui_edge_coord_t edge_x_at_y(
    ipgui_edge_param_t * p, 
    ipgui_coord_t        y)
{
    if (p->dy == 0) return 0;
    s64_t temp;
    ipgui_edge_coord_t dy;

    dy = (y * 64) - p->y1;
    temp = (s64_t)dy * p->dx;
    return temp / p->dy + p->x1;
}

/* 修正因子，分母256
 * 为什么只用127个数，因为斜率小于1/127时
 * 此时轴向距离和垂直距离差距不大，不用修正
 * 其实斜率小于1/10时这两个距离就差不太多了，等RAM/ROM不够再优化吧
 */
const u8_t correction_frac[127] = {
         255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 254, 254,
    254, 254, 254, 253, 253, 253, 253, 252, 252, 252, 251, 251, 251, 250, 250, 250,
    249, 249, 248, 248, 247, 247, 247, 246, 246, 245, 245, 244, 244, 243, 243, 242,
    242, 241, 240, 240, 239, 239, 238, 237, 237, 236, 235, 235, 234, 233, 233, 232,
    231, 231, 230, 229, 229, 228, 227, 226, 226, 225, 224, 223, 223, 222, 221, 220,
    220, 219, 218, 217, 216, 216, 215, 214, 213, 212, 212, 211, 210, 209, 208, 208,
    207, 206, 205, 204, 203, 203, 202, 201, 200, 200, 199, 198, 197, 196, 196, 195,
    194, 193, 192, 192, 191, 190, 189, 189, 188, 187, 186, 186, 185, 184, 183, 183
};

__IPGUI_API__ ipgui_edge_param_t ipgui_edge_param_init(
    ipgui_edge_coord_t x1,
    ipgui_edge_coord_t y1,
    ipgui_edge_coord_t x2,
    ipgui_edge_coord_t y2)
{
    ipgui_edge_param_t param;

    ipgui_memset(&param, 0, sizeof(ipgui_edge_param_t));

    if (y2 > y1) {
        param.dy = y2 - y1;
        param.dx = x2 - x1;
        param.x1 = x1;
        param.y1 = y1;
    } else {
        param.dy = y1 - y2;
        param.dx = x1 - x2;
        param.x1 = x2;
        param.y1 = y2;
    }

    if (IPGUI_ABS(param.dx) >= param.dy) {
        /* mark the edge flatten 
         * and scale to delta_y / 65536
         */
        param.flatten = 1;
        // param.delta_x = 65536; /* 这个参数好像没用 */

        s64_t scaled_dy = (s64_t)param.dy << 16;
        s32_t half_dx   = param.dx / 2;

        if (param.dx > 0) {
            param.delta_y = (scaled_dy + half_dx) / IPGUI_ABS(param.dx);
        } else if (param.dx < 0) {
            param.delta_y = (scaled_dy - half_dx) / IPGUI_ABS(param.dx);
        }

        /* 计算修正因子索引（64 位，避免大坐标下 32 位乘法溢出） */ 
        s64_t abs_dx = IPGUI_ABS(param.dx);
        s64_t abs_dy = param.dy;
        param.correction_frac_index = (u8_t)((abs_dy * 126 + (abs_dx >> 1)) / abs_dx);
    } else {
        /* a steep edge */
        param.flatten = 0;

        /* 计算修正因子索引（64 位，避免大坐标下 32 位乘法溢出） */ 
        s64_t abs_dx = IPGUI_ABS(param.dx);
        s64_t abs_dy = param.dy;
        param.correction_frac_index = (u8_t)((abs_dx * 126 + (abs_dy >> 1)) / abs_dy);
    }

    return param;
}

ipgui_edge_coord_t dist_to_lower_64(ipgui_edge_coord_t c)
{
    return c & 63;
}

ipgui_edge_coord_t dist_to_upper_64(ipgui_edge_coord_t c)
{
    return (64 - (c & 63)) & 63;
}

ipgui_edge_coord_t align_down_64(ipgui_edge_coord_t c)
{
    return c & ~(ipgui_edge_coord_t)63;
}

ipgui_edge_coord_t align_up_64(ipgui_edge_coord_t c)
{
    return (c + 63) & ~(ipgui_edge_coord_t)63;
}

__IPGUI_API__ void ipgui_gen_edge_halfplane_mask_dsc(
    ipgui_edge_halfplane_mask_dsc_t * res,
    edge_halfplane_dir_t    dir,
    ipgui_edge_param_t    * p,
    ipgui_coord_t           y)
{
    ipgui_edge_coord_t x_sub = edge_x_at_y(p, y);
    ipgui_edge_coord_t frac_x;

    res->dir = dir;
    res->p   = p;
    res->y   = y;

    if (dir == EDGE_HALFPLANE_DIR_LEFT) {
        res->x_start = align_up_64(x_sub) >> 6;
        frac_x       = dist_to_upper_64(x_sub);
    } else {
        res->x_start = align_down_64(x_sub) >> 6;
        frac_x       = dist_to_lower_64(x_sub);
    }
    res->frac_x = frac_x;
}

#define correct_d_and_return_mask(distance)\
                distance = distance * correction_frac[dsc->p->correction_frac_index] >> 8;\
                if (!distance) return 255;\
                else if (distance > 64) return 0;\
                else return (64 - distance) << 2;\

/* get mask of point(x, dsc->y) */
__IPGUI_API__ u8_t ipgui_edge_halfplane_mask(
    ipgui_edge_halfplane_mask_dsc_t * dsc,
    ipgui_edge_coord_t      x)
{
    ipgui_edge_coord_t d;
    ipgui_edge_coord_t delta_x;

    if (dsc->dir == EDGE_HALFPLANE_DIR_LEFT) {
        if (x < dsc->x_start) {
            return 255;
        } else {
            delta_x = ((x - dsc->x_start) << 6) + dsc->frac_x;
        }
    } else if (dsc->dir == EDGE_HALFPLANE_DIR_RIGHT) {
        if (x > dsc->x_start) {
            return 255;
        } else {
            delta_x = ((dsc->x_start - x) << 6) + dsc->frac_x;
        }
    }
    if (dsc->p->flatten) {
        d = ((s64_t)delta_x * dsc->p->delta_y + 32768) >> 16;/* 转化成轴向距离 */
    } else {
        d = delta_x;
    }
    correct_d_and_return_mask(d);
}

#if 0
/* 测试，逐点遍历效率很低 */
#include "ipgui_pattle.h"
#include "ipgui_color.h"
void test_first_octant_halfplane(ipgui_surf_t * surf)
{
    ipgui_color_t g_color;
    IPGUI_COLOR_SET(g_color, 255, IPGUI_COLOR_RED);

    ipgui_edge_param_t edge_param = ipgui_edge_param_init(
        1 << 6, 0 << 6, /* 自己定义起点(Q64) */
        1 << 6, 400 << 6 /* 自己定义终点(Q64) */
    );

    ipgui_edge_halfplane_mask_dsc_t dsc;
    
    for (ipgui_coord_t y = 0; y < 480; y ++) {
        ipgui_gen_edge_halfplane_mask_dsc(&dsc, EDGE_HALFPLANE_DIR_LEFT, &edge_param, y);
        for (ipgui_coord_t x = 0; x < 800; x ++) {
            u8_t mask = ipgui_edge_halfplane_mask(&dsc, x);
            if (mask > 0) {
                ipgui_draw_pixel(
                    surf,
                    NULL,
                    x, y,
                    g_color,
                    mask,
                    255,
                    0
                );
            }
        }
    }
}
#endif