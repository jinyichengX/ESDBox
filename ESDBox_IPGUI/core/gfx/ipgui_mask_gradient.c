#include "ipgui_mask_gradient.h"
#include "ipgui_memory.h"

/* 缩放因子 */
#define IPGUI_MASK_GRADIENT_SCALE_FACTOR 255.0f
#define IPGUI_MASK_GRADIENT_SCALE(v) (int)((v) * IPGUI_MASK_GRADIENT_SCALE_FACTOR)

#if IPGUI_MASK_GRADIENT_LUT_EN == 1
/* 遮罩插值查找表 - 用于快速混合 */
static unsigned char mask_blend_table[256][256] = {0};

__IPGUI_API__ __IPGUI_INIT__ void ipgui_mask_gradient_lut_init(void)
{
    for (int dist = 0; dist < 256; dist++) {
        for (int value = 0; value < 256; value++) {
            mask_blend_table[dist][value] = (dist * value + 127) / 255;
        }
    }
}
#endif

/* 内部工具：插值两个遮罩值 */
__IPGUI_STATIC__ __IPGUI_INLINE__ unsigned char interpolate_mask_value(
    unsigned char val1, unsigned char dist,
    unsigned char val2, unsigned char idist)
{
#if IPGUI_MASK_GRADIENT_LUT_EN == 1
    return mask_blend_table[dist][val2] + mask_blend_table[idist][val1];
#else
    return (unsigned char)(((int)val2 * dist + 127) / 255 + 
                           ((int)val1 * idist + 127) / 255);
#endif
}

/* ==================== 线性遮罩渐变实现 ==================== */

__IPGUI_API__ void ipgui_liner_mask_gradient_init(
    ipgui_liner_mask_gradient_t * gradient,
    float x_start, float y_start,
    float x_end, float y_end)
{
    gradient->opacity = 255;
    gradient->stop_nr = 0;
    gradient->x_start = IPGUI_MASK_GRADIENT_SCALE(x_start);
    gradient->y_start = IPGUI_MASK_GRADIENT_SCALE(y_start);
    gradient->x_end = IPGUI_MASK_GRADIENT_SCALE(x_end);
    gradient->y_end = IPGUI_MASK_GRADIENT_SCALE(y_end);
}

__IPGUI_API__ void ipgui_liner_mask_gradient_init_direct(
    ipgui_liner_mask_gradient_t * gradient,
    ipgui_coord_t x_start, ipgui_coord_t y_start,
    ipgui_coord_t x_end, ipgui_coord_t y_end)
{
    gradient->opacity = 255;
    gradient->stop_nr = 0;
    gradient->x_start = x_start;
    gradient->y_start = y_start;
    gradient->x_end = x_end;
    gradient->y_end = y_end;
    gradient->x_start_abs = x_start;
    gradient->y_start_abs = y_start;
    gradient->x_end_abs = x_end;
    gradient->y_end_abs = y_end;
    gradient->gradient_vector.x = x_end - x_start;
    gradient->gradient_vector.y = y_end - y_start;
    gradient->gradient_vec_mod_pow = gradient->gradient_vector.x * gradient->gradient_vector.x;
    gradient->gradient_vec_mod_pow += gradient->gradient_vector.y * gradient->gradient_vector.y;
    if (gradient->gradient_vec_mod_pow == 0) gradient->gradient_vec_mod_pow = 1;/* 防止0做除数 */
}

__IPGUI_API__ int ipgui_liner_mask_gradient_add_stop(
    ipgui_liner_mask_gradient_t * gradient,
    ipgui_mask_gradient_stop_t * stop)
{
    if (gradient->stop_nr >= IPGUI_GRADIENT_STOP_MAX)
        return -1;

    int stop_nr = gradient->stop_nr;
    ipgui_mask_gradient_stop_t * iter;
    int idx = 0;
    
    for (; idx < stop_nr; idx++) {
        iter = &gradient->stops[idx];
        if (iter->pos > stop->pos) {
            for (int i = stop_nr; i > idx; i--) {
                gradient->stops[i].value = gradient->stops[i - 1].value;
                gradient->stops[i].pos = gradient->stops[i - 1].pos;
            }
            break;
        }
    }
    gradient->stops[idx].value = stop->value;
    gradient->stops[idx].pos = stop->pos;
    gradient->stop_nr++;

    return 0;
}

__IPGUI_API__ int ipgui_liner_mask_gradient_remove_stop(
    ipgui_liner_mask_gradient_t * gradient,
    unsigned char pos)
{
    int stop_nr = gradient->stop_nr;
    ipgui_mask_gradient_stop_t * iter;
    int idx;

    for (idx = 0; idx < stop_nr; idx++) {
        iter = &gradient->stops[idx];
        if (iter->pos == pos) {
            for (int i = idx + 1; i < stop_nr; i++) {
                gradient->stops[i - 1].pos = gradient->stops[i].pos;
                gradient->stops[i - 1].value = gradient->stops[i].value;
            }
            gradient->stop_nr--;
            return 0;
        }
    }
    return -1;
}

