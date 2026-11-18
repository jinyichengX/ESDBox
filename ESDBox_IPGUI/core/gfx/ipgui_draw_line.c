#include "ipgui_draw_line.h"
#include "ipgui_draw_arc.h"
#include "ipgui_edge_halfplane_mask.h"
#include "ipgui_edge_wdf_mask.h"
#include "ipgui_mask_buf.h"
#include "ipgui_debug.h"

extern premult_blend_func_t premult_blend_table[PIX_FMT_MAX];

/* Wu's draw thin line algorithm */
__IPGUI_API__ void ipgui_draw_line_classic(
        ipgui_surf_t       * surf, 
        ipgui_aabb_t       * clip,
        ipgui_line_t       * line, 
        ipgui_line_style_t * style)
{
    if ((!surf) || (!line) || (!style) || (style->opacity < 3))
        return;

    if (style->paint.type == IPGUI_PAINT_IMAGE)
        return;

    /* 零长度线：Wu 算法会落到 dx_abs == dy_abs == 0，后面除以 dy_abs 直接触发
     * 整数除零异常。这里按一个像素处理，与 generic 版本的退化行为保持一致。 */
    if ((line->start.x == line->end.x) && (line->start.y == line->end.y)) {
        ipgui_aabb_t px;
        px.start = line->start;
        px.end   = line->end;
        ipgui_blend(surf, clip, &px, &style->paint, style->opacity,
                    (u8_t *)0, (ipgui_aabb_t *)0, style->blend_mode);
        return;
    }

    ipgui_aabb_t self, draw;
    self.start.x = IPGUI_MIN(line->start.x, line->end.x);
    self.start.y = IPGUI_MIN(line->start.y, line->end.y);
    self.end.x   = IPGUI_MAX(line->start.x, line->end.x);
    self.end.y   = IPGUI_MAX(line->start.y, line->end.y);

    if (clip) {
        if (0 != ipgui_aabb_overlap(&draw, clip, &surf->surf))
            return;
    } else {
        draw = surf->surf;
    }

    if (0 != ipgui_aabb_overlap(&draw, &draw, &self))
        return;

    s8_t flat;
    ipgui_coord_t x1 = line->start.x;
    ipgui_coord_t y1 = line->start.y;
    ipgui_coord_t x2 = line->end.x;
    ipgui_coord_t y2 = line->end.y;
    ipgui_coord_t dx_abs = IPGUI_ABS(x2 - x1);
    ipgui_coord_t dy_abs = IPGUI_ABS(y2 - y1);
    flat = dx_abs > dy_abs ? 1 : 0;

    ipgui_coord_t ix, iy;
    u8_t * cr;
    u8_t alpha;
    s32_t cover; /* 0-255 */
    s32_t err;
    ipgui_color_t premult;ipgui_color_t paint_cr;
    premult_blend_func_t blend_func = premult_blend_table[surf->pix_fmt];

    if (flat) {
        ipgui_coord_t y_step;
        ipgui_coord_t dy;
        ipgui_coord_t ys, xs = IPGUI_MIN(x1, x2); /* 从小x值开始 */
        if (xs == x1) { 
            ys = y1;
            dy = y2 - y1;
        } else {
            ys = y2;
            dy = y1 - y2;
        }
        if (dy > 0) y_step = 1;
        else y_step = -1;
        err = dy * (draw.start.x - xs); /* 起始误差 */
        
        ipgui_coord_t x, y = ys + (err / dx_abs);
        err %= dx_abs;
        for (x = draw.start.x; x <= draw.end.x; x ++) {
            if (IPGUI_ABS(err) >= dx_abs) {
                if (err < 0) {
                    err += dx_abs;
                } else {
                    err -= dx_abs;
                }
                y += y_step;
            }
            cover = 255 - (IPGUI_ABS(err) * 255 / dx_abs);
            alpha = cover * style->opacity >> 8;
            if (ipgui_point_in_aabb(x, y, &draw)) {
                ix = x - surf->surf.start.x;
                iy = y - surf->surf.start.y;
                cr = ipgui_surf_color_get(surf, ix, iy);

                if (style->paint.type == IPGUI_PAINT_COLOR) paint_cr = style->paint.src.color;
                else if (style->paint.type == IPGUI_PAINT_GRADIENT) {
                    if (style->paint.src.grad_src.grad_type == IPGUI_GRADIENT_TYPE_LINEAR) {
                        ipgui_liner_gradient_color_get(&style->paint.src.grad_src.grad.liner_grad, ipgui_get_liner_gradient_pos_at_xy(&style->paint.src.grad_src.grad.liner_grad, x, y), &paint_cr);
                    } else if (style->paint.src.grad_src.grad_type == IPGUI_GRADIENT_TYPE_RADIAL) {
                        ipgui_radial_gradient_color_get(&style->paint.src.grad_src.grad.radial_grad, ipgui_get_radial_gradient_pos_at_xy(&style->paint.src.grad_src.grad.radial_grad, x, y), &paint_cr);
                    } else if (style->paint.src.grad_src.grad_type == IPGUI_GRADIENT_TYPE_CONIC) {
                        ipgui_conic_gradient_color_get(&style->paint.src.grad_src.grad.conic_grad, ipgui_get_conic_gradient_pos_at_xy(&style->paint.src.grad_src.grad.conic_grad, x, y), &paint_cr);
                    }
                }

                premult = ipgui_color_combine_opacity_and_premultiply(&paint_cr, alpha);
                blend_func(premult, cr, style->blend_mode);
            }
            cover = 255 - cover;
            alpha = cover * style->opacity >> 8;
            if (ipgui_point_in_aabb(x, (y + y_step), &draw)) {
                ix = x - surf->surf.start.x;
                iy = y + y_step - surf->surf.start.y;
                cr = ipgui_surf_color_get(surf, ix, iy);

                if (style->paint.type == IPGUI_PAINT_COLOR) paint_cr = style->paint.src.color;
                else if (style->paint.type == IPGUI_PAINT_GRADIENT) {
                    if (style->paint.src.grad_src.grad_type == IPGUI_GRADIENT_TYPE_LINEAR) {
                        ipgui_liner_gradient_color_get(&style->paint.src.grad_src.grad.liner_grad, ipgui_get_liner_gradient_pos_at_xy(&style->paint.src.grad_src.grad.liner_grad, x, y), &paint_cr);
                    } else if (style->paint.src.grad_src.grad_type == IPGUI_GRADIENT_TYPE_RADIAL) {
                        ipgui_radial_gradient_color_get(&style->paint.src.grad_src.grad.radial_grad, ipgui_get_radial_gradient_pos_at_xy(&style->paint.src.grad_src.grad.radial_grad, x, y), &paint_cr);
                    } else if (style->paint.src.grad_src.grad_type == IPGUI_GRADIENT_TYPE_CONIC) {
                        ipgui_conic_gradient_color_get(&style->paint.src.grad_src.grad.conic_grad, ipgui_get_conic_gradient_pos_at_xy(&style->paint.src.grad_src.grad.conic_grad, x, y), &paint_cr);
                    }
                }

                premult = ipgui_color_combine_opacity_and_premultiply(&paint_cr, alpha);
                blend_func(premult, cr, style->blend_mode);
            }
            err += dy;
        }
    } else {
        ipgui_coord_t x_step;
        ipgui_coord_t dx;
        ipgui_coord_t xs, ys = IPGUI_MIN(y1, y2); /* 从小y值开始 */
        if (ys == y1) {
            xs = x1;
            dx = x2 - x1;
        } else {
            xs = x2;
            dx = x1 - x2;
        }
        if (dx > 0) x_step = 1;
        else x_step = -1;
        err = dx * (draw.start.y - ys); /* 起始误差 */

        ipgui_coord_t x = xs + (err / dy_abs), y;
        err %= dy_abs;
        for (y = draw.start.y; y <= draw.end.y; y ++) {
            if (IPGUI_ABS(err) >= dy_abs) {
                if (err < 0) {
                    err += dy_abs;
                } else {
                    err -= dy_abs;
                }
                x += x_step;
            }
            cover = 255 - (IPGUI_ABS(err) * 255 / dy_abs);
            alpha = cover * style->opacity >> 8;
            if (ipgui_point_in_aabb(x, y, &draw)) {
                ix = x - surf->surf.start.x;
                iy = y - surf->surf.start.y;
                cr = ipgui_surf_color_get(surf, ix, iy);

                if (style->paint.type == IPGUI_PAINT_COLOR) paint_cr = style->paint.src.color;
                else if (style->paint.type == IPGUI_PAINT_GRADIENT) {
                    if (style->paint.src.grad_src.grad_type == IPGUI_GRADIENT_TYPE_LINEAR) {
                        ipgui_liner_gradient_color_get(&style->paint.src.grad_src.grad.liner_grad, ipgui_get_liner_gradient_pos_at_xy(&style->paint.src.grad_src.grad.liner_grad, x, y), &paint_cr);
                    } else if (style->paint.src.grad_src.grad_type == IPGUI_GRADIENT_TYPE_RADIAL) {
                        ipgui_radial_gradient_color_get(&style->paint.src.grad_src.grad.radial_grad, ipgui_get_radial_gradient_pos_at_xy(&style->paint.src.grad_src.grad.radial_grad, x, y), &paint_cr);
                    } else if (style->paint.src.grad_src.grad_type == IPGUI_GRADIENT_TYPE_CONIC) {
                        ipgui_conic_gradient_color_get(&style->paint.src.grad_src.grad.conic_grad, ipgui_get_conic_gradient_pos_at_xy(&style->paint.src.grad_src.grad.conic_grad, x, y), &paint_cr);
                    }
                }

                premult = ipgui_color_combine_opacity_and_premultiply(&paint_cr, alpha);
                blend_func(premult, cr, style->blend_mode);
            }
            cover = 255 - cover;
            alpha = cover * style->opacity >> 8;
            if (ipgui_point_in_aabb((x + x_step), y, &draw)) {
                ix = x + x_step - surf->surf.start.x;
                iy = y - surf->surf.start.y;
                cr = ipgui_surf_color_get(surf, ix, iy);

                if (style->paint.type == IPGUI_PAINT_COLOR) paint_cr = style->paint.src.color;
                else if (style->paint.type == IPGUI_PAINT_GRADIENT) {
                    if (style->paint.src.grad_src.grad_type == IPGUI_GRADIENT_TYPE_LINEAR) {
                        ipgui_liner_gradient_color_get(&style->paint.src.grad_src.grad.liner_grad, ipgui_get_liner_gradient_pos_at_xy(&style->paint.src.grad_src.grad.liner_grad, x, y), &paint_cr);
                    } else if (style->paint.src.grad_src.grad_type == IPGUI_GRADIENT_TYPE_RADIAL) {
                        ipgui_radial_gradient_color_get(&style->paint.src.grad_src.grad.radial_grad, ipgui_get_radial_gradient_pos_at_xy(&style->paint.src.grad_src.grad.radial_grad, x, y), &paint_cr);
                    } else if (style->paint.src.grad_src.grad_type == IPGUI_GRADIENT_TYPE_CONIC) {
                        ipgui_conic_gradient_color_get(&style->paint.src.grad_src.grad.conic_grad, ipgui_get_conic_gradient_pos_at_xy(&style->paint.src.grad_src.grad.conic_grad, x, y), &paint_cr);
                    }
                }

                premult = ipgui_color_combine_opacity_and_premultiply(&paint_cr, alpha);
                blend_func(premult, cr, style->blend_mode);
            }
            err += dx;
        }
    }
}

