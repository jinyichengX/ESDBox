/* ============================================================
 * RoboKit - RPLIDAR A1 激光雷达扫描数据采集
 *
 * 基于 Slamtec RPLIDAR SDK v2.1.0 的 ultra_simple 示例移植。
 * 通过 USB 转串口（CP210x）与 RPLIDAR A1 通信，
 * 读取并打印一圈 360 度扫描数据（角度 / 距离 / 信号质量）。
 *
 * Windows 平台驱动说明：
 *   1. 安装 CP210x USB 转串口驱动
 *      （SDK 附带: src/rplidar_sdk-master/tools/cp2102_driver/CP210x_Windows_Drivers.zip）
 *   2. 将 RPLIDAR A1 接入电脑 USB，在“设备管理器 -> 端口(COM 和 LPT)”
 *      中查看分配到的 COM 端口号（如 COM3）
 *   3. 运行程序（命令行参数与 ultra_simple 一致）：
 *        app.exe --channel --serial \\.\COMx 115200
 *      其中 COMx 为实际端口号；A1 波特率固定 115200
 *   4. 不传参数时默认使用 DEFAULT_COM_PORT @ 115200
 *
 * 旧的 RRT 路径可视化（IPGUI/SDL）代码已在下方 #if 0 块中整体保留，
 * 需要恢复时把 #if 0 改成 #if 1 即可。
 * ============================================================ */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <cstdint>
#include <cmath>

#include <SDL.h>

#include "sl_lidar.h"
#include "sl_lidar_driver.h"

extern "C" {
#include "ipgui_core.h"
#include "ipgui_screen.h"
#include "ipgui_widget.h"
#include "ipgui_memory.h"
#include "ipgui_blend.h"
#include "ipgui_draw_pixel.h"
#include "ipgui_draw_line.h"
#include "ipgui_draw_arc.h"
#include "sdl_draw.h"
}

#undef main

#ifdef _WIN32
#include <Windows.h>
#define delay(x) ::Sleep(x)
#endif

/* MinGW 8.1.0 的 std::mutex 不可用（缺 gthreads），用 Win32 临界区替代，
 * 与 SDK/IPGUI 的同步风格一致 */
class CriticalSection {
public:
    CriticalSection()  { InitializeCriticalSection(&cs_); }
    ~CriticalSection() { DeleteCriticalSection(&cs_); }
    void lock()   { EnterCriticalSection(&cs_); }
    void unlock() { LeaveCriticalSection(&cs_); }
private:
    CRITICAL_SECTION cs_;
};
class CSLock {
public:
    explicit CSLock(CriticalSection& cs) : cs_(cs) { cs_.lock(); }
    ~CSLock() { cs_.unlock(); }
private:
    CriticalSection& cs_;
};

#ifndef _countof
#define _countof(_Array) (int)(sizeof(_Array) / sizeof(_Array[0]))
#endif

/* A1 默认串口：COM 必须大写（Windows 的 \\.\ 设备名大小写敏感，
 * SDK 直接把字符串传给 CreateFile，小写 com6 会打开失败） */
#define DEFAULT_COM_PORT "\\\\.\\COM6"
/* A1 波特率固定 115200 */
#define DEFAULT_BAUDRATE 115200

/* 屏幕分辨率（与原 RRT 可视化保持一致） */
#define SCREEN_W 800
#define SCREEN_H 480

using namespace sl;

/* ============================================================
 * IPGUI 屏幕（SDL 驱动）全局状态
 * ============================================================ */
extern "C" {
ipgui_scr_t main_screen;
}

static ipgui_scr_drv_t sdl_drv;
static u8_t main_screen_frame_buf[SCREEN_W * 4 * SCREEN_H];

/* 最新一圈扫描数据，由扫描线程写入、widget_render 读取 */
static sl_lidar_response_measurement_node_hq_t g_nodes[8192];
static size_t g_nodes_count = 0;
static CriticalSection g_nodes_mtx;

/* 全屏 widget 指针，扫描循环里 mark_dirty 用 */
static ipgui_widget_t * g_widget = nullptr;

/* ============================================================
 * 原有 RRT 路径可视化代码（IPGUI/SDL），整体注释保留
 * ============================================================ */
