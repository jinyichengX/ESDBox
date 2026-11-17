#include "ipgui_edge_wdf_mask.h"
#include "ipgui_memory.h"

/* 这个文件用于计算edge的wdf蒙版 
 * 暂时只支持整数端点edge！！！！！！
 */

/* 已知y，求直线对应的x坐标（仅对斜线和垂线有效） */
__IPGUI_STATIC__ void ipgui_line_x_at_y(
    ipgui_edge_wdf_param_t * param,
    ipgui_coord_t            y, 
    ipgui_coord_t          * x,   /* res */
    s32_t                  * frac /* res */)
{
    ipgui_coord_t dy;
    s32_t temp;

    dy = y - param->y1;
    temp = (s32_t)dy * param->dx;
    * x = temp / param->dy;
    * frac = temp - ((* x) * param->dy);
    if ((* frac) < 0) {
        * x -= 1;
        * frac += param->dy;
    }
    * x += param->x1;
}

__IPGUI_STATIC__ void ipgui_line_x_step(
    ipgui_xidx_t              * x_idx,
    ipgui_xstep_t               step,
    ipgui_edge_wdf_mask_dsc_t * dsc)
{
    x_idx->inte += step.inte;
    x_idx->frac += step.frac;

    if (dsc->p->dx > 0) {
        while (IPGUI_ABS(x_idx->frac) >= dsc->p->dy) {
            x_idx->inte += 1;
            x_idx->frac -= dsc->p->dy;
        }
    } else {
        while (IPGUI_ABS(x_idx->frac) >= dsc->p->dy) {
            x_idx->inte -= 1;
            x_idx->frac += dsc->p->dy; 
        }
        if (x_idx->frac < 0) {
            x_idx->inte -= 1;
            x_idx->frac += dsc->p->dy; 
        }
    }
}

__IPGUI_API__ ipgui_edge_wdf_param_t ipgui_edge_wdf_param_init(
    ipgui_coord_t x1,
    ipgui_coord_t y1,
    ipgui_coord_t x2,
    ipgui_coord_t y2)
{
    ipgui_edge_wdf_param_t param;
    
    param.a = y1 - y2;
    param.b = x2 - x1;
    param.c = x1 * y2 - x2 * y1;

    if (y1 > y2) {
        param.dy = y1 - y2;
        param.dx = x1 - x2;
        param.x1 = x2;
        param.y1 = y2;
    } else {
        param.dy = y2 - y1;
        param.dx = x2 - x1;
        param.x1 = x1;
        param.y1 = y1;
    }

    if (IPGUI_ABS(param.dx) >= param.dy) {
        param.flatten = 1;

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
        param.flatten = 0;
        
        /* 计算修正因子索引（64 位，避免大坐标下 32 位乘法溢出） */ 
        s64_t abs_dx = IPGUI_ABS(param.dx);
        s64_t abs_dy = param.dy;
        param.correction_frac_index = (u8_t)((abs_dx * 126 + (abs_dy >> 1)) / abs_dy);
    }

    return param;
}

/* 求横向跨度 */
__IPGUI_API__ ipgui_coord_t ipgui_edge_wdf_x_halfspan(
    ipgui_edge_wdf_param_t * param,
    ipgui_coord_t            width)
{
    /* 随便选取一个直线端点向右单点步进试探
     * 直到 d^2 <= (Ax0 + By0 + C)^2 / (A^2 + B^2) 
     * 附：点到直线距离公式d = |Ax0 + By0 + C| / sqrt(A^2 + B^2)
     */
    ipgui_coord_t x_step = param->x1 + 1;
    ipgui_coord_t x_half_span;
    
    /* 全部用 64 位计算：线宽或坐标较大时，32 位乘法会溢出，
     * 导致半跨度退化成 2，粗线会被画成细线 */
    s64_t half_w2 = width >> 1;
    if (width & 1) half_w2 += 1;/* 这里+1是为了补偿 */
    half_w2 += 1;   /* 这里+1是为了解决边界问题，否则在光栅化平坦线时会有严重的锯齿 */
    half_w2 *= half_w2;

    s64_t a2_plus_b2 = (s64_t)param->a * param->a 
                     + (s64_t)param->b * param->b;

    s64_t temp;
    while (1) {
        temp = (s64_t)param->a * x_step    + 
               (s64_t)param->b * param->y1 + 
               param->c;
        temp *= temp;
        if (temp >= half_w2 * a2_plus_b2) {
            x_step ++;
            break;
        }
        x_step ++;
    }

    x_half_span = x_step - param->x1;
    return x_half_span;
}