/* draw a horizontal line */
__IPGUI_STATIC__ void ipgui_draw_hor_line(       
    ipgui_surf_t       * surf,
    ipgui_aabb_t       * clip,
    ipgui_line_t       * line,
    ipgui_line_style_t * style)
{
    ipgui_aabb_t self, draw;

    /* clip surf first */
    if (clip) {
        if (0 != ipgui_aabb_overlap(&draw, &surf->surf, clip))
            return;/* not intersect, then just return */
    } else {
        draw = surf->surf;
    }

    /* calc horizontal line region */
    self.start.x = IPGUI_MIN(line->start.x, line->end.x);
    self.end.x   = IPGUI_MAX(line->start.x, line->end.x);
    self.start.y = line->start.y - (style->width >> 1);
    self.end.y   = self.start.y  + style->width - 1;

    /* secondly, clip self */
    if (0 != ipgui_aabb_overlap(&draw, &self, &draw))
        return;/* not intersect, then just return */

    ipgui_blend(
        surf, 
        (ipgui_aabb_t *)0,
        &draw, 
        &style->paint, 
        style->opacity,
        (u8_t *)0,
        (ipgui_aabb_t *)0,
        style->blend_mode);

    if (style->cap == IPGUI_LINE_CAP_ROUND) {
        /* draw circle endpoints */
        ipgui_arc_t circle;
        circle.cx = line->start.x;
        circle.cy = line->start.y;
        circle.dir = IPGUI_ARC_DRAW_DIR_CCW;
        circle.er = style->width >> 1;
        circle.ir = 0;
        circle.start = 0;
        circle.angle = 360;

        ipgui_arc_style_t circle_style;
        circle_style.blend_mode = style->blend_mode;
        circle_style.sep_type = IPGUI_ARC_ENDPOINT_TYPE_BUTT;
        circle_style.eep_type = IPGUI_ARC_ENDPOINT_TYPE_BUTT;
        circle_style.opacity  = style->opacity;
        circle_style.paint    = style->paint;
        ipgui_draw_arc(surf, clip, &circle, &circle_style);

        circle.cx = line->end.x;
        circle.cy = line->end.y;
        ipgui_draw_arc(surf, clip, &circle, &circle_style);
    }
}