#if 0
/* ============================================================
 * RoboKit - RRT 路径可视化
 *
 * 用 IPGUI（SDL 驱动）绘制 RRT 规划结果：
 *   - 蓝色：RRT 树的所有边（分叉路径）
 *   - 红色：回溯路径（start -> end 的主干）
 *
 * 同时链接 elNET（网络协议栈，C 库）。
 * ============================================================ */

#include <cstdio>
#include <cstdint>

#include <SDL.h>

#include "planner_rrt.hpp"

extern "C" {
#include "ipgui_core.h"
#include "ipgui_screen.h"
#include "ipgui_widget.h"
#include "ipgui_memory.h"
#include "ipgui_blend.h"
#include "ipgui_draw_line.h"
#include "ipgui_draw_filled_circle.h"
#include "sdl_draw.h"
#include "elnet.h"
}

#undef main

/* ipgui_widget.c 中 extern 引用的全局屏幕，需在此定义 */
extern "C" {
ipgui_scr_t main_screen;
}

static ipgui_scr_drv_t sdl_drv;
static u8_t main_screen_frame_buf[800 * 4 * 480];

static test::RRTPlanner* g_planner = nullptr;

/* ---------------- 绘制辅助 ---------------- */

/* 地图坐标 (0,0)-(100,100) -> 屏幕坐标（缩放 4 倍 + 偏移） */
static ipgui_point_t MapToScreen(double mx, double my)
{
    ipgui_point_t p;
    p.x = (ipgui_coord_t)(mx * 4.0 + 200.0);
    p.y = (ipgui_coord_t)(my * 4.0 + 40.0);
    return p;
}

static void DrawLine(ipgui_surf_t* surf, ipgui_aabb_t* clip,
                     const ipgui_point_t& a, const ipgui_point_t& b,
                     ipgui_color_t color)
{
    ipgui_line_t line;
    line.start = a;
    line.end   = b;

    ipgui_line_style_t style;
    style.width      = 2;
    style.cap        = IPGUI_LINE_CAP_ROUND;
    style.opacity    = 255;
    style.blend_mode = IPGUI_BLEND_NORMAL;
    style.paint.type = IPGUI_PAINT_COLOR;
    style.paint.src.color = color;

    ipgui_draw_line_generic(surf, clip, &line, &style);
}

/* ---------------- 控件渲染回调 ---------------- */

static void widget_render(ipgui_widget_t* widget, ipgui_widget_render_ctx_t* ctx)
{
    (void)widget;
    ipgui_surf_t* surf = ctx->surf;

    /* 背景 */
    ipgui_color_t bg;
    IPGUI_COLOR_SET(bg, 255, 0x202830);
    ipgui_blend_color(surf, NULL, &surf->surf, bg, 255, NULL, NULL, IPGUI_BLEND_NORMAL);

    if (!g_planner)
        return;

    ipgui_color_t blue, red;
    IPGUI_COLOR_SET(blue, 255, 0x4090FF);   /* 分叉路径（树边） */
    IPGUI_COLOR_SET(red,  255, 0xFF4040);   /* 回溯路径 */

    /* 蓝色：RRT 树的所有边 */
    const auto& nodes = g_planner->node_array_;
    for (size_t i = 1; i < nodes.size(); i++) {
        if (nodes[i].parent_idx < 0)
            continue;
        DrawLine(surf, &surf->surf,
                 MapToScreen(nodes[i].point.x, nodes[i].point.y),
                 MapToScreen(nodes[nodes[i].parent_idx].point.x,
                             nodes[nodes[i].parent_idx].point.y),
                 blue);
    }

    /* 红色：回溯路径 */
    const auto& path = g_planner->GetPath();
    for (size_t i = 1; i < path.size(); i++) {
        DrawLine(surf, &surf->surf,
                 MapToScreen(path[i - 1].x, path[i - 1].y),
                 MapToScreen(path[i].x, path[i].y),
                 red);
    }

    /* 粉色：圆形障碍物 */
    const auto& obss = g_planner->GetObstacles();
    ipgui_filled_circle_style_t cstyle;
    cstyle.opacity    = 200;
    cstyle.blend_mode = IPGUI_BLEND_NORMAL;
    cstyle.paint.type = IPGUI_PAINT_COLOR;
    IPGUI_COLOR_SET(cstyle.paint.src.color, 255, 0xFF69B4);   /* 粉色 */
    for (const auto& obs : obss) {
        ipgui_point_t c = MapToScreen(obs.center.x, obs.center.y);
        ipgui_draw_filled_circle(surf, &surf->surf, c.x, c.y,
                                 (ipgui_coord_t)(obs.radius * 4.0), &cstyle);
    }
}

