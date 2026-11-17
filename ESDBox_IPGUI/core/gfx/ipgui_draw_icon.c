#include "ipgui_draw_icon.h"
#include "ipgui_debug.h"
#include "ipgui_mask_buf.h"

/* some fixed macros */
#ifndef IPGUI_FIXED_BITS
#define IPGUI_FIXED_BITS 10
#endif

#ifndef IPGUI_FIXED_PRECI
#define IPGUI_FIXED_PRECI (1 << IPGUI_FIXED_BITS) /* 像素精度1/(1 << 10) = 1/1024 ≈ 0.000976 */
#endif

#ifndef IPGUI_FIXED_MASK
#define IPGUI_FIXED_MASK ((~0U) << IPGUI_FIXED_BITS)
#endif

#ifndef IPGUI_FIXED_HALF
#define IPGUI_FIXED_HALF  (1 << (IPGUI_FIXED_BITS - 1))
#endif

/* 计算定点数前面的整数点（向负无穷方向） */
#ifndef IPGUI_FIXED_FLOOR
#define IPGUI_FIXED_FLOOR(fixed_val) ((fixed_val) >> IPGUI_FIXED_BITS)
#endif

/* 计算定点数后面的整数点（向正无穷方向） */
#ifndef IPGUI_FIXED_CEIL
#define IPGUI_FIXED_CEIL(fixed_val) ((fixed_val + IPGUI_FIXED_PRECI - 1) >> IPGUI_FIXED_BITS)
#endif

__IPGUI_STATIC__ __IPGUI_INLINE__ ipgui_scoord_t fixed_mult(ipgui_scoord_t a, ipgui_scoord_t b)
{
    s8_t sign = 1;
    ipgui_scoord_t c;
    
    /* 处理符号，转为正数计算 */
    if (a < 0) { a = -a; sign = -sign; }
    if (b < 0) { b = -b; sign = -sign; }
    
    /* 计算：c = (a * b + 0.5) >> IPGUI_FIXED_BITS */
    c = (ipgui_scoord_t)(((s64_t)a * b + IPGUI_FIXED_HALF) >> IPGUI_FIXED_BITS);
    
    return (sign > 0) ? c : -c;
}

__IPGUI_STATIC__ __IPGUI_INLINE__ ipgui_scoord_t fixed_div(ipgui_scoord_t a, ipgui_scoord_t b)
{
    s8_t sign = 1;
    ipgui_scoord_t q;
    
    /* 处理符号，转为正数计算 */
    if (a < 0) { a = -a; sign = -sign; }
    if (b < 0) { b = -b; sign = -sign; }
    
    if (b == 0) return 0x7fffffff;  /* 防除零 */
    
    /* 计算：q = ((a << IPGUI_FIXED_BITS) + b/2) / b */
    s64_t q64 = (((s64_t)a << IPGUI_FIXED_BITS) + (b >> 1)) / b;

    /* 退化矩阵（det 接近 0）时商会超出 ipgui_scoord_t 范围，这里饱和处理 */
    if (q64 >  0x7fffffffLL) q64 =  0x7fffffffLL;
    if (q64 < -0x7fffffffLL) q64 = -0x7fffffffLL;
    q = (ipgui_scoord_t)q64;
    
    return (sign > 0) ? q : -q;
}

__IPGUI_STATIC__ void ipgui_icon_trans_matrix_invert(
        ipgui_trans_mat_t * mat,
        ipgui_trans_mat_t * res)
{
    /* 2D matrix A inverted formula
     * if T = |a  b|
     *        |c  d|
     * then T(-1) = 1 / (a*d - b*c) * |d  -b|
     *                                |-c  a|
     */
    ipgui_trans_mat_t temp;
    ipgui_scoord_t det;

    ipgui_scoord_t a, b, c, d;
    a = mat->a;
    b = mat->b;
    c = mat->c;
    d = mat->d;

    det = fixed_mult(a, d) - fixed_mult(b, c);
    temp.a = fixed_div( d, det);
    temp.b = fixed_div(-b, det);
    temp.c = fixed_div(-c, det);
    temp.d = fixed_div( a, det);

    res->a = temp.a;
    res->b = temp.b;
    res->c = temp.c;
    res->d = temp.d;
}

/* 扩展为整数包围盒 */
__IPGUI_STATIC__ void ipgui_aabb_expand_integer(ipgui_saabb_t * in, ipgui_aabb_t * out)
{
    out->start.x = IPGUI_FIXED_FLOOR(in->start.x);
    out->start.y = IPGUI_FIXED_FLOOR(in->start.y);
    out->end.x   = IPGUI_FIXED_CEIL(in->end.x);
    out->end.y   = IPGUI_FIXED_CEIL(in->end.y);
}