__IPGUI_API__ void ipgui_liner_mask_gradient_set_opacity(
    ipgui_liner_mask_gradient_t * gradient,
    unsigned char opacity)
{
    gradient->opacity = opacity;
}

__IPGUI_API__ void ipgui_liner_mask_gradient_apply_to_aabb(
    ipgui_liner_mask_gradient_t * gradient,
    ipgui_aabb_t * aabb)
{
    gradient->aabb.start.x = aabb->start.x;
    gradient->aabb.start.y = aabb->start.y;
    gradient->aabb.end.x = aabb->end.x;
    gradient->aabb.end.y = aabb->end.y;

    ipgui_coord_t w = aabb->end.x - aabb->start.x + 1;
    ipgui_coord_t h = aabb->end.y - aabb->start.y + 1;
    
    gradient->x_start_abs = aabb->start.x + (gradient->x_start * w + 127) / 255;
    gradient->y_start_abs = aabb->start.y + (gradient->y_start * h + 127) / 255;
    gradient->x_end_abs = aabb->start.x + (gradient->x_end * w + 127) / 255;
    gradient->y_end_abs = aabb->start.y + (gradient->y_end * h + 127) / 255;

    gradient->gradient_vector.x = gradient->x_end_abs - gradient->x_start_abs;
    gradient->gradient_vector.y = gradient->y_end_abs - gradient->y_start_abs;

    gradient->gradient_vec_mod_pow = gradient->gradient_vector.x * gradient->gradient_vector.x;
    gradient->gradient_vec_mod_pow += gradient->gradient_vector.y * gradient->gradient_vector.y;
    if (gradient->gradient_vec_mod_pow == 0) gradient->gradient_vec_mod_pow = 1;/* 防止0做除数 */
}

__IPGUI_STATIC__ __IPGUI_INLINE__ unsigned char liner_first_stop_value(
    ipgui_liner_mask_gradient_t * gradient)
{
    return gradient->stops[0].value;
}

__IPGUI_STATIC__ __IPGUI_INLINE__ unsigned char liner_last_stop_value(
    ipgui_liner_mask_gradient_t * gradient)
{
    return gradient->stops[gradient->stop_nr - 1].value;
}

__IPGUI_API__ unsigned char ipgui_liner_mask_gradient_value_get(
    ipgui_liner_mask_gradient_t * gradient,
    unsigned char pos)
{
    if (gradient->stop_nr < 2)
        return 0;

    if (pos <= gradient->stops[0].pos) {
        return liner_first_stop_value(gradient);
    }

    for (int idx = 1; idx < gradient->stop_nr; idx++) {
        ipgui_mask_gradient_stop_t * iter = &gradient->stops[idx];

        if (iter->pos >= pos) {
            if (iter->pos == pos) {
                return iter->value;
            } else {
                ipgui_mask_gradient_stop_t * last = &gradient->stops[idx - 1];
                unsigned char delta = iter->pos - last->pos;
                unsigned char dist = (pos - last->pos) * 255 / delta;
                unsigned char idist = 255 - dist;
                
                return interpolate_mask_value(last->value, dist, iter->value, idist);
            }
        }
    }
    
    return liner_last_stop_value(gradient);
}

__IPGUI_API__ unsigned char ipgui_get_liner_mask_gradient_pos_at_xy(
    ipgui_liner_mask_gradient_t * gradient,
    ipgui_coord_t x, ipgui_coord_t y)
{
    ipgui_vector_t start2p;
    start2p.x = x - gradient->x_start_abs;
    start2p.y = y - gradient->y_start_abs;

    int v_dot = start2p.x * gradient->gradient_vector.x +
                start2p.y * gradient->gradient_vector.y;
    
    int proj_int = v_dot / gradient->gradient_vec_mod_pow;
    int proj_frac = v_dot % gradient->gradient_vec_mod_pow;

    if ((proj_int < 0) || (proj_frac < 0)) {
        return 0;
    } else if (proj_int >= 1) {
        return 255;
    } else {
        return (unsigned char)(proj_frac * 255 / gradient->gradient_vec_mod_pow);
    }
}

__IPGUI_API__ unsigned char ipgui_liner_mask_gradient_value_at_xy(
    ipgui_liner_mask_gradient_t * gradient,
    ipgui_coord_t x, ipgui_coord_t y)
{
    unsigned char pos = ipgui_get_liner_mask_gradient_pos_at_xy(gradient, x, y);
    unsigned char value = ipgui_liner_mask_gradient_value_get(gradient, pos);
    
    /* 应用整体不透明度影响 */
    return (unsigned char)((value * gradient->opacity) >> 8);
}