__IPGUI_API__ void ipgui_gen_edge_wdf_mask_dsc(
    ipgui_edge_wdf_mask_dsc_t * res,
    ipgui_edge_wdf_param_t    * param,
    ipgui_coord_t               start_y,
    ipgui_coord_t               width)
{
    if (!res || !param) return;

    /* gen x_half_span */
    res->x_half_span = ipgui_edge_wdf_x_halfspan(param, width);

    /* gen x_step */
    res->x_step.inte = param->dx / param->dy;
    res->x_step.frac = param->dx - res->x_step.inte * param->dy;

    /* gen half_width64 */
    res->half_width64 = width << 5;

    /* 计算起始点的x坐标和frac */
    ipgui_line_x_at_y(param, start_y, &res->x_idx.inte, &res->x_idx.frac);

    res->p = param;
}

__IPGUI_API__ void ipgui_edge_wdf_mask_dsc_next_y(
    ipgui_edge_wdf_mask_dsc_t * dsc)
{
    ipgui_line_x_step(&dsc->x_idx, dsc->x_step, dsc);
}

#define correct_d_and_mask(distance, half_width)\
    distance = distance * correction_frac[dsc->p->correction_frac_index] >> 8;\
    if (distance >= (half_width + 64)) mask = 0;\
    else if (distance <= half_width) mask = 255;\
    else mask = (64 - (distance - half_width)) << 2;\

#define calc_mask(_dsc, _dist64)\
    if (_dsc->p->flatten) {\
        d = ((s64_t)_dist64 * _dsc->p->delta_y + 32768) >> 16;/* 转化成到直线的轴向距离 */\
    } else {\
        d = _dist64;\
    }\
    correct_d_and_mask(d, _dsc->half_width64);\

extern const u8_t correction_frac[127];

/* 单行（批量）mask生成，不混合只填充 */
__IPGUI_API__ void ipgui_edge_wdf_mask(
    ipgui_edge_wdf_mask_dsc_t * dsc,
    ipgui_coord_t               sx, /* start coordinate of x */
    u8_t                      * mask_buf,
    ipgui_coord_t               len /* length of mask buffer */)
{
    if (!dsc || !dsc->p || !mask_buf || len < 1) return;

    ipgui_coord_t x_left, x_right;
    u8_t lfrac64 = 0;

    if (dsc->x_idx.frac) {
        x_left  = dsc->x_idx.inte - dsc->x_half_span;
        x_right = dsc->x_idx.inte + 1 + dsc->x_half_span;
        lfrac64 = ((s64_t)dsc->x_idx.frac << 6) / dsc->p->dy;/* x_idx.frac scale to 0 - 64 */
    } else {
        x_left  = dsc->x_idx.inte - dsc->x_half_span;
        x_right = dsc->x_idx.inte + dsc->x_half_span;
    }

    ipgui_coord_t ex = sx + len - 1;
    /* case 1: mask buffer与[x_left, x_right]无交集 */
    if ((ex < x_left) || (sx > x_right)) {
        ipgui_memset(mask_buf, 0, len); /* mask all zero */
        return;
    }

    /* case 2: mask buffer与[x_left, x_right]有交集 */
    /* 先裁剪两侧，先裁剪掉x_left左侧，再裁剪掉x_right右侧
     * 剩下的部分就是[x_left, x_right]的子区间
     * 再从左向右遍历直到mask为255退出
     * 再从右向左遍历直到mask为255退出
     * 中间剩余部分mask全部为255
     */
    if (sx < x_left) {
        ipgui_coord_t left_zero_len = x_left - sx;
        ipgui_memset(mask_buf, 0, left_zero_len); /* mask left part zero */
        mask_buf += left_zero_len;
        len      -= left_zero_len;
        sx        = x_left;
    }

    if (ex > x_right) {
        ipgui_coord_t right_zero_len = ex - x_right;
        ipgui_memset(mask_buf + (len - right_zero_len), 0, right_zero_len); /* mask right part zero */
        len -= right_zero_len;
        ex   = x_right;
    }

    /* now we only need to focus on the middle part belong to [x_left, x_right]*/
    u8_t mask;
    u32_t dist64, d;

    /* three cases :
     * case 1: all mask buffer at the left of dsc->x_idx
     * case 2: all mask buffer at the right of dsc->x_idx
     * case 3: mask buffer across dsc->x_idx
     */
    if (ex <= dsc->x_idx.inte) {
        /* case 1: all mask buffer at the left of dsc->x_idx */
        dist64 = ((dsc->x_idx.inte - sx) << 6) + lfrac64;
        ipgui_coord_t x = sx;
        for (; x <= ex; x ++) {
            calc_mask(dsc, dist64);
            if (mask == 255)
                break;
            * mask_buf = mask;
            mask_buf ++;
            dist64 -= 64;
        }
        /* fill left（剩余） mask with 255 */
        ipgui_memset(mask_buf, 255, ex - x + 1);
    } else if ( ((!dsc->x_idx.frac) && (sx >= dsc->x_idx.inte)) ||
                 ((dsc->x_idx.frac) && (sx > dsc->x_idx.inte))) {
        /* case 2: all mask buffer at the right of dsc->x_idx */
        if (dsc->x_idx.frac)
            dist64 = ((ex - dsc->x_idx.inte) << 6) - lfrac64;
        else
            dist64 = ((ex - dsc->x_idx.inte) << 6);
        ipgui_coord_t x = ex;
        u8_t * mask_buf_end = mask_buf + len - 1;
        for (; x >= sx; x --) {
            calc_mask(dsc, dist64);
            if (mask == 255)
                break;
            * mask_buf_end = mask;
            mask_buf_end --;
            dist64 -= 64;
        }
        /* fill left（剩余） mask with 255 */
        ipgui_memset(mask_buf, 255, x - sx + 1);
    } else {
        ipgui_coord_t x;
        u8_t * mask_buf_end = mask_buf + len - 1;
        /* case 3: mask buffer across dsc->x_idx */
        /* firstly, from sx to dsc->x_idx.inte */
        dist64 = ((dsc->x_idx.inte - sx) << 6) + lfrac64;
        x = sx;
        for (; x <= dsc->x_idx.inte; x ++) {
            calc_mask(dsc, dist64);
            if (mask == 255)
                break;
            * mask_buf = mask;
            mask_buf ++;
            dist64 -= 64;
        }

        /* secondly, from ex to dsc->x_idx.inte */
        dist64 = ((ex - dsc->x_idx.inte) << 6) - lfrac64;
        x = ex;
        for (; x > dsc->x_idx.inte; x --) {
            calc_mask(dsc, dist64);
            if (mask == 255)
                break;
            * mask_buf_end = mask;
            mask_buf_end --;
            dist64 -= 64;
        }

        /* lastly, fill the middle part with 255 */
        if (mask_buf_end >= mask_buf)
            ipgui_memset(mask_buf, 255, mask_buf_end - mask_buf + 1);
    }
}