/* ---------------- 主程序 ---------------- */

int main()
{
    /* 1. RRT 规划（地图 0-100，起点 (10,10) 到终点 (90,90)） */
    test::RRTPlanner planner(test::Vec2(0, 0), test::Vec2(100, 100), 5.0, 10000);

    /* 添加圆形障碍物（分散布局） */
    planner.AddCircleObstacle(test::Vec2(25, 70), 11);
    planner.AddCircleObstacle(test::Vec2(75, 25), 10);
    planner.AddCircleObstacle(test::Vec2(50, 50), 13);
    planner.AddCircleObstacle(test::Vec2(18, 45), 8);
    planner.AddCircleObstacle(test::Vec2(82, 60), 8);

    planner.DoRRTPlan(test::Vec2(10, 10), test::Vec2(90, 90));
    std::printf("RRT nodes=%zu, path_len=%zu\n",
                planner.node_array_.size(), planner.GetPath().size());
    g_planner = &planner;

    /* 2. IPGUI 屏幕初始化（SDL 驱动，参考 m:\ESDBox\main.c） */
    ipgui_memset(&main_screen, 0, sizeof(main_screen));
    ipgui_memset(&sdl_drv, 0, sizeof(sdl_drv));
    ipgui_memset(&g_sdl_private, 0, sizeof(g_sdl_private));

    sdl_drv.xreso      = 800;
    sdl_drv.yreso      = 480;
    sdl_drv.pri_data   = &g_sdl_private;
    sdl_drv.put_pixel  = sdl_put_pixel;
    sdl_drv.fill_region = sdl_fill_region;
    sdl_drv.flush      = sdl_flush;

    if (ipgui_screen_init(&main_screen, &sdl_drv) != IPGUI_ERR_OK) {
        std::printf("screen init failed\n");
        return 1;
    }
    if (ipgui_sdl_screen_init(&main_screen) != 0) {
        std::printf("sdl init failed\n");
        return 1;
    }
    ipgui_scr_create_pfb(&main_screen, main_screen_frame_buf,
                         sizeof(main_screen_frame_buf), PIX_FMT_RGBA8888);
    ipgui_init();

    /* 3. 全屏 widget 画 RRT */
    ipgui_widget_t* widget = ipgui_widget_create(NULL);
    widget->render = widget_render;
    widget->x = 0;
    widget->y = 0;
    widget->w = 800;
    widget->h = 480;

    /* 4. 事件循环 */
    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT)
                running = false;
        }
        ipgui_widget_mark_dirty(widget);
        ipgui_screen_render(&main_screen);   /* 内部最后会调用 drv->flush */
        SDL_Delay(16);
    }

    return 0;
}
#endif /* 原有 RRT/IPGUI 代码结束 */

/* ============================================================
 * RPLIDAR A1 扫描数据采集（active）
 * ============================================================ */

static void print_usage(const char * argv0)
{
    printf("RPLIDAR A1 扫描数据采集 (ultra_simple 移植)\n"
           "SDK Version: %s\n"
           "用法:\n"
           "  串口: %s --channel --serial <COM 端口> [波特率]\n"
           "  UDP : %s --channel --udp <IP 地址> [端口]\n"
           "示例:\n"
           "  %s --channel --serial \\\\.\\COM3 115200   (A1, Windows)\n"
           "不传参数时默认: %s @ %d\n"
           "各型号波特率: A1(115200) A2M7(256000) A2M8(115200) A3(256000) "
           "S1(256000) S2(1000000) S3(1000000)\n",
           SL_LIDAR_SDK_VERSION,
           argv0, argv0, argv0, DEFAULT_COM_PORT, DEFAULT_BAUDRATE);
}

/* 检查雷达健康状态，返回 true 表示可正常工作 */
static bool checkLidarHealth(ILidarDriver * drv)
{
    sl_result op_result;
    sl_lidar_response_device_health_t healthinfo;

    op_result = drv->getHealth(healthinfo);
    if (SL_IS_OK(op_result)) {
        printf("SLAMTEC Lidar health status : %d\n", healthinfo.status);
        if (healthinfo.status == SL_LIDAR_STATUS_ERROR) {
            fprintf(stderr, "Error, slamtec lidar internal error detected. "
                    "Please reboot the device to retry.\n");
            return false;
        }
        return true;
    } else {
        fprintf(stderr, "Error, cannot retrieve the lidar health code: 0x%x\n",
                op_result);
        return false;
    }
}

