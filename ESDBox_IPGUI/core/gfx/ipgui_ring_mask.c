#include "ipgui_ring_mask.h"
#include "ipgui_memory.h"

#define DEBUG_CORNER_MASK 1

#if DEBUG_CORNER_MASK == 1
#warning "DEBUG_CORNER_MASK is enabled"
#endif

#if DEBUG_CORNER_MASK == 1
#include "ipgui_debug.h"
#endif

LIST_HEAD(g_out_corner_mask_cache);
static u32_t g_corner_cache_tick = 0;
static void normalize_corner_cache_tick(void);
static u32_t corner_cache_item_cnt = 0;

/* out corner mask table */
static const u8_t oc1_mask[1] = {
    149  /* row 1 y = 1 */
};

static const u8_t oc2_mask[4] = {
    255, 194, /* row 1 y = 1 */
    194,  43  /* row 2 y = 2 */
};

static const u8_t oc3_mask[9] = {
    255, 255, 213, /* row 1 y = 1 */
    255, 255, 100, /* row 2 y = 2 */
    213, 100,   0  /* row 3 y = 3 */
};

static const u8_t oc4_mask[16] = {
    255, 255, 255, 223, /* row 1 y = 1 */
    255, 255, 255, 134, /* row 2 y = 2 */
    255, 255, 193,   0, /* row 3 y = 3 */
    223, 134,   0,   0  /* row 4 y = 4 */
};

static const u8_t oc5_mask[25] = {
    255, 255, 255, 255, 229, /* row 1 y = 1 */
    255, 255, 255, 255, 156, /* row 2 y = 2 */
    255, 255, 255, 255,  43, /* row 3 y = 3 */
    255, 255, 255,  87,   0, /* row 4 y = 4 */
    229, 156,  43,   0,   0  /* row 5 y = 5 */
};

static const u8_t oc6_mask[36] = {
    255, 255, 255, 255, 255, 233, /* row 1 y = 1 */
    255, 255, 255, 255, 255, 172, /* row 2 y = 2 */
    255, 255, 255, 255, 255,  74, /* row 3 y = 3 */
    255, 255, 255, 255, 152,   0, /* row 4 y = 4 */
    255, 255, 255, 152,   0,   0, /* row 5 y = 5 */
    233, 172,  74,   0,   0,   0  /* row 6 y = 6 */
};

static const u8_t ic1_mask[4] = {
    105, 255,
    255, 255,
};

static const u8_t ic2_mask[9] = {
      0,  60, 255,
     60, 211, 255,
    255, 255, 255,
};

static const u8_t ic3_mask[16] = {
      0,   0,  41, 255,
      0,   0, 154, 255,
     41, 154, 255, 255,
    255, 255, 255, 255,
};

static const u8_t ic4_mask[25] = {
      0,   0,   0,  31, 255,
      0,   0,   0, 120, 255,
      0,   0,  61, 255, 255,
     31, 120, 255, 255, 255,
    255, 255, 255, 255, 255,
};

static const u8_t ic5_mask[36] = {
      0,   0,   0,   0,  25, 255,
      0,   0,   0,   0,  98, 255,
      0,   0,   0,   0, 211, 255,
      0,   0,   0, 167, 255, 255,
     25,  98, 211, 255, 255, 255,
    255, 255, 255, 255, 255, 255,
};

void out_corner_param_init(corner_param_t * p, ipgui_coord_t r)
{
    p->r       = r;
    p->r2      = r * r;
    p->rmax2   = (r + 1) * (r + 1);
    p->rmin2   = (r - 1) * (r - 1);
    p->inv_out = (0xffff) / (p->rmax2 - p->r2);
    p->inv_in  = (0xffff) / (p->r2 - p->rmin2);
}

