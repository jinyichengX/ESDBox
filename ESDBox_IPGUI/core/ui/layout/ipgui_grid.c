#include "ipgui_grid.h"

#include "ipgui_widget.h"
#include "ipgui_memory.h"

#define GRID_COLS   IPGUI_GRID_MAX_COLS
#define GRID_ROWS   IPGUI_GRID_MAX_ROWS
#define GRID_AUTO   IPGUI_GRID_AUTO

/*
 * 单条轨道的运行状态
 *
 * 轨道存储按"实际使用的行列数"临时申请，不再为每条轴固定预留
 * IPGUI_GRID_MAX_ROWS 条（那会让 ipgui_grid_apply 占用约 1KB 栈，
 * 在默认栈只有几百字节的 MCU 任务里直接溢出）。
 */
typedef struct {
    ipgui_coord_t size;   /* FIXED: 像素; AUTO: 内容尺寸; FR: 分配结果 */
    ipgui_coord_t fr;     /* FR 份额 */
    ipgui_coord_t pos;    /* 轨道起点（相对内容区） */
    u8_t          type;   /* ipgui_grid_track_type_t */
} grid_track_t;

/* 单轴轨道视图：track 指向外部存储的首地址，count 为参与计算的轨道数 */
typedef struct {
    grid_track_t * track;
    u8_t           count;
    u8_t           def_count;   /* 显式定义的轨道数量 */
} grid_axis_t;

/* 遍历一个子控件时的回调，c0/r0 为落位，cs/rs 为跨格数 */
typedef void (* grid_visit_fn)(
    void *          ctx,
    ipgui_widget_t * child,
    int             c0,
    int             r0,
    int             cs,
    int             rs);

/* 初始化轨道：显式定义之外的轨道全部按 auto 处理 */
static void grid_axis_init(
    grid_axis_t *               axis,
    grid_track_t *              storage,
    const ipgui_grid_track_t *  defs,
    u8_t                        def_count,
    u8_t                        count)
{
    int i;

    ipgui_memset(storage, 0, (u32_t)sizeof(grid_track_t) * count);

    axis->track     = storage;
    axis->count     = count;
    axis->def_count = (def_count > count) ? count : def_count;

    for (i = 0; i < count; i++) {
        if (defs && i < axis->def_count) {
            if (defs[i].type == IPGUI_GRID_TRACK_FIXED) {
                axis->track[i].type = IPGUI_GRID_TRACK_FIXED;
                axis->track[i].size = (defs[i].value > 0) ? defs[i].value : 0;
            } else if (defs[i].type == IPGUI_GRID_TRACK_FR) {
                axis->track[i].type = IPGUI_GRID_TRACK_FR;
                axis->track[i].fr   = (defs[i].value > 0) ? defs[i].value : 1;
            } else {
                axis->track[i].type = IPGUI_GRID_TRACK_AUTO;
            }
        } else {
            axis->track[i].type = IPGUI_GRID_TRACK_AUTO;
        }
    }
}

/*
 * 解析最终轨道尺寸与起点
 * fixed / auto 尺寸已就绪，FR 轨道按剩余空间等比例分配，余数给最后一条 FR。
 */
static void grid_axis_resolve(
    grid_axis_t *   axis,
    ipgui_coord_t   avail,
    ipgui_coord_t   gap)
{
    ipgui_coord_t used_px = 0;
    ipgui_coord_t fr_total = 0;
    ipgui_coord_t free_px;
    ipgui_coord_t remain;
    ipgui_coord_t pos = 0;
    int last_fr = -1;
    int used = axis->count;
    int i;

    if (used <= 0) return;

    for (i = 0; i < used; i++) {
        if (axis->track[i].type == IPGUI_GRID_TRACK_FR)
            fr_total += axis->track[i].fr;
        else
            used_px += axis->track[i].size;
    }

    free_px = avail - used_px;
    if (used > 1) free_px -= (ipgui_coord_t)(used - 1) * gap;
    if (free_px < 0) free_px = 0;

    remain = free_px;
    for (i = 0; i < used; i++) {
        if (axis->track[i].type != IPGUI_GRID_TRACK_FR) continue;

        axis->track[i].size =
            (ipgui_coord_t)((s64_t)free_px * axis->track[i].fr / fr_total);
        remain -= axis->track[i].size;
        last_fr = i;
    }
    if (last_fr >= 0) axis->track[last_fr].size += remain;

    for (i = 0; i < used; i++) {
        axis->track[i].pos = pos;
        pos += axis->track[i].size + gap;
    }
}