/* ==================== 径向遮罩渐变实现 ==================== */

__IPGUI_API__ void ipgui_radial_mask_gradient_init(
    ipgui_radial_mask_gradient_t * gradient,
    ipgui_coord_t center_x, ipgui_coord_t center_y,
    ipgui_coord_t radius)
{
    gradient->center_x = center_x;
    gradient->center_y = center_y;
    gradient->radius = radius;
    gradient->radius_pow = radius * radius;
    gradient->opacity = 255;
    gradient->stop_nr = 0;
}

__IPGUI_API__ int ipgui_radial_mask_gradient_add_stop(
    ipgui_radial_mask_gradient_t * gradient,
    ipgui_mask_gradient_stop_t * stop)
{
    if (gradient->stop_nr >= IPGUI_GRADIENT_STOP_MAX)
        return -1;

    int stop_nr = gradient->stop_nr;
    ipgui_mask_gradient_stop_t * iter;
    int idx = 0;
    
    for (; idx < stop_nr; idx++) {
        iter = &gradient->stops[idx];
        if (iter->pos > stop->pos) {
            for (int i = stop_nr; i > idx; i--) {
                gradient->stops[i].value = gradient->stops[i - 1].value;
                gradient->stops[i].pos = gradient->stops[i - 1].pos;
            }
            break;
        }
    }
    gradient->stops[idx].value = stop->value;
    gradient->stops[idx].pos = stop->pos;
    gradient->stop_nr++;

    return 0;
}

__IPGUI_API__ int ipgui_radial_mask_gradient_remove_stop(
    ipgui_radial_mask_gradient_t * gradient,
    unsigned char pos)
{
    int stop_nr = gradient->stop_nr;
    ipgui_mask_gradient_stop_t * iter;
    int idx;

    for (idx = 0; idx < stop_nr; idx++) {
        iter = &gradient->stops[idx];
        if (iter->pos == pos) {
            for (int i = idx + 1; i < stop_nr; i++) {
                gradient->stops[i - 1].pos = gradient->stops[i].pos;
                gradient->stops[i - 1].value = gradient->stops[i].value;
            }
            gradient->stop_nr--;
            return 0;
        }
    }
    return -1;
}

__IPGUI_API__ void ipgui_radial_mask_gradient_set_opacity(
    ipgui_radial_mask_gradient_t * gradient,
    unsigned char opacity)
{
    gradient->opacity = opacity;
}

__IPGUI_API__ unsigned char ipgui_radial_mask_gradient_value_get(
    ipgui_radial_mask_gradient_t * gradient,
    unsigned char pos)
{
    if (gradient->stop_nr < 2)
        return 0;

    if (pos <= gradient->stops[0].pos) {
        return gradient->stops[0].value;
    }

    for (int idx = 1; idx < gradient->stop_nr; idx++) {
        ipgui_mask_gradient_stop_t * iter = &gradient->stops[idx];

        if (iter->pos >= pos) {
            if (iter->pos == pos) {
                return iter->value;
            } else {
                ipgui_mask_gradient_stop_t * last = &gradient->stops[idx - 1];
                unsigned char delta = iter->pos - last->pos;
                unsigned char dist = (pos - last->pos) * 255 / delta;
                unsigned char idist = 255 - dist;
                
                return interpolate_mask_value(last->value, dist, iter->value, idist);
            }
        }
    }
    
    return gradient->stops[gradient->stop_nr - 1].value;
}

__IPGUI_API__ unsigned char ipgui_get_radial_mask_gradient_pos_at_xy(
    ipgui_radial_mask_gradient_t * gradient,
    ipgui_coord_t x, ipgui_coord_t y)
{
    int dx = x - gradient->center_x;
    int dy = y - gradient->center_y;
    int dp = dx * dx + dy * dy;
    
    if (dp >= gradient->radius_pow) 
        return 255;
    
    /* 计算距离比例 */
    unsigned int temp = (unsigned int)dp * 255U / gradient->radius;
    unsigned int pos_sq = temp * 255U / gradient->radius;
    
    /* 查找平方根 */
    unsigned char pos = 0;
    while (pos < 255) {
        if (((pos + 1) * (pos + 1)) > pos_sq)
            break;
        pos++;
    }
    
    return pos;
}

__IPGUI_API__ unsigned char ipgui_radial_mask_gradient_value_at_xy(
    ipgui_radial_mask_gradient_t * gradient,
    ipgui_coord_t x, ipgui_coord_t y)
{
    unsigned char pos = ipgui_get_radial_mask_gradient_pos_at_xy(gradient, x, y);
    unsigned char value = ipgui_radial_mask_gradient_value_get(gradient, pos);
    
    /* 应用整体不透明度影响 */
    return (unsigned char)((value * gradient->opacity) >> 8);
}