static bool g_ctrl_c_pressed = false;
static void onCtrlC(int)
{
    g_ctrl_c_pressed = true;
}

/* ============================================================
 * 点云可视化（IPGUI + SDL）
 * 绘制参数与坐标映射参考 SDK 官方 frame_grabber/scanView.cpp
 * ============================================================ */

#define PI_F 3.14159265f

/* 雷达原点：屏幕正中心（与官方 scanView 的 centerPt 一致） */
#define LIDAR_CX (SCREEN_W / 2)
#define LIDAR_CY (SCREEN_H / 2)
/* 最大显示半径：短边/2 - 边距(参考官方 DEF_MARGIN=20) */
#define MAX_PIXEL_R ((SCREEN_W < SCREEN_H ? SCREEN_W : SCREEN_H) / 2 - 20)
/* 默认显示量程 8000mm = 8m（参考官方 DISP_DEFAULT_DIST），画面更饱满 */
#define DISPLAY_RANGE_MM 8000.0f
/* 像素/毫米 = maxPixelR / 量程(mm)（参考官方 distScale） */
#define DIST_SCALE ((float)MAX_PIXEL_R / DISPLAY_RANGE_MM)
/* 距离环间隔（参考官方 DISP_RING_ABS_DIST=100 像素） */
#define RING_STEP_PX 100

/* 将极坐标(角度deg, 距离mm)映射到屏幕坐标。
 * 映射方式与官方 scanView.cpp:127-131 完全一致：
 *   endptX = sin(rad)*distPixel + centerPt.x
 *   endptY = centerPt.y - cos(rad)*distPixel
 * 即 0° 指向屏幕上方、顺时针增大。 */
static inline void polarToScreen(float angle_deg, float dist_mm,
                                 float * sx, float * sy)
{
    float rad = angle_deg * PI_F / 180.0f;
    float distPixel = dist_mm * DIST_SCALE;
    *sx = sinf(rad) * distPixel + LIDAR_CX;
    *sy = LIDAR_CY - cosf(rad) * distPixel;
}

/* 控件渲染回调：清屏 -> 画 grid(角度射线+距离环) -> 画点云 */
static void widget_render(ipgui_widget_t * widget, ipgui_widget_render_ctx_t * ctx)
{
    (void)widget;
    ipgui_surf_t * surf = ctx->surf;

    /* 背景（与原 RRT main 一致） */
    ipgui_color_t bg;
    IPGUI_COLOR_SET(bg, 255, 0x202830);
    ipgui_blend_color(surf, NULL, &surf->surf, bg, 255, NULL, NULL, IPGUI_BLEND_NORMAL);

    /* ---- grid：角度刻度线 ----
     * 用 Bresenham 画线(ipgui_draw_line_classic)一次画一条，替代逐点打线。
     * 0°/90°/180°/270° 亮黄色主轴线，其余每 30° 深灰。 */
    ipgui_color_t grid;
    IPGUI_COLOR_SET(grid, 255, 0x3C3C3C);
    ipgui_color_t axis;
    IPGUI_COLOR_SET(axis, 255, 0xFFD060);   /* 主方向：亮黄 */

    ipgui_line_style_t lineStyle;
    lineStyle.width      = 1;
    lineStyle.cap        = IPGUI_LINE_CAP_BUTT;
    lineStyle.opacity    = 255;
    lineStyle.blend_mode = IPGUI_BLEND_NORMAL;
    lineStyle.paint.type = IPGUI_PAINT_COLOR;

    ipgui_point_t center;
    center.x = LIDAR_CX;
    center.y = LIDAR_CY;

    for (int angle = 0; angle < 360; angle += 30) {
        bool isMainAxis = (angle % 90 == 0);
        lineStyle.paint.src.color = isMainAxis ? axis : grid;
        float rad = angle * PI_F / 180.0f;
        ipgui_line_t ln;
        ln.start = center;
        ln.end.x = (ipgui_coord_t)(LIDAR_CX + MAX_PIXEL_R * sinf(rad));
        ln.end.y = (ipgui_coord_t)(LIDAR_CY - MAX_PIXEL_R * cosf(rad));
        ipgui_draw_line_classic(surf, NULL, &ln, &lineStyle);
    }

    /* ---- grid：距离环（每 100px 一圈，用 ipgui_draw_arc 整圆绘制） ---- */
    ipgui_arc_style_t arcStyle;
    arcStyle.opacity    = 255;
    arcStyle.blend_mode = IPGUI_BLEND_NORMAL;
    arcStyle.sep_type   = IPGUI_ARC_ENDPOINT_TYPE_BUTT;
    arcStyle.eep_type   = IPGUI_ARC_ENDPOINT_TYPE_BUTT;
    arcStyle.paint.type = IPGUI_PAINT_COLOR;
    arcStyle.paint.src.color = grid;

    for (int ringR = RING_STEP_PX; ringR <= MAX_PIXEL_R; ringR += RING_STEP_PX) {
        ipgui_arc_t ring;
        ring.cx    = LIDAR_CX;
        ring.cy    = LIDAR_CY;
        ring.er    = ringR;          /* 外半径 */
        ring.ir    = ringR - 1;      /* 内半径 = 外半径-1，画 1px 圆环 */
        ring.start = 0;
        ring.dir   = IPGUI_ARC_DRAW_DIR_CW;
        ring.angle = 360;            /* 整圆 */
        ipgui_draw_arc(surf, NULL, &ring, &arcStyle);
    }


    /* ---- 画最新一圈点云 ----
     * 颜色按信号质量调亮度（参考官方 scanView:140-143）：
     *   brightness = quality*2 + 128（clamp 255），RGB(0, b, b) 青色渐变
     * 点用 2x2 像素块（参考官方 FillSolidRect(x-1,y-1,2,2)），单像素太细 */
    CSLock lk(g_nodes_mtx);
    for (size_t i = 0; i < g_nodes_count; ++i) {
        float dist_mm = g_nodes[i].dist_mm_q2 / 4.0f;
        if (dist_mm <= 0.0f)
            continue;   /* 无效测距点（参考官方 if(!dist_mm_q2) continue） */
        float angle_deg = (g_nodes[i].angle_z_q14 * 90.f) / 16384.f;

        float sx, sy;
        polarToScreen(angle_deg, dist_mm, &sx, &sy);
        ipgui_coord_t px = (ipgui_coord_t)sx;
        ipgui_coord_t py = (ipgui_coord_t)sy;

        /* 质量调亮度 */
        int b = (g_nodes[i].quality << 1) + 128;
        if (b > 255) b = 255;
        ipgui_color_t pc;
        pc.a = 255; pc.r = 0; pc.g = (u8_t)b; pc.b = (u8_t)b;

        /* 2x2 像素块 */
        ipgui_draw_pixel(surf, NULL, px,     py,     pc, 255, 255, IPGUI_BLEND_NORMAL);
        ipgui_draw_pixel(surf, NULL, px + 1, py,     pc, 255, 255, IPGUI_BLEND_NORMAL);
        ipgui_draw_pixel(surf, NULL, px,     py + 1, pc, 255, 255, IPGUI_BLEND_NORMAL);
        ipgui_draw_pixel(surf, NULL, px + 1, py + 1, pc, 255, 255, IPGUI_BLEND_NORMAL);
    }
}

/* 初始化 IPGUI 屏幕（SDL 驱动），参考原 RRT main 的初始化流程 */
static bool init_screen()
{
    ipgui_memset(&main_screen, 0, sizeof(main_screen));
    ipgui_memset(&sdl_drv, 0, sizeof(sdl_drv));
    ipgui_memset(&g_sdl_private, 0, sizeof(g_sdl_private));

    sdl_drv.xreso       = SCREEN_W;
    sdl_drv.yreso       = SCREEN_H;
    sdl_drv.pri_data    = &g_sdl_private;
    sdl_drv.put_pixel   = sdl_put_pixel;
    sdl_drv.fill_region = sdl_fill_region;
    sdl_drv.flush       = sdl_flush;

    if (ipgui_screen_init(&main_screen, &sdl_drv) != IPGUI_ERR_OK) {
        fprintf(stderr, "ipgui screen init failed\n");
        return false;
    }
    if (ipgui_sdl_screen_init(&main_screen) != 0) {
        fprintf(stderr, "ipgui sdl screen init failed\n");
        return false;
    }
    ipgui_scr_create_pfb(&main_screen, main_screen_frame_buf,
                         sizeof(main_screen_frame_buf), PIX_FMT_RGBA8888);
    ipgui_init();

    /* 全屏 widget，render 回调里画点云 */
    g_widget = ipgui_widget_create(NULL);
    g_widget->render = widget_render;
    g_widget->x = 0;
    g_widget->y = 0;
    g_widget->w = SCREEN_W;
    g_widget->h = SCREEN_H;

    return true;
}