/* 占位矩阵：occ[col] 的第 row 位为 1 表示该格已被占用 */
static int grid_area_free(const u32_t * occ, int col, int row, int cs, int rs)
{
    int c, r;

    for (c = col; c < col + cs; c++) {
        for (r = row; r < row + rs; r++) {
            if (occ[c] & ((u32_t)1u << r)) return 0;
        }
    }
    return 1;
}

static void grid_area_occupy(u32_t * occ, int col, int row, int cs, int rs)
{
    int c, r;

    for (c = col; c < col + cs; c++) {
        for (r = row; r < row + rs; r++) {
            occ[c] |= ((u32_t)1u << r);
        }
    }
}

/* 把跨格内容尺寸摊到其覆盖的 auto 轨道上 */
static void grid_fit_span(
    grid_axis_t *   axis,
    int             start,
    int             span,
    ipgui_coord_t   need,
    ipgui_coord_t   gap)
{
    ipgui_coord_t req;
    ipgui_coord_t each;
    int has_auto = 0;
    int i;

    if (start < 0 || span <= 0) return;
    if (start + span > axis->count) return;

    for (i = start; i < start + span; i++) {
        if (axis->track[i].type == IPGUI_GRID_TRACK_AUTO) has_auto = 1;
    }
    if (!has_auto) return;

    req = need - (ipgui_coord_t)(span - 1) * gap;
    if (req < 0) req = 0;
    each = (req + span - 1) / span;             /* 向上取整 */

    for (i = start; i < start + span; i++) {
        if (axis->track[i].type == IPGUI_GRID_TRACK_AUTO &&
            axis->track[i].size < each)
            axis->track[i].size = each;
    }
}

/*
 * 走一遍所有子控件，完成占位并回调 visit
 *
 * 落位算法是确定性的：同样的容器、cfg 与子控件顺序必然得到同样的 (c0,r0)，
 * 因此同一次 apply 内多次调用结果一致，可以分别用于"量尺寸"和"写几何"。
 * 返回实际占用的列数 / 行数。
 */
static void grid_walk(
    ipgui_widget_t *            container,
    const ipgui_grid_cfg_t *    cfg,
    int                         ncols,
    u32_t *                     occ,
    grid_visit_fn               visit,
    void *                      visit_ctx,
    int *                       used_cols,
    int *                       used_rows)
{
    int used_c = (cfg->col_count > 0) ? cfg->col_count : 1;
    int used_r = cfg->row_count;
    int cursor_col = 0;
    int cursor_row = 0;
    int idx = 0;
    int i;
    struct widget_link_t * link = container->link.first_child;

    for (i = 0; i < GRID_COLS; i++) occ[i] = 0;
    if (used_c > GRID_COLS) used_c = GRID_COLS;
    if (used_r > GRID_ROWS) used_r = GRID_ROWS;

    while (link) {
        ipgui_widget_t * child = ipgui_container_of(link, ipgui_widget_t, link);
        const ipgui_grid_item_t * item =
            (cfg->items && idx < cfg->item_count) ? &cfg->items[idx] : (const ipgui_grid_item_t *)0;

        int cs = 1, rs = 1;
        int ex_c = -1, ex_r = -1;
        int c0 = -1, r0 = -1;

        if (item) {
            cs = item->col_span ? item->col_span : 1;
            rs = item->row_span ? item->row_span : 1;
            if (item->col != GRID_AUTO) ex_c = item->col;
            if (item->row != GRID_AUTO) ex_r = item->row;
        }
        if (cs > GRID_COLS) cs = GRID_COLS;
        if (rs > GRID_ROWS) rs = GRID_ROWS;
        if (cs > ncols) cs = ncols;            /* ncols 已包含最大跨列 */

        if (ex_c >= 0 && ex_r >= 0) {
            /* 行列都显式指定：直接落位，允许与其它控件重叠 */
            c0 = (ex_c > GRID_COLS - cs) ? GRID_COLS - cs : ex_c;
            r0 = (ex_r > GRID_ROWS - rs) ? GRID_ROWS - rs : ex_r;
            if (c0 < 0 || r0 < 0) { c0 = -1; r0 = -1; }
        } else if (ex_c >= 0) {
            /* 只指定列：在该列内自上而下找空位 */
            c0 = (ex_c > GRID_COLS - cs) ? GRID_COLS - cs : ex_c;
            if (c0 < 0) c0 = -1;
            else {
                for (r0 = 0; r0 + rs <= GRID_ROWS; r0++) {
                    if (grid_area_free(occ, c0, r0, cs, rs)) break;
                }
                if (r0 + rs > GRID_ROWS) c0 = -1;
            }
        } else if (ex_r >= 0) {
            /* 只指定行：在该行内自左向右找空位 */
            r0 = (ex_r > GRID_ROWS - rs) ? GRID_ROWS - rs : ex_r;
            if (r0 < 0) r0 = -1;
            else {
                for (c0 = 0; c0 + cs <= GRID_COLS; c0++) {
                    if (grid_area_free(occ, c0, r0, cs, rs)) break;
                }
                if (c0 + cs > GRID_COLS) c0 = -1;
            }
        } else {
            /* 自动流：从游标起按行优先扫描第一个放得下的位置 */
            int c = cursor_col;
            int r = cursor_row;

            while (r + rs <= GRID_ROWS) {
                if (c + cs > ncols) { c = 0; r++; continue; }
                if (grid_area_free(occ, c, r, cs, rs)) { c0 = c; r0 = r; break; }
                c++;
                if (c >= ncols) { c = 0; r++; }
            }
            if (c0 >= 0) { cursor_col = c0; cursor_row = r0; }
        }

        if (c0 >= 0 && r0 >= 0) {
            grid_area_occupy(occ, c0, r0, cs, rs);
            if (c0 + cs > used_c) used_c = c0 + cs;
            if (r0 + rs > used_r) used_r = r0 + rs;

            if (visit) visit(visit_ctx, child, c0, r0, cs, rs);
        }
        /* 放不下（越界）的子控件保持原样，不参与本次排布 */

        idx++;
        link = link->sib_next;
        if (link == container->link.first_child) break;
    }

    *used_cols = used_c;
    *used_rows = used_r;
}

