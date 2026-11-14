#ifndef IPGUI_TYPES_H
#define IPGUI_TYPES_H

typedef enum
{
    IPGUI_ERR_OK                = 0,
    IPGUI_ERR_NOK               = -1,

    IPGUI_ERR_PARAM             = 1,/* 传参错误 */
    IPGUI_ERR_OVERFLOW          = 2,             /* parameter result in overflow */
    IPGUI_ERR_OBJ_INVALID       = 3,

    IPGUI_ERR_MEM               = 5,
    IPGUI_ERR_NOMEM             = 6,

    IPGUI_ERR_EVT_NOT_SUPPORTED = 7,

    IPGUI_ERR_FS_FWRITE         = 8,
    IPGUI_ERR_FS_FREAD          = 9,
    IPGUI_ERR_FS_FOPEN          = 10,
    IPGUI_ERR_FS_FCLOSE         = 11,
    IPGUI_ERR_FS_MISC           = 12,
    IPGUI_ERR_FS_DENTER         = 13,
    IPGUI_ERR_FS_DRENAME        = 15,
    IPGUI_ERR_FS_DCREATE        = 16,

    IPGUI_ERR_QUEUE_FULL        = 17,
    IPGUI_ERR_QUEUE_EMPTY       = 18,
    IPGUI_ERR_LOGIC             = 19,
    IPGUI_ERR_CHILDREN_LIMIT_EXCEEDED = 20,

    /* bmp file header check */
    IPGUI_ERR_BMP_FIL_HDR_STRUCT_LEN,
    /* bmp open */
    IPGUI_ERR_BMPV4V5,
    IPGUI_ERR_BMP_BAD,
    IPGUI_ERR_BMP_RLE,
    IPGUI_ERR_BMP_FAKE,
    IPGUI_ERR_BMP_PAT_READ,
    IPGUI_ERR_BMP_MASK,
    /* bmp read line by line */
    IPGUI_ERR_OUT_OF_BOUNDS,
    IPGUI_ERR_BMP_PAT_IDX_CACHE,
    IPGUI_ERR_BMP_PAT_NULL,

    IPGUI_ERR_PNG_SIG,  /* PNG签名错误 */
    IPGUI_ERR_PNG_CHUNK,        /* chunk解析错误 */
    IPGUI_ERR_PNG_IHDR,         /* IHDR错误 */
    IPGUI_ERR_PNG_PLTE,         /* PLTE错误 */
    IPGUI_ERR_PNG_COMPRESS,     /* 不支持的压缩方法 */
    IPGUI_ERR_PNG_FILTER,       /* 不支持的滤波方法 */
    IPGUI_ERR_PNG_DEPTH,        /* 无效的位深度 */
    IPGUI_ERR_PNG_COLOR,        /* 无效的颜色类型 */
    IPGUI_ERR_PNG_INFLATE,      /* 解压失败 */
    IPGUI_ERR_PNG_RANGE,    /* 坐标越界 */


    /* 输入相关 */
    IPGUI_ERR_READ_INPUT_SRC_EVT_OK,
    IPGUI_ERR_READ_INPUT_SRC_EVT_ERR,
    IPGUI_ERR_INVALID_ID,

    IPGUI_ERR_INVALID_PFB_PIX_FMT,

    /* 动画相关 */
    IPGUI_ERR_ANIM_NOT_READY, /* 动画未就绪 */
    // IPGUI_ERR_ANIM_ALREADY_PAUSED,  /* 动画已是暂停态 */
    // IPGUI_ERR_ANIM_IS_PAUSED,       /* 动画处于暂停态 */
    // IPGUI_ERR_ANIM_NOT_PAUSED,      /* 动画未处于暂停态 */
    // IPGUI_ERR_ANIM_NOT_RUNNING,     /* 动画未在运行中 */
    // IPGUI_ERR_ANIM_NOT_PAUSED_OR_RUNNING, /* 动画未暂停也未运行 */

    /* 控件业务监听 */
    IPGUI_ERR_LISTENER_FULL,       /* 该控件监听槽已满 */
    IPGUI_ERR_LISTENER_NOT_FOUND,  /* 未找到对应监听 */
}ipgui_err_t;

typedef enum {
    IPGUI_YES,
    IPGUI_NO
}ipgui_yes_no_t;

/* 用户指定类型（默认PC） */
typedef unsigned int u32_t;
typedef unsigned short u16_t;
typedef unsigned char u8_t;
typedef int s32_t;
typedef short s16_t;
typedef signed char s8_t; //由char改为signed char，因为有些编译器会默认char为无符号字符比如arm-linux-gnueabihf-gcc
typedef long long s64_t;
typedef unsigned long long u64_t;
typedef unsigned long long uintptr_t;//uintptr_t 的大小必须适配当前系统的指针宽度（只支持32/64bit）

/* 动画值类型（不允许修改） */
typedef s32_t ipgui_anim_value_t;

/* 系统时基类型（不允许修改） */
typedef u32_t ipgui_tick_t;

#endif