/* draw a vertical line */
__IPGUI_STATIC__ void ipgui_draw_ver_line(       
        ipgui_surf_t       * surf,
        ipgui_aabb_t       * clip,
        ipgui_line_t       * line,
        ipgui_line_style_t * style)
{
    ipgui_aabb_t self, draw;

    /* clip surf first */
    if (clip) {
        if (0 != ipgui_aabb_overlap(&draw, &surf->surf, clip))
            return;/* not intersect, then just return */
    } else {
        draw = surf->surf;
    }

    /* calc vertical line region */
    self.start.y = IPGUI_MIN(line->start.y, line->end.y);
    self.end.y   = IPGUI_MAX(line->start.y, line->end.y);
    self.start.x = line->start.x - (style->width >> 1);
    self.end.x   = self.start.x  + style->width - 1;

    /* secondly, clip self */
    if (0 != ipgui_aabb_overlap(&draw, &self, &draw))
        return;/* not intersect, then just return */

    ipgui_blend(
        surf, 
        (ipgui_aabb_t *)0,
        &draw, 
        &style->paint, 
        style->opacity,
        (u8_t *)0,
        (ipgui_aabb_t *)0,
        style->blend_mode);

    if (style->cap == IPGUI_LINE_CAP_ROUND) {
        /* draw circle endpoints */
        ipgui_arc_t circle;
        circle.cx = line->start.x;
        circle.cy = line->start.y;
        circle.dir = IPGUI_ARC_DRAW_DIR_CCW;
        circle.er = style->width >> 1;
        circle.ir = 0;
        circle.start = 0;
        circle.angle = 360;

        ipgui_arc_style_t circle_style;
        circle_style.blend_mode = style->blend_mode;
        circle_style.sep_type = IPGUI_ARC_ENDPOINT_TYPE_BUTT;
        circle_style.eep_type = IPGUI_ARC_ENDPOINT_TYPE_BUTT;
        circle_style.opacity  = style->opacity;
        circle_style.paint    = style->paint;
        ipgui_draw_arc(surf, clip, &circle, &circle_style);

        circle.cx = line->end.x;
        circle.cy = line->end.y;
        ipgui_draw_arc(surf, clip, &circle, &circle_style);
    }
}