/* 单点mask生成 */
__IPGUI_API__ u8_t ipgui_edge_wdf_mask_point(
    ipgui_edge_wdf_mask_dsc_t * dsc,
    ipgui_coord_t               x)
{
    u8_t mask;
    u32_t dist64, d;
    u8_t lfrac64 = 0;
    lfrac64 = ((s64_t)dsc->x_idx.frac << 6) / dsc->p->dy;/* x_idx.frac scale to 0 - 64 */
    if (dsc->x_idx.frac) {
        if (x <= dsc->x_idx.inte) {
            dist64 = ((dsc->x_idx.inte - x) << 6) + lfrac64;
        } else {
            dist64 = ((x - dsc->x_idx.inte) << 6) - lfrac64;
        }
    } else {
        dist64 = (IPGUI_ABS(x - dsc->x_idx.inte)) << 6;
    }
    calc_mask(dsc, dist64);
    return mask;
}

#if 0
/* 测试 */
#include "ipgui_pattle.h"
#include "ipgui_color.h"
void test_first_octant_wdf(ipgui_surf_t * surf)
{
    ipgui_color_t g_color;
    IPGUI_COLOR_SET(g_color, 255, 0x2196f3/*IPGUI_COLOR_BLUE*//* 蓝色 */);

    static ipgui_coord_t y_step = 110;

    ipgui_edge_wdf_param_t edge_param = ipgui_edge_wdf_param_init(
        0,   100,   /* 自己定义起点 */
        200, y_step++     /* 自己定义终点 */
    );

    ipgui_edge_wdf_mask_dsc_t dsc;
    ipgui_gen_edge_wdf_mask_dsc(&dsc, &edge_param, 0, 11);


    for (ipgui_coord_t y = 0; y < 480; y ++) {
#if 0 /* z逐点 */ /* 这个分支测试完成 */
        for (ipgui_coord_t x = 0; x < 800; x ++) {
            u8_t mask;
            mask = ipgui_edge_wdf_mask_point(&dsc, x);
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
#else /* 逐行批量 */
        static u8_t mask_buf[800];
        ipgui_edge_wdf_mask(&dsc, 0, mask_buf, 800);
        for (ipgui_coord_t x = 0; x < 800; x ++) {
            u8_t mask = mask_buf[x];
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
#endif
        ipgui_edge_wdf_mask_dsc_next_y(&dsc);
    }
}
#endif