#ifndef IPGUI_RECT_SLICE_H
#define IPGUI_RECT_SLICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ipgui_prim.h"

typedef struct {
    ipgui_coord_t remain_w;
    ipgui_coord_t remain_h;

    ipgui_coord_t slice_len;

    ipgui_coord_t full_w;        /* width of rect */
    ipgui_coord_t full_h;        /* height of rect */

    ipgui_coord_t orig_start_x;  /* rect->start.x */
    ipgui_coord_t orig_bottom;   /* rect->end.y + 1，current top y = orig_bottom - remain_h */

    /*
     * Division optimization: precomputed max rows per call for the final narrow strip.
     * - When strip_w == slice_len (hot path, ~99% calls): rows = 1, no division
     * - When strip_w <  slice_len (cold path, last 1-2 calls): rows = MIN(remain_h, rows_when_narrow)
     * - rows_when_narrow = 0 means no narrow strip exists (width is exact multiple of slice_len)
     */
    ipgui_coord_t rows_when_narrow;
}ipgui_rect_slice_ctx;

extern __IPGUI_API__ void ipgui_rect_slice_ctx_init(
    ipgui_rect_slice_ctx * ctx,
    ipgui_rect_t         * rect,
    ipgui_coord_t          slice_len);

extern __IPGUI_API__ s32_t ipgui_get_rect_slice(
    ipgui_rect_slice_ctx * ctx,
    ipgui_rect_t         * res);

#ifdef __cplusplus
}
#endif

#endif