extern void edge_clip_mask_with_aa(
    ipgui_edge_param_t   * edge,
    edge_halfplane_dir_t   edge_dir,
    u8_t                 * mask,
    ipgui_aabb_t         * mask_aabb,
    ipgui_coord_t          mask_stride);

__IPGUI_STATIC__ void init_cross_edge(
    ipgui_edge_wdf_param_t * param,
    u8_t                     se,/* 1 : edge start point, 0 : edge end point*/
    ipgui_edge_param_t     * res_param,
    edge_halfplane_dir_t   * res_dir)
{
    ipgui_coord_t deltax, deltay;
    ipgui_scoord_t cross_middle_x, cross_middle_y;

    /* y - y0 = (-dx/dy) * (x - x0) */ 
    deltax = 256; /* deltax = x - x0 */
    deltay = -param->dx * deltax / param->dy;
    if (deltay == 0) {
        if (param->dx > 0) deltay = -1;
        else deltay = 1;
    }

    if (se) {
        cross_middle_x = param->x1;
        cross_middle_y = param->y1;
    } else {
        cross_middle_x = param->x1 + param->dx;
        cross_middle_y = param->y1 + param->dy;
    }

    * res_param = ipgui_edge_param_init(
        (cross_middle_x + deltax) * 64,
        (cross_middle_y + deltay) * 64,
        (cross_middle_x - deltax) * 64,
        (cross_middle_y - deltay) * 64);

    if (param->dx > 0) { /* dx > 0 */
        if (se) * res_dir = EDGE_HALFPLANE_DIR_RIGHT;
        else * res_dir = EDGE_HALFPLANE_DIR_LEFT;
    } else { /* dx < 0 */
        if (se) * res_dir = EDGE_HALFPLANE_DIR_LEFT;
        else * res_dir = EDGE_HALFPLANE_DIR_RIGHT;
    }
}

