#include "ipgui_draw_image.h"
#include "ipgui_debug.h"
#include "ipgui_image_buf.h"
#include "ipgui_pixel_lerp.h"
#include "ipgui_memory.h"

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
    q = (ipgui_scoord_t)(((s64_t)a << IPGUI_FIXED_BITS) + (b >> 1)) / b;
    
    return (sign > 0) ? q : -q;
}

__IPGUI_STATIC__ void ipgui_image_trans_matrix_invert(
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

/* 求图像变换后的包围盒（相对于pivot） */
__IPGUI_API__ ipgui_aabb_t ipgui_calc_image_transformed_aabb_rel_to_pivot(
    ipgui_point_t     * pivot,    /* 相对于图片(0,0)点的变换点 如果是子图那么就是相对于子图的 */
    ipgui_trans_mat_t * trans,
    ipgui_coord_t       img_w,
    ipgui_coord_t       img_h)
{
    ipgui_spoint_t p_vec[4]; /* the transformed image's 4 vertexes */
    /* 这四个点是以变换中心为原点 */
    /* screen coordinate
     * p_vec[0]     p_vec[1]
     *
     * p_vec[2]     p_vec[3]
     */
    ipgui_saabb_t aabb_trans;
    ipgui_aabb_t img_aabb;
    for (u8_t i = 0; i < 4; i ++) {
        ipgui_coord_t v_x, v_y;
        switch (i) {
            case 0: v_x = -pivot->x;
                    v_y = -pivot->y;
                    break;
            case 1: v_x = img_w - 1 - pivot->x;
                    v_y = -pivot->y;
                    break;
            case 2: v_x = -pivot->x;
                    v_y = img_h - 1 - pivot->y;
                    break;
            case 3: v_x = img_w - 1 - pivot->x;
                    v_y = img_h - 1 - pivot->y;
                    break;
        }
        /* calculate and save it */
        p_vec[i].x = trans->a * v_x + trans->b * v_y;
        p_vec[i].y = trans->c * v_x + trans->d * v_y;
    }
    /* generate aabb(rel to pivot) */
    ipgui_aabb_generate_with_points(&aabb_trans, (ipgui_point_t *)p_vec, 4);
    ipgui_aabb_expand_integer(&aabb_trans, &img_aabb);

    return img_aabb;
}

/* get image aabb(absolute coordinate on surf) */
__IPGUI_API__ ipgui_aabb_t ipgui_locate_image(
    ipgui_point_t * pivot,    /* 相对于图片的变换点 如果是子图那么就是相对于子图的 */
    ipgui_point_t * anchor,
    ipgui_coord_t   img_w,
    ipgui_coord_t   img_h)
{
    ipgui_aabb_t img_aabb;
    img_aabb.start.x = anchor->x - pivot->x;
    img_aabb.start.y = anchor->y - pivot->y;
    img_aabb.end.x   = img_aabb.start.x + img_w - 1;
    img_aabb.end.y   = img_aabb.start.y + img_h - 1;

    return img_aabb;
}

/* 求图像变换后的包围盒（相对于pivot） */
__IPGUI_API__ ipgui_aabb_t ipgui_calc_image_transformed_aabb(
    ipgui_point_t     * pivot,    /* 相对于图片(0,0)点的变换点 如果是子图那么就是相对于子图的 */
    ipgui_point_t     * anchor,
    ipgui_trans_mat_t * trans,
    ipgui_coord_t       img_w,
    ipgui_coord_t       img_h)
{
    ipgui_aabb_t img_aabb;
    img_aabb = ipgui_calc_image_transformed_aabb_rel_to_pivot(
                        pivot,
                        trans,
                        img_w,
                        img_h);

    ipgui_coord_t w, h;
    w = ipgui_aabb_width (&img_aabb);
    h = ipgui_aabb_height(&img_aabb);

    /* get image aabb(absolute coordinate on surf) */
    return ipgui_locate_image(pivot, anchor, w, h);
}

__IPGUI_STATIC__ __IPGUI_INLINE__ u8_t * image_pixmap_get(ipgui_image_data_t * img_data, ipgui_coord_t x/* rel to image's(0,0) */, ipgui_coord_t y)
{
    return img_data->pixmap + (img_data->stride * y) + img_data->px_size * x;
}


/* 根据直觉设计画图片的API
 * 指定一个离屏缓冲表面surf，然后将图钉钉在图片的某个点pivot （也就是给一个二维坐标系的原点，这个坐标系就是图片坐标系）类似于鼠标点住这个点进行拖动，需要注意pivot就是二维坐标系原点
 * 然后再把图钉钉在surf的某个点anchor（也就是将图片坐标系原点移动到surf上的某一点anchor）（可以在surf之外）类似于鼠标拖动到的点，
 * 然后图片进行变换（以这个二维坐标系），如果图片中某个点变换后还在surf中那就在surf中显示。
 */

__IPGUI_API__ void ipgui_draw_image(
    ipgui_surf_t             * surf,
    ipgui_aabb_t             * clip,
    ipgui_image_data_t       * img_data,
    ipgui_point_t            * pivot,    /* 相对于图片的变换点 如果是子图那么就是相对于子图的 */
    ipgui_point_t            * anchor,
    ipgui_trans_mat_t        * trans,
    ipgui_image_draw_style_t * style,
    ipgui_image_quality_t      quality)
{
    if ((!surf) || (!img_data) || (!pivot) || (!anchor) || (!style))
        return;

    if (style->opacity < 3)
        return;

    if (!trans) {
        ipgui_aabb_t img_aabb;
        /* get image aabb(absolute coordinate on surf) */
        img_aabb = ipgui_locate_image(
            pivot,
            anchor,
            img_data->w,
            img_data->h);

        ipgui_image_src_t img_src;
        img_src.buf       = img_data->pixmap;
        img_src.img_pxfmt = img_data->fmt;
        img_src.px_size   = img_data->px_size;
        img_src.stride    = img_data->stride;
        img_src.img_aabb  = &img_aabb;

        ipgui_blend_image_v1(
            surf,
            clip,
            &img_src,
            (u8_t *)0,
            (ipgui_aabb_t *)0,
            style->opacity,
            style->blend_mode);
        return;
    }

    /* draw transformed image */
    /* check if matrix is invertible */
    if (trans->a * trans->d == trans->b * trans->c) {
        ipgui_dbg_warning("warning: the transfrom matrix err, because it is 不可逆的\r\n");
        return;
    }

    ipgui_aabb_t img_rel_pivot;
    ipgui_aabb_t img_in_surf;
    ipgui_coord_t img_w, img_h;
    img_rel_pivot = ipgui_calc_image_transformed_aabb_rel_to_pivot(pivot, trans, img_data->w, img_data->h);
    img_w = ipgui_aabb_width (&img_rel_pivot);
    img_h = ipgui_aabb_height(&img_rel_pivot);

    /* get image aabb(absolute coordinate on surf) */
    ipgui_point_t transformed_img_rel_pivot;
    transformed_img_rel_pivot.x = -img_rel_pivot.start.x;
    transformed_img_rel_pivot.y = -img_rel_pivot.start.y;
    img_in_surf = ipgui_locate_image(&transformed_img_rel_pivot, anchor, img_w, img_h);

    /* calc the aabb need to be drawn */
    ipgui_aabb_t  draw_in_surf;
    ipgui_coord_t draw_w, draw_h;
    if (0 != ipgui_aabb_overlap(&draw_in_surf, &surf->surf, &img_in_surf))
        return;
    if (clip) {
        if (0 != ipgui_aabb_overlap(&draw_in_surf, &draw_in_surf, clip))
            return;
    }
    draw_w = ipgui_aabb_width (&draw_in_surf);
    draw_h = ipgui_aabb_height(&draw_in_surf);

    /* draw draw area */
    /* allocate image buffer first */
    ipgui_coord_t res_h = 0;
    u8_t * pixmap;
    u8_t * mask_buf;
    pixmap = ipgui_image_buf_acquire(
        (img_data->px_size + 1)/* "+1" means extra byte for mask */ * draw_w,
        1,/* render line by line */
        &res_h);
    if ((pixmap == (u8_t *)0) || (!res_h))
        return;
    mask_buf = pixmap + img_data->px_size * draw_w * res_h; /* mask buffer is after pixel buffer */
    
    /* get aabb relative to pivot */
    ipgui_aabb_t draw_rel_pivot;
    draw_rel_pivot.start.x = img_rel_pivot.start.x  + (draw_in_surf.start.x - img_in_surf.start.x);
    draw_rel_pivot.start.y = img_rel_pivot.start.y  + (draw_in_surf.start.y - img_in_surf.start.y);
    draw_rel_pivot.end.x   = draw_rel_pivot.start.x + draw_w - 1;
    draw_rel_pivot.end.y   = draw_rel_pivot.start.y + draw_h - 1;

    u8_t * cr_a, * cr_b, * cr_c, * cr_d;/* 插值点周围4点颜色值索引 */
    ipgui_coord_t temp_x, temp_y;
    s32_t alpha, d;/* for edge mask */
    u8_t cr[10];
    u32_t hd, vd;
    s32_t idx = 0;
#define SHIFT (8 - IPGUI_FIXED_BITS)
    u32_t px_sz  = img_data->px_size; /* per pixel size */
    u32_t stride = img_data->stride;

    ipgui_scoord_t ori_dx, ori_dy;
    ori_dx = pivot->x * IPGUI_FIXED_PRECI;
    ori_dy = pivot->y * IPGUI_FIXED_PRECI;
    ipgui_coord_t x, y;
    ipgui_scoord_t xo, yo; /* the transformed xy and orig's xy */

    ipgui_scoord_t border_x     = (img_data->w - 1) * IPGUI_FIXED_PRECI;
    ipgui_scoord_t border_y     = (img_data->h - 1) * IPGUI_FIXED_PRECI;
    ipgui_scoord_t max_border_x = border_x + IPGUI_FIXED_PRECI;
    ipgui_scoord_t max_border_y = border_y + IPGUI_FIXED_PRECI;

    ipgui_scoord_t xo_start_row, yo_start_row; /* the transformed xy and orig's xy */
    ipgui_scoord_t next_pix_incx/* inv.a */, next_pix_incy/* inv.c */;
    ipgui_scoord_t next_row_incx/* inv.b */, next_row_incy/* inv.d */;
    ipgui_trans_mat_t inv;
    ipgui_image_trans_matrix_invert(trans, &inv); 
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
    ipgui_image_src_t img_src;
    img_src.px_size   = img_data->px_size;
    img_src.buf       = pixmap;
    img_src.stride    = draw_w * img_data->px_size;
    img_src.img_pxfmt = img_data->fmt;
    img_src.img_aabb  = &mask_aabb;

    /* reverse mapping（反向映射）every pixel */
    for (; y <= draw_rel_pivot.end.y; y ++) { /* xy是变换后的图像点相对于变换点的deltax和deltay */
        x  = draw_rel_pivot.start.x;
        xo = xo_start_row;
        yo = yo_start_row;
        for (; x <= draw_rel_pivot.end.x; x ++) {
            /* check if in the original image's aabb */
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
                /* use src color to lerp color */
                if (IPGUI_IMAGE_QUALITY_LOW == quality) { /* 点采样 */
                    /* left top point(a) coordinate */
                    temp_x = IPGUI_FIXED_FLOOR(xo);
                    temp_y = IPGUI_FIXED_FLOOR(yo);
                    /* get src color */
                    cr_a = image_pixmap_get(img_data, temp_x, temp_y); /* the top left point */
                    /* write to pixel buffer */
                    ipgui_memcpy(pixmap + idx * px_sz, cr_a, px_sz);
                } else if (IPGUI_IMAGE_QUALITY_MEDIUM == quality) { /* 最近邻插值 */
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

                    if (hd >= 128) temp_x ++;
                    if (vd >= 128) temp_y ++;
                    ipgui_memcpy(pixmap + idx * px_sz, 
                        image_pixmap_get(img_data, temp_x, temp_y), 
                        px_sz);
                } else { /* 双线性插值 */
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

                    /* get src color */
                    cr_a = image_pixmap_get(img_data, temp_x, temp_y); /* the top left point */
                    cr_b = cr_a + px_sz;
                    cr_c = cr_a + stride;
                    cr_d = cr_c + px_sz;
                    /* lerp */
                    g_pix_lerp[img_data->fmt].bilinear(cr_a, cr_b, cr_c, cr_d, (u8_t)hd, (u8_t)vd, cr);
                    /* write to pixel buffer */
                    ipgui_memcpy(pixmap + idx * px_sz, cr, px_sz);
                }

                mask_buf[idx++] = 255;
            } else {                /* generate edge mask */
                if (xo < 0) { /* left edge */
                    if (yo < 0) {
                        d = 0;
                        alpha = IPGUI_MIN((0 - xo), (0 - yo));
                        cr_a = cr_b = image_pixmap_get(img_data, 0, 0);
                    } else if (yo > border_y) {
                        d = 0;
                        alpha = IPGUI_MIN((0 - xo), (yo & (~IPGUI_FIXED_MASK)));
                        cr_a = cr_b = image_pixmap_get(img_data, 0, img_data->h - 1);
                    } else {
                        alpha = 0 - xo;
                        temp_x = IPGUI_FIXED_CEIL(xo);
                        temp_y = IPGUI_FIXED_FLOOR(yo);
                        /* get src color */
                        cr_a = image_pixmap_get(img_data, temp_x, temp_y);
                        cr_b = cr_a + stride;
                        d = (yo - (yo & IPGUI_FIXED_MASK));
                    }
                } else if (xo > border_x) { /* right edge */
                    if (yo < 0) {
                        d = 0;
                        alpha = IPGUI_MIN((xo & (~IPGUI_FIXED_MASK)), (0 - yo));
                        cr_a = cr_b = image_pixmap_get(img_data, img_data->w - 1, 0);
                    } else if (yo > border_y) {
                        d = 0;
                        alpha = IPGUI_MIN((xo & (~IPGUI_FIXED_MASK)), (yo & (~IPGUI_FIXED_MASK)));
                        cr_a = cr_b = image_pixmap_get(img_data, img_data->w - 1, img_data->h - 1);
                    } else {
                        alpha = xo - border_x;
                        temp_x = IPGUI_FIXED_FLOOR(xo);
                        temp_y = IPGUI_FIXED_FLOOR(yo);
                        /* get src color */
                        cr_a = image_pixmap_get(img_data, temp_x, temp_y);
                        cr_b = cr_a + stride;
                        d = (yo - (yo & IPGUI_FIXED_MASK));
                    }
                } else if (yo < 0) { /* top edge */
                    alpha = 0 - yo;
                    temp_x = IPGUI_FIXED_FLOOR(xo);
                    temp_y = IPGUI_FIXED_CEIL(yo);
                    /* get src color */
                    cr_a = image_pixmap_get(img_data, temp_x, temp_y);
                    cr_b = cr_a + px_sz;
                    d = (xo - (xo & IPGUI_FIXED_MASK));
                } else if (yo > border_y) { /* bottom edge */
                    alpha = yo - border_y;
                    temp_x = IPGUI_FIXED_FLOOR(xo);
                    temp_y = IPGUI_FIXED_FLOOR(yo);
                    /* get src color */
                    cr_a = image_pixmap_get(img_data, temp_x, temp_y);
                    cr_b = cr_a + px_sz;
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
                /* use src color to lerp color */
                g_pix_lerp[img_data->fmt].linear(cr_a, cr_b, (u8_t)d, cr);

                /* write to pixel buffer */
                ipgui_memcpy(pixmap + idx * px_sz, cr, px_sz);

                mask_buf[idx++] = (u8_t)alpha;
            }

_next_pix_inc:
            xo += next_pix_incx;/* inv.a */
            yo += next_pix_incy;/* inv.c */
        }
        xo_start_row += next_row_incx;/* inv.b */
        yo_start_row += next_row_incy;/* inv.d */
        idx = 0;
    
        ipgui_blend_image_v1(
            surf,
            (ipgui_aabb_t *)0,
            &img_src,
            mask_buf,
            &mask_aabb,
            style->opacity,
            style->blend_mode);
        
        mask_aabb.start.y ++;
        mask_aabb.end.y = mask_aabb.start.y;
    }

    /* free image buffer */
    ipgui_image_buf_free(pixmap);
}

/* 把定点坐标 v 包裹到 [0, period) 范围内 —— 平铺(repeat)模式的核心 */
__IPGUI_STATIC__ __IPGUI_INLINE__ ipgui_scoord_t ipgui_fixed_wrap(ipgui_scoord_t v, ipgui_scoord_t period)
{
    v %= period;
    if (v < 0) v += period;     /* C 的 % 对负数保留符号，这里修正到正值 */
    return v;
}

/* 把整数像素索引包裹回 [0, size) —— 用于双线性/最近邻采样的右/下邻居越界回绕 */
__IPGUI_STATIC__ __IPGUI_INLINE__ ipgui_coord_t ipgui_idx_wrap(ipgui_coord_t v, ipgui_coord_t size)
{
    if (v >= size) return 0;
    if (v < 0)     return size - 1;
    return v;
}

/* 平铺(repeat)模式绘制图像：参考draw_tile_image.html 中的 edgeMode === 'repeat' 行为
 *
 * 与 ipgui_draw_image 的语义完全一致（pivot/anchor/trans/quality 参数含义不变），
 * 唯一区别在于“源图像坐标越界”时的处理方式：
 *   - ipgui_draw_image (clamp/羽化)：越界像素做边缘羽化（mask 渐变到 0），只绘制
 *     变换后图像的 AABB 内部；
 *   - ipgui_draw_image_tiled (repeat/平铺)：源坐标用取模包裹回 [0,w)×[0,h)，图像在
 *     无穷平面上重复平铺，因此绘制区域 = surf∩clip 的全部像素，每个像素都是完全不透明。
 *
 * 对应 draw_tile_image.html 中的 edgeMode === 'repeat' 行为。
 * trans 为空时按单位矩阵处理（此时即为“不旋转/缩放的纯平铺”）。
 */
__IPGUI_API__ void ipgui_draw_image_tiled(
    ipgui_surf_t             * surf,
    ipgui_aabb_t             * clip,
    ipgui_image_data_t       * img_data,
    ipgui_point_t            * pivot,    /* 相对于图片的变换点 如果是子图那么就是相对于子图的 */
    ipgui_point_t            * anchor,
    ipgui_trans_mat_t        * trans,
    ipgui_image_draw_style_t * style,
    ipgui_image_quality_t      quality)
{
    if ((!surf) || (!img_data) || (!pivot) || (!anchor) || (!style))
        return;

    if (style->opacity < 3)
        return;

    /* 平铺模式下图像尺寸必须 > 0，否则 period=0 会导致取模除零 */
    if ((img_data->w <= 0) || (img_data->h <= 0) || (!img_data->pixmap))
        return;

    /* trans 为空时按单位矩阵处理（纯平铺） */
    ipgui_trans_mat_t identity;
    if (!trans) {
        identity.a = IPGUI_FIXED_PRECI;
        identity.b = 0;
        identity.c = 0;
        identity.d = IPGUI_FIXED_PRECI;
        trans = &identity;
    }

    /* check if matrix is invertible */
    if (trans->a * trans->d == trans->b * trans->c) {
        ipgui_dbg_warning("warning: the transfrom matrix err, because it is 不可逆的\r\n");
        return;
    }

    /* 平铺模式下图像覆盖整个无穷平面，绘制区域 = surf∩clip（若有 clip） */
    ipgui_aabb_t  draw_in_surf;
    ipgui_coord_t draw_w, draw_h;
    if (clip) {
        if (0 != ipgui_aabb_overlap(&draw_in_surf, &surf->surf, clip))
            return;
    } else {
        draw_in_surf = surf->surf;
    }
    draw_w = ipgui_aabb_width (&draw_in_surf);
    draw_h = ipgui_aabb_height(&draw_in_surf);
    if ((draw_w <= 0) || (draw_h <= 0))
        return;

    /* allocate image buffer first: 每行一个缓冲（逐行渲染），平铺无需 mask 缓冲 */
    ipgui_coord_t res_h = 0;
    u8_t * pixmap;
    pixmap = ipgui_image_buf_acquire(
        img_data->px_size * draw_w,
        1,/* render line by line */
        &res_h);
    if ((pixmap == (u8_t *)0) || (!res_h))
        return;

    /* 反向映射矩阵：屏幕 → 源图像 */
    ipgui_trans_mat_t inv;
    ipgui_image_trans_matrix_invert(trans, &inv);
    ipgui_scoord_t next_pix_incx/* inv.a */, next_pix_incy/* inv.c */;
    ipgui_scoord_t next_row_incx/* inv.b */, next_row_incy/* inv.d */;
    next_pix_incx = inv.a;
    next_pix_incy = inv.c;
    next_row_incx = inv.b;
    next_row_incy = inv.d;

    /* 平铺周期（定点） */
    ipgui_scoord_t period_x = (ipgui_scoord_t)img_data->w * IPGUI_FIXED_PRECI;
    ipgui_scoord_t period_y = (ipgui_scoord_t)img_data->h * IPGUI_FIXED_PRECI;

    /* pivot 在源图像坐标系下的定点坐标 */
    ipgui_scoord_t ori_dx, ori_dy;
    ori_dx = pivot->x * IPGUI_FIXED_PRECI;
    ori_dy = pivot->y * IPGUI_FIXED_PRECI;

    /* 屏幕坐标相对于 anchor 的范围（反向映射输入） */
    ipgui_coord_t x0 = draw_in_surf.start.x - anchor->x;
    ipgui_coord_t y0 = draw_in_surf.start.y - anchor->y;
    ipgui_coord_t x1 = draw_in_surf.end.x   - anchor->x;
    ipgui_coord_t y1 = draw_in_surf.end.y   - anchor->y;

    /* 起始行对应的源坐标（定点）：xo = inv.a*x + inv.b*y + pivot.x */
    ipgui_scoord_t xo_start_row, yo_start_row;
    xo_start_row = inv.a * x0 + inv.b * y0 + ori_dx;
    yo_start_row = inv.c * x0 + inv.d * y0 + ori_dy;
    /* 保持行起点包裹在 [0, period) 内，避免多行累积溢出 */
    xo_start_row = ipgui_fixed_wrap(xo_start_row, period_x);
    yo_start_row = ipgui_fixed_wrap(yo_start_row, period_y);

    /* 单行混合区域（每次只混合一行），mask 传 NULL：平铺每个像素完全不透明 */
    ipgui_aabb_t row_aabb;
    row_aabb.start.x = draw_in_surf.start.x;
    row_aabb.end.x   = draw_in_surf.end.x;
    row_aabb.start.y = row_aabb.end.y = draw_in_surf.start.y;

    ipgui_image_src_t img_src;
    img_src.px_size   = img_data->px_size;
    img_src.buf       = pixmap;
    img_src.stride    = draw_w * img_data->px_size;
    img_src.img_pxfmt = img_data->fmt;
    img_src.img_aabb  = &row_aabb;

    u32_t px_sz  = img_data->px_size; /* per pixel size */
    ipgui_coord_t x, y;
    ipgui_scoord_t xo, yo;        /* 反向映射得到的源图像定点坐标（已包裹） */
    u32_t idx = 0;
    u8_t cr[10];
    u8_t * cr_a, * cr_b, * cr_c, * cr_d;
    u32_t hd, vd;                 /* 采样点在 a 右侧/下侧的小数距离，缩放到 0-255 */
    ipgui_coord_t temp_x, temp_y;
    ipgui_coord_t temp_x1, temp_y1;  /* 右/下邻居（越界回绕到 0） */

    /* reverse mapping（反向映射）every pixel */
    for (y = y0; y <= y1; y ++) {
        x   = x0;
        xo  = xo_start_row;
        yo  = yo_start_row;
        idx = 0;
        for (; x <= x1; x ++) {
            /* xo,yo 已在 [0, period) 内，故整数像素索引落在 [0, w-1]×[0, h-1] */
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
            /* 右/下邻居越界回绕（平铺：右边界连到左边界，下边界连到上边界） */
            temp_x1 = ipgui_idx_wrap(temp_x + 1, img_data->w);
            temp_y1 = ipgui_idx_wrap(temp_y + 1, img_data->h);

            if (IPGUI_IMAGE_QUALITY_LOW == quality) { /* 点采样 */
                cr_a = image_pixmap_get(img_data, temp_x, temp_y);
                ipgui_memcpy(pixmap + idx * px_sz, cr_a, px_sz);
            } else if (IPGUI_IMAGE_QUALITY_MEDIUM == quality) { /* 最近邻插值 */
                ipgui_coord_t nx = temp_x, ny = temp_y;
                if (hd >= 128) nx = temp_x1;   /* 四舍五入，右边界回绕 */
                if (vd >= 128) ny = temp_y1;   /* 四舍五入，下边界回绕 */
                cr_a = image_pixmap_get(img_data, nx, ny);
                ipgui_memcpy(pixmap + idx * px_sz, cr_a, px_sz);
            } else { /* 双线性插值 */
                cr_a = image_pixmap_get(img_data, temp_x,  temp_y);
                cr_b = image_pixmap_get(img_data, temp_x1, temp_y);
                cr_c = image_pixmap_get(img_data, temp_x,  temp_y1);
                cr_d = image_pixmap_get(img_data, temp_x1, temp_y1);
                /* lerp */
                g_pix_lerp[img_data->fmt].bilinear(cr_a, cr_b, cr_c, cr_d, (u8_t)hd, (u8_t)vd, cr);
                /* write to pixel buffer */
                ipgui_memcpy(pixmap + idx * px_sz, cr, px_sz);
            }

            idx ++;
            /* 推进到下一像素的源坐标，并包裹回 [0, period) */
            xo = ipgui_fixed_wrap(xo + next_pix_incx, period_x);
            yo = ipgui_fixed_wrap(yo + next_pix_incy, period_y);
        }
        /* 推进到下一行的行起点源坐标，并包裹回 [0, period) */
        xo_start_row = ipgui_fixed_wrap(xo_start_row + next_row_incx, period_x);
        yo_start_row = ipgui_fixed_wrap(yo_start_row + next_row_incy, period_y);

        /* 平铺：每个像素完全不透明，mask 传 NULL，按 opacity 整体混合 */
        ipgui_blend_image_v1(
            surf,
            (ipgui_aabb_t *)0,
            &img_src,
            (u8_t *)0,
            (ipgui_aabb_t *)0,
            style->opacity,
            style->blend_mode);

        row_aabb.start.y ++;
        row_aabb.end.y = row_aabb.start.y;
    }

    /* free image buffer */
    ipgui_image_buf_free(pixmap);
}