/* 求图标变换后的包围盒（相对于pivot） */
__IPGUI_API__ ipgui_aabb_t ipgui_calc_icon_transformed_aabb_rel_to_pivot(
    ipgui_point_t     * pivot,    /* 相对于图标(0,0)点的变换点 */
    ipgui_trans_mat_t * trans,
    ipgui_coord_t       icon_w,
    ipgui_coord_t       icon_h)
{
    ipgui_spoint_t p_vec[4]; /* the transformed icon's 4 vertexes */
    /* 这四个点是以变换中心为原点 */
    /* screen coordinate
     * p_vec[0]     p_vec[1]
     *
     * p_vec[2]     p_vec[3]
     */
    ipgui_saabb_t aabb_trans;
    ipgui_aabb_t icon_aabb;
    for (u8_t i = 0; i < 4; i ++) {
        ipgui_coord_t v_x, v_y;
        switch (i) {
            case 0: v_x = -pivot->x;
                    v_y = -pivot->y;
                    break;
            case 1: v_x = icon_w - 1 - pivot->x;
                    v_y = -pivot->y;
                    break;
            case 2: v_x = -pivot->x;
                    v_y = icon_h - 1 - pivot->y;
                    break;
            case 3: v_x = icon_w - 1 - pivot->x;
                    v_y = icon_h - 1 - pivot->y;
                    break;
        }
        /* calculate and save it */
        p_vec[i].x = trans->a * v_x + trans->b * v_y;
        p_vec[i].y = trans->c * v_x + trans->d * v_y;
    }
    /* generate aabb(rel to pivot) */
    ipgui_aabb_generate_with_points(&aabb_trans, p_vec, 4);
    ipgui_aabb_expand_integer(&aabb_trans, &icon_aabb);

    return icon_aabb;
}

/* get icon aabb(absolute coordinate on surf) */
__IPGUI_API__ ipgui_aabb_t ipgui_locate_icon(
    ipgui_point_t * pivot,    /* 相对于图标的变换点 */
    ipgui_point_t * anchor,
    ipgui_coord_t   icon_w,
    ipgui_coord_t   icon_h)
{
    ipgui_aabb_t icon_aabb;
    icon_aabb.start.x = anchor->x - pivot->x;
    icon_aabb.start.y = anchor->y - pivot->y;
    icon_aabb.end.x   = icon_aabb.start.x + icon_w - 1;
    icon_aabb.end.y   = icon_aabb.start.y + icon_h - 1;

    return icon_aabb;
}

/* 求图标变换后的包围盒（相对于pivot） */
__IPGUI_API__ ipgui_aabb_t ipgui_calc_icon_transformed_aabb(
    ipgui_point_t     * pivot,    /* 相对于图标(0,0)点的变换点 */
    ipgui_point_t     * anchor,
    ipgui_trans_mat_t * trans,
    ipgui_coord_t       icon_w,
    ipgui_coord_t       icon_h)
{
    ipgui_aabb_t icon_aabb;
    icon_aabb = ipgui_calc_icon_transformed_aabb_rel_to_pivot(
                        pivot,
                        trans,
                        icon_w,
                        icon_h);

    ipgui_coord_t w, h;
    w = ipgui_aabb_width (&icon_aabb);
    h = ipgui_aabb_height(&icon_aabb);

    /* get icon aabb(absolute coordinate on surf) */
    return ipgui_locate_icon(pivot, anchor, w, h);
}

__IPGUI_STATIC__ __IPGUI_INLINE__ u8_t * icon_mask_get(ipgui_icon_data_t * icon_data, ipgui_coord_t x/* rel to icon's(0,0) */, ipgui_coord_t y)
{
    return icon_data->mask + (icon_data->w * y) + x;
}

/* linear interpolation algorithm */
__IPGUI_STATIC__ __IPGUI_INLINE__ u8_t liner_core(u8_t a, u8_t b, u8_t d) /* d is scaled 255 (0.0 ~ 1.0 ---> 0 ~ 255)*/
{
    /* the five points p,a,b position like below
     * a--d--p       b
     */
    return (u8_t)(((s32_t)(255 - d) * a + b * d + 128) >> 8);
}