/* 第二轮：把子控件的内容固有尺寸收进 auto 轨道 */
typedef struct {
    grid_axis_t * ax;
    grid_axis_t * ay;
    ipgui_coord_t col_gap;
    ipgui_coord_t row_gap;
} grid_fit_ctx_t;

static void grid_visit_fit(
    void *          ctx,
    ipgui_widget_t * child,
    int             c0,
    int             r0,
    int             cs,
    int             rs)
{
    grid_fit_ctx_t * fit = (grid_fit_ctx_t *)ctx;
    ipgui_coord_t cw, ch;

    /* 有 measure 的控件报内容尺寸，没有的退回当前 w/h */
    ipgui_widget_content_size(child, &cw, &ch);

    grid_fit_span(fit->ax, c0, cs, cw, fit->col_gap);
    grid_fit_span(fit->ay, r0, rs, ch, fit->row_gap);
}

/* 第三轮：写入最终几何 */
typedef struct {
    grid_axis_t *            ax;
    grid_axis_t *            ay;
    const ipgui_grid_cfg_t * cfg;
} grid_write_ctx_t;

static void grid_visit_write(
    void *          ctx,
    ipgui_widget_t * child,
    int             c0,
    int             r0,
    int             cs,
    int             rs)
{
    grid_write_ctx_t * wr = (grid_write_ctx_t *)ctx;
    grid_track_t * col0 = &wr->ax->track[c0];
    grid_track_t * coln = &wr->ax->track[c0 + cs - 1];
    grid_track_t * row0 = &wr->ay->track[r0];
    grid_track_t * rown = &wr->ay->track[r0 + rs - 1];

    ipgui_coord_t x = wr->cfg->pad_left + col0->pos;
    ipgui_coord_t y = wr->cfg->pad_top  + row0->pos;
    ipgui_coord_t w = coln->pos + coln->size - col0->pos;
    ipgui_coord_t h = rown->pos + rown->size - row0->pos;

    if (x == child->x && y == child->y && w == child->w && h == child->h)
        return;

    /* 旧位置先标脏，改写后新位置再标脏 */
    ipgui_widget_mark_dirty(child);
    child->x = x;
    child->y = y;
    child->w = w;
    child->h = h;
    ipgui_widget_mark_dirty(child);

    /* 尺寸变了，子控件自己作为布局容器也要重排；
     * 只置自身标志，不向上传播——本次遍历马上会进到它，父级无需再排一遍
     */
    if (child->layout)
        child->layout_dirty = 1;
}

