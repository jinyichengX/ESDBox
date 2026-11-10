#ifndef IPGUI_CONF_H
#define IPGUI_CONF_H

/* use file system */
#define IPGUI_USE_FILESYSTEM        1

#if IPGUI_USE_FILESYSTEM == 1
/* 没有自动识别系统手动选择一下 */
#if defined(_elos_)
#define IPGUI_OPERATING_SYSTEM_ELOS
#elif defined(_LINUX_) || defined (_LINUX)
#define IPGUI_OPERATING_SYSTEM_LINUX
#elif defined(_WIN32) || defined(_WIN64)
#define IPGUI_OPERATING_SYSTEM_WINDOWS
#else
#define IPGUI_OPERATING_SYSTEM_NO
#endif

#if defined(IPGUI_OPERATING_SYSTEM_NO)
#error "don't support this operating system"
#endif

#if defined(IPGUI_OPERATING_SYSTEM_NO)
#define IPGUI_FS_FATFS
#define IPGUI_FS_ZNFAT
#endif
#endif

/* define the number of ticks per second */
#ifndef IPGUI_TICK_PER_SECOND
#define IPGUI_TICK_PER_SECOND       1000U //不要动这个宏
#endif

/* define kernel heap memory size */
#define IPGUI_SMEM_SIZE             (6U * 1024U * 1024U)//2MB

#define IPGUI_GRADIENT_LUT_EN 0 /* the LUT need 64KB memory */
#define IPGUI_GRADIENT_STOP_MAX 15 /* at least 2 */

#ifndef IPGUI_ENDIAN_LITTLE
#define IPGUI_ENDIAN_LITTLE 1 /* must be 0 or 1,0: big endian, 1: little endian */
#endif

#ifndef USE_INV_TABLE
#define USE_INV_TABLE 1 /* 用于加速计算渐变插值和RGBA8888等32位像素混合 */
#endif

#ifndef CORNER_CACHE_ITEM_MAX_NUM
#define CORNER_CACHE_ITEM_MAX_NUM 10
#endif

/* 输入相关 */
#define INPUT_SRC_MAX 2
#if INPUT_SRC_MAX < 1
#error "INPUT_SRC_MAX must be greater than 0"
#endif

/* 1:开启FPS计算 0:关闭FPS计算 */
#define IPGUI_FPS_MONITOR_ON 1
#if IPGUI_FPS_MONITOR_ON
/* 1:显示内置的FPS控件 0:不显示内置的FPS控件，可以自己写FPS控件来显示screen.fps */
#define IPGUI_BUILTIN_FPS_WID_SHOW 1
#endif

#define SCREEN_MAX 2
#if SCREEN_MAX < 1
#error "SCREEN_MAX must be greater than 0"
#endif

/* 每个屏幕的页面数量，每页一棵独立的控件树
 * 页面结构体直接内嵌在 ipgui_scr_t 中，增大该值会线性增加屏幕对象的内存占用
 */
#define SCREEN_PAGE_MAX 10
#if SCREEN_PAGE_MAX < 1
#error "SCREEN_PAGE_MAX must be greater than 0"
#elif SCREEN_PAGE_MAX > 255
#error "SCREEN_PAGE_MAX must be little than 255"
#endif

#define EVENT_POOL_SIZE 5
#if EVENT_POOL_SIZE < 1
#error "EVENT_POOL_SIZE must be greater than 0"
#endif

#define IPGUI_ANIM_POOL_SIZE 16
#if IPGUI_ANIM_POOL_SIZE < 1
#error "IPGUI_ANIM_POOL_SIZE must be greater than 0"
#endif

#endif