/* 把 SDL 事件抽干（窗口关闭则退出） */
static bool pump_sdl_events()
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT)
            return false;
    }
    return true;
}

int main(int argc, const char * argv[])
{
    const char * opt_channel            = NULL;
    const char * opt_channel_param_first = NULL;
    sl_u32       opt_channel_param_second = 0;
    sl_result    op_result;
    int          opt_channel_type = CHANNEL_TYPE_SERIALPORT;
    bool         useArgcBaudrate  = false;
    IChannel *   _channel         = NULL;
    /* 扫描模式信息：startScan 返回 us_per_sample(每点采样时长)，官方转速算法用 */
    LidarScanMode usedScanMode;
    /* 每秒打印一次摘要的节流时间戳 */
    unsigned long lastPrintTick = 0;

    printf("Ultra simple LIDAR data grabber for SLAMTEC LIDAR.\n"
           "Version: %s\n", SL_LIDAR_SDK_VERSION);

    /* ---- 解析命令行参数 ---- */
    if (argc > 1 && strcmp(argv[1], "--channel") == 0) {
        opt_channel = argv[2];
        if (strcmp(opt_channel, "-s") == 0 ||
            strcmp(opt_channel, "--serial") == 0) {
            opt_channel_param_first = argv[3];
            if (argc > 4) opt_channel_param_second =
                (sl_u32)strtoul(argv[4], NULL, 10);
            useArgcBaudrate = true;
        } else if (strcmp(opt_channel, "-u") == 0 ||
                   strcmp(opt_channel, "--udp") == 0) {
            opt_channel_param_first = argv[3];
            if (argc > 4) opt_channel_param_second =
                (sl_u32)strtoul(argv[4], NULL, 10);
            opt_channel_type = CHANNEL_TYPE_UDP;
        } else {
            print_usage(argv[0]);
            return -1;
        }
    } else if (argc == 1) {
        /* 无参数：使用默认串口（A1） */
        opt_channel_param_first = DEFAULT_COM_PORT;
        opt_channel_param_second = DEFAULT_BAUDRATE;
        useArgcBaudrate = true;
        printf("未指定参数，使用默认串口 %s @ %d\n",
               DEFAULT_COM_PORT, DEFAULT_BAUDRATE);
    } else {
        print_usage(argv[0]);
        return -1;
    }

    /* 串口默认端口兜底 */
    if (opt_channel_type == CHANNEL_TYPE_SERIALPORT &&
        !opt_channel_param_first) {
        opt_channel_param_first = DEFAULT_COM_PORT;
    }

    /* ---- 创建驱动实例 ---- */
    ILidarDriver * drv = *createLidarDriver();
    if (!drv) {
        fprintf(stderr, "insufficent memory, exit\n");
        return -2;
    }

    sl_lidar_response_device_info_t devinfo;
    bool connectSuccess = false;

    /* ---- 连接雷达 ---- */
    if (opt_channel_type == CHANNEL_TYPE_SERIALPORT) {
        _channel = (*createSerialPortChannel(opt_channel_param_first,
                                             opt_channel_param_second));
        if (SL_IS_OK(drv->connect(_channel))) {
            op_result = drv->getDeviceInfo(devinfo);
            if (SL_IS_OK(op_result)) {
                connectSuccess = true;
            } else {
                /* 串口能打开但雷达无应答：打印原因便于排查 */
                fprintf(stderr, "串口已打开，但 getDeviceInfo 失败 0x%x "
                        "(雷达无应答)\n", op_result);
                delete drv;
                drv = NULL;
            }
        } else {
            fprintf(stderr, "串口打开失败，请确认 COM 口未被占用\n");
        }
    } else if (opt_channel_type == CHANNEL_TYPE_UDP) {
        _channel = *createUdpChannel(opt_channel_param_first,
                                     opt_channel_param_second);
        if (SL_IS_OK(drv->connect(_channel))) {
            op_result = drv->getDeviceInfo(devinfo);
            if (SL_IS_OK(op_result)) {
                connectSuccess = true;
            } else {
                delete drv;
                drv = NULL;
            }
        }
    }

    if (!connectSuccess) {
        if (opt_channel_type == CHANNEL_TYPE_SERIALPORT)
            fprintf(stderr, "Error, cannot bind to the specified serial port %s.\n",
                    opt_channel_param_first);
        else
            fprintf(stderr, "Error, cannot connect to the specified ip addr %s.\n",
                    opt_channel_param_first);
        goto on_finished;
    }

    /* ---- 打印设备信息 ---- */
    printf("SLAMTEC LIDAR S/N: ");
    for (int pos = 0; pos < 16; ++pos) {
        printf("%02X", devinfo.serialnum[pos]);
    }
    printf("\n"
           "Firmware Ver: %d.%02d\n"
           "Hardware Rev: %d\n",
           devinfo.firmware_version >> 8,
           devinfo.firmware_version & 0xFF,
           (int)devinfo.hardware_version);

    /* ---- 健康检查 ---- */
    if (!checkLidarHealth(drv)) {
        goto on_finished;
    }

    signal(SIGINT, onCtrlC);

    /* ---- 启动电机（A1 通过 DTR 信号驱动电机，串口模式才需要） ---- */
    if (opt_channel_type == CHANNEL_TYPE_SERIALPORT)
        drv->setMotorSpeed();

    /* ---- 初始化 IPGUI 屏幕（SDL 窗口）画点云 ---- */
    if (!init_screen()) {
        fprintf(stderr, "Warning: screen init failed, 仅输出到控制台\n");
    }

    /* ---- 开始扫描 ----
     * outUsedScanMode 返回当前扫描模式的 us_per_sample，供转速计算 */
    drv->startScan(0, 1, 0, &usedScanMode);

    /* ---- 循环抓取扫描数据：刷新点云窗口 + 控制台打印 ----
     * 只在拿到新一圈数据时重绘，避免 60fps 全屏画重复内容（点云 7.5Hz） */
    while (1) {
        /* SDL 窗口事件每轮都处理，保证关窗/退出响应及时 */
        if (!pump_sdl_events()) {
            g_ctrl_c_pressed = true;
            break;
        }

        sl_lidar_response_measurement_node_hq_t nodes[8192];
        size_t count = _countof(nodes);

        op_result = drv->grabScanDataHq(nodes, count);

        if (SL_IS_OK(op_result)) {
            drv->ascendScanData(nodes, count);

            /* 更新共享点云缓冲区供 widget_render 读取 */
            {
                CSLock lk(g_nodes_mtx);
                size_t n = count > _countof(g_nodes) ? _countof(g_nodes) : count;
                if (n) memcpy(g_nodes, nodes, n * sizeof(g_nodes[0]));
                g_nodes_count = n;
            }

            /* 转速：官方 scanView.cpp:268 算法
             *   一圈耗时(us) = 本圈点数 × 每点采样时长(us_per_sample)
             *   扫描频率 Hz   = 1e6 / 一圈耗时(us)
             * 单位换算 RPM = Hz × 60 */
            float hz = 0.0f;
            if (usedScanMode.us_per_sample > 0.0f) {
                hz = 1000000.0f / (count * usedScanMode.us_per_sample);
            }

            /* 每秒打印一次摘要（转速 + 点数），替代原来每点打印（刷屏） */
            unsigned long nowTick = GetTickCount();
            if (nowTick - lastPrintTick >= 1000) {
                printf("[扫描] %.2f Hz (%.1f RPM), 本圈 %d 点\n",
                       hz, hz * 60.0f, (int)count);
                lastPrintTick = nowTick;
            }

            /* 有新数据才重绘（点云只有这一帧变了，grid 静态） */
            if (g_widget)
                ipgui_widget_mark_dirty(g_widget);
            ipgui_screen_render(&main_screen);   /* 内部调用 drv->flush */
        }

        if (g_ctrl_c_pressed) {
            break;
        }
    }

    /* ---- 停止扫描并停电机 ---- */
    drv->stop();
    delay(200);
    if (opt_channel_type == CHANNEL_TYPE_SERIALPORT)
        drv->setMotorSpeed(0);

on_finished:
    if (drv) {
        delete drv;
        drv = NULL;
    }
    return 0;
}