__IPGUI_STATIC__ void ipgui_draw_skew_line(
    ipgui_surf_t       * surf,
    ipgui_aabb_t       * clip,
    ipgui_line_t       * line, 
    ipgui_line_style_t * style)/* style->cap must be IPGUI_LINE_CAP_BUTT */
{
    /* clip surf and get draw aabb */
    ipgui_aabb_t draw, self;
    if (clip) {
        if (0 != ipgui_aabb_overlap(&draw, &surf->surf, clip))
            return;/* not intersect, then just return */
    } else {
        draw = surf->surf;
    }

    /* calc line region and clip self with draw */
    ipgui_aabb_generate_with_points(&self, &line->start, 2);
    ipgui_aabb_expand(&self, (style->width >> 1) + (style->width & 1));

    if (0 != ipgui_aabb_overlap(&draw, &self, &draw))
        return;/* not intersect, then just return */

    /* allocate mask buffer first  */
    ipgui_coord_t draw_w, draw_h, res_h;

    draw_w = ipgui_aabb_width (&draw);
    draw_h = ipgui_aabb_height(&draw);

    u8_t * mask = ipgui_mask_buf_acquire(draw_w, draw_h, &res_h);
    if ((!res_h) || (!mask)) {
        ipgui_dbg_error("error: line rasterization failed to acquire mask buffer\r\n");
        return;
    }

    /* init edge wdf param */
    ipgui_edge_wdf_param_t param;
    param = ipgui_edge_wdf_param_init(
        line->start.x,
        line->start.y,
        line->end.x,
        line->end.y);

    /* generate mask description */
    ipgui_edge_wdf_mask_dsc_t mask_dsc;
    ipgui_gen_edge_wdf_mask_dsc(
        &mask_dsc,
        &param,
        draw.start.y,
        style->width);
    
    /* 生成端点横截线参数，用于裁剪edge wdf mask */
    ipgui_edge_param_t   cross_start;
    edge_halfplane_dir_t cross_start_dir;
    ipgui_edge_param_t   cross_end;
    edge_halfplane_dir_t cross_end_dir;
    init_cross_edge(&param, 1, &cross_start, &cross_start_dir);
    init_cross_edge(&param, 0, &cross_end,   &cross_end_dir  );

    ipgui_aabb_t mask_aabb;
    mask_aabb.start.x = draw.start.x;
    mask_aabb.end.x   = draw.end.x;
    ipgui_coord_t y   = draw.start.y;
    u8_t * mask_buf;
    while (draw_h > 0) {
        ipgui_coord_t current_h = IPGUI_MIN(draw_h, res_h);
        mask_aabb.start.y = y;
        mask_aabb.end.y   = y + current_h - 1;

        /* firstly, fill mask buffer with line edge wdf mask */
        mask_buf = mask;
        for (; y <= mask_aabb.end.y; y ++) {
            ipgui_edge_wdf_mask(&mask_dsc, mask_aabb.start.x, mask_buf, draw_w);
            ipgui_edge_wdf_mask_dsc_next_y(&mask_dsc);
            mask_buf += draw_w;
        }

        /* secondly, clip mask buffer with line endpoint cross edge halfspan mask */
        edge_clip_mask_with_aa(
            &cross_start,
            cross_start_dir,
            mask,
            &mask_aabb,
            draw_w
        );
        edge_clip_mask_with_aa(
            &cross_end,
            cross_end_dir,
            mask,
            &mask_aabb,
            draw_w
        );

        ipgui_blend(surf, 
            (ipgui_aabb_t *)0, 
            &mask_aabb,
            &style->paint,
            style->opacity,
            mask,
            &mask_aabb,
            style->blend_mode);

        draw_h -= current_h;
    }

    if (style->cap == IPGUI_LINE_CAP_ROUND) {
        /* draw circle endpoints */
        ipgui_arc_t circle;
        circle.cx = line->start.x;
        circle.cy = line->start.y;
        circle.dir = IPGUI_ARC_DRAW_DIR_CCW;
        circle.er = style->width >> 1;
        circle.ir = 0;
        circle.start = 0;
        circle.angle = 360;

        ipgui_arc_style_t circle_style;
        circle_style.blend_mode = style->blend_mode;
        circle_style.sep_type = IPGUI_ARC_ENDPOINT_TYPE_BUTT;
        circle_style.eep_type = IPGUI_ARC_ENDPOINT_TYPE_BUTT;
        circle_style.opacity  = style->opacity;
        circle_style.paint    = style->paint;
        ipgui_draw_arc(surf, clip, &circle, &circle_style);

        circle.cx = line->end.x;
        circle.cy = line->end.y;
        ipgui_draw_arc(surf, clip, &circle, &circle_style);
    }

    ipgui_mask_buf_free(mask);
}

__IPGUI_API__ void ipgui_draw_line_generic(       
    ipgui_surf_t       * surf,
    ipgui_aabb_t       * clip,
    ipgui_line_t       * line, 
    ipgui_line_style_t * style)
{
    if ((!surf) || (!line) || (!style))
        return;

    if ((style->opacity < 3) || (style->width < 1))
        return;

    if (line->start.x == line->end.x) 
        ipgui_draw_ver_line(surf, clip, line, style); 
    else if (line->start.y == line->end.y)
        ipgui_draw_hor_line(surf, clip, line, style);
    else
        ipgui_draw_skew_line(surf, clip, line, style);
}