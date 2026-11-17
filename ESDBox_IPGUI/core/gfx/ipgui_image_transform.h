// #ifndef IPGUI_IMAGE_TRANSFORM_H
// #define IPGUI_IMAGE_TRANSFORM_H

// #ifdef __cplusplus
// extern "C" {
// #endif

// #include "ipgui_math.h"
// #include "ipgui_prim.h"

// __IPGUI_STATIC__ ipgui_scoord_t fixed_mult(ipgui_scoord_t a, ipgui_scoord_t b)
// {
//     int sign = 1;
//     ipgui_scoord_t c;
    
//     /* 处理符号，转为正数计算 */
//     if (a < 0) { a = -a; sign = -sign; }
//     if (b < 0) { b = -b; sign = -sign; }
    
//     /* 计算：c = (a * b + 0.5) >> IPGUI_FIXED_BITS */
//     c = (ipgui_scoord_t)(((long long)a * b + IPGUI_FIXED_HALF) >> IPGUI_FIXED_BITS);
    
//     return (sign > 0) ? c : -c;
// }

// __IPGUI_STATIC__ ipgui_scoord_t fixed_div(ipgui_scoord_t a, ipgui_scoord_t b)
// {
//     int sign = 1;
//     ipgui_scoord_t q;
    
//     /* 处理符号，转为正数计算 */
//     if (a < 0) { a = -a; sign = -sign; }
//     if (b < 0) { b = -b; sign = -sign; }
    
//     if (b == 0) return 0x7FFFFFFF;  /* 防除零 */
    
//     /* 计算：q = ((a << IPGUI_FIXED_BITS) + b/2) / b */
//     q = (ipgui_scoord_t)(((long long)a << IPGUI_FIXED_BITS) + (b >> 1)) / b;
    
//     return (sign > 0) ? q : -q;
// }

// __IPGUI_STATIC__ void ipgui_image_trans_init(ipgui_img_geo_trans_t * trans)
// {
//     trans->pivot.x = 0;
//     trans->pivot.y = 0;
//     trans->mat.a = IPGUI_FIXED_PRECI;
//     trans->mat.b = 0;
//     trans->mat.c = 0;
//     trans->mat.d = IPGUI_FIXED_PRECI;
// }

// __IPGUI_STATIC__ void ipgui_image_trans_pivot(ipgui_img_geo_trans_t * trans, ipgui_point_t pp)
// {
//     trans->pivot = pp;
// }

// /* vector P and transform matrix T1 and T2
//  * P1 = T1·P
//  * P2 = T2·P1
//  * then P2 = T2·T1·P
// */

// __IPGUI_STATIC__ void ipgui_image_trans_scale(ipgui_img_geo_trans_t * trans, float sx, float sy)
// {
//     /* T = |sx  0 |
//      *     |0   sy|
//      */
//     ipgui_scoord_t fsx, fsy;
//     ipgui_scoord_t a, b, c, d;

//     fsx = (ipgui_scoord_t)(sx * (float)IPGUI_FIXED_PRECI);
//     fsy = (ipgui_scoord_t)(sy * (float)IPGUI_FIXED_PRECI);
//     a = trans->mat.a;
//     b = trans->mat.b;
//     c = trans->mat.c;
//     d = trans->mat.d;

//     trans->mat.a = fixed_mult(a, fsx);
//     trans->mat.b = fixed_mult(b, fsx);
//     trans->mat.c = fixed_mult(c, fsy);
//     trans->mat.d = fixed_mult(d, fsy);
// }

// __IPGUI_STATIC__ void ipgui_image_trans_rotate_degree(ipgui_img_geo_trans_t * trans, int deg/* 0-360 is a period */)
// {
//     /* T = |cos(θ)  -sin(θ)|
//      *     |sin(θ)   cos(θ)|
//      */
//     ipgui_scoord_t a, b, c, d;
//     a = trans->mat.a;
//     b = trans->mat.b;
//     c = trans->mat.c;
//     d = trans->mat.d;
//     // deg = -deg; /* convert to screen coordinate */
//     trans->mat.a = ((a * ipgui_cos(deg) - c * ipgui_sin(deg)) + 16384) / 32768;
//     trans->mat.b = ((b * ipgui_cos(deg) - d * ipgui_sin(deg)) + 16384) / 32768;
//     trans->mat.c = ((a * ipgui_sin(deg) + c * ipgui_cos(deg)) + 16384) / 32768;
//     trans->mat.d = ((b * ipgui_sin(deg) + d * ipgui_cos(deg)) + 16384) / 32768;
// }

// /* 上下翻转 */
// __IPGUI_STATIC__ void ipgui_image_trans_reflect_y(ipgui_img_geo_trans_t * trans)
// {
//     /* T = |1  0 |
//      *     |0  -1|
//      */
//     ipgui_scoord_t c, d;
//     c = trans->mat.c;
//     d = trans->mat.d;

//     trans->mat.c = -c;
//     trans->mat.d = -d;
// }

// /* 左右翻转 */
// __IPGUI_STATIC__ void ipgui_image_trans_reflect_x(ipgui_img_geo_trans_t * trans)
// {
//     /* T = |-1  0|
//      *     |0   1|
//      */
//     ipgui_scoord_t a, b;
//     a = trans->mat.a;
//     b = trans->mat.b;

//     trans->mat.a = -a;
//     trans->mat.b = -b;
// }

// #ifdef __cplusplus
// }
// #endif

// #endif