/* bilinear interpolation algorithm */
__IPGUI_STATIC__ __IPGUI_INLINE__ u8_t biliner_core(u8_t a, u8_t b, u8_t c, u8_t d, 
    u8_t hd, u8_t vd) /* hd and vd is scaled 255 (0.0 ~ 1.0 ---> 0 ~ 255)*/
{
    /* the five points p,a,b,c,d position like below
     * a--hd--     b
     * |      |
     * vd     |
     * |      |
     *  ----- p
     * 
     * c           d
     */
    return (u8_t)((((s32_t)a * (255 - hd) * (255 - vd) + b * hd * (255 - vd) +
			 c * vd * (255 - hd) + d * hd * vd) + 32768) >> 16);
}

/* 和画图片不同，画图标不支持子图标 */
__IPGUI_API__ void ipgui_draw_icon(
    ipgui_surf_t             * surf,
    ipgui_aabb_t             * clip,
    ipgui_icon_data_t        * icon_data,
    ipgui_point_t            * pivot,    /* 相对于图标的变换点 */
    ipgui_point_t            * anchor,
    ipgui_trans_mat_t        * trans,
    ipgui_draw_icon_style_t  * style)
{
    if ((!surf) || (!icon_data) || (!pivot) || (!anchor) || (!style))
        return;

    if (style->opacity < 3)
        return;

    /* 入参合法性检查：宽高必须为正，否则采样索引会为负/越界 */
    if ((icon_data->w <= 0) || (icon_data->h <= 0))
        return;

    if (!trans) {
        ipgui_aabb_t icon_aabb;
        /* get icon aabb(absolute coordinate on surf) */
        icon_aabb = ipgui_locate_icon(
            pivot,
            anchor,
            icon_data->w,
            icon_data->h);

        ipgui_blend(
            surf,
            clip,
            &icon_aabb,
            &style->paint,
            style->opacity,
            icon_data->mask,
            &icon_aabb,
            style->blend_mode);

        return;
    }

    /* draw transformed icon */
    /* 变换路径必须读取 mask，空指针直接返回 */
    if (!icon_data->mask)
        return;

    /* check if matrix is invertible */
    if ((s64_t)trans->a * trans->d == (s64_t)trans->b * trans->c) {
        ipgui_dbg_warning("warning: the transfrom matrix err, because it is 不可逆的\r\n");
        return;
    }

    ipgui_aabb_t icon_rel_pivot;
    ipgui_aabb_t icon_in_surf;
    ipgui_coord_t icon_w, icon_h;
    const ipgui_coord_t src_w = icon_data->w; /* 源图标宽高缓存到局部变量，避免热循环反复访存 */
    const ipgui_coord_t src_h = icon_data->h;
    icon_rel_pivot = ipgui_calc_icon_transformed_aabb_rel_to_pivot(pivot, trans, icon_data->w, icon_data->h);
    icon_w = ipgui_aabb_width (&icon_rel_pivot);
    icon_h = ipgui_aabb_height(&icon_rel_pivot);

    /* get icon aabb(absolute coordinate on surf) */
    ipgui_point_t transformed_icon_rel_pivot;
    transformed_icon_rel_pivot.x = -icon_rel_pivot.start.x;
    transformed_icon_rel_pivot.y = -icon_rel_pivot.start.y;
    icon_in_surf = ipgui_locate_icon(&transformed_icon_rel_pivot, anchor, icon_w, icon_h);

    /* calc the aabb need to be drawn */
    ipgui_aabb_t  draw_in_surf;
    ipgui_coord_t draw_w, draw_h;
    if (0 != ipgui_aabb_overlap(&draw_in_surf, &surf->surf, &icon_in_surf))
        return;
    if (clip) {
        if (0 != ipgui_aabb_overlap(&draw_in_surf, &draw_in_surf, clip))
            return;
    }
    draw_w = ipgui_aabb_width (&draw_in_surf);
    draw_h = ipgui_aabb_height(&draw_in_surf);

    /* draw draw area */
    /* allocate icon buffer first */
    ipgui_coord_t res_h = 0;
    u8_t * mask_buf;
    mask_buf = ipgui_mask_buf_acquire(
        draw_w,
        1,/* render line by line */
        &res_h);
    if ((mask_buf == (u8_t *)0) || (!res_h))
        return;
    
    /* get aabb relative to pivot */
    ipgui_aabb_t draw_rel_pivot;
    draw_rel_pivot.start.x = icon_rel_pivot.start.x  + (draw_in_surf.start.x - icon_in_surf.start.x);
    draw_rel_pivot.start.y = icon_rel_pivot.start.y  + (draw_in_surf.start.y - icon_in_surf.start.y);
    draw_rel_pivot.end.x   = draw_rel_pivot.start.x + draw_w - 1;
    draw_rel_pivot.end.y   = draw_rel_pivot.start.y + draw_h - 1;

     u8_t * cr_a, * cr_b, * cr_c, * cr_d;/* 插值点周围4点mask值索引 */
    ipgui_coord_t temp_x, temp_y;
    s32_t alpha, d;/* for edge mask */
    u32_t hd, vd;
    s32_t idx = 0;
#define SHIFT (8 - IPGUI_FIXED_BITS)

    ipgui_scoord_t ori_dx, ori_dy;
    ori_dx = pivot->x * IPGUI_FIXED_PRECI;
    ori_dy = pivot->y * IPGUI_FIXED_PRECI;
    ipgui_coord_t x, y;
    ipgui_scoord_t xo, yo; /* the transformed xy and orig's xy */

    ipgui_scoord_t border_x     = (icon_data->w - 1) * IPGUI_FIXED_PRECI;
    ipgui_scoord_t border_y     = (icon_data->h - 1) * IPGUI_FIXED_PRECI;
    ipgui_scoord_t max_border_x = border_x + IPGUI_FIXED_PRECI;
    ipgui_scoord_t max_border_y = border_y + IPGUI_FIXED_PRECI;

    ipgui_scoord_t xo_start_row, yo_start_row; /* the transformed xy and orig's xy */
    ipgui_scoord_t next_pix_incx/* inv.a */, next_pix_incy/* inv.c */;
    ipgui_scoord_t next_row_incx/* inv.b */, next_row_incy/* inv.d */;
    ipgui_trans_mat_t inv;

    ipgui_icon_trans_matrix_invert(trans, &inv); 
    next_pix_incx = inv.a;
    next_pix_incy = inv.c;
    next_row_incx = inv.b;
    next_row_incy = inv.d;
    x = draw_rel_pivot.start.x;
    y = draw_rel_pivot.start.y;

    xo_start_row = inv.a * x + inv.b * y + ori_dx;
    yo_start_row = inv.c * x + inv.d * y + ori_dy;

    ipgui_aabb_t mask_aabb;
    mask_aabb.start.x = draw_in_surf.start.x;
    mask_aabb.end.x   = draw_in_surf.end.x;
    mask_aabb.start.y = mask_aabb.end.y = draw_in_surf.start.y;

     /* reverse mapping（反向映射）every pixel */
    for (; y <= draw_rel_pivot.end.y; y ++) { /* xy是变换后的图像点相对于变换点的deltax和deltay */
        x  = draw_rel_pivot.start.x;
        xo = xo_start_row;
        yo = yo_start_row;
        for (; x <= draw_rel_pivot.end.x; x ++) {
            /* check if in the original icon's aabb */
            if ((xo <= -IPGUI_FIXED_PRECI) || (xo >= max_border_x)) {
                mask_buf[idx ++] = 0;
                goto _next_pix_inc;
            }
            if ((yo <= -IPGUI_FIXED_PRECI) || (yo >= max_border_y)) {
                mask_buf[idx ++] = 0;
                goto _next_pix_inc;
            }

            if (((xo >= 0) && (xo <= border_x)) \
             && ((yo >= 0) && (yo <= border_y))) {
                hd = xo & (~IPGUI_FIXED_MASK);
                vd = yo & (~IPGUI_FIXED_MASK);
                /* scale to 0-255 */
#if SHIFT > 0
                hd = hd << SHIFT;
                vd = vd << SHIFT;
#elif SHIFT < 0
                hd = hd >> (-SHIFT);
                vd = vd >> (-SHIFT);
#endif

                /* left top point(a) coordinate */
                temp_x = IPGUI_FIXED_FLOOR(xo);
                temp_y = IPGUI_FIXED_FLOOR(yo);

                /* get src mask：右/下邻居越界时偏移退化为 0（等价钳制到当前像素），
                 * 只对 cr_a 做一次地址计算，避免每个采样点重复乘加寻址 */
                u32_t dx = (temp_x + 1 < src_w) ? 1 : 0;
                u32_t dy = (temp_y + 1 < src_h) ? (u32_t)src_w : 0;
                cr_a = icon_mask_get(icon_data, temp_x, temp_y); /* the top left point */
                cr_b = cr_a + dx;
                cr_c = cr_a + dy;
                cr_d = cr_c + dx;

                /* use src mask to lerp mask */
                mask_buf[idx++] = biliner_core(*cr_a, *cr_b, *cr_c, *cr_d, (u8_t)hd, (u8_t)vd);
            } else {                /* generate edge mask */
                if (xo < 0) { /* left edge */
                    if (yo < 0) {
                        d = 0;
                        alpha = IPGUI_MIN((0 - xo), (0 - yo));
                        cr_a = cr_b = icon_mask_get(icon_data, 0, 0);
                    } else if (yo > border_y) {
                        d = 0;
                        alpha = IPGUI_MIN((0 - xo), (yo & (~IPGUI_FIXED_MASK)));
                        cr_a = cr_b = icon_mask_get(icon_data, 0, icon_data->h - 1);
                    } else {
                        alpha = 0 - xo;
                        temp_x = IPGUI_FIXED_CEIL(xo);
                        temp_y = IPGUI_FIXED_FLOOR(yo);
                        /* get src color（下一行越界时退化为当前像素） */
                        cr_a = icon_mask_get(icon_data, temp_x, temp_y);
                        cr_b = cr_a + ((temp_y + 1 < src_h) ? (u32_t)src_w : 0);
                        d = (yo - (yo & IPGUI_FIXED_MASK));
                    }
                } else if (xo > border_x) { /* right edge */
                    if (yo < 0) {
                        d = 0;
                        alpha = IPGUI_MIN((xo & (~IPGUI_FIXED_MASK)), (0 - yo));
                        cr_a = cr_b = icon_mask_get(icon_data, icon_data->w - 1, 0);
                    } else if (yo > border_y) {
                        d = 0;
                        alpha = IPGUI_MIN((xo & (~IPGUI_FIXED_MASK)), (yo & (~IPGUI_FIXED_MASK)));
                        cr_a = cr_b = icon_mask_get(icon_data, icon_data->w - 1, icon_data->h - 1);
                    } else {
                        alpha = xo - border_x;
                        temp_x = IPGUI_FIXED_FLOOR(xo);
                        temp_y = IPGUI_FIXED_FLOOR(yo);
                        /* get src color（下一行越界时退化为当前像素） */
                        cr_a = icon_mask_get(icon_data, temp_x, temp_y);
                        cr_b = cr_a + ((temp_y + 1 < src_h) ? (u32_t)src_w : 0);
                        d = (yo - (yo & IPGUI_FIXED_MASK));
                    }
                } else if (yo < 0) { /* top edge */
                    alpha = 0 - yo;
                    temp_x = IPGUI_FIXED_FLOOR(xo);
                    temp_y = IPGUI_FIXED_CEIL(yo);
                    /* get src color（右邻居越界时退化为当前像素） */
                    cr_a = icon_mask_get(icon_data, temp_x, temp_y);
                    cr_b = cr_a + ((temp_x + 1 < src_w) ? 1 : 0);
                    d = (xo - (xo & IPGUI_FIXED_MASK));
                } else if (yo > border_y) { /* bottom edge */
                    alpha = yo - border_y;
                    temp_x = IPGUI_FIXED_FLOOR(xo);
                    temp_y = IPGUI_FIXED_FLOOR(yo);
                    /* get src color（右邻居越界时退化为当前像素） */
                    cr_a = icon_mask_get(icon_data, temp_x, temp_y);
                    cr_b = cr_a + ((temp_x + 1 < src_w) ? 1 : 0);
                    d = (xo - (xo & IPGUI_FIXED_MASK));
                }

                /* pre-handle alpha and d */
                alpha = IPGUI_FIXED_PRECI - alpha;
#if SHIFT > 0
                alpha = alpha << SHIFT;
                d = d << SHIFT;
#elif SHIFT < 0
                alpha = alpha >> (-SHIFT);
                d = d >> (-SHIFT);
#endif
                mask_buf[idx ++] = (((u32_t)liner_core(* cr_a, * cr_b, d) * alpha) + 128) >> 8;
            }

_next_pix_inc:
            xo += next_pix_incx;/* inv.a */
            yo += next_pix_incy;/* inv.c */
        }
        xo_start_row += next_row_incx;/* inv.b */
        yo_start_row += next_row_incy;/* inv.d */
        idx = 0;
    
        ipgui_blend(
            surf,
            (ipgui_aabb_t *)0,
            &mask_aabb,
            &style->paint,
            style->opacity,
            mask_buf,
            &mask_aabb,
            style->blend_mode);
        
        mask_aabb.start.y ++;
        mask_aabb.end.y = mask_aabb.start.y;
    }

    ipgui_mask_buf_free(mask_buf);
}
