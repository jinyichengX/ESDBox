# IPGUI 使用手册（20260823更新）

IPGUI 是一套面向嵌入式系统的轻量级 GUI 框架，包含完整的绘图库、控件系统、输入分发、动画系统与屏幕驱动抽象。本手册覆盖：目录结构、移植步骤、系统初始化与主循环、控件 API、动画系统、绘图 API 参考及完整示例。

---
## demo
平台1：STM32板卡

<img width="640" height="853" alt="f3e243f7814968c229bfb0843a7f20c5" src="https://github.com/user-attachments/assets/32b62f53-7c2d-43ba-b559-f172ad7c98fb" />

https://github.com/user-attachments/assets/23de4f86-1820-44dd-abe7-4ee350a44ea6

平台2 i.MX6U板卡

https://github.com/user-attachments/assets/526d786a-7cdf-415e-93cd-566b23a5d4fe

平台3：SDL2

<img width="802" height="507" alt="0e8834add9d40863bea9b0c0b96cd363" src="https://github.com/user-attachments/assets/8fac0674-9787-49cc-bdbe-9446d18c0142" />


## 目录

- [第一部分：目录结构](#第一部分目录结构)
- [第二部分：移植指南](#第二部分移植指南)
- [第三部分：系统初始化与主循环](#第三部分系统初始化与主循环)
- [第四部分：控件 Widget API](#第四部分控件-widget-api)
- [第五部分：动画系统](#第五部分动画系统)
- [第六部分：绘图 API 参考](#第六部分绘图-api-参考)
- [第七部分：完整示例（SDL 版本）](#第七部分完整示例sdl-版本)

---

# 第一部分：目录结构

```
ESDBox_IPGUI/
├── Include/                 # 全局配置 & 核心类型定义
│   ├── ipgui_conf.h            ★ 必须修改 — 配置项
│   ├── ipgui_types.h           ★ 必须检查 — 平台类型 / 错误码 / tick_t
│   ├── ipgui_core.h            — ipgui_surf_t / ipgui_pfb_t / ipgui_init()
│   ├── ipgui_color.h           — 颜色类型 & 宏
│   ├── ipgui_coord.h           — 坐标类型
│   ├── ipgui_utils.h           — 编译宏 & 工具宏（多编译器适配）
│   ├── ipgui_defs.h            — 常量
│   ├── ipgui_angle.h           — 三角函数 LUT
│   ├── ipgui_graphic2.h        —
│   └── ipgui_lcd_pix_fmt.h     — 像素格式枚举
│
├── base/                    # 基础库
│   ├── ipgui_memory.c/h        — 内核堆分配（★ 必须）
│   ├── ipgui_debug.c/h         — 调试打印 & assert（★ 必须，含弱符号 ipgui_putck）
│   ├── ipgui_math.c/h          — sin/cos 等（★ 必须）
│   ├── ipgui_avl.c/h           — AVL 树（多边形光栅化用，★ 必须）
│   ├── ipgui_membox.c/h        — 内存块池（多边形用，★ 必须）
│   ├── ipgui_mempool.c/h       — 伙伴内存池（mask/image buffer，★ 必须）
│   ├── ipgui_time.c/h          — 系统时基 tick（动画/输入用，系统级必须）
│   ├── ipgui_timer.c/h         — 时间轮定时器（可选）
│   ├── ipgui_norm_queue.c/h    — 通用队列（输入事件队列用）
│   ├── ipgui_queue.c/h         — 优先队列
│   ├── ipgui_darray.c/h        — 动态数组
│   ├── ipgui_vector.c/h        — 向量运算
│   └── ipgui_list.h            — 双链表（控件树/动画链表用）
│
├── al/hal/                  # 硬件抽象层（HAL）
│   ├── ipgui_screen.c/h        — 屏幕驱动模型 + PFB + 脏矩形渲染（★ 系统核心）
│   ├── ipgui_rect_slice.c/h    — 脏矩形分片渲染
│   └── input/
│       ├── ipgui_input_dispatcher.c/h  — 输入事件分发器
│       ├── ipgui_input_src.h           — 输入源抽象
│       └── ipgui_input_src_event.h     — 原始输入事件类型
│
├── core/                    # 核心库
│   ├── ipgui_core.c            — ipgui_init() 入口
│   ├── composite/             # 混合（blend）引擎
│   │   ├── ipgui_blend.c/h              — 统一绘制入口 ipgui_blend() + ipgui_paint_t
│   │   ├── ipgui_blend_mode.h           — 混合模式枚举
│   │   ├── blend_color/ipgui_blend_color.c/h      — 纯色填充/混合
│   │   ├── blend_gradient/...           — 渐变混合
│   │   └── blend_image/...               — 图像混合
│   ├── gfx/                   # 图形绘制原语
│   │   ├── ipgui_draw_triangle / line / arc / filled_circle
│   │   ├── ipgui_draw_box_background / box_border / box_style
│   │   ├── ipgui_draw_polygon / pixel
│   │   ├── ipgui_draw_image / draw_image_api / draw_image_ex
│   │   ├── ipgui_draw_icon / draw_icon_api
│   │   ├── ipgui_draw_builtin_font
│   │   ├── ipgui_draw_box_shadow
│   │   ├── ipgui_blur                      — alpha mask 盒模糊
│   │   ├── ipgui_gradient_color
│   │   ├── ipgui_mask_buf / mask_gradient / ring_mask
│   │   ├── ipgui_edge_halfplane_mask / edge_wdf_mask
│   │   ├── ipgui_image_buf / image_mask / pixel_lerp
│   ├── ui/
│   │   ├── widget_manager/    # 控件系统
│   │   │   ├── ipgui_widget.c/h           — ipgui_widget_t + 生命周期
│   │   │   ├── ipgui_widget_tree.c/h      — 控件树
│   │   │   ├── ipgui_widget_event.c/h     — 事件分发到控件
│   │   │   ├── ipgui_widget_geometry.c/h  — 坐标变换
│   │   │   ├── ipgui_widget_state.c/h     — 控件状态
│   │   │   ├── ipgui_widget_style.c/h     — 样式
│   │   │   ├── ipgui_dirty_rect.c/h       — 脏矩形管理
│   │   │   ├── ipgui_scroll.c/h           — 惯性滚动
│   │   │   ├── ipgui_widget_ref.c/h
│   │   │   └── ipgui_widget_evt.h         — 控件事件类型
│   │   ├── layout/              # 布局（grid / flex）
│   │   └── animation/          # 动画系统
│   │       ├── ipgui_animation.c/h        — 动画对象/内存池/驱动
│   │       ├── ipgui_anim_curve.c/h
│   │       └── builtin_anim/             — liner / bounce / ease 曲线
│   ├── image/                 # 图像加载与处理
│   │   ├── ipgui_image.h                 — ipgui_img_dsc_t / ipgui_image_data_t
│   │   ├── decoder/  (bmp / png / jpeg)
│   │   └── proc/     (geometry_transform / enhance)
│   ├── vector_render/         # 矢量渲染（path / stroker / curve / transform）
│   ├── ipml/                  # IPML 标记语言加载器
│   └── misc/                 # prim / line_clip / pattle / angle / lcd_pix_fmt
│
├── widget/                  # 控件 demo 实现
│   ├── inc/  (ipgui_switch.h, ipgui_button.h)
│   └── src/  (ipgui_widget1_arc ... ipgui_widget9_curve)
│
├── font/                   # 内置点阵字体（OpenSans / Quicksand Medium，多字号）
│
└── port/                   ★ 用户实现 — 平台适配
    ├── ipgui_usr_putchar.c     — 重写 ipgui_putck() 对接串口
    ├── sdl/                    — PC/SDL 移植（sdl_draw / sdl_input_event）
    └── arm/scr_driver/         — MCU 屏幕驱动
        ├── stm32/stm32f4/      — DMA2D 加速
        ├── imx6ull/            — eLCDIF
        └── f1c100s/            — Allwinner framebuffer
```

---

# 第二部分：移植指南

## 2.1 修改 `Include/ipgui_conf.h`

### 必须核对/修改的项

| 宏 | 含义 | 默认值 | 说明 |
|----|------|--------|------|
| `IPGUI_SMEM_SIZE` | 内核堆大小（字节） | `6MB` | 仅绘图库可降到 256KB；带控件/动画/图像建议 ≥2MB |
| `IPGUI_ENDIAN_LITTLE` | 字节序 | `1` | 1=小端，0=大端，MCU 通常为 1 |

### 系统 / 输入 / 动画配置

| 宏 | 含义 | 默认值 | 说明 |
|----|------|--------|------|
| `IPGUI_TICK_PER_SECOND` | 每秒 tick 数 | `1000` | **不要修改**，时基单位为 ms |
| `INPUT_SRC_MAX` | 最大输入源数量 | `2` | 如同时支持触摸+按键则需 ≥2 |
| `SCREEN_MAX` | 最大屏幕数量 | `2` | 多屏时调大 |
| `EVENT_POOL_SIZE` | 输入事件池大小 | `5` | 事件预分配池 |
| `IPGUI_ANIM_POOL_SIZE` | 动画对象池大小 | `16` | 同时存在的最大动画数 |
| `IPGUI_DIRTY_RECT_POOL` | 脏矩形池大小 | `8` | 单帧最大合并脏区数 |
| `IPGUI_MERGE_COST_THRESHOLD` | 脏矩形合并代价阈值 | `0` | 0=最精确不合并，越大越激进 |

### 绘图相关可选项

| 宏 | 含义 | 默认值 | 说明 |
|----|------|--------|------|
| `IPGUI_USE_FILESYSTEM` | 是否使用文件系统 | `1` | 裸机无 FS 置 0 |
| `IPGUI_GRADIENT_LUT_EN` | 渐变 LUT 加速 | `0` | 开启需 64KB 内存 |
| `IPGUI_GRADIENT_STOP_MAX` | 渐变最大颜色停止点 | `15` | 必须 ≥2 |
| `USE_INV_TABLE` | 除法 LUT | `1` | 512B，加速渐变和 32 位像素混合 |
| `CORNER_CACHE_ITEM_MAX_NUM` | 圆角角点缓存项数 | `10` | 绘制圆角矩形用 |

## 2.2 检查 `Include/ipgui_types.h`

所有 typedef 必须逐项核对目标平台的字宽。其中 `ipgui_tick_t`、`ipgui_anim_value_t` 为框架固定类型，不要修改。

**32 位 ARM CPU 示例：**

```c
typedef unsigned int   u32_t;
typedef unsigned short u16_t;
typedef unsigned char  u8_t;
typedef int            s32_t;
typedef short          s16_t;
typedef signed char    s8_t;          // 必须用 signed char，防止默认 unsigned
typedef long long      s64_t;
typedef unsigned long long u64_t;
typedef unsigned int   uintptr_t;      // 32 位系统指针宽度
typedef s32_t          ipgui_anim_value_t;  // 不允许修改
typedef u32_t          ipgui_tick_t;        // 不允许修改
```

**64 位系统（当前默认值）：** 同上，但 `typedef unsigned long long uintptr_t;`。

`ipgui_err_t` 为统一错误码枚举（`IPGUI_ERR_OK`/`IPGUI_ERR_NOMEM`/`IPGUI_ERR_BMP_*`/`IPGUI_ERR_PNG_*`/`IPGUI_ERR_ANIM_NOT_READY` 等）。

## 2.3 实现平台适配 `port/`

### 2.3.1 串口打印 — `ipgui_putck()`

`base/ipgui_debug.c` 中声明为 `__WEAK__` 弱符号，用户定义同名强符号即可覆盖：

```c
/* port/ipgui_usr_putchar.c */
void ipgui_putck(char c)
{
    /* 替换为自己的串口发送函数，例如：
     * USART_SendData(USART1, c);
     * while (!(USART1->SR & USART_SR_TXE));
     */
}
```

### 2.3.2 屏幕驱动 — `ipgui_scr_drv_t`（系统级必须）

控件/渲染系统通过屏幕驱动把 PFB 内容写到物理屏幕。用户需实现三个回调：

```c
typedef struct {
    void   * pri_data;          /* 驱动私有数据 */
    ipgui_coord_t xreso;        /* 屏幕水平分辨率 */
    ipgui_coord_t yreso;        /* 屏幕垂直分辨率 */

    /* 写单像素到屏幕 (x,y) */
    void (* put_pixel)  (ipgui_scr_t * scr, ipgui_coord_t x, ipgui_coord_t y, u8_t * pix);

    /* 用 pix_buf 填充矩形区域 (x1,y1)-(x2,y2)，stride 为 pix_buf 行跨度 */
    void (* fill_region)(ipgui_scr_t * scr, ipgui_coord_t x1, ipgui_coord_t y1,
                                      ipgui_coord_t x2, ipgui_coord_t y2,
                         u8_t * pix_buf, s32_t stride);

    /* 把 PFB 刷到物理屏幕（脏区刷新完成后调用） */
    void (* flush)      (ipgui_scr_t * scr);
} ipgui_scr_drv_t;
```

参考实现：`port/arm/scr_driver/stm32/stm32f4/dma2d.c`（DMA2D 加速）、`port/sdl/sdl_draw.c`（SDL）。

### 2.3.3 输入源 — `ipgui_input_src_t`（需要交互时必须）

输入源负责从底层硬件读取原始事件：

```c
typedef struct {
    void * priv_data;
    /* 读取原始事件，返回 IPGUI_ERR_READ_INPUT_SRC_EVT_OK / _ERR */
    ipgui_err_t (* input_src_event_read_cb)(void * priv_data, ipgui_input_src_evt_t * raw_evt);
} ipgui_input_src_t;
```

原始事件结构 `ipgui_input_src_evt_t`：

| 字段 | 类型 | 说明 |
|------|------|------|
| `input_src_id` | `ipgui_input_src_id_t` | 输入源 ID（注册时返回） |
| `input_src_evt` | `ipgui_input_evt_type_t` | `POINTER_PRESS` / `POINTER_RELEASE` / `KEY_DOWN` / `KEY_UP` |
| `evt_info.pointer_pos` | `ipgui_pointer_pos_t{x,y}` | 指针坐标（指针事件） |
| `evt_info.key_code` | `ipgui_key_code_t{code}` | 键码（按键事件） |
| `evt_tick` | `ipgui_tick_t` | 事件时间戳 |

参考实现：`port/sdl/sdl_input_event.c`（`ipgui_sdl_mouse_event_poll`）。

---

# 第三部分：系统初始化与主循环

## 3.1 核心数据结构

### Surface（绘图目标）

```c
typedef struct {
    ipgui_aabb_t    surf;        /* 该 surface 在屏幕上的区域（可为相对坐标） */
    u8_t          * color;      /* 指向 surf 左上角首像素 */
    u32_t           stride;     /* 每行字节数 */
    ipgui_pix_fmt_t pix_fmt;     /* 像素格式 */
    u8_t            pix_size;    /* 每像素字节数（可含对齐空字节） */
} ipgui_surf_t;
```

### 屏幕 `ipgui_scr_t`

```c
typedef struct ipgui_scr_ctx {
    void               * pri_data;
    ipgui_scr_drv_t    * drv;          /* 屏幕驱动 */
    struct widget_tree_t tree;         /* 控件树根 */
    ipgui_dirty_rect_mgr_t dirty;      /* 脏矩形管理器 */
    ipgui_pfb_t          pfb;         /* 离屏部分帧缓冲 */
    ipgui_color_t        bg_color;     /* 背景色（render_bg 为 NULL 时使用） */
    void (* render_bg)(ipgui_scr_t * scr, ipgui_surf_t * surf);  /* 背景渲染回调 */
} ipgui_scr_t;
```

## 3.2 初始化流程

```c
#include "ipgui_core.h"
#include "ipgui_screen.h"
#include "ipgui_input_dispatcher.h"

ipgui_input_dispatcher_t dispatcher;
ipgui_scr_t              main_screen;
ipgui_input_src_t        pointer_src;

/* 用户实现的屏幕驱动与输入读取 */
extern ipgui_scr_drv_t  my_scr_drv;
extern ipgui_err_t my_pointer_read(void * priv, ipgui_input_src_evt_t * raw);

int main(void)
{
    /* 1. 输入分发器初始化 */
    ipgui_input_dispatcher_init(&dispatcher);

    /* 2. 配置输入源 */
    pointer_src.priv_data = (void *)0;
    pointer_src.input_src_event_read_cb = my_pointer_read;

    /* 3. 屏幕初始化 + 创建 PFB */
    ipgui_screen_init(&main_screen, &my_scr_drv);
    static u8_t pfb_buf[800 * 4 * 480];        /* 足够大的 PFB 缓冲 */
    ipgui_scr_create_pfb(&main_screen, pfb_buf, sizeof(pfb_buf), PIX_FMT_RGBA8888);

    /* 4. 库初始化（内存/BMP/多边形/渐变 LUT） */
    if (ipgui_init() != IPGUI_ERR_OK) { while (1); }

    /* 5. 注册输入源与屏幕，并绑定 */
    ipgui_input_src_id_t pid = ipgui_dispatcher_register_input_src(&dispatcher, &pointer_src);
    ipgui_scr_id_t      sid = ipgui_dispatcher_register_screen(&dispatcher, &main_screen);
    ipgui_bind_input_src_with_screen(&dispatcher, pid, sid);

    /* 6. 设置背景渲染（可选） */
    main_screen.render_bg = draw_background;   /* NULL 时使用 bg_color */

    /* 7. 创建控件 ... */
}
```

`ipgui_init()` 内部依次初始化：内存模块 → BMP 解码模块 → 多边形光栅化 → 渐变 LUT（可选）。

## 3.3 主循环（顺序严格）

```c
while (1) {
    /* 1. 读取原始事件并入队 */
    ipgui_input_src_evt_t raw_evt;
    pointer_src.input_src_event_read_cb(&pointer_src, &raw_evt);
    ipgui_norm_queue_post(&dispatcher.evt_queue, &raw_evt, sizeof(raw_evt));

    /* 2. 分发输入事件（命中测试 → 控件 event_handler） */
    ipgui_dispatch_input_event(&dispatcher);

    /* 3. 驱动所有动画（推送值、回收已完成动画），必须在渲染之前 */
    ipgui_anim_update_all();

    /* 4. 屏幕渲染（脏矩形合并 → PFB 切片 → 控件 render → flush） */
    ipgui_screen_render(&main_screen);

    /* 5. 各控件自己的 per-frame update（业务逻辑） */
    my_widget_update(widget);

    /* 6. 系统时基 +1（动画/输入依赖，单位 1ms） */
    ipgui_tick_inc();
}
```

> 渲染采用**脏矩形 + PFB 切片**：`ipgui_widget_mark_dirty()` 把控件绝对区域加入屏幕脏矩形池；`ipgui_screen_render()` 合并脏区、按列宽切片成条带，对每条带构造 `ipgui_surf_t` 调用控件 `render`，最后通过驱动 `fill_region`/`flush` 刷屏。

---

# 第四部分：控件 Widget API

## 4.1 控件结构 `ipgui_widget_t`

```c
typedef struct ipgui_widget {
    void                 * priv_data;        /* 用户扩展数据 */

    struct widget_link_t   link;             /* 控件树节点（parent/sibling/child） */

    ipgui_coord_t          x, y;             /* 位置（父控件局部坐标系） */
    ipgui_coord_t          w, h;             /* 大小 */

    /* 滚动（仅 SCROLLABLE 控件有效） */
    ipgui_coord_t          scroll_x, scroll_y;
    ipgui_scroll_dir_t     scroll_dir;       /* X / Y / GESTURE */
    ipgui_scroll_t         x_scroll;         /* X 轴惯性滚动状态 */
    ipgui_scroll_t         y_scroll;         /* Y 轴惯性滚动状态 */

    u32_t                  flags;            /* 见 4.3 标志位 */
    const char           * name;             /* 调试用名称 */

    void (*render)       (struct ipgui_widget * widget, ipgui_widget_render_ctx_t * ctx);
    void (*event_handler)(struct ipgui_widget * widget, ipgui_widget_evt_t * evt);
} ipgui_widget_t;
```

## 4.2 生命周期 API

| API | 说明 |
|-----|------|
| `ipgui_widget_t * ipgui_widget_create(ipgui_widget_t * parent)` | 创建控件；`parent=NULL` 为根级控件挂到屏幕 root；传 `parent` 则作为子控件 |
| `ipgui_widget_set_render(widget, render)` | 设置渲染回调 |
| `ipgui_widget_set_event_handler(widget, handler)` | 设置事件回调 |
| `ipgui_widget_mark_dirty(widget)` | 标脏控件（触发屏幕脏矩形重绘） |
| `ipgui_widget_abs_pos(widget, &aabb)` | 取屏幕绝对坐标 |
| `ipgui_widget_get_screen(widget)` | 反查所属屏幕 |
| `ipgui_widget_set_top / set_bottom / set_behind / set_front` | 调整控件树层级（z-order） |

## 4.3 控件标志位 `ipgui_widget_flag_t`

| 标志 | 含义 |
|------|------|
| `IPGUI_WIDGET_FLAG_NONE` | 默认 |
| `IPGUI_WIDGET_FLAG_INVISIBLE` | 不可见：跳过渲染，但保留布局空间 |
| `IPGUI_WIDGET_FLAG_OVERFLOW_VISIBLE` | 子控件可超出自身边界绘制（默认裁剪） |
| `IPGUI_WIDGET_FLAG_DISABLED` | 禁用：不响应事件 |
| `IPGUI_WIDGET_FLAG_SCROLLABLE` | 可滚动 |

## 4.4 渲染上下文

```c
typedef struct {
    ipgui_surf_t * surf;        /* 目标绘制表面（已平移到控件本地坐标 (0,0)） */
    void         * user_data;   /* 预留扩展 */
} ipgui_widget_render_ctx_t;
```

> **坐标空间**：控件 `render` 回调内所有坐标处于控件本地坐标系（左上角为 0,0，`widget->w/h` 为画布边界），无需调用 `ipgui_widget_abs_pos()`。

## 4.5 控件事件

```c
typedef enum {
    IPGUI_WIDGET_EVENT_PRESSED,    /* 按下瞬间 + 持续按压（每帧） */
    IPGUI_WIDGET_EVENT_RELEASED,   /* 释放瞬间 */
    IPGUI_WIDGET_EVENT_HOVER,      /* 悬停（仅鼠标） */
} ipgui_widget_event_type_t;

typedef struct {
    ipgui_widget_t * target;
    ipgui_widget_event_type_t type;
    union {
        ipgui_widget_pressed_evt_t  pressed_evt;   /* x,y / first_press / last_press */
        ipgui_widget_released_evt_t released_evt;  /* x,y / first_press / prev_press */
        ipgui_widget_hover_evt_t    hover_evt;     /* x,y */
    } evt;
} ipgui_widget_evt_t;
```

- `pressed_evt.last_press_x/y`：上一次按压坐标，可用于计算拖拽增量。
- `released_evt.prev_press_x/y`：最后一次按压坐标，用于计算滑动末速度（惯性滚动）。

## 4.6 创建控件的标准范式（取自 `main.c`）

```c
/* 根级控件 */
ipgui_widget_t * w = ipgui_widget_create(NULL);
w->name          = "旋钮圆";
w->render        = knob_circle_render;        /* 渲染回调 */
w->event_handler = knob_circle_event_handler;  /* 事件回调 */
w->x = 60;  w->y = 102;
w->w = 26;  w->h = 26;

/* 子控件（挂到父控件） */
ipgui_widget_t * child = ipgui_widget_create(w);
child->x = 10; child->y = 10; ...
```

## 4.7 渲染回调范式

```c
void color_render(ipgui_widget_t * w, ipgui_widget_render_ctx_t * ctx)
{
    /* 本地坐标 (0,0)~(w-1,h-1) */
    ipgui_aabb_t box = {{0,0},{w->w - 1, w->h - 1}};

    ipgui_box_style_t s; ipgui_memset(&s, 0, sizeof(s));
    s.left_top_radius = s.right_top_radius =
    s.left_bottom_radius = s.right_bottom_radius = 12;

    ipgui_box_bg_style_t bg;
    bg.paint.type = IPGUI_PAINT_COLOR;
    IPGUI_COLOR_SET(bg.paint.src.color, 255, 0x4a90d9);
    bg.opacity = 100; bg.blend_mode = IPGUI_BLEND_NORMAL;

    ipgui_draw_box_background(ctx->surf, NULL, &box, &s, &bg);

    /* 居中绘制控件名（内置字体） */
    if (w->name) {
        ipgui_font_style_t fs; ipgui_memset(&fs, 0, sizeof(fs));
        fs.font = &open_sans_18px;
        fs.paint.type = IPGUI_PAINT_COLOR;
        IPGUI_COLOR_SET(fs.paint.src.color, 255, 0xFFFFFF);
        fs.opacity = 220; fs.blend_mode = IPGUI_BLEND_NORMAL;

        ipgui_coord_t tw = ipgui_builtin_text_width(fs.font, (const s8_t *)w->name);
        ipgui_draw_builtin_text(ctx->surf, NULL, &fs,
                                (const s8_t *)w->name,
                                ((w->w - 1) - tw) / 2,
                                ((w->h - 1) - fs.font->line_height) / 2);
    }
}
```

## 4.8 事件回调范式（拖拽）

```c
void drag_handler(ipgui_widget_t * w, ipgui_widget_evt_t * e)
{
    if (e->type != IPGUI_WIDGET_EVENT_PRESSED) return;
    ipgui_coord_t dx = e->evt.pressed_evt.x - e->evt.pressed_evt.last_press_x;
    ipgui_coord_t dy = e->evt.pressed_evt.y - e->evt.pressed_evt.last_press_y;
    if (dx == 0 && dy == 0) return;
    /* 先标脏旧位置，再移动，再标脏新位置，避免旧位置残留 */
    ipgui_widget_mark_dirty(w);
    w->x += dx;  w->y += dy;
    ipgui_widget_mark_dirty(w);
}
```

## 4.9 滚动

设置 `flags |= IPGUI_WIDGET_FLAG_SCROLLABLE` 启用滚动，通过 `scroll_x / scroll_y` 偏移子内容。`scroll_dir` 选择 `IPGUI_SCROLL_DIR_X` / `_Y` / `_GESTURE`。惯性滚动 API：

```c
void ipgui_inertia_scroll_stop(ipgui_widget_t * widget);
void ipgui_scroll_start(ipgui_widget_t * widget, s32_t scroll_v, u8_t axis); /* 0=x, 1=y */
```

> 滚动模块独立于动画系统，不依赖 `ipgui_anim_t`。

## 4.10 脏矩形与局部重绘

```c
typedef struct {
    ipgui_dirty_rect_t pool[IPGUI_DIRTY_RECT_POOL];
    s32_t              pool_num;
} ipgui_dirty_rect_mgr_t;
```

- `ipgui_dirty_rect_add(mgr, &dr)` / `ipgui_dirty_rect_add_xywh(mgr, x, y, w, h)`
- `ipgui_dirty_rect_flush(mgr)` 合并脏区
- `ipgui_dirty_rect_get(mgr, index)` 取第 i 个脏区

控件移动后**务必**先 `mark_dirty` 旧位置再移动再 `mark_dirty` 新位置，否则会残留上一帧像素。

---

# 第五部分：动画系统

## 5.1 核心结构

```c
/* 曲线函数：纯函数 f(t)，无副作用 */
typedef ipgui_anim_value_t (* ipgui_anim_func_t)(struct ipgui_anim_t * anim, ipgui_tick_t t, void * data);

/* 每帧推送值回调 */
typedef void (* ipgui_anim_path_cb_t)(struct ipgui_anim_t * anim, ipgui_anim_value_t value, void * user_data);

/* 完成回调（归还池前调用） */
typedef void (* ipgui_anim_finish_cb_t)(struct ipgui_anim_t * anim, void * user_data);

typedef struct {
    ipgui_anim_func_t      anim_func;          /* 曲线函数 */
    void                 * data;
    ipgui_tick_t           t1, t2;             /* 函数区间 [t1, t2] */
    ipgui_anim_loop_type_t loop_type;          /* DEFAULT / PING_PONG */
    u32_t                  loop_count;         /* 0=无限 */
    ipgui_tick_t           start_delay;        /* 仅第一次循环有效 */
    ipgui_anim_path_cb_t   path_cb;           /* 每帧回调 */
    void                 * path_cb_user_data;
    ipgui_anim_finish_cb_t finish_cb;
    void                 * finish_cb_user_data;
} ipgui_anim_dsc_t;

struct ipgui_anim_t {
    struct list_head node;
    ipgui_anim_dsc_t dsc;
    u8_t             state;        /* READY / RUNNING / DEAD */
    ipgui_tick_t     duration;
    ipgui_tick_t     start;
};
```

## 5.2 API

| API | 说明 |
|-----|------|
| `ipgui_anim_t * ipgui_anim_create(const ipgui_anim_dsc_t * dsc)` | 从内存池分配，池满返回 NULL |
| `ipgui_err_t ipgui_anim_start(ipgui_anim_t * anim)` | 启动，每段只能 start 一次 |
| `void ipgui_anim_update_all(void)` | 每帧调用，推进所有动画、推送值、回收已完成动画 |

## 5.3 内置曲线

| 函数 | 说明 |
|------|------|
| `ipgui_anim_liner(anim, t, data)` | 线性：`output = t` |
| `ipgui_anim_bounce(anim, t, data)` | 弹簧过冲：三次贝塞尔，冲过终点再回弹收敛 |

## 5.4 Tick 系统

```c
void       ipgui_tick_inc(void);            /* 主循环每帧调用，+1（=1ms） */
ipgui_tick_t ipgui_tick_now(void);          /* 当前 tick */
ipgui_tick_t ipgui_tick_passed_last(void);  /* 距上次 tick 的时间 */
ipgui_tick_t ipgui_millis2tick(u32_t ms);
u32_t         ipgui_tick2millis(ipgui_tick_t t);
```

## 5.5 创建自定义动画

```c
/* path_cb：把动画值写到控件属性 */
void my_path_cb(ipgui_anim_t * anim, ipgui_anim_value_t v, void * user_data)
{
    ipgui_widget_t * w = (ipgui_widget_t *)user_data;
    ipgui_widget_mark_dirty(w);
    w->x = v;                         /* 幂等写入 */
    ipgui_widget_mark_dirty(w);
}

ipgui_anim_dsc_t dsc;
ipgui_memset(&dsc, 0, sizeof(dsc));
dsc.anim_func = ipgui_anim_liner;
dsc.t1 = 0;  dsc.t2 = 200;           /* 0~200ms */
dsc.path_cb = my_path_cb;
dsc.path_cb_user_data = widget;

ipgui_anim_t * anim = ipgui_anim_create(&dsc);
if (anim) ipgui_anim_start(anim);
```

## 5.6 内存安全要点

- **禁止**在 `path_cb` / `finish_cb` 内调用 `ipgui_anim_create/start/销毁` 或修改动画链表。
- 动画播完自动回收回池，**不要长期持有** `anim` 指针（回收后可能被复用）。
- `path_cb` 同一值可能被推送多次（启动/结束时），回调必须**幂等**。
- 无限循环（`loop_count=0`）不会调用 `finish_cb`。
- 滚动模块（`ipgui_scroll_t`）虽持有 `anim` 指针，但滚动独立于动画系统，停止时调用 `ipgui_inertia_scroll_stop`。

---

# 第六部分：绘图 API 参考

## 6.1 像素格式

`ipgui_surf_t.pix_fmt` 支持：

| 枚举 | 说明 | pix_size |
|------|------|----------|
| `PIX_FMT_RGBA8888` | 32位 RGBA | 4 |
| `PIX_FMT_BGRA8888` | 32位 BGRA | 4 |
| `PIX_FMT_ARGB8888` | 32位 ARGB | 4 |
| `PIX_FMT_ABGR8888` | 32位 ABGR | 4 |
| `PIX_FMT_RGB888` | 24位 RGB | 3 |
| `PIX_FMT_BGR888` | 24位 BGR | 3 |
| `PIX_FMT_RGB565` | 16位 RGB565 | 2 |
| `PIX_FMT_BGR565` | 16位 BGR565 | 2 |

## 6.2 混合模式

```c
typedef enum {
    IPGUI_BLEND_NORMAL,   /* Porter-Duff OVER，预乘 alpha（默认） */
    IPGUI_BLEND_DODGE,    /* 颜色减淡 */
} ipgui_blend_mode_t;
```

## 6.3 统一绘制入口 `ipgui_blend()`（推荐）

所有填充源（纯色/渐变/图像）通过统一入口，支持 mask 蒙版：

```c
typedef enum {
    IPGUI_PAINT_COLOR, IPGUI_PAINT_GRADIENT, IPGUI_PAINT_IMAGE,
} ipgui_paint_type_t;

typedef struct {
    ipgui_paint_type_t type;
    union {
        ipgui_color_t     color;
        ipgui_grad_src_t  grad_src;
        ipgui_image_src_t image_src;
    } src;
} ipgui_paint_t;

void ipgui_blend(ipgui_surf_t * surf, ipgui_aabb_t * clip,
                 ipgui_aabb_t * dest, ipgui_paint_t * paint,
                 u8_t opacity, u8_t * mask, ipgui_aabb_t * mask_aabb,
                 ipgui_blend_mode_t blend_mode);
```

`mask` 为 8bit alpha 蒙版（0=不绘制，255=完全绘制），`mask_aabb` 必须 ≥ `dest` 区域。

## 6.4 纯色填充 / 混合 `ipgui_blend_color()`

```c
void ipgui_blend_color(ipgui_surf_t * surf, ipgui_aabb_t * clip,
                       ipgui_aabb_t * dest, ipgui_color_t color,
                       u8_t opacity, u8_t * mask, ipgui_aabb_t * mask_aabb,
                       ipgui_blend_mode_t blend_mode);

/* 无 mask 的快速填充 */
void ipgui_fill_color(ipgui_surf_t * surf, ipgui_aabb_t * clip,
                      ipgui_aabb_t * dest, ipgui_color_t color,
                      u8_t opacity, ipgui_blend_mode_t blend_mode);
```

## 6.5 三角形 `ipgui_draw_triangle`

含 halfplane mask 抗锯齿。

```c
ipgui_point_t p1={100,20}, p2={20,170}, p3={180,170};
ipgui_triangle_style_t style;
style.blend_mode = IPGUI_BLEND_NORMAL;
style.opacity    = 255;
style.paint.type = IPGUI_PAINT_COLOR;
IPGUI_COLOR_SET(style.paint.src.color, 255, 0xFF0000);
ipgui_draw_triangle(&surf, NULL, &p1, &p2, &p3, &style);
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `paint` | `ipgui_paint_t` | 填充源 |
| `opacity` | `u8_t` | 不透明度 0~255 |
| `blend_mode` | `ipgui_blend_mode_t` | 混合模式 |

## 6.6 直线 `ipgui_draw_line_generic`

含 WDF mask 抗锯齿（斜线）或直接混合（水平/垂直）。

```c
ipgui_line_t line; line.start={10,10}; line.end={190,100};
ipgui_line_style_t style;
style.width=6; style.cap=IPGUI_LINE_CAP_BUTT;  /* BUTT=平头, ROUND=圆头 */
style.blend_mode=IPGUI_BLEND_NORMAL; style.opacity=255;
style.paint.type=IPGUI_PAINT_COLOR;
IPGUI_COLOR_SET(style.paint.src.color, 255, 0x0000FF);
ipgui_draw_line_generic(&surf, NULL, &line, &style);
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `width` | `ipgui_coord_t` | 线宽 |
| `cap` | `ipgui_line_cap_t` | 线帽样式 |
| `paint` / `opacity` / `blend_mode` | — | 同上 |

## 6.7 像素 `ipgui_draw_pixel`

```c
ipgui_draw_pixel(&surf, NULL, 50, 50, color,
                 255,   /* mask 0~255 */
                 255,   /* opacity */
                 IPGUI_BLEND_NORMAL);
```

## 6.8 圆弧 / 圆环 / 圆 / 扇形 `ipgui_draw_arc`

通过 `er`（外圆半径）和 `ir`（内圆半径）组合（约束 `er > ir ≥ 0`）：

| 配置 | 效果 |
|------|------|
| `er>ir>0`, `angle<360` | 圆弧 |
| `er>ir>0`, `angle>=360` | 圆环 |
| `er>0, ir==0`, `angle<360` | 扇形 |
| `er>0, ir==0`, `angle>=360` | 实心圆 |

```c
ipgui_arc_t arc;
arc.cx=100; arc.cy=50; arc.er=25; arc.ir=10;
arc.start=0; arc.dir=IPGUI_ARC_DRAW_DIR_CW; arc.angle=270;

ipgui_arc_style_t style;
style.blend_mode=IPGUI_BLEND_NORMAL; style.opacity=255;
style.sep_type=IPGUI_ARC_ENDPOINT_TYPE_ROUND;  /* 起始端点 */
style.eep_type=IPGUI_ARC_ENDPOINT_TYPE_ROUND;  /* 结束端点 */
style.paint.type=IPGUI_PAINT_COLOR;
IPGUI_COLOR_SET(style.paint.src.color, 255, 0x00FF00);
ipgui_draw_arc(&surf, NULL, &arc, &style);
```

**`ipgui_arc_t`：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `cx`, `cy` | `ipgui_coord_t` | 圆心 |
| `er` | `ipgui_coord_t` | 外圆半径 |
| `ir` | `ipgui_coord_t` | 内圆半径（0=实心） |
| `start` | `ipgui_arc_angle_t` | 起始角度（度） |
| `dir` | `ipgui_arc_draw_dir_t` | CW/CCW |
| `angle` | `u16_t` | 绘制角度（度） |

## 6.9 实心圆 `ipgui_draw_filled_circle`

```c
ipgui_filled_circle_style_t style;
style.blend_mode=IPGUI_BLEND_NORMAL; style.opacity=255;
style.paint.type=IPGUI_PAINT_COLOR;
IPGUI_COLOR_SET(style.paint.src.color, 255, 0xFFA500);
ipgui_draw_filled_circle(&surf, NULL, 100, 100, 50, &style);
```

## 6.10 圆角矩形 `ipgui_draw_box_background` + `ipgui_draw_box_border`

```c
ipgui_aabb_t box; box.start={10,80}; box.end={190,180};

ipgui_box_style_t box_style;            /* padding + 四角圆角半径 */
box_style.left_top_radius = box_style.right_top_radius =
box_style.left_bottom_radius = box_style.right_bottom_radius = 20;

ipgui_box_bg_style_t bg;
bg.blend_mode=IPGUI_BLEND_NORMAL; bg.opacity=200;
bg.paint.type=IPGUI_PAINT_COLOR;
IPGUI_COLOR_SET(bg.paint.src.color, 255, 0xFFA500);
ipgui_draw_box_background(&surf, NULL, &box, &box_style, &bg);

ipgui_box_border_style_t border;
border.blend_mode=IPGUI_BLEND_NORMAL; border.width=3; border.opacity=255;
border.paint.type=IPGUI_PAINT_COLOR;
IPGUI_COLOR_SET(border.paint.src.color, 255, 0xFF0000);
ipgui_draw_box_border(&surf, NULL, &box, &box_style, &border);
```

**`ipgui_box_style_t`：** `left/right/top/bottom_padding` + `left_top/right_top/left_bottom/right_bottom_radius`（圆角是 padding box 的圆角）。

## 6.11 盒阴影 `ipgui_draw_box_shadow`

```c
ipgui_box_shadow_style_t sh;
sh.color... ; sh.opacity=180; sh.blur=20; sh.spread=0;
sh.offset_x=0; sh.offset_y=8;
ipgui_draw_box_shadow(&surf, NULL, &box, &box_style, &sh);
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `color` | `ipgui_color_t` | 阴影颜色 |
| `opacity` | `u8_t` | 不透明度 |
| `blur` | `ipgui_coord_t` | 模糊半径 |
| `spread` | `ipgui_coord_t` | 扩展半径 |
| `offset_x/y` | `ipgui_coord_t` | 偏移 |

> 阴影内部用 alpha mask 盒模糊（3 次 box blur 逼近高斯），再 `ipgui_blend_color` 叠加。

## 6.12 盒模糊 `ipgui_blur`（alpha mask）

```c
typedef struct {
    u8_t           * mask;    /* A8，仅 alpha，行主序 */
    ipgui_coord_t    w, h;     /* stride 隐式 = w，必须连续无 padding */
} ipgui_mask_surface_t;

void ipgui_average_blur_hor(ipgui_mask_surface_t * in, ipgui_coord_t kn, ipgui_mask_surface_t * out);
void ipgui_average_blur_ver(ipgui_mask_surface_t * in, ipgui_coord_t kn, ipgui_mask_surface_t * out);
void ipgui_blur(ipgui_mask_surface_t * in, ipgui_coord_t h_kn, ipgui_coord_t v_kn, ipgui_mask_surface_t * out);
```

> 3 次盒模糊近似高斯（中心极限定理）。可分离卷积：先水平后垂直，计算量降低。

## 6.13 多边形 `ipgui_draw_polygon`

```c
ipgui_polygon_ras_t ras; ipgui_polygon_ras_init(&ras);
ipgui_point_t pts[] = {{100,10},{150,60},{130,110},{70,110},{50,60}};
ipgui_polygon_style_t attr;
attr.blend_mode=IPGUI_BLEND_NORMAL; attr.alpha=255;
IPGUI_COLOR_SET(attr.color, 255, 0x800080);
ipgui_draw_polygon(&surf, NULL, pts, 5, &ras, &attr);
```

> 顶点必须按轮廓顺序排列，否则光栅化结果虽不出错但与预期不符。

## 6.14 图像 `ipgui_draw_image` 系列

### 6.14.1 图像数据结构

```c
/* 内存中的完整图像描述 */
typedef struct {
    ipgui_coord_t       w, h;
    u32_t               stride;     /* 行跨度（字节），可含 padding */
    u8_t              * pixmap;     /* 像素数据 */
    ipgui_image_format_t fmt;       /* IPGUI_IMG_FMT_RGBA8888 等 */
    u8_t                px_size;     /* 每像素字节（可含对齐空字节） */
} ipgui_image_data_t;

/* BMP 解码结果 */
typedef struct {
    ipgui_image_format_t fmt;
    ipgui_coord_t w, h, stride;
    unsigned char * pixmap;
    unsigned char * mask;           /* 整图 mask（与 alpha 通道不同） */
} ipgui_img_dsc_t;
```

加载 BMP（需文件系统）：

```c
ipgui_img_dsc_t dsc;
if (test_bmp("M:/path/to/img.bmp", &dsc) != 0) { /* 失败 */ }

ipgui_image_data_t img;
img.pixmap  = dsc.pixmap;
img.px_size = dsc.stride / dsc.w;
img.fmt     = IPGUI_IMG_FMT_RGBA8888;
img.stride  = dsc.stride;
img.w = dsc.w;  img.h = dsc.h;
```

### 6.14.2 底层 API — `ipgui_draw_image`（含质量参数）

```c
typedef struct {
    ipgui_scoord_t a, b, c, d;       /* 变换矩阵，26.6 定点数，64=1.0 */
} ipgui_trans_mat_t;

typedef enum {
    IPGUI_IMAGE_QUALITY_LOW,     /* 取样下限 */
    IPGUI_IMAGE_QUALITY_MEDIUM,  /* 最近邻 + hd/vd 判断 */
    IPGUI_IMAGE_QUALITY_HIGH,    /* 双线性插值 */
} ipgui_image_quality_t;

typedef struct { u8_t opacity; ipgui_blend_mode_t blend_mode; } ipgui_image_draw_style_t;

void ipgui_draw_image(ipgui_surf_t * surf, ipgui_aabb_t * clip,
                      ipgui_image_data_t * img,
                      ipgui_point_t * pivot, ipgui_point_t * anchor,
                      ipgui_trans_mat_t * trans,        /* NULL=不变换 */
                      ipgui_image_draw_style_t * style,
                      ipgui_image_quality_t quality);
```

### 6.14.3 高级 API — `ipgui_draw_image_api.h`

```c
/* 在 (x,y) 处绘制（左上角对齐） */
void ipgui_draw_image_at(&surf, &img, 10, 20, &style, IPGUI_IMAGE_QUALITY_HIGH);

/* 以 (cx,cy) 为中心绘制 */
void ipgui_draw_image_centered(&surf, &img, 100, 100, &style, IPGUI_IMAGE_QUALITY_HIGH);

/* 在目标矩形内按对齐+缩放模式绘制 */
ipgui_aabb_t target; target.start={10,10}; target.end={190,190};
void ipgui_draw_image_in_rect(&surf, &img, &target,
                              IPGUI_IMG_ALIGN_CENTER,    /* 九宫格对齐 */
                              IPGUI_IMG_FIT_FIT,         /* 缩放模式 */
                              &style, IPGUI_IMAGE_QUALITY_HIGH);
```

**对齐 `ipgui_image_align_t`：** `TOP_LEFT/TOP/TOP_RIGHT/LEFT/CENTER/RIGHT/BOTTOM_LEFT/BOTTOM/BOTTOM_RIGHT`

**缩放 `ipgui_image_fit_t`：**

| 值 | 说明 |
|----|------|
| `IPGUI_IMG_FIT_NONE` | 原图大小，不缩放 |
| `IPGUI_IMG_FIT_FIT` | 等比缩放至完全容纳（可能留空） |
| `IPGUI_IMG_FIT_FILL` | 等比缩放至完全覆盖（超出裁剪） |
| `IPGUI_IMG_FIT_STRETCH` | 拉伸填满，不保持宽高比 |

## 6.15 图标（mask 矢量） `ipgui_draw_icon`

图标用连续 alpha mask（无 stride padding）+ 填充源，区别于位图：

```c
typedef struct {
    ipgui_coord_t w, h;
    u8_t * mask;   /* w*h 字节，行主序，stride 隐式=w，必须连续无 padding */
} ipgui_icon_data_t;

typedef struct { u8_t opacity; ipgui_paint_t paint; ipgui_blend_mode_t blend_mode; } ipgui_draw_icon_style_t;

void ipgui_draw_icon(&surf, NULL, &icon_data, &pivot, &anchor, &trans, &style);
```

`paint` 可为 `IPGUI_PAINT_COLOR` / `IPGUI_PAINT_GRADIENT` / `IPGUI_PAINT_IMAGE`（用图片填充图标形状）。

## 6.16 内置字体 `ipgui_draw_builtin_font`

```c
typedef struct {
    ipgui_coord_t width, height;
    ipgui_coord_t bearing_x, bearing_y, advance;
    const u8_t * cover_map;        /* 抗锯齿 0~255 */
} ipgui_glyph_t;

typedef struct {
    ipgui_glyph_t * glyphs;        /* ASCII 0~127 索引 */
    ipgui_coord_t   line_height, baseline, space_width;
    ipgui_coord_t   font_size, max_height;   /* 暂时无效 */
} ipgui_font_t;

typedef struct {
    const ipgui_font_t * font;
    ipgui_paint_t        paint;
    u8_t                 opacity;
    u16_t                line_spacing;
    ipgui_blend_mode_t   blend_mode;
} ipgui_font_style_t;

ipgui_coord_t ipgui_builtin_text_width(const ipgui_font_t * font, const s8_t * text);
ipgui_coord_t ipgui_draw_builtin_char(surf, clip, &style, x, y, ch);
ipgui_coord_t ipgui_draw_builtin_text(surf, clip, &style, text, x, y);  /* 返回 advance */
```

字体源文件在 `font/opensans/` 与 `font/quicksand/`，按字号生成（如 `open_sans_18px`、`quicksand_medium_50px`）。引用方式：`extern const ipgui_font_t open_sans_18px;`（具体见对应头文件）。

## 6.17 渐变填充

三种渐变：线性、径向、锥形。每个渐变最多 `IPGUI_GRADIENT_STOP_MAX` 个停止点。

```c
/* 线性渐变 */
ipgui_liner_gradient_color_t liner;
ipgui_liner_gradient_init_direct(&liner, 0, 0, 100, 100);  /* 起→终 */
ipgui_gradient_color_stop_t s0; s0.pos=0;   IPGUI_COLOR_SET(s0.color,255,0xFF0000);
ipgui_liner_gradient_add_stop(&liner, &s0);
ipgui_gradient_color_stop_t s1; s1.pos=255; IPGUI_COLOR_SET(s1.color,255,0x0000FF);
ipgui_liner_gradient_add_stop(&liner, &s1);

ipgui_paint_t grad_paint;
grad_paint.type = IPGUI_PAINT_GRADIENT;
grad_paint.src.grad_src.grad_type = IPGUI_GRADIENT_TYPE_LINEAR;
grad_paint.src.grad_src.grad.liner_grad = liner;

/* 径向：ipgui_radial_gradient_init(&r, cx, cy, r); ... ipgui_radial_gradient_add_stop */
/* 锥形：ipgui_conic_gradient_init(&c, cx, cy, start_angle); ... ipgui_conic_gradient_add_stop */
```

## 6.18 数据类型速查

### 坐标与几何（`ipgui_prim.h`）

```c
ipgui_point_t  p;  p.x=100; p.y=50;
ipgui_line_t   line; line.start={0,0}; line.end={100,100};
ipgui_aabb_t   box; box.start={10,20}; box.end={200,150};   /* start≤end */
/* 子像素坐标（26.6 定点数，64=1.0） */
ipgui_spoint_t sp; sp.x = 64*100;   /* =100.0 */
```

### 颜色

```c
ipgui_color_t color;
IPGUI_COLOR_SET(color, 255, 0xFF0000);       /* alpha, RGB */
color.a=255; color.r=0xFF; color.g=0; color.b=0;
u32_t packed = IPGUI_COLOR_RGBA(255,0,0,255); /* R,G,B,A */
```

### Surface

```c
int w=200,h=200, stride=w*4;
unsigned char * buf = malloc((size_t)stride*h);
ipgui_surf_t surf;
surf.surf.start={0,0}; surf.surf.end={w-1,h-1};
surf.color=buf; surf.stride=stride;
surf.pix_fmt=PIX_FMT_RGBA8888; surf.pix_size=4;
```

---

# 第七部分：完整示例（SDL 版本）

以下为基于 SDL 的系统级 demo（取自 `main.c`），演示屏幕/输入初始化、控件创建、渲染回调、拖拽事件、滚动与图像加载的完整用法。

```c
#include <stdio.h>
#include "SDL.h"
#include "sdl_draw.h"
#include "sdl_input_event.h"
#include "ipgui_screen.h"
#include "ipgui_widget.h"
#include "ipgui_input_dispatcher.h"
#include "ipgui_image.h"
#include "ipgui_time.h"
#include "ipgui_animation.h"
#include "ipgui_draw_box_background.h"
#include "ipgui_draw_builtin_font.h"
#include "ipgui_memory.h"
#undef main

extern ipgui_err_t ipgui_sdl_mouse_event_poll(void * priv, ipgui_input_src_evt_t * raw);

ipgui_input_dispatcher_t dispatcher;
ipgui_scr_t              main_screen;
ipgui_input_src_t        pointer_src;

/* 用户实现的 SDL 屏幕驱动 */
ipgui_scr_drv_t sdl_drv = {
    .xreso = 800, .yreso = 480,
    .pri_data    = &g_sdl_private,
    .put_pixel   = sdl_put_pixel,
    .fill_region = sdl_fill_region,
    .flush       = sdl_flush,
};

static u8_t main_screen_frame_buf[800 * 4 * 480];

/* 颜色块控件：圆角矩形 + 居中文字 */
typedef struct { u8_t r,g,b; } widget_color_t;

void color_render(ipgui_widget_t * w, ipgui_widget_render_ctx_t * ctx)
{
    widget_color_t * c = (widget_color_t *)w->priv_data;
    ipgui_aabb_t box = {{0,0},{w->w-1, w->h-1}};
    ipgui_box_style_t s; ipgui_memset(&s,0,sizeof(s));
    s.left_top_radius=s.right_top_radius=s.left_bottom_radius=s.right_bottom_radius=12;
    ipgui_box_bg_style_t bg;
    bg.paint.type=IPGUI_PAINT_COLOR;
    IPGUI_COLOR_SET(bg.paint.src.color,255,((u32_t)c->r<<16)|((u32_t)c->g<<8)|c->b);
    bg.opacity=100; bg.blend_mode=IPGUI_BLEND_NORMAL;
    ipgui_draw_box_background(ctx->surf, NULL, &box, &s, &bg);
}

/* 拖拽：直接改 x/y */
void drag_handler(ipgui_widget_t * w, ipgui_widget_evt_t * e)
{
    if (e->type != IPGUI_WIDGET_EVENT_PRESSED) return;
    ipgui_coord_t dx = e->evt.pressed_evt.x - e->evt.pressed_evt.last_press_x;
    ipgui_coord_t dy = e->evt.pressed_evt.y - e->evt.pressed_evt.last_press_y;
    if (dx==0 && dy==0) return;
    ipgui_widget_mark_dirty(w);
    w->x += dx;  w->y += dy;
    ipgui_widget_mark_dirty(w);
}

int main(void)
{
    /* 1. 输入分发器 */
    ipgui_input_dispatcher_init(&dispatcher);
    pointer_src.priv_data = (void *)0;
    pointer_src.input_src_event_read_cb = ipgui_sdl_mouse_event_poll;

    /* 2. 屏幕 + PFB */
    ipgui_screen_init(&main_screen, &sdl_drv);
    ipgui_sdl_screen_init(&main_screen);
    ipgui_scr_create_pfb(&main_screen, main_screen_frame_buf,
                         sizeof(main_screen_frame_buf), PIX_FMT_RGBA8888);

    /* 3. 库初始化 */
    if (ipgui_init() != IPGUI_ERR_OK) { printf("ipgui_init_err"); return 0; }

    /* 4. 注册并绑定输入源/屏幕 */
    ipgui_input_src_id_t pid =
        ipgui_dispatcher_register_input_src(&dispatcher, &pointer_src);
    ipgui_scr_id_t sid =
        ipgui_dispatcher_register_screen(&dispatcher, &main_screen);
    ipgui_bind_input_src_with_screen(&dispatcher, pid, sid);

    /* 5. 创建控件 */
    static widget_color_t col_orange = {0xf0,0x8a,0x3a};
    ipgui_widget_t * w = ipgui_widget_create(NULL);
    w->name="wid_drag"; w->render=color_render; w->event_handler=drag_handler;
    w->x=230; w->y=100; w->w=130; w->h=220;
    w->priv_data = (void *)&col_orange;

    /* 6. 主循环 */
    ipgui_input_src_evt_t raw_evt;
    while (1) {
        pointer_src.input_src_event_read_cb(&pointer_src, &raw_evt);
        ipgui_norm_queue_post(&dispatcher.evt_queue, &raw_evt, sizeof(raw_evt));

        ipgui_dispatch_input_event(&dispatcher);
        ipgui_anim_update_all();
        ipgui_screen_render(&main_screen);
        ipgui_tick_inc();
        Sleep(1);
    }
    return 0;
}
```

> **编译器支持**：`ipgui_utils.h` 自动适配 GCC / Clang / IAR / ARMCC v5&v6 / MSVC。属性宏（`__WEAK__`, `__PACKED__`, `IPGUI_ST_ALIGN` 等）在各平台自动展开。

> **注意**：完整绘图 API（含 BMP 导出示例）的独立参考另见 `绘图API参考手册.md`；纯绘图库移植（不含控件/动画）见 `移植手册.md`。