ipgui_coord_t corner_left_dig_xy(corner_param_t * p)
{
    /* guess first, res ≈ r / sqrt(2) */
    u32_t res = ((p->r * 0x5a82) + 114) >> 15;

    if ((2 * res * res) > p->rmin2) {/* outside the circle edge */
        while (-- res) {
            if ((2 * res * res) < p->rmin2) {
                res ++;
                break;
            }
        }
    } else if ((2 * res * res) < p->rmin2) {
        while (++ res) {
            if ((2 * res * res) > p->rmin2) {
                break;
            }
        }
    } /* dont worry, there is no third case */
    return (ipgui_coord_t)res;
}

void * ipgui_alloc_corner_mask_item(u32_t size)
{
    if (corner_cache_item_cnt >= CORNER_CACHE_ITEM_MAX_NUM)
        return (void *)0;
    void * p = ipgui_mem_alloc_def(size);
    if (p) corner_cache_item_cnt ++;
    return p;
}

void ipgui_free_corner_mask_item(void * p)
{
    if (p) {
        ipgui_mem_free_def(p);
        if (corner_cache_item_cnt > 0) {
            corner_cache_item_cnt --;
        }
    }
}

/* generate corner mask */
corner_mask_cache_item_t * ipgui_gen_corner_mask_cache_item(corner_param_t * p)
{
    ipgui_coord_t left_dig_xy;

    left_dig_xy = corner_left_dig_xy(p);

    /* alloc memory for out corner mask cache */
    corner_mask_cache_item_t * item = 
    (corner_mask_cache_item_t *)ipgui_alloc_corner_mask_item(
    sizeof(corner_mask_cache_item_t) /* manager */
    + (sizeof(u16_t) + sizeof(ipgui_coord_t)) * left_dig_xy /* index buffer */ 
    + p->r * 2 + 12 /* mask buffer(经验值) */);

    if (!item) return item;

    /* init item manager */
    item->r = p->r;
    item->dig_mask_start_xy = left_dig_xy;
    list_head_init(&item->node);
    list_add(&item->node, &g_out_corner_mask_cache);
    if (g_corner_cache_tick >= 0xffffff00U)
        normalize_corner_cache_tick();
    item->last_used_tick = ++ g_corner_cache_tick;
    item->refcnt = 0;

    u8_t * ptr = (u8_t *)item + sizeof(corner_mask_cache_item_t);
    /* 对齐，防止处理器不支持非对齐访问 */
    ptr = (u8_t *)IPGUI_ALIGN((uintptr_t )ptr, sizeof(u16_t));
    item->mask_index_at_y = (u16_t *)ptr;

    /* 对齐，防止处理器不支持非对齐访问 */
    ptr += (sizeof(u16_t) * (left_dig_xy - 1));
    ptr = (u8_t *)IPGUI_ALIGN((uintptr_t )ptr, sizeof(ipgui_coord_t));
    item->mask_start_x_at_y = (ipgui_coord_t *)ptr;

    ptr += (sizeof(ipgui_coord_t) * (left_dig_xy - 1));
    item->mask = ptr;

    u8_t mask_temp[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    u8_t mask_num_in_temp = 0;
    u8_t mask_index_temp;
    u32_t mask_num_recorded = 0;

    ipgui_coord_t startx; /* startx是第一象限八分之一圆每行第一个需要做抗锯齿的点的x坐标 */
    ipgui_coord_t x, y;

    u8_t mask;
    u32_t temp, y2;
    /* start from cartesian coordinates's (radius - 1, 1)  */
    x = p->r - 1; 
    y = 1/* current y */;
    while (1) {
        mask_index_temp = 4;
        mask_num_in_temp = 0;
        item->mask_index_at_y[y - 1] = mask_num_recorded;
        y2 = y * y;

        /* if x2 + y2 > r2, 
         * then (x, y) is outside the circle
         */
        if ((x * x + y2) > p->r2) {
            x --;
        }

        startx = x + 1;
        while (1) {
            /* calculate the mask */
            x ++;
            if (x > p->r)
                break;
            temp = x * x + y2;

            if (p->rmax2 < temp)
                break; /* for example: (10,5) will trigger this */

            mask = ((p->rmax2 - temp) * p->inv_out) >> 8;
            if (mask == 0) 
                break;
            
            mask_temp[mask_index_temp ++] = mask;
            mask_num_in_temp ++;
        }

        x = startx;
        mask_index_temp = 3;
        u8_t in_mask_cnt = 0;
        while (1) {
            x --;
            temp = x * x + y2;

            if (p->rmin2 > temp)
                break;

            mask = ((temp - p->rmin2) * p->inv_in) >> 8;
            if (mask == 0)
                break;
            
            mask_temp[mask_index_temp --] = mask;
            mask_num_in_temp ++;
            in_mask_cnt ++;
        }

        mask_index_temp ++;
        /* record the mask */
        for (int i = 0; i < mask_num_in_temp; i ++) {
            item->mask[mask_num_recorded ++] = mask_temp[mask_index_temp + i];
        }

        item->mask_start_x_at_y[y - 1] = startx - in_mask_cnt;

        /* go to next row */
        if (++ y == left_dig_xy)
            break;
        x = startx - 1;
    }

    item->dig_mask_index = mask_num_recorded;

#define gen_mask(xx, yy)\
do {\
    temp = xx * xx + yy * yy;\
    if (p->rmax2 <= temp) mask = 0;\
    else if (p->rmin2 >= temp) mask = 0;\
    else if (temp < p->r2)\
        mask = ((temp - p->rmin2) * p->inv_in) >> 8;\
    else if (temp > p->r2)\
        mask = ((p->rmax2 - temp) * p->inv_out) >> 8;\
    else mask = 255;\
}while(0)

    /* 从左下到右上记录 */
    ipgui_coord_t xx, yy;
    ipgui_coord_t end_xy = left_dig_xy + 2;
    for (yy = left_dig_xy; yy <= end_xy; yy ++)
    for (xx = left_dig_xy; xx <= end_xy; xx ++) {
        /* only need to record 4 points */
        gen_mask(xx, yy);
        item->mask[mask_num_recorded ++] = mask;
    }
#undef gen_mask

    return item;
}

static void normalize_corner_cache_tick(void)
{
    u32_t prev_old_tick = 0;
    u32_t new_tick = 1;

    while (1) {
        struct list_head *iter;
        corner_mask_cache_item_t *best = (corner_mask_cache_item_t *)0;
        u32_t best_old_tick = 0xffffffffU;

        list_for_each(iter, &g_out_corner_mask_cache) {
            corner_mask_cache_item_t *item =
                list_entry(iter, corner_mask_cache_item_t, node);

            if ((item->last_used_tick > prev_old_tick) &&
                (item->last_used_tick < best_old_tick)) {
                best_old_tick = item->last_used_tick;
                best = item;
            }
        }

        if (!best)
            break;

        prev_old_tick = best_old_tick;
        best->last_used_tick = new_tick ++;
    }

    g_corner_cache_tick = new_tick - 1;
}

/* 淘汰最老的或未被使用的item，return 0表示淘汰了一个 */
static int evict_oldest_cache(void)
{
    struct list_head * iter;
    corner_mask_cache_item_t * oldest = (corner_mask_cache_item_t *)0;

    list_for_each(iter, &g_out_corner_mask_cache) {
        corner_mask_cache_item_t * item = list_entry(iter, corner_mask_cache_item_t, node);
        if (item->refcnt == 0) { /* 优先淘汰未被使用的cache item */
            if (!oldest || item->last_used_tick < oldest->last_used_tick) {
                oldest = item;
            }
        }
    }

    if (oldest) {
        list_del(&oldest->node);
        ipgui_free_corner_mask_item(oldest);
        return 0;
    } else {
        return -1;
    }
}

static corner_mask_cache_item_t * get_corner_mask_cache_item(ipgui_coord_t r, s8_t out)
{
    corner_mask_cache_item_t * item;
    item = (corner_mask_cache_item_t *)0;
    struct list_head * iter;
    list_for_each(iter, &g_out_corner_mask_cache) {
        item = list_entry(iter, corner_mask_cache_item_t, node);
        if (item->r == (out ? r : r + 1)) {
            /* 可能下一次渲染还需要，插到最前面，加快查找效率 */
            list_del(&item->node);
            list_add(&item->node, &g_out_corner_mask_cache);

            if (g_corner_cache_tick >= 0xffffff00U)
                normalize_corner_cache_tick();
            item->last_used_tick = ++ g_corner_cache_tick;
            return item;
        }
    }

    /* alloc memory for item */
    corner_param_t param;
    out_corner_param_init(&param, out ? r : r + 1);
    int res = 0;
    int try_cnt = 0; /* 防止当所有item都被引用且内存不够分配新item时引起的死循环 */
    while (res == 0 && ((try_cnt ++) < CORNER_CACHE_ITEM_MAX_NUM)) {
        /* then try to alloc item cache again */
        item = ipgui_gen_corner_mask_cache_item(&param);
        if (item) break;
        
        /* 淘汰未被使用的最老的item */
        res = evict_oldest_cache();
    }

    return item;
}

static inline u32_t get_y_mask_index(ipgui_coord_t y, corner_mask_cache_item_t * item)
{
    return item->mask_index_at_y[y - 1];
}

static inline ipgui_coord_t get_start_x_at_y(ipgui_coord_t y, corner_mask_cache_item_t * item)
{
    return item->mask_start_x_at_y[y - 1];
}

static inline ipgui_coord_t get_y_mask_num(ipgui_coord_t y, corner_mask_cache_item_t * item)
{
    if (y == item->dig_mask_start_xy - 1) {
        return item->dig_mask_index - item->mask_index_at_y[y - 1];
    } else { 
        return item->mask_index_at_y[y] - item->mask_index_at_y[y - 1];
    }
}

static inline int point_outside_radius(ipgui_coord_t x, ipgui_coord_t y, ipgui_coord_t r)
{
    return (x * x + y * y) > (r * r);
}

/* x和y都是从1开始有效，如果是0那么就偏移-1 */
/* fetch mask from corner mask cache item */
static int ipgui_fetch_out_corner_mask_from_cache(
    ipgui_coord_t x, ipgui_coord_t y, ipgui_coord_t r,
    s8_t step,/* step = 1/-1 */
    u8_t * mask, u16_t len)
{
    corner_mask_cache_item_t * cache_item;
    /* use cache or generate new one */
    cache_item = get_corner_mask_cache_item(r, 1);
    if (!cache_item)
        return -1;

    cache_item->refcnt ++;
    if (y >= cache_item->dig_mask_start_xy) {
        ipgui_coord_t left_dig_xy = cache_item->dig_mask_start_xy;
        ipgui_coord_t end_xy      = left_dig_xy + 2;
        ipgui_coord_t grid_w      = end_xy - left_dig_xy + 1; /* = 3 */

        for (int i = 0; i < len; i ++) {
            /* step 决定坐标方向 */
            ipgui_coord_t cx = (step == 1) ? (x + i) : (x - i);

            if (cx < left_dig_xy) {
                /* 对称坐标查普通行缓存 */
                ipgui_coord_t qx  = y;
                ipgui_coord_t qy  = cx;

                // u32_t         idx = get_y_mask_index(qy, cache_item);
                ipgui_coord_t sx  = get_start_x_at_y(qy, cache_item);
                // ipgui_coord_t num = get_y_mask_num(qy, cache_item);

                if (qx < sx) {
                    mask[i] = 255;
                } else if (qx >= sx + get_y_mask_num(qy, cache_item)) {
                    mask[i] = 0;
                } else {
                    /* 落在mask范围内但点在圆内，仍返回255 */
                    if (!point_outside_radius(qx, qy, cache_item->r))
                        mask[i] = 255;
                    else
                        mask[i] = cache_item->mask[get_y_mask_index(qy, cache_item) + (qx - sx)];
                }
            } else {
                /* 查对角线小网格 */
                if (cx <= end_xy && y <= end_xy) {
                    u8_t m = cache_item->mask[
                        cache_item->dig_mask_index +
                        (y  - left_dig_xy) * grid_w +
                        (cx - left_dig_xy)];
                    /* 同理，圆内点直接给255 */
                    if (!point_outside_radius(cx, y, cache_item->r))
                        mask[i] = 255;
                    else
                        mask[i] = m;
                } else {
                    mask[i] = 0;
                }
            }
        }
    } else {
        u16_t         idx = get_y_mask_index(y, cache_item);
        ipgui_coord_t sx  = get_start_x_at_y(y, cache_item);
        ipgui_coord_t num = get_y_mask_num(y, cache_item);
        ipgui_coord_t ex  = sx + num - 1;
        if(step == 1) {
            /* fill inside sx */
            if (x < sx) {
                u16_t inside_len = sx - x;
                inside_len       = inside_len > len ? len : inside_len;
                ipgui_memset(mask, 255, inside_len);
                len  -= inside_len;
                if (!len)
                    goto _return;
                mask += inside_len;
                x    = sx;
            }
#if DEBUG_CORNER_MASK == 1
            if (len > r) ipgui_dbg_error("err: len: %d\r\n", len);
#endif
            /* fill inside ex */
            if (x <= ex) {
                u16_t mask_len    = ex - x + 1;
                mask_len          = mask_len > len ? len : mask_len;
                u16_t index_start = idx + (x - sx);
                for (int i = 0; i < mask_len; i ++) {
                    if (!point_outside_radius(x + i, y, r))
                        mask[i] = 255;
                    else 
                        mask[i] = cache_item->mask[index_start + i];
                }
                len  -= mask_len;
                if (!len)
                    goto _return;
                mask += mask_len;
                x    = ex + 1;
            }
#if DEBUG_CORNER_MASK == 1
            if (len == 0) ipgui_dbg_error("err: len is 0 \r\n");
#endif
            /* fill outside ex */
            ipgui_memset(mask, 0, len);
        } else if (step == -1) {
            if (x > ex) {
                u16_t outside_len = x - ex;
                outside_len       = outside_len > len ? len : outside_len;
                ipgui_memset(mask, 0, outside_len);
                len  -= outside_len;
                if (!len)
                    goto _return;
                mask += outside_len;
                x    = ex;
            }
#if DEBUG_CORNER_MASK == 1
            if (len > r) ipgui_dbg_error("err: len: %d\r\n", len);
#endif
            if (x >= sx) {
                u16_t mask_len = x - sx + 1;
                mask_len       = mask_len > len ? len : mask_len;
                for (int i = 0; i < mask_len; i ++) {
                    /* 索引随 x 递减，每步减 1 */
                    if (!point_outside_radius(x - i, y, r))
                        mask[i] = 255;
                    else
                        mask[i] = cache_item->mask[idx + (x - i - sx)];
                }
                len  -= mask_len;
                if (!len)
                    goto _return;
                mask += mask_len;
                x    = sx - 1;
            }
#if DEBUG_CORNER_MASK == 1
            if (len == 0) ipgui_dbg_error("err: len is 0 \r\n");
#endif
            ipgui_memset(mask, 255, len);
        }
    }
_return:
    cache_item->refcnt --;
    return 0;
}

#if DEBUG_CORNER_MASK == 1
void print_item_list(void) 
{
    corner_mask_cache_item_t * item;
    struct list_head * iter;
    list_for_each(iter, &g_out_corner_mask_cache) {
        item = list_entry(iter, corner_mask_cache_item_t, node);
        ipgui_dbg_error("item: %p, r: %d, refcnt: %d, last used time: %d\r\n", item, item->r, item->refcnt, item->last_used_tick);
    }
}
#endif

static ipgui_err_t ipgui_fetch_out_corner_mask(
    ipgui_coord_t x, ipgui_coord_t y, ipgui_coord_t r,
    s8_t step,/* step = 1/-1 */
    u8_t * mask, u16_t len)
{
    if (r < 7) {
        /* fetch from oc mask table */
        static const u8_t * oc_masks[] = {
            oc1_mask, oc2_mask, oc3_mask,
            oc4_mask, oc5_mask, oc6_mask
        };
        const u8_t * tbl = oc_masks[r - 1];
        ipgui_coord_t cx;
        for (int i = 0; i < len; i ++) {
            cx = x + (ipgui_coord_t)(step * i);
            mask[i] = tbl[(y - 1) * r + (cx - 1)];
        }
        return IPGUI_ERR_OK;
    }
    if (0 == ipgui_fetch_out_corner_mask_from_cache(x, y, r, step, mask, len))
        return IPGUI_ERR_OK;

    /* calculate mask directly */
#if DEBUG_CORNER_MASK == 1
    ipgui_dbg_error("not support calculate outer mask directly now!!!\r\n");
#endif

    return IPGUI_ERR_NOK;
}

/* 成功返回0，失败返回-1 */ 
int ipgui_fetch_ring_mask(ipgui_coord_t x, ipgui_coord_t y, 
    ipgui_coord_t ir, ipgui_coord_t er,
    s8_t step,/* step = 1/-1 */
    u8_t * mask, u16_t len)
{
    if ((y < 1) || (y > er))
        return 0;

    if ((ir >= er) || (er <= 0) || (ir < 0))
        return 0;

    if ((!mask) || (!len))
        return -1;    

    /* clip mask buffer */
    if (step == 1) {
        if (x > er) return 0; /* out of radius right side */
        if (x < 1) { /* out of radius left side */
            if ((x + len) <= 1)
                return 0;
            len  -= (1 - x);
            mask += (1 - x);
            x    =  1;
        }
        if ((x + len - 1) > er) len -= (x + len - 1 - er);
    } else if (step == -1) {
        if (x < 1) return 0;                   /* 完全在有效范围左侧 */
        if (x > er) {                            /* 右侧超出，跳过头部 */
            ipgui_coord_t skip = x - er;
            if (skip >= (ipgui_coord_t)len) return 0;
            mask += skip;
            len  -= (u16_t)skip;
            x    =  er;
        }
        if ((x - (ipgui_coord_t)len + 1) < 1)  /* 左侧超出，截断尾部 */
            len = (u16_t)x;
    }

    /* fetch outside corner mask */
    if (IPGUI_ERR_OK != ipgui_fetch_out_corner_mask(x, y, er, step, mask, len))
        return -1;
    
    if (ir <= 0) return 0;
    
    /* fetch from const table first */
    if (ir < 6) {
        static const u8_t * ic_masks[] = {
            ic1_mask, ic2_mask, ic3_mask, ic4_mask, ic5_mask
        };

        const u8_t * tbl = ic_masks[ir - 1];
        ipgui_coord_t side = ir + 1;

        if (y < 1 || y > side)
            return 0;

        for (int i = 0; i < len; i ++) {
            ipgui_coord_t cx = (step == 1) ? (x + i) : (x - i);

            if (cx < 1 || cx > side) {
                if (step == 1 && cx > side)
                    break;
                continue;
            }

            u8_t m = tbl[(y - 1) * side + (cx - 1)];

            if (m == 255) {
                if (step == 1)
                    break;   /* 后面也都是255，不用再改 */
                continue;    /* step=-1 不能break，只能跳过 */
            }

            mask[i] = m;     /* 用inner mask覆盖outer mask */
        }

        return 0;
    }

    /* try to fetch from cache when ir > 6 */
    corner_mask_cache_item_t * cache_item;
    cache_item = get_corner_mask_cache_item(ir, 0);
    if (!cache_item) {
        /* calculate mask directly */
#if DEBUG_CORNER_MASK == 1
    ipgui_dbg_error("not support calculate inner mask directly now!!!\r\n");
#endif

        return -1;
    }
    cache_item->refcnt ++;
    if (y >= cache_item->dig_mask_start_xy) {
        ipgui_coord_t end_xy      = cache_item->dig_mask_start_xy + 2;
        ipgui_coord_t grid_w      = end_xy - cache_item->dig_mask_start_xy + 1; /* = 3 */
        for (int i = 0; i < len; i ++) {
            ipgui_coord_t cx = (step == 1) ? (x + i) : (x - i);

            if (cx < cache_item->dig_mask_start_xy) {
                /* 对称坐标查普通行缓存 */
                ipgui_coord_t qx  = y;
                ipgui_coord_t qy  = cx;

                // u32_t         idx = get_y_mask_index(qy, cache_item);
                ipgui_coord_t sx  = get_start_x_at_y(qy, cache_item);
                ipgui_coord_t num = get_y_mask_num(qy, cache_item);
                ipgui_coord_t ex  = sx + num - 1;

                if (qx < sx) {
                    mask[i] = 0;
                } else if (qx > ex) {
                    /* keep outside corner mask */
                } else {
                    /* 过渡区，用inner的边缘mask值 */
                    if (!point_outside_radius(qx, qy, cache_item->r))
                        mask[i] = cache_item->mask[get_y_mask_index(qy, cache_item) + (qx - sx)];
                }
            } else {
                /* 查对角线小网格 */
                if (cx <= end_xy && y <= end_xy) {
                    u8_t m = cache_item->mask[
                        cache_item->dig_mask_index +
                        (y  - cache_item->dig_mask_start_xy) * grid_w +
                        (cx - cache_item->dig_mask_start_xy)];

                    if (m) {
                        /* 过渡区，使用inner边缘mask */
                        if (!point_outside_radius(cx, y, cache_item->r))
                            mask[i] = m;
                    }
                }
            }
        }
    } else {
        u16_t         idx = get_y_mask_index(y, cache_item);
        ipgui_coord_t sx  = get_start_x_at_y(y, cache_item);
        ipgui_coord_t num = get_y_mask_num(y, cache_item);
        ipgui_coord_t ex  = sx + num - 1;
        if (step == 1) {
            if (x < sx) {
                u16_t inside_len = sx - x;
                inside_len       = inside_len > len ? len : inside_len;
                ipgui_memset(mask, 0, inside_len);
                len  -= inside_len;
                if (!len)
                    goto _return;
                mask += inside_len;
                x    = sx;
            }

            if (x <= ex) {
                u16_t mask_len    = ex - x + 1;
                mask_len          = mask_len > len ? len : mask_len;
                u16_t index_start = idx + (x - sx);
                for (int i = 0; i < mask_len; i ++) {
                    if(point_outside_radius(x + i, y, cache_item->r)) 
                        break;
                    mask[i] = cache_item->mask[index_start + i];
                }
            }
        } else {
            if (x > ex) {
                u16_t outside_len = x - ex;
                outside_len       = outside_len > len ? len : outside_len;
                len  -= outside_len;
                if (!len)
                    goto _return;
                mask += outside_len;
                x    = ex;
            }

            if (x >= sx) {
                u16_t mask_len = x - sx + 1;
                mask_len       = mask_len > len ? len : mask_len;
                for (int i = 0; i < mask_len; i ++) {
                    ipgui_coord_t cx = x - i;
                    if (!point_outside_radius(cx, y, cache_item->r)) {
                        mask[i] = cache_item->mask[idx + (cx - sx)];
                    }
                }

                len  -= mask_len;
                if (!len)
                    goto _return;
                mask += mask_len;
                x    = sx - 1;
            }

            /* sx 左侧完全在 inner 圆内，挖空 */
            ipgui_memset(mask, 0, len);
        }
    }
_return:
    cache_item->refcnt --;
    return 0;
}