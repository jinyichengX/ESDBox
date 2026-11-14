#include "ipgui_image_buf.h"
#include "ipgui_memory.h"

u8_t * ipgui_image_buf_acquire(
    u32_t           w_stride,
    ipgui_coord_t   h,
    ipgui_coord_t * res_h)
{
    u8_t * p = (u8_t *)0;

    if (w_stride <= 0 || h <= 0 || !res_h) {
        if (res_h) * res_h = 0;
        return (u8_t *)0;
    }

    while (h > 0) {
        p = (u8_t *)ipgui_mem_alloc_def(w_stride * h);
        
        if (p) {
            break;
        }
#if 0
        h --;
#else 
        h = h / 2; /* 每次失败尝试减半，提高效率 */
#endif
    }

    if (res_h)
        * res_h = h;
        
    return p;
}

void ipgui_image_buf_free(void * data)
{
    ipgui_mem_free_def(data);
}