__IPGUI_API__ ipgui_err_t ipgui_grid_apply(
    ipgui_widget_t *        container,
    const ipgui_grid_cfg_t * cfg)
{
    u32_t occ[GRID_COLS];
    grid_track_t * storage;
    grid_axis_t ax, ay;
    grid_fit_ctx_t fit;
    grid_write_ctx_t wr;
    ipgui_coord_t avail_w, avail_h;
    int ncols, max_span, used_c, used_r;
    u8_t cols_used, rows_used;
    int i;

    if (!container || !cfg || !cfg->cols || cfg->col_count == 0)
        return IPGUI_ERR_PARAM;

    if (IPGUI_YES == ipgui_widget_link_is_detached(&container->link))
        return IPGUI_ERR_OBJ_INVALID;

    if (!container->link.first_child)
        return IPGUI_ERR_OK;

    /* 自动流的可用列数：不小于显式列数，也需容纳最大跨列 */
    ncols = (cfg->col_count > 0) ? cfg->col_count : 1;
    max_span = 1;
    for (i = 0; cfg->items && i < cfg->item_count; i++) {
        int s = cfg->items[i].col_span ? cfg->items[i].col_span : 1;
        if (s > max_span) max_span = s;
    }
    if (max_span > ncols) ncols = max_span;
    if (ncols > GRID_COLS) ncols = GRID_COLS;

    /* 第一遍：只做占位，得到实际使用的行列数 */
    grid_walk(container, cfg, ncols, occ,
              (grid_visit_fn)0, (void *)0, &used_c, &used_r);

    if (used_c <= 0) used_c = 1;
    if (used_c > GRID_COLS) used_c = GRID_COLS;
    if (used_r < 0) used_r = 0;
    if (used_r > GRID_ROWS) used_r = GRID_ROWS;

    /* 轨道存储按实际用量申请，典型 4x6 网格约 160 字节，用完即还 */
    storage = (grid_track_t *)ipgui_mem_alloc_def(
        (ipgui_mem_unit_type_t)
        (sizeof(grid_track_t) * (u32_t)(used_c + used_r)));
    if (!storage)
        return IPGUI_ERR_NOMEM;

    cols_used = (u8_t)used_c;
    rows_used = (u8_t)used_r;

    grid_axis_init(&ax, storage,               cfg->cols, cfg->col_count, cols_used);
    grid_axis_init(&ay, &storage[cols_used],   cfg->rows, cfg->row_count, rows_used);

    /* 第二遍：同一套落位算法，收集 auto 轨道的内容尺寸 */
    fit.ax      = &ax;
    fit.ay      = &ay;
    fit.col_gap = cfg->col_gap;
    fit.row_gap = cfg->row_gap;
    grid_walk(container, cfg, ncols, occ,
              grid_visit_fit, &fit, &used_c, &used_r);

    /* 解析最终轨道尺寸 */
    avail_w = container->w - cfg->pad_left - cfg->pad_right;
    avail_h = container->h - cfg->pad_top  - cfg->pad_bottom;
    if (avail_w < 0) avail_w = 0;
    if (avail_h < 0) avail_h = 0;

    grid_axis_resolve(&ax, avail_w, cfg->col_gap);
    grid_axis_resolve(&ay, avail_h, cfg->row_gap);

    /* 第三遍：写入几何 */
    wr.ax  = &ax;
    wr.ay  = &ay;
    wr.cfg = cfg;
    grid_walk(container, cfg, ncols, occ,
              grid_visit_write, &wr, &used_c, &used_r);

    ipgui_mem_free_def(storage);
    return IPGUI_ERR_OK;
}

/* 框架重排时调用的排版回调，cfg 由 layout_ctx 带回来 */
static void grid_layout_cb(ipgui_widget_t * container, void * ctx)
{
    ipgui_grid_apply(container, (const ipgui_grid_cfg_t *)ctx);
}

__IPGUI_API__ ipgui_err_t ipgui_grid_attach(
    ipgui_widget_t *  container,
    ipgui_grid_cfg_t * cfg)
{
    if (!container || !cfg)
        return IPGUI_ERR_PARAM;

    ipgui_widget_set_layout(container, grid_layout_cb, (void *)cfg);

    return ipgui_grid_apply(container, cfg);
}

__IPGUI_API__ void ipgui_grid_detach(ipgui_widget_t * container)
{
    if (!container) return;
    ipgui_widget_set_layout(container, (ipgui_widget_layout_fn)0, (void *)0);
}
