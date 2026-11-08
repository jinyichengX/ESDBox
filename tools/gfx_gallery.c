/*
 * IPGUI 图册生成器 v2
 * 每张图只讲一个 API，横向排 3-4 个参数变体，参数值直接标在图里。
 * 画布 1000x420，深色底、大字号，避免缩到文档里看不清。
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "ipgui_core.h"
#include "ipgui_screen.h"
#include "ipgui_input_dispatcher.h"
#include "ipgui_input_src.h"
#include "ipgui_blend.h"
#include "ipgui_blend_mode.h"
#include "ipgui_draw_pixel.h"
#include "ipgui_draw_line.h"
#include "ipgui_draw_triangle.h"
#include "ipgui_draw_filled_circle.h"
#include "ipgui_draw_arc.h"
#include "ipgui_draw_polygon.h"
#include "ipgui_draw_box_background.h"
#include "ipgui_draw_box_border.h"
#include "ipgui_draw_box_shadow.h"
#include "ipgui_draw_builtin_font.h"
#include "ipgui_draw_image.h"
#include "ipgui_draw_image_api.h"
#include "ipgui_draw_icon.h"
#include "ipgui_draw_icon_api.h"
#include "ipgui_gradient_color.h"
#include "ipgui_mask_gradient.h"

#include "open_sans.h"
#include "quicksand_medium.h"

#define W 1000
#define H 420

/* 旧 demo 控件仍参与链接 */
ipgui_image_data_t wave_img;
ipgui_image_data_t main_bg_img;
ipgui_image_data_t power_img;
ipgui_image_data_t tablelamp_img;
ipgui_image_data_t tablelamp_on_img;
ipgui_scr_t            main_screen;
ipgui_input_dispatcher_t dispatcher;
ipgui_input_src_t      pointer_src;
ipgui_input_src_id_t   pointer_id;
ipgui_scr_id_t         main_scr_id;

static u8_t      g_buf[W * H * 4];
static ipgui_surf_t g_surf;

/* 填充源演示用的 Lena 像素（在 build_fill_image 里加载） */
static u8_t g_fill_img[220 * 220 * 4];

static const u8_t BG_R = 255, BG_G = 255, BG_B = 255;
static const u8_t LB_R = 45, LB_G = 55, LB_B = 70;

static ipgui_color_t C(u8_t r, u8_t g, u8_t b)
{
    ipgui_color_t c;
    c.r = r; c.g = g; c.b = b; c.a = 255;
    return c;
}

/* ---- 图内标注 ------------------------------------------------------------
 * 框架内置字体只有 ASCII，中文标注没法用 ipgui_draw_builtin_text 画。
 * 这里只记录 字号|位置|颜色|文字，等 BMP 落盘后由 tools/render_captions.ps1
 * 用系统中文字体叠上去。
 * ------------------------------------------------------------------------ */
static FILE * g_cap;
static char   g_caps[128][256];
static int    g_cap_n;

static void cap_line(int size, int x, int y, int center, ipgui_color_t c,
                     const char * s)
{
    if (!g_cap || g_cap_n >= 128) return;
    snprintf(g_caps[g_cap_n], sizeof(g_caps[0]), "%d|%d|%d|%d|%02X%02X%02X|%s",
             size, x, y, center, c.r, c.g, c.b, s);
    g_cap_n ++;
}

static void cap_left(int size, int x, int y, const char * s)
{
    cap_line(size, x, y, 0, C(LB_R, LB_G, LB_B), s);
}

static void cap_center(int size, int cx, int y, const char * s)
{
    cap_line(size, cx, y, 1, C(LB_R, LB_G, LB_B), s);
}

static void cap_flush(const char * bmp_path)
{
    const char * base = strrchr(bmp_path, '/');
    char name[64];
    char * dot;
    int i;

    base = base ? base + 1 : bmp_path;
    snprintf(name, sizeof(name), "%s", base);
    dot = strrchr(name, '.');
    if (dot) *dot = 0;

    if (g_cap) {
        for (i = 0; i < g_cap_n; i ++)
            fprintf(g_cap, "%s|%s\n", name, g_caps[i]);
    }
    g_cap_n = 0;
}

static void save_bmp(const char * path)
{
    FILE * f = fopen(path, "wb");
    int row = W * 4;
    int size = 54 + row * H;
    u8_t hdr[54];
    int x, y;

    if (!f) { printf("open fail %s\n", path); return; }
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B'; hdr[1] = 'M';
    memcpy(&hdr[2], &size, 4);
    memcpy(&hdr[10], &(int){54}, 4);
    memcpy(&hdr[14], &(int){40}, 4);
    memcpy(&hdr[18], &(int){W}, 4);
    memcpy(&hdr[22], &(int){-H}, 4);
    memcpy(&hdr[26], &(int){1}, 2);
    memcpy(&hdr[28], &(int){32}, 2);
    memcpy(&hdr[34], &(int){row * H}, 4);
    fwrite(hdr, 1, 54, f);

    for (y = 0; y < H; y ++) {
        u8_t * p = g_buf + y * W * 4;
        for (x = 0; x < W; x ++) {
            u8_t bgr[4];
            bgr[0] = p[2]; bgr[1] = p[1]; bgr[2] = p[0]; bgr[3] = 0;
            fwrite(bgr, 1, 4, f);
            p += 4;
        }
    }
    fclose(f);
    printf("  -> %s\n", path);
    cap_flush(path);
}

static void text(const ipgui_font_t * f, const char * s, int x, int y,
                 ipgui_color_t c, u8_t op)
{
    ipgui_font_style_t st;
    memset(&st, 0, sizeof(st));
    st.font = f;
    st.paint.type = IPGUI_PAINT_COLOR;
    st.paint.src.color = c;
    st.opacity = op;
    st.blend_mode = IPGUI_BLEND_NORMAL;
    ipgui_draw_builtin_text(&g_surf, (ipgui_aabb_t *)0, &st,
                            (const s8_t *)s, x, y);
}

static void fill(int x, int y, int w, int h, ipgui_color_t c, u8_t op)
{
    ipgui_aabb_t box;
    ipgui_box_style_t bs;
    ipgui_box_bg_style_t bg;
    box.start.x = x; box.start.y = y;
    box.end.x = x + w - 1; box.end.y = y + h - 1;
    memset(&bs, 0, sizeof(bs));
    memset(&bg, 0, sizeof(bg));
    bg.paint.type = IPGUI_PAINT_COLOR;
    bg.paint.src.color = c;
    bg.opacity = op;
    bg.blend_mode = IPGUI_BLEND_NORMAL;
    ipgui_draw_box_background(&g_surf, (ipgui_aabb_t *)0, &box, &bs, &bg);
}

static void canvas(const char * title, const char * sub)
{
    int i;

    memset(g_buf, 0, sizeof(g_buf));

    g_surf.surf.start.x = 0; g_surf.surf.start.y = 0;
    g_surf.surf.end.x = W - 1; g_surf.surf.end.y = H - 1;
    g_surf.color = g_buf;
    g_surf.stride = W * 4;
    g_surf.pix_fmt = PIX_FMT_RGBA8888;
    g_surf.pix_size = 4;

    fill(0, 0, W, H, C(BG_R, BG_G, BG_B), 255);

    for (i = 40; i < W; i += 40)
        fill(i, 80, 1, H - 80, C(238, 241, 246), 255);
    for (i = 80; i < H; i += 40)
        fill(0, i, W, 1, C(238, 241, 246), 255);

    fill(0, 0, W, 80, C(250, 251, 253), 255);
    fill(0, 79, W, 2, C(46, 120, 190), 255);

    cap_line(24, 36, 8, 0, C(20, 26, 36), title);
    cap_line(12, 37, 54, 0, C(108, 120, 136), sub);
}

static void label(int cx, const char * s, ipgui_color_t c)
{
    cap_line(16, cx, 352, 1, c, s);
}

/* ------------------------------------------------------------------ */
static void demo_pixel(void)
{
    int i, k;

    canvas("像素 · ipgui_draw_pixel", "单像素写入：opacity 与 mask 都是逐像素生效");

    /* 点阵：3 行 x 12 列单像素 */
    for (k = 0; k < 3; k ++)
        for (i = 0; i < 12; i ++)
            ipgui_draw_pixel(&g_surf, 0, 96 + i * 34, 110 + k * 22,
                             C(78, 170, 255), 255, 255, IPGUI_BLEND_NORMAL);
    cap_left(16, 96, 186, "x 步长 34，y 步长 22");

    /* opacity 斜坡：60 列 x 8 行 */
    for (i = 0; i < 60; i ++) {
        u8_t op = (u8_t)(30 + i * 3);
        for (k = 0; k < 8; k ++)
            for (int j = 0; j < 3; j ++)
                ipgui_draw_pixel(&g_surf, 0, 96 + i * 6 + j, 226 + k,
                                 C(78, 170, 255), 255, op, IPGUI_BLEND_NORMAL);
    }
    cap_left(16, 96, 248, "不透明度 30 → 207");

    /* mask 斜坡 */
    for (i = 0; i < 60; i ++) {
        u8_t mk = (u8_t)(30 + i * 3);
        for (k = 0; k < 8; k ++)
            for (int j = 0; j < 3; j ++)
                ipgui_draw_pixel(&g_surf, 0, 520 + i * 6 + j, 226 + k,
                                 C(255, 120, 120), mk, 255, IPGUI_BLEND_NORMAL);
    }
    cap_left(16, 520, 248, "覆盖率 30 → 207");

    save_bmp("docs/images/01-pixel.bmp");
}

/* ------------------------------------------------------------------ */
static void line_at(int x, int y, int len, int w, ipgui_line_cap_t cap,
                    ipgui_color_t c)
{
    ipgui_line_t ln;
    ipgui_line_style_t st;
    ln.start.x = x; ln.start.y = y;
    ln.end.x = x + len; ln.end.y = y;
    memset(&st, 0, sizeof(st));
    st.width = (ipgui_coord_t)w;
    st.cap = cap;
    st.paint.type = IPGUI_PAINT_COLOR;
    st.paint.src.color = c;
    st.opacity = 255;
    st.blend_mode = IPGUI_BLEND_NORMAL;
    ipgui_draw_line_generic(&g_surf, 0, &ln, &st);
}

static void demo_line(void)
{
    ipgui_line_t ln;
    ipgui_line_style_t st;
    int widths[4] = {1, 3, 6, 10};
    int ops[3] = {70, 140, 220};
    int i;

    canvas("直线 · ipgui_draw_line_generic", "斜线：只改线宽 / 只改不透明度");

    memset(&st, 0, sizeof(st));
    st.paint.type = IPGUI_PAINT_COLOR;
    st.blend_mode = IPGUI_BLEND_NORMAL;

    /* 左：四条同角度斜线，只改 width */
    st.paint.src.color = C(78, 170, 255);
    st.cap = IPGUI_LINE_CAP_BUTT;
    st.opacity = 255;

    {
        const char * tags[4] = { "线宽 1", "线宽 3", "线宽 6", "线宽 10" };

        for (i = 0; i < 4; i ++) {
            ln.start.x = 60 + i * 98;
            ln.start.y = 300;
            ln.end.x   = ln.start.x + 70;
            ln.end.y   = 120;

            st.width = (ipgui_coord_t)widths[i];
            ipgui_draw_line_generic(&g_surf, 0, &ln, &st);

            cap_center(14, ln.start.x + 35, 320, tags[i]);
        }
    }

    /* 右：三条交叉斜线，只改 opacity，交叉处叠深 */
    st.paint.src.color = C(255, 120, 120);
    st.width = 10;
    st.cap = IPGUI_LINE_CAP_ROUND;

    ln.start.x = 560; ln.start.y = 110;
    ln.end.x   = 900; ln.end.y   = 320;
    st.opacity = (u8_t)ops[0];
    ipgui_draw_line_generic(&g_surf, 0, &ln, &st);

    ln.start.x = 560; ln.start.y = 320;
    ln.end.x   = 900; ln.end.y   = 110;
    st.opacity = (u8_t)ops[1];
    ipgui_draw_line_generic(&g_surf, 0, &ln, &st);

    ln.start.x = 730; ln.start.y = 100;
    ln.end.x   = 730; ln.end.y   = 340;
    st.opacity = (u8_t)ops[2];
    ipgui_draw_line_generic(&g_surf, 0, &ln, &st);

    cap_center(14, 730, 352, "不透明度 70 / 140 / 220");

    save_bmp("docs/images/02-line.bmp");
}

/* ------------------------------------------------------------------ */
static void demo_triangle(void)
{
    ipgui_point_t p1, p2, p3;
    ipgui_triangle_style_t st;
    int cx[3] = {380, 500, 620};
    int ops[3] = {255, 170, 90};
    int i;

    canvas("三角形 · ipgui_draw_triangle", "形状相同，不透明度 255 / 170 / 90，相互重叠");

    memset(&st, 0, sizeof(st));
    st.paint.type = IPGUI_PAINT_COLOR;
    st.paint.src.color = C(78, 170, 255);
    st.blend_mode = IPGUI_BLEND_NORMAL;

    for (i = 0; i < 3; i ++) {
        p1.x = cx[i];       p1.y = 130;
        p2.x = cx[i] + 110; p2.y = 280;
        p3.x = cx[i] - 110; p3.y = 280;
        st.opacity = (u8_t)ops[i];
        ipgui_draw_triangle(&g_surf, 0, &p1, &p2, &p3, &st);
    }

    label(380, "不透明度 255", C(LB_R, LB_G, LB_B));
    label(500, "不透明度 170", C(LB_R, LB_G, LB_B));
    label(620, "不透明度 90",  C(LB_R, LB_G, LB_B));

    save_bmp("docs/images/03-triangle.bmp");
}

/* ------------------------------------------------------------------ */
static void demo_circle(void)
{
    ipgui_filled_circle_style_t st;
    int cx[3] = {380, 500, 620};
    int ops[3] = {255, 170, 90};
    int i;

    canvas("实心圆 · ipgui_draw_filled_circle", "半径同为 90，不透明度 255 / 170 / 90，相互重叠");

    memset(&st, 0, sizeof(st));
    st.paint.type = IPGUI_PAINT_COLOR;
    st.paint.src.color = C(78, 170, 255);
    st.blend_mode = IPGUI_BLEND_NORMAL;

    for (i = 0; i < 3; i ++) {
        st.opacity = (u8_t)ops[i];
        ipgui_draw_filled_circle(&g_surf, 0, cx[i], 205, 90, &st);
    }

    label(380, "不透明度 255", C(LB_R, LB_G, LB_B));
    label(500, "不透明度 170", C(LB_R, LB_G, LB_B));
    label(620, "不透明度 90",  C(LB_R, LB_G, LB_B));

    save_bmp("docs/images/04-circle.bmp");
}

/* ------------------------------------------------------------------ */
static void demo_arc(void)
{
    int starts[3]  = { 17, 41, 128 };
    int angles[3]  = { 63, 138, 227 };
    int caps[3]    = { 1, 0, 1 };          /* 1 = round, 0 = butt */
    int i;

    canvas("圆弧 · ipgui_draw_arc", "er / ir 为内外半径，start 为起始角度，angle 为再画多少度");

    for (i = 0; i < 3; i ++) {
        ipgui_arc_t a;
        ipgui_arc_style_t st;
        int cx = 220 + i * 290;

        a.cx = cx;  a.cy = 210;
        a.er = 100; a.ir = 72;
        a.start = (ipgui_arc_angle_t)starts[i];
        a.dir = IPGUI_ARC_DRAW_DIR_CCW;
        a.angle = (u16_t)angles[i];

        memset(&st, 0, sizeof(st));
        st.paint.type = IPGUI_PAINT_COLOR;
        st.paint.src.color = C(78, 170, 255);
        st.opacity = 255;
        st.sep_type = caps[i] ? IPGUI_ARC_ENDPOINT_TYPE_ROUND : IPGUI_ARC_ENDPOINT_TYPE_BUTT;
        st.eep_type = caps[i] ? IPGUI_ARC_ENDPOINT_TYPE_ROUND : IPGUI_ARC_ENDPOINT_TYPE_BUTT;
        st.blend_mode = IPGUI_BLEND_NORMAL;
        ipgui_draw_arc(&g_surf, 0, &a, &st);
    }

    label(220, "起始角度 17°，逆时针画 63°",  C(LB_R, LB_G, LB_B));
    label(510, "起始角度 41°，逆时针画 138°", C(LB_R, LB_G, LB_B));
    label(800, "起始角度 128°，逆时针画 227°", C(LB_R, LB_G, LB_B));

    save_bmp("docs/images/05-arc.bmp");
}

/* ------------------------------------------------------------------ */
static ipgui_polygon_ras_t g_ras_ctx;
static int g_ras_ready = 0;

static void poly_draw(ipgui_point_t * pts, int n, ipgui_color_t c,
                      ipgui_fill_rule_t rule)
{
    ipgui_polygon_style_t st;

    if (!g_ras_ready) {
        memset(&g_ras_ctx, 0, sizeof(g_ras_ctx));
        ipgui_polygon_ras_init(&g_ras_ctx);
        g_ras_ready = 1;
    }
    ipgui_polygon_ras_set_fill_rule(&g_ras_ctx, rule);

    memset(&st, 0, sizeof(st));
    st.color = c;
    st.alpha = 255;
    st.blend_mode = IPGUI_BLEND_NORMAL;
    ipgui_draw_polygon(&g_surf, (ipgui_aabb_t *)0, pts, n, &g_ras_ctx, &st);
}

static void demo_polygon(void)
{
    ipgui_point_t pts[8];
    int i;

    canvas("多边形 · ipgui_draw_polygon", "同样的 5 个顶点，填充规则：偶奇 vs 非零");

    /* convex pentagon */
    for (i = 0; i < 5; i ++) {
        double a = -1.5708 + i * 2.0 * 3.1415926 / 5.0;
        pts[i].x = (ipgui_coord_t)(190 + 95 * cos(a));
        pts[i].y = (ipgui_coord_t)(205 + 95 * sin(a));
    }
    poly_draw(pts, 5, C(78, 170, 255), IPGUI_FILL_RULE_EVENODD);

    /* pentagram, even-odd */
    for (i = 0; i < 5; i ++) {
        double a = -1.5708 + (i * 2 % 5) * 2.0 * 3.1415926 / 5.0;
        pts[i].x = (ipgui_coord_t)(500 + 100 * cos(a));
        pts[i].y = (ipgui_coord_t)(205 + 100 * sin(a));
    }
    poly_draw(pts, 5, C(255, 190, 90), IPGUI_FILL_RULE_EVENODD);

    /* pentagram, non-zero */
    for (i = 0; i < 5; i ++) {
        double a = -1.5708 + (i * 2 % 5) * 2.0 * 3.1415926 / 5.0;
        pts[i].x = (ipgui_coord_t)(810 + 100 * cos(a));
        pts[i].y = (ipgui_coord_t)(205 + 100 * sin(a));
    }
    poly_draw(pts, 5, C(120, 220, 160), IPGUI_FILL_RULE_NONZERO);

    label(190, "凸五边形 / 偶奇", C(LB_R, LB_G, LB_B));
    label(500, "五角星 / 偶奇", C(LB_R, LB_G, LB_B));
    label(810, "五角星 / 非零", C(LB_R, LB_G, LB_B));

    save_bmp("docs/images/06-polygon.bmp");
}

/* ------------------------------------------------------------------ */
static void round_box(int x, int y, int w, int h,
                      int tl, int tr, int br, int bl,
                      ipgui_paint_t * paint, u8_t op)
{
    ipgui_aabb_t box;
    ipgui_box_style_t bs;
    ipgui_box_bg_style_t bg;
    box.start.x = x; box.start.y = y;
    box.end.x = x + w - 1; box.end.y = y + h - 1;
    memset(&bs, 0, sizeof(bs));
    bs.left_top_radius     = (ipgui_coord_t)tl;
    bs.right_top_radius    = (ipgui_coord_t)tr;
    bs.right_bottom_radius = (ipgui_coord_t)br;
    bs.left_bottom_radius  = (ipgui_coord_t)bl;
    memset(&bg, 0, sizeof(bg));
    bg.paint = *paint;
    bg.opacity = op;
    bg.blend_mode = IPGUI_BLEND_NORMAL;
    ipgui_draw_box_background(&g_surf, 0, &box, &bs, &bg);
}

static void demo_box_bg(void)
{
    ipgui_paint_t paint;
    /* 左上 / 右上 / 右下 / 左下 */
    int radii[4][4] = {
        {  0,  0,  0,  0 },
        { 12, 12, 12, 12 },
        { 30, 30, 30, 30 },
        { 60, 40, 20,  0 },
    };
    int i;

    canvas("圆角背景 · ipgui_draw_box_background", "圆角半径 0 ～ 60，四个角可分别设置");

    paint.type = IPGUI_PAINT_COLOR;
    paint.src.color = C(78, 170, 255);

    for (i = 0; i < 4; i ++) {
        int cx = 180 + i * 220;
        round_box(cx - 75, 125, 150, 150,
                  radii[i][0], radii[i][1], radii[i][2], radii[i][3],
                  &paint, 255);
    }
    label(180, "圆角半径 0",  C(LB_R, LB_G, LB_B));
    label(400, "圆角半径 12", C(LB_R, LB_G, LB_B));
    label(620, "圆角半径 30", C(LB_R, LB_G, LB_B));
    label(840, "圆角半径 60/40/20/0", C(LB_R, LB_G, LB_B));

    save_bmp("docs/images/07-box-bg.bmp");
}

/* ------------------------------------------------------------------ */
static void label_y(int cx, int y, const char * s)
{
    cap_center(14, cx, y, s);
}

static void demo_box_border(void)
{
    /* 左上 / 右上 / 右下 / 左下 */
    int radius[4][4] = {
        {  0,  0,  0,  0 },
        { 12, 12, 12, 12 },
        { 26, 26, 26, 26 },
        { 44, 30, 15,  0 },
    };
    int widths[4] = { 1, 3, 6, 10 };
    int i;

    canvas("圆角边框 · ipgui_draw_box_border", "上排只改圆角半径，下排只改边框宽度");

    /* 上排：只改圆角半径，边框宽 4 */
    for (i = 0; i < 4; i ++) {
        ipgui_aabb_t             box;
        ipgui_box_style_t        bs;
        ipgui_box_border_style_t st;
        int cx = 160 + i * 240;

        box.start.x = cx - 55;  box.start.y = 100;
        box.end.x   = cx + 55;  box.end.y   = 210;

        memset(&bs, 0, sizeof(bs));
        bs.left_top_radius     = (ipgui_coord_t)radius[i][0];
        bs.right_top_radius    = (ipgui_coord_t)radius[i][1];
        bs.right_bottom_radius = (ipgui_coord_t)radius[i][2];
        bs.left_bottom_radius  = (ipgui_coord_t)radius[i][3];

        memset(&st, 0, sizeof(st));
        st.paint.type = IPGUI_PAINT_COLOR;
        IPGUI_COLOR_SET(st.paint.src.color, 255, 0x4EAAFF);
        st.width = 4;
        st.opacity = 255;
        st.blend_mode = IPGUI_BLEND_NORMAL;
        ipgui_draw_box_border(&g_surf, 0, &box, &bs, &st);

        {
            const char * tags[4] = { "圆角 0", "圆角 12", "圆角 26", "圆角 44/30/15/0" };
            label_y(cx, 222, tags[i]);
        }
    }

    /* 下排：只改边框宽度，圆角 14 */
    for (i = 0; i < 4; i ++) {
        ipgui_aabb_t             box;
        ipgui_box_style_t        bs;
        ipgui_box_border_style_t st;
        int cx = 160 + i * 240;

        box.start.x = cx - 55;  box.start.y = 262;
        box.end.x   = cx + 55;  box.end.y   = 372;

        memset(&bs, 0, sizeof(bs));
        bs.left_top_radius = 14;  bs.right_top_radius = 14;
        bs.left_bottom_radius = 14;  bs.right_bottom_radius = 14;

        memset(&st, 0, sizeof(st));
        st.paint.type = IPGUI_PAINT_COLOR;
        IPGUI_COLOR_SET(st.paint.src.color, 255, 0xFF7878);
        st.width = (ipgui_coord_t)widths[i];
        st.opacity = 255;
        st.blend_mode = IPGUI_BLEND_NORMAL;
        ipgui_draw_box_border(&g_surf, 0, &box, &bs, &st);

        {
            const char * tags[4] = { "线宽 1", "线宽 3", "线宽 6", "线宽 10" };
            label_y(cx, 384, tags[i]);
        }
    }

    save_bmp("docs/images/08-box-border.bmp");
}

/* ------------------------------------------------------------------ */
static void demo_shadow(void)
{
    /* 左上 / 右上 / 右下 / 左下 */
    int radius[4][4] = {
        {  0,  0,  0,  0 },
        { 12, 12, 12, 12 },
        { 26, 26, 26, 26 },
        { 40, 28, 14,  0 },
    };
    int blurs[4] = {0, 10, 22, 40};
    int i;

    canvas("外阴影 · ipgui_draw_box_outer_shadow", "上排只改圆角半径，下排只改模糊半径");

    /* 上排：只改圆角半径，blur 固定 18 */
    for (i = 0; i < 4; i ++) {
        ipgui_aabb_t box;
        ipgui_box_style_t bs;
        ipgui_box_outer_shadow_style_t sh;
        ipgui_box_bg_style_t bg;
        int cx = 160 + i * 240;

        box.start.x = cx - 50; box.start.y = 100;
        box.end.x = cx + 50;   box.end.y = 200;

        memset(&bs, 0, sizeof(bs));
        bs.left_top_radius     = (ipgui_coord_t)radius[i][0];
        bs.right_top_radius    = (ipgui_coord_t)radius[i][1];
        bs.right_bottom_radius = (ipgui_coord_t)radius[i][2];
        bs.left_bottom_radius  = (ipgui_coord_t)radius[i][3];

        memset(&sh, 0, sizeof(sh));
        sh.paint.type = IPGUI_PAINT_COLOR;
        sh.paint.src.color = C(0, 0, 0);
        sh.opacity = 190;
        sh.blur = 18;
        sh.spread = 0;
        sh.offset_x = 0;
        sh.offset_y = 10;
        ipgui_draw_box_outer_shadow(&g_surf, 0, &box, &bs, &sh);

        memset(&bg, 0, sizeof(bg));
        bg.paint.type = IPGUI_PAINT_COLOR;
        bg.paint.src.color = C(38, 48, 62);
        bg.opacity = 255;
        bg.blend_mode = IPGUI_BLEND_NORMAL;
        ipgui_draw_box_background(&g_surf, 0, &box, &bs, &bg);

        {
            const char * tags[4] = { "圆角 0", "圆角 12", "圆角 26", "圆角 40/28/14/0" };
            label_y(cx, 222, tags[i]);
        }
    }

    /* 下排：只改模糊半径，圆角固定 14 */
    for (i = 0; i < 4; i ++) {
        ipgui_aabb_t box;
        ipgui_box_style_t bs;
        ipgui_box_outer_shadow_style_t sh;
        ipgui_box_bg_style_t bg;
        int cx = 160 + i * 240;

        box.start.x = cx - 50; box.start.y = 262;
        box.end.x = cx + 50;   box.end.y = 362;

        memset(&bs, 0, sizeof(bs));
        bs.left_top_radius = bs.right_top_radius = 14;
        bs.left_bottom_radius = bs.right_bottom_radius = 14;

        memset(&sh, 0, sizeof(sh));
        sh.paint.type = IPGUI_PAINT_COLOR;
        sh.paint.src.color = C(0, 0, 0);
        sh.opacity = 190;
        sh.blur = (ipgui_coord_t)blurs[i];
        sh.spread = 0;
        sh.offset_x = 0;
        sh.offset_y = 10;
        ipgui_draw_box_outer_shadow(&g_surf, 0, &box, &bs, &sh);

        memset(&bg, 0, sizeof(bg));
        bg.paint.type = IPGUI_PAINT_COLOR;
        bg.paint.src.color = C(38, 48, 62);
        bg.opacity = 255;
        bg.blend_mode = IPGUI_BLEND_NORMAL;
        ipgui_draw_box_background(&g_surf, 0, &box, &bs, &bg);

        {
            const char * tags[4] = { "模糊 0", "模糊 10", "模糊 22", "模糊 40" };
            label_y(cx, 384, tags[i]);
        }
    }

    save_bmp("docs/images/09-shadow.bmp");
}

/* ------------------------------------------------------------------ */
static u8_t g_img[64 * 64 * 4];
static u8_t g_pai[120 * 120 * 4];
static ipgui_image_data_t g_img_data;

static void build_image(void)
{
    FILE * f = fopen("tools/diaochan120_rgba.bin", "rb");

    if (f) {
        if (fread(g_pai, 1, sizeof(g_pai), f) == sizeof(g_pai)) {
            fclose(f);
            g_img_data.w       = 120;
            g_img_data.h       = 120;
            g_img_data.stride  = 120 * 4;
            g_img_data.pixmap  = g_pai;
            g_img_data.fmt     = IPGUI_IMG_FMT_RGBA8888;
            g_img_data.px_size = 4;
            return;
        }
        fclose(f);
    }

    {
        int x, y;
        for (y = 0; y < 64; y ++) {
            for (x = 0; x < 64; x ++) {
                u8_t * p = g_img + (y * 64 + x) * 4;
                int dx = x - 32, dy = y - 32;
                int d = (int)sqrt((double)(dx * dx + dy * dy));
                if (d < 14)      { p[0]=255; p[1]=255; p[2]=255; }
                else if (d < 24) { p[0]=78;  p[1]=170; p[2]=255; }
                else if (d < 34) { p[0]=255; p[1]=120; p[2]=120; }
                else             { p[0]=40;  p[1]=60;  p[2]=90;  }
                p[3] = 255;
            }
        }
    }
    g_img_data.w = 64;
    g_img_data.h = 64;
    g_img_data.stride = 64 * 4;
    g_img_data.pixmap = g_img;
    g_img_data.fmt = IPGUI_IMG_FMT_RGBA8888;
    g_img_data.px_size = 4;
}

static void draw_img(int cx, int cy, double scale, double deg)
{
    ipgui_point_t pivot, anchor;
    ipgui_trans_mat_t mat;
    ipgui_image_draw_style_t st;
    double a = deg * 3.1415926 / 180.0;

    pivot.x = g_img_data.w / 2;  pivot.y = g_img_data.h / 2;
    anchor.x = cx; anchor.y = cy;

    mat.a = (ipgui_scoord_t)(cos(a) * scale * 1024);
    mat.b = (ipgui_scoord_t)(-sin(a) * scale * 1024);
    mat.c = (ipgui_scoord_t)(sin(a) * scale * 1024);
    mat.d = (ipgui_scoord_t)(cos(a) * scale * 1024);

    memset(&st, 0, sizeof(st));
    st.opacity = 255;
    st.blend_mode = IPGUI_BLEND_NORMAL;
    ipgui_draw_image(&g_surf, 0, &g_img_data, &pivot, &anchor, &mat, &st,
                     IPGUI_IMAGE_QUALITY_HIGH);
}

/* ------------------------------------------------------------------ */
static u8_t g_dog[64 * 64 * 4];
static ipgui_image_data_t g_dog_data;

static void build_dog(void)
{
    FILE * f = fopen("tools/dog64_rgba.bin", "rb");

    if (f) {
        if (fread(g_dog, 1, sizeof(g_dog), f) != sizeof(g_dog)) {
            memset(g_dog, 0, sizeof(g_dog));
        }
        fclose(f);
    }

    g_dog_data.w       = 64;
    g_dog_data.h       = 64;
    g_dog_data.stride  = 64 * 4;
    g_dog_data.pixmap  = g_dog;
    g_dog_data.fmt     = IPGUI_IMG_FMT_RGBA8888;
    g_dog_data.px_size = 4;
}

static void demo_image_tiled(void)
{
    ipgui_aabb_t             r1, r2, r3;
    ipgui_point_t            pivot, anchor;
    ipgui_trans_mat_t        half, rot;
    ipgui_image_draw_style_t st;
    double a = 20.0 * 3.1415926 / 180.0;
    double s = 0.6;

    canvas("平铺 · ipgui_draw_image_tiled", "左：原尺寸　中：缩放 0.5　右：旋转 20° + 缩放 0.6");

    build_dog();

    pivot.x = 32;  pivot.y = 32;
    anchor.x = 0;  anchor.y = 0;

    memset(&st, 0, sizeof(st));
    st.opacity    = 255;
    st.blend_mode = IPGUI_BLEND_NORMAL;

    /* 左：原尺寸平铺，trans 传 NULL 即单位矩阵 */
    r1.start.x = 55;   r1.start.y = 110;
    r1.end.x   = 335;  r1.end.y   = 310;
    ipgui_draw_image_tiled(&g_surf, &r1, &g_dog_data, &pivot, &anchor, 0, &st,
                           IPGUI_IMAGE_QUALITY_HIGH);

    /* 中：缩放 0.5 后平铺，tile 变成 32px */
    half.a = 512;  half.b = 0;
    half.c = 0;    half.d = 512;
    r2.start.x = 360;  r2.start.y = 110;
    r2.end.x   = 640;  r2.end.y   = 310;
    ipgui_draw_image_tiled(&g_surf, &r2, &g_dog_data, &pivot, &anchor, &half, &st,
                           IPGUI_IMAGE_QUALITY_HIGH);

    /* 右：旋转 20° + 缩放 0.6，瓦片跟着转 */
    rot.a = (ipgui_scoord_t)(cos(a) * s * 1024);
    rot.b = (ipgui_scoord_t)(-sin(a) * s * 1024);
    rot.c = (ipgui_scoord_t)(sin(a) * s * 1024);
    rot.d = (ipgui_scoord_t)(cos(a) * s * 1024);
    r3.start.x = 665;  r3.start.y = 110;
    r3.end.x   = 945;  r3.end.y   = 310;
    ipgui_draw_image_tiled(&g_surf, &r3, &g_dog_data, &pivot, &anchor, &rot, &st,
                           IPGUI_IMAGE_QUALITY_HIGH);

    label(195, "瓦片 64×64", C(LB_R, LB_G, LB_B));
    label(500, "缩放 0.5",    C(LB_R, LB_G, LB_B));
    label(805, "旋转 20°，缩放 0.6", C(LB_R, LB_G, LB_B));

    save_bmp("docs/images/10b-image-tiled.bmp");
}

static void demo_image(void)
{
    canvas("图像 · ipgui_draw_image", "trans.a/d = 1024 表示 1.0 倍；pivot + anchor + 旋转");

    build_image();
    draw_img(190, 205, 1.0, 0);
    draw_img(500, 205, 1.8, 0);
    draw_img(820, 205, 1.0, 30);

    label(190, "缩放 1.0", C(LB_R, LB_G, LB_B));
    label(500, "缩放 1.8", C(LB_R, LB_G, LB_B));
    label(820, "缩放 1.0，旋转 30°", C(LB_R, LB_G, LB_B));

    save_bmp("docs/images/10-image.bmp");
}

/* ------------------------------------------------------------------
 * 图像便捷接口：at / centered / in_rect
 * ------------------------------------------------------------------ */
static void container(int x, int y, int w, int h)
{
    fill(x, y, w, h, C(234, 239, 246), 255);
    fill(x, y, w, 1, C(196, 208, 223), 255);
    fill(x, y + h - 1, w, 1, C(196, 208, 223), 255);
    fill(x, y, 1, h, C(196, 208, 223), 255);
    fill(x + w - 1, y, 1, h, C(196, 208, 223), 255);
}

static void demo_image_api(void)
{
    ipgui_aabb_t             box;
    ipgui_image_draw_style_t st;

    canvas("图像便捷接口 · ipgui_draw_image_api", "at / centered / in_rect：九宫格对齐 + 四种缩放模式");

    build_image();

    memset(&st, 0, sizeof(st));
    st.opacity    = 255;
    st.blend_mode = IPGUI_BLEND_NORMAL;

    /* 上排：at / centered / in_rect NONE */
    container(60, 95, 250, 125);
    ipgui_draw_image_at(&g_surf, &g_img_data, 60, 95, &st,
                        IPGUI_IMAGE_QUALITY_HIGH);

    container(370, 95, 250, 125);
    ipgui_draw_image_centered(&g_surf, &g_img_data, 495, 157, &st,
                              IPGUI_IMAGE_QUALITY_HIGH);

    box.start.x = 680;  box.start.y = 95;
    box.end.x   = 929;  box.end.y   = 219;
    container(680, 95, 250, 125);
    ipgui_draw_image_in_rect(&g_surf, &g_img_data, &box,
                             IPGUI_IMG_ALIGN_BOTTOM_RIGHT, IPGUI_IMG_FIT_NONE,
                             &st, IPGUI_IMAGE_QUALITY_HIGH);

    cap_center(14, 185, 228, "at：左上角定位");
    cap_center(14, 495, 228, "centered：中心定位");
    cap_center(14, 805, 228, "in_rect NONE：右下对齐");

    /* 下排：in_rect 的三种缩放模式 */
    box.start.x = 60;   box.start.y = 248;
    box.end.x   = 309;  box.end.y   = 362;
    container(60, 248, 250, 115);
    ipgui_draw_image_in_rect(&g_surf, &g_img_data, &box,
                             IPGUI_IMG_ALIGN_CENTER, IPGUI_IMG_FIT_FIT,
                             &st, IPGUI_IMAGE_QUALITY_HIGH);

    box.start.x = 370;  box.start.y = 248;
    box.end.x   = 619;  box.end.y   = 362;
    container(370, 248, 250, 115);
    ipgui_draw_image_in_rect(&g_surf, &g_img_data, &box,
                             IPGUI_IMG_ALIGN_CENTER, IPGUI_IMG_FIT_FILL,
                             &st, IPGUI_IMAGE_QUALITY_HIGH);

    box.start.x = 680;  box.start.y = 248;
    box.end.x   = 929;  box.end.y   = 362;
    container(680, 248, 250, 115);
    ipgui_draw_image_in_rect(&g_surf, &g_img_data, &box,
                             IPGUI_IMG_ALIGN_CENTER, IPGUI_IMG_FIT_STRETCH,
                             &st, IPGUI_IMAGE_QUALITY_HIGH);

    cap_center(14, 185, 372, "FIT：等比完整放入");
    cap_center(14, 495, 372, "FILL：等比裁剪填满");
    cap_center(14, 805, 372, "STRETCH：拉伸变形");

    save_bmp("docs/images/10c-image-api.bmp");
}

/* ------------------------------------------------------------------
 * 图标：8 位 alpha 蒙版 + paint 上色
 * ------------------------------------------------------------------ */
static u8_t g_icon_mask[96 * 96];
static ipgui_icon_data_t g_icon;              /* 96x96，接口演示用 */

static u8_t g_icon_lg_mask[160 * 160];
static ipgui_icon_data_t g_icon_lg;           /* 160x160，填充演示用 */

static void load_icon_mask(const char * path, u8_t * mask, int w)
{
    FILE * f = fopen(path, "rb");

    if (f) {
        if (fread(mask, 1, (size_t)(w * w), f) != (size_t)(w * w))
            memset(mask, 0, (size_t)(w * w));
        fclose(f);
        return;
    }

    /* 兜底：画一个圆 */
    {
        int x, y, r = w / 2 - 3;
        for (y = 0; y < w; y ++) {
            for (x = 0; x < w; x ++) {
                int dx = x - w / 2, dy = y - w / 2;
                mask[y * w + x] =
                    (dx * dx + dy * dy <= r * r) ? 255 : 0;
            }
        }
    }
}

static void build_icon(void)
{
    load_icon_mask("tools/icon_pic96.bin",  g_icon_mask,    96);
    load_icon_mask("tools/icon_pic160.bin", g_icon_lg_mask, 160);

    g_icon.w    = 96;
    g_icon.h    = 96;
    g_icon.mask = g_icon_mask;

    g_icon_lg.w    = 160;
    g_icon_lg.h    = 160;
    g_icon_lg.mask = g_icon_lg_mask;
}

/* 1024 = 1.0，和图像共用同一套矩阵约定 */
static void icon_mat(ipgui_trans_mat_t * m, double scale, double deg)
{
    double a = deg * 3.1415926 / 180.0;

    m->a = (ipgui_scoord_t)( cos(a) * scale * 1024);
    m->b = (ipgui_scoord_t)(-sin(a) * scale * 1024);
    m->c = (ipgui_scoord_t)( sin(a) * scale * 1024);
    m->d = (ipgui_scoord_t)( cos(a) * scale * 1024);
}

static void demo_icon(void)
{
    ipgui_draw_icon_style_t st;
    ipgui_point_t pivot, anchor;
    ipgui_trans_mat_t mat;

    canvas("图标 · ipgui_draw_icon",
           "同一个图标只换 paint；矩阵 = 缩放 × 旋转，正角度顺时针，负角度逆时针");

    build_icon();

    memset(&st, 0, sizeof(st));
    st.opacity    = 255;
    st.blend_mode = IPGUI_BLEND_NORMAL;

    pivot.x = 80;  pivot.y = 80;          /* 160x160 图标的中心 */

    /* ① 纯色，1.0 倍 */
    st.paint.type = IPGUI_PAINT_COLOR;
    IPGUI_COLOR_SET(st.paint.src.color, 255, 0x4EAAFF);
    anchor.x = 160;  anchor.y = 210;
    ipgui_draw_icon(&g_surf, 0, &g_icon_lg, &pivot, &anchor, 0, &st);

    /* ② 线性填充 + 放大 1.2 倍 + 顺时针 25° */
    {
        ipgui_liner_gradient_color_t lg;
        ipgui_gradient_color_stop_t s0, s1;

        ipgui_liner_gradient_init_direct(&lg, 300, 130, 500, 290);
        s0.pos = 0;   s0.color = C(78, 170, 255);
        s1.pos = 255; s1.color = C(255, 120, 120);
        ipgui_liner_gradient_add_stop(&lg, &s0);
        ipgui_liner_gradient_add_stop(&lg, &s1);

        st.paint.type = IPGUI_PAINT_GRADIENT;
        st.paint.src.grad_src.grad_type = IPGUI_GRADIENT_TYPE_LINEAR;
        st.paint.src.grad_src.grad.liner_grad = lg;
    }
    icon_mat(&mat, 1.2, 25);
    anchor.x = 400;  anchor.y = 210;
    ipgui_draw_icon(&g_surf, 0, &g_icon_lg, &pivot, &anchor, &mat, &st);

    /* ③ 径向填充，1.0 倍 */
    {
        ipgui_radial_gradient_color_t rg;
        ipgui_gradient_color_stop_t s0, s1;

        ipgui_radial_gradient_init(&rg, 640, 210, 85);
        s0.pos = 0;   s0.color = C(255, 190, 90);
        s1.pos = 255; s1.color = C(255, 120, 120);
        ipgui_radial_gradient_add_stop(&rg, &s0);
        ipgui_radial_gradient_add_stop(&rg, &s1);

        st.paint.type = IPGUI_PAINT_GRADIENT;
        st.paint.src.grad_src.grad_type = IPGUI_GRADIENT_TYPE_RADIAL;
        st.paint.src.grad_src.grad.radial_grad = rg;
    }
    anchor.x = 640;  anchor.y = 210;
    ipgui_draw_icon(&g_surf, 0, &g_icon_lg, &pivot, &anchor, 0, &st);

    /* ④ 图像填充 + 逆时针 15° */
    {
        ipgui_image_src_t isrc;
        ipgui_aabb_t      ibox;

        ibox.start.x = 780;  ibox.start.y = 110;
        ibox.end.x   = 980;  ibox.end.y   = 310;

        isrc.img_aabb  = &ibox;
        isrc.stride    = 220 * 4;
        isrc.buf       = g_fill_img;
        isrc.img_pxfmt = IPGUI_IMG_FMT_RGBA8888;
        isrc.px_size   = 4;

        st.paint.type = IPGUI_PAINT_IMAGE;
        st.paint.src.image_src = isrc;
    }
    icon_mat(&mat, 1.0, -15);
    anchor.x = 880;  anchor.y = 210;
    ipgui_draw_icon(&g_surf, 0, &g_icon_lg, &pivot, &anchor, &mat, &st);

    label(160, "纯色", C(LB_R, LB_G, LB_B));
    label(400, "线性填充（放大 1.2 倍，顺时针 25°）", C(LB_R, LB_G, LB_B));
    label(640, "径向填充", C(LB_R, LB_G, LB_B));
    label(880, "图像填充（逆时针 15°）", C(LB_R, LB_G, LB_B));

    save_bmp("docs/images/15-icon.bmp");
}

static void demo_icon_api(void)
{
    ipgui_draw_icon_style_t st;
    ipgui_aabb_t box;
    int cx[5] = { 105, 295, 485, 675, 865 };
    int i;

    canvas("图标接口 · ipgui_draw_icon_at / _centered / _in_rect",
           "九宫格对齐、缩放模式与旋转");

    build_icon();

    memset(&st, 0, sizeof(st));
    st.paint.type = IPGUI_PAINT_COLOR;
    IPGUI_COLOR_SET(st.paint.src.color, 255, 0x4EAAFF);
    st.opacity    = 255;
    st.blend_mode = IPGUI_BLEND_NORMAL;

    for (i = 0; i < 5; i ++)
        container(cx[i] - 85, 120, 170, 150);

    /* ① at：左上角 */
    ipgui_draw_icon_at(&g_surf, &g_icon, cx[0] - 85, 120, &st);

    /* ② centered：容器中心 */
    ipgui_draw_icon_centered(&g_surf, &g_icon, cx[1], 195, &st);

    /* ③ in_rect + FIT */
    box.start.x = cx[2] - 85;  box.start.y = 120;
    box.end.x   = cx[2] + 84;  box.end.y   = 269;
    ipgui_draw_icon_in_rect(&g_surf, &g_icon, &box,
                            IPGUI_ICON_ALIGN_CENTER, IPGUI_ICON_FIT_FIT, &st);

    /* ④ in_rect + STRETCH */
    box.start.x = cx[3] - 85;  box.start.y = 120;
    box.end.x   = cx[3] + 84;  box.end.y   = 269;
    ipgui_draw_icon_in_rect(&g_surf, &g_icon, &box,
                            IPGUI_ICON_ALIGN_CENTER, IPGUI_ICON_FIT_STRETCH, &st);

    /* ⑤ 旋转 30° */
    {
        ipgui_point_t     pivot, anchor;
        ipgui_trans_mat_t mat;
        double a = 30.0 * 3.1415926 / 180.0;

        pivot.x  = 48;        pivot.y  = 48;      /* 96x96 图标的中心 */
        anchor.x = cx[4];     anchor.y = 195;

        mat.a = (ipgui_scoord_t)( cos(a) * 1024);
        mat.b = (ipgui_scoord_t)(-sin(a) * 1024);
        mat.c = (ipgui_scoord_t)( sin(a) * 1024);
        mat.d = (ipgui_scoord_t)( cos(a) * 1024);

        ipgui_draw_icon(&g_surf, 0, &g_icon, &pivot, &anchor, &mat, &st);
    }

    cap_center(14, cx[0], 300, "at：左上角");
    cap_center(14, cx[1], 300, "centered：居中");
    cap_center(14, cx[2], 300, "in_rect FIT");
    cap_center(14, cx[3], 300, "in_rect STRETCH");
    cap_center(14, cx[4], 300, "旋转 30°");

    save_bmp("docs/images/15b-icon-api.bmp");
}

/* ------------------------------------------------------------------ */
static void demo_text(void)
{
    canvas("文字 · ipgui_draw_builtin_text", "内置点阵字体，5px ～ 50px");

    text(&open_sans_14px, "Open Sans 14px", 90, 160, C(24, 30, 40), 255);
    text(&open_sans_20px, "Open Sans 20px", 90, 200, C(24, 30, 40), 255);
    text(&open_sans_28px, "Open Sans 28px", 90, 240, C(24, 30, 40), 255);
    text(&open_sans_40px, "Open Sans 40px", 90, 290, C(24, 30, 40), 255);

    text(&quicksand_medium_20px, "Quicksand 20px", 620, 190, C(30, 100, 160), 255);
    text(&quicksand_medium_28px, "Quicksand 28px", 620, 240, C(30, 100, 160), 255);

    text(&open_sans_12px, "&open_sans_14px", 90, 344, C(108, 120, 136), 255);
    text(&open_sans_12px, "&quicksand_medium_20px", 620, 300, C(108, 120, 136), 255);

    save_bmp("docs/images/11-text.bmp");
}

/* ------------------------------------------------------------------
 * 文字阴影：先画阴影再画正文
 * ------------------------------------------------------------------ */
static void demo_text_shadow(void)
{
    int    cx[4]      = { 140, 390, 640, 890 };
    int    blurs[4]   = { 0, 6, 14, 14 };
    int    offs[4]    = { 4, 4, 6, 6 };
    int    colored[4] = { 0, 0, 0, 1 };
    const char * tags[4] = {
        "blur 0（硬阴影）", "blur 6", "blur 14", "blur 14，蓝色阴影"
    };
    int tw, i;

    canvas("文字阴影 · ipgui_draw_builtin_text_shadowed", "先画阴影再画正文：blur / 偏移 / 阴影颜色");

    tw = ipgui_builtin_text_width(&quicksand_medium_36px, (const s8_t *)"Shadow");

    for (i = 0; i < 4; i ++) {
        ipgui_font_style_t        st;
        ipgui_text_shadow_style_t sh;

        memset(&st, 0, sizeof(st));
        st.font = &quicksand_medium_36px;
        st.paint.type = IPGUI_PAINT_COLOR;
        st.paint.src.color = C(38, 48, 62);
        st.opacity = 255;
        st.blend_mode = IPGUI_BLEND_NORMAL;

        memset(&sh, 0, sizeof(sh));
        sh.color   = colored[i] ? C(78, 150, 255) : C(0, 0, 0);
        sh.opacity = 160;
        sh.blur    = (ipgui_coord_t)blurs[i];
        sh.offset_x = (ipgui_coord_t)offs[i];
        sh.offset_y = (ipgui_coord_t)offs[i];
        sh.quality = IPGUI_TEXT_SHADOW_QUALITY_HIGH;
        sh.blend_mode = IPGUI_BLEND_NORMAL;

        ipgui_draw_builtin_text_shadowed(&g_surf, (ipgui_aabb_t *)0, &st, &sh,
                                         (const s8_t *)"Shadow",
                                         cx[i] - tw / 2, 200);

        cap_center(14, cx[i], 310, tags[i]);
    }

    save_bmp("docs/images/11c-text-shadow.bmp");
}

/* ------------------------------------------------------------------ */
static void demo_gradient(void)
{
    ipgui_gradient_color_stop_t s[3];
    int i;

    canvas("渐变 · ipgui_paint_t", "线性填充 / 径向填充 / 锥形填充，色标位置取 0 ～ 255");

    /* linear */
    {
        ipgui_liner_gradient_color_t g;
        ipgui_paint_t p;
        ipgui_aabb_t box;
        ipgui_box_style_t bs;
        ipgui_box_bg_style_t bg;

        ipgui_liner_gradient_init_direct(&g, 90, 0, 330, 0);
        s[0].pos = 0;   s[0].color = C(78, 170, 255);
        s[1].pos = 255; s[1].color = C(255, 120, 120);
        ipgui_liner_gradient_add_stop(&g, &s[0]);
        ipgui_liner_gradient_add_stop(&g, &s[1]);

        box.start.x = 90;  box.start.y = 120;
        box.end.x = 330;   box.end.y = 280;
        memset(&bs, 0, sizeof(bs));
        memset(&bg, 0, sizeof(bg));
        p.type = IPGUI_PAINT_GRADIENT;
        p.src.grad_src.grad_type = IPGUI_GRADIENT_TYPE_LINEAR;
        p.src.grad_src.grad.liner_grad = g;
        bg.paint = p; bg.opacity = 255; bg.blend_mode = IPGUI_BLEND_NORMAL;
        ipgui_draw_box_background(&g_surf, 0, &box, &bs, &bg);
    }

    /* radial */
    {
        ipgui_radial_gradient_color_t g;
        ipgui_paint_t p;
        ipgui_aabb_t box;
        ipgui_box_style_t bs;
        ipgui_box_bg_style_t bg;

        ipgui_radial_gradient_init(&g, 500, 200, 90);
        s[0].pos = 0;   s[0].color = C(255, 190, 90);    /* c_amber */
        s[1].pos = 255; s[1].color = C(255, 120, 120);   /* c_red   */
        ipgui_radial_gradient_add_stop(&g, &s[0]);
        ipgui_radial_gradient_add_stop(&g, &s[1]);

        box.start.x = 410; box.start.y = 110;
        box.end.x = 590;   box.end.y = 290;
        memset(&bs, 0, sizeof(bs));
        memset(&bg, 0, sizeof(bg));
        p.type = IPGUI_PAINT_GRADIENT;
        p.src.grad_src.grad_type = IPGUI_GRADIENT_TYPE_RADIAL;
        p.src.grad_src.grad.radial_grad = g;
        bg.paint = p; bg.opacity = 255; bg.blend_mode = IPGUI_BLEND_NORMAL;
        ipgui_draw_box_background(&g_surf, 0, &box, &bs, &bg);
    }

    /* conic */
    {
        ipgui_conic_gradient_color_t g;
        ipgui_paint_t p;
        ipgui_aabb_t box;
        ipgui_box_style_t bs;
        ipgui_box_bg_style_t bg;

        ipgui_conic_gradient_init(&g, 790, 200, 0);
        s[0].pos = 0;   s[0].color = C(78, 170, 255);
        s[1].pos = 85;  s[1].color = C(120, 220, 160);
        s[2].pos = 170; s[2].color = C(255, 190, 90);
        ipgui_conic_gradient_add_stop(&g, &s[0]);
        ipgui_conic_gradient_add_stop(&g, &s[1]);
        ipgui_conic_gradient_add_stop(&g, &s[2]);

        box.start.x = 700; box.start.y = 110;
        box.end.x = 880;   box.end.y = 290;
        memset(&bs, 0, sizeof(bs));
        memset(&bg, 0, sizeof(bg));
        p.type = IPGUI_PAINT_GRADIENT;
        p.src.grad_src.grad_type = IPGUI_GRADIENT_TYPE_CONIC;
        p.src.grad_src.grad.conic_grad = g;
        bg.paint = p; bg.opacity = 255; bg.blend_mode = IPGUI_BLEND_NORMAL;
        ipgui_draw_box_background(&g_surf, 0, &box, &bs, &bg);
    }

    label(210, "线性填充", C(LB_R, LB_G, LB_B));
    label(500, "径向填充", C(LB_R, LB_G, LB_B));
    label(790, "锥形填充",  C(LB_R, LB_G, LB_B));

    (void)i;
    save_bmp("docs/images/12-gradient.bmp");
}

/* ------------------------------------------------------------------ */
static void demo_blend(void)
{
    int ops[3] = {255, 170, 90};
    ipgui_paint_t paint;
    ipgui_aabb_t box;
    int i;

    canvas("混合 · ipgui_blend", "左：同色只改 opacity　中：三层彩色叠加　右：渐变填充源");

    /* 左：深色底 + 三块同色方块，只改 opacity，互不重叠 */
    fill(60, 120, 270, 150, C(30, 70, 120), 255);
    for (i = 0; i < 3; i ++) {
        box.start.x = 70 + i * 90;
        box.start.y = 135;
        box.end.x = box.start.x + 69;
        box.end.y = box.start.y + 119;
        paint.type = IPGUI_PAINT_COLOR;
        paint.src.color = C(255, 120, 120);
        ipgui_blend(&g_surf, 0, &box, &paint, (u8_t)ops[i], 0, 0,
                    IPGUI_BLEND_NORMAL);
    }
    cap_center(14, 105, 300, "opacity 255");
    cap_center(14, 195, 300, "opacity 170");
    cap_center(14, 285, 300, "opacity 90");

    /* 中：三种颜色各 160 交错叠加，看 alpha 累积 */
    {
        ipgui_color_t cols[3] = {
            { 78, 170, 255, 255 },
            { 120, 220, 160, 255 },
            { 255, 120, 120, 255 },
        };

        for (i = 0; i < 3; i ++) {
            box.start.x = 380 + i * 80;
            box.start.y = 120;
            box.end.x = box.start.x + 149;
            box.end.y = box.start.y + 149;
            paint.type = IPGUI_PAINT_COLOR;
            paint.src.color = cols[i];
            ipgui_blend(&g_surf, 0, &box, &paint, 160, 0, 0,
                        IPGUI_BLEND_NORMAL);
        }
        cap_center(14, 535, 300, "三层，各 opacity 160");
    }

    /* 右：渐变 paint through blend */
    {
        ipgui_liner_gradient_color_t g;
        ipgui_gradient_color_stop_t s0, s1;

        ipgui_liner_gradient_init_direct(&g, 700, 0, 940, 0);
        s0.pos = 0;   s0.color = C(78, 170, 255);
        s1.pos = 255; s1.color = C(255, 190, 90);
        ipgui_liner_gradient_add_stop(&g, &s0);
        ipgui_liner_gradient_add_stop(&g, &s1);

        box.start.x = 700; box.start.y = 120;
        box.end.x = 940;   box.end.y = 270;
        paint.type = IPGUI_PAINT_GRADIENT;
        paint.src.grad_src.grad_type = IPGUI_GRADIENT_TYPE_LINEAR;
        paint.src.grad_src.grad.liner_grad = g;
        ipgui_blend(&g_surf, 0, &box, &paint, 255, 0, 0, IPGUI_BLEND_NORMAL);
        cap_center(14, 820, 300, "填充源 = 线性渐变");
    }

    save_bmp("docs/images/13-blend.bmp");
}

/* ------------------------------------------------------------------ */
static void demo_mask(void)
{
    ipgui_liner_mask_gradient_t lin;
    ipgui_radial_mask_gradient_t rad;
    ipgui_mask_gradient_stop_t s0, s1;
    int x, y;

    canvas("蒙版渐变 · ipgui_mask_gradient", "渐变生成 0 ～ 255 的 alpha，逐像素生效");

    /* linear */
    ipgui_liner_mask_gradient_init_direct(&lin, 80, 0, 440, 0);
    s0.pos = 0;   s0.value = 255;
    s1.pos = 255; s1.value = 0;
    ipgui_liner_mask_gradient_add_stop(&lin, &s0);
    ipgui_liner_mask_gradient_add_stop(&lin, &s1);

    for (x = 0; x < 360; x ++) {
        u8_t v = ipgui_liner_mask_gradient_value_at_xy(&lin, 80 + x, 200);
        fill(80 + x, 130, 1, 150, C(78, 170, 255), v);
    }
    label(260, "线性 255 → 0", C(LB_R, LB_G, LB_B));

    /* radial */
    ipgui_radial_mask_gradient_init(&rad, 770, 205, 150);
    s0.pos = 0;   s0.value = 255;
    s1.pos = 255; s1.value = 0;
    ipgui_radial_mask_gradient_add_stop(&rad, &s0);
    ipgui_radial_mask_gradient_add_stop(&rad, &s1);

    for (y = 0; y < 150; y ++) {
        for (x = 0; x < 300; x ++) {
            u8_t v = ipgui_radial_mask_gradient_value_at_xy(&rad,
                                                            620 + x, 130 + y);
            if (v > 4) fill(620 + x, 130 + y, 1, 1, C(120, 220, 160), v);
        }
    }
    label(770, "径向 255 → 0", C(LB_R, LB_G, LB_B));

    save_bmp("docs/images/14-mask.bmp");
}

/* ---------------------------------------------------------------------------
 * 填充源演示：同一图元分别用 线性 / 径向 / 锥形 / 图像 四种 paint
 * ------------------------------------------------------------------------- */
static ipgui_aabb_t g_fill_aabb;

static void build_fill_image(void)
{
    FILE * f = fopen("tools/lena220_rgba.bin", "rb");
    int x, y;

    /* 优先用 Lena 测试图（220x220 RGBA 原始数据） */
    if (f) {
        if (fread(g_fill_img, 1, sizeof(g_fill_img), f) == sizeof(g_fill_img)) {
            fclose(f);
            return;
        }
        fclose(f);
    }

    /* 找不到文件时的兜底图案 */
    for (y = 0; y < 220; y ++) {
        for (x = 0; x < 220; x ++) {
            u8_t * p = g_fill_img + (y * 220 + x) * 4;
            p[0] = (u8_t)(40 + (x / 4) * 3);
            p[1] = (u8_t)(200 - (y / 4) * 2);
            p[2] = (u8_t)(90 + ((x / 16 + y / 16) & 1) * 90);
            p[3] = 255;
        }
    }
}

static void paint_linear(ipgui_paint_t * p, int cx, int cy, int half)
{
    ipgui_liner_gradient_color_t g;
    ipgui_gradient_color_stop_t s0, s1;

    ipgui_liner_gradient_init_direct(&g, cx - half, cy - half, cx + half, cy + half);
    IPGUI_COLOR_SET(s0.color, 255, 0x4EAAFF);  s0.pos = 0;
    IPGUI_COLOR_SET(s1.color, 255, 0xFF7878);  s1.pos = 255;
    ipgui_liner_gradient_add_stop(&g, &s0);
    ipgui_liner_gradient_add_stop(&g, &s1);

    p->type = IPGUI_PAINT_GRADIENT;
    p->src.grad_src.grad_type = IPGUI_GRADIENT_TYPE_LINEAR;
    p->src.grad_src.grad.liner_grad = g;
}

static void paint_radial(ipgui_paint_t * p, int cx, int cy, int r)
{
    ipgui_radial_gradient_color_t g;
    ipgui_gradient_color_stop_t s0, s1;

    ipgui_radial_gradient_init(&g, cx, cy, r);
    IPGUI_COLOR_SET(s0.color, 255, 0xFFBE5A);  s0.pos = 0;
    IPGUI_COLOR_SET(s1.color, 255, 0xFF7878);  s1.pos = 255;
    ipgui_radial_gradient_add_stop(&g, &s0);
    ipgui_radial_gradient_add_stop(&g, &s1);

    p->type = IPGUI_PAINT_GRADIENT;
    p->src.grad_src.grad_type = IPGUI_GRADIENT_TYPE_RADIAL;
    p->src.grad_src.grad.radial_grad = g;
}

static void paint_conic(ipgui_paint_t * p, int cx, int cy)
{
    ipgui_conic_gradient_color_t g;
    ipgui_gradient_color_stop_t s0, s1, s2;

    ipgui_conic_gradient_init(&g, cx, cy, 0);
    IPGUI_COLOR_SET(s0.color, 255, 0x4EAAFF);  s0.pos = 0;
    IPGUI_COLOR_SET(s1.color, 255, 0x78DC9F);  s1.pos = 85;
    IPGUI_COLOR_SET(s2.color, 255, 0xFFBE5A);  s2.pos = 170;
    ipgui_conic_gradient_add_stop(&g, &s0);
    ipgui_conic_gradient_add_stop(&g, &s1);
    ipgui_conic_gradient_add_stop(&g, &s2);

    p->type = IPGUI_PAINT_GRADIENT;
    p->src.grad_src.grad_type = IPGUI_GRADIENT_TYPE_CONIC;
    p->src.grad_src.grad.conic_grad = g;
}

static void paint_image(ipgui_paint_t * p, int x0, int y0, int x1, int y1)
{
    g_fill_aabb.start.x = x0;
    g_fill_aabb.start.y = y0;
    g_fill_aabb.end.x   = x1;
    g_fill_aabb.end.y   = y1;

    p->type = IPGUI_PAINT_IMAGE;
    p->src.image_src.img_aabb  = &g_fill_aabb;
    p->src.image_src.stride    = 220 * 4;
    p->src.image_src.buf       = g_fill_img;
    p->src.image_src.img_pxfmt = IPGUI_IMG_FMT_RGBA8888;
    p->src.image_src.px_size   = 4;
}

typedef void (*shape_fn)(int cx, int idx, ipgui_paint_t * paint);

static void shape_line(int cx, int idx, ipgui_paint_t * paint)
{
    ipgui_line_t ln;
    ipgui_line_style_t st;

    ln.start.x = cx - 70;  ln.start.y = 300;
    ln.end.x   = cx + 70;  ln.end.y   = 120;

    memset(&st, 0, sizeof(st));
    st.width = 18;
    st.cap = IPGUI_LINE_CAP_BUTT;
    st.paint = *paint;
    st.opacity = 255;
    st.blend_mode = IPGUI_BLEND_NORMAL;
    ipgui_draw_line_generic(&g_surf, 0, &ln, &st);
}

static void shape_tri(int cx, int idx, ipgui_paint_t * paint)
{
    ipgui_point_t p1, p2, p3;
    ipgui_triangle_style_t st;

    p1.x = cx;       p1.y = 120;
    p2.x = cx + 85;  p2.y = 300;
    p3.x = cx - 85;  p3.y = 300;

    memset(&st, 0, sizeof(st));
    st.paint = *paint;
    st.opacity = 255;
    st.blend_mode = IPGUI_BLEND_NORMAL;
    ipgui_draw_triangle(&g_surf, 0, &p1, &p2, &p3, &st);
}

static void shape_circle(int cx, int idx, ipgui_paint_t * paint)
{
    ipgui_filled_circle_style_t st;

    memset(&st, 0, sizeof(st));
    st.paint = *paint;
    st.opacity = 255;
    st.blend_mode = IPGUI_BLEND_NORMAL;
    ipgui_draw_filled_circle(&g_surf, 0, cx, 210, 90, &st);
}

static void shape_arc(int cx, int idx, ipgui_paint_t * paint)
{
    ipgui_arc_t a;
    ipgui_arc_style_t st;

    a.cx = cx; a.cy = 210;
    a.er = 100; a.ir = 46;
    a.start = 23;
    a.dir = IPGUI_ARC_DRAW_DIR_CCW;
    a.angle = 215;

    memset(&st, 0, sizeof(st));
    st.paint = *paint;
    st.opacity = 255;
    st.sep_type = IPGUI_ARC_ENDPOINT_TYPE_ROUND;
    st.eep_type = IPGUI_ARC_ENDPOINT_TYPE_ROUND;
    st.blend_mode = IPGUI_BLEND_NORMAL;
    ipgui_draw_arc(&g_surf, 0, &a, &st);
}

static void shape_box(int cx, int idx, ipgui_paint_t * paint)
{
    ipgui_aabb_t box;
    ipgui_box_style_t bs;
    ipgui_box_bg_style_t bg;

    box.start.x = cx - 75;  box.start.y = 120;
    box.end.x   = cx + 75;  box.end.y   = 300;

    memset(&bs, 0, sizeof(bs));
    if (idx == 3) {                        /* 图像填充这张：四个角各不相同 */
        bs.left_top_radius     = 46;
        bs.right_top_radius    = 30;
        bs.right_bottom_radius = 15;
        bs.left_bottom_radius  = 0;
    } else {
        bs.left_top_radius = 20;  bs.right_top_radius = 20;
        bs.left_bottom_radius = 20;  bs.right_bottom_radius = 20;
    }

    memset(&bg, 0, sizeof(bg));
    bg.paint = *paint;
    bg.opacity = 255;
    bg.blend_mode = IPGUI_BLEND_NORMAL;
    ipgui_draw_box_background(&g_surf, 0, &box, &bs, &bg);
}

static void shape_border(int cx, int idx, ipgui_paint_t * paint)
{
    ipgui_aabb_t box;
    ipgui_box_style_t bs;
    ipgui_box_border_style_t st;

    box.start.x = cx - 75;  box.start.y = 120;
    box.end.x   = cx + 75;  box.end.y   = 300;

    memset(&bs, 0, sizeof(bs));
    if (idx == 3) {                        /* 图像填充这张：四个角各不相同 */
        bs.left_top_radius     = 46;
        bs.right_top_radius    = 30;
        bs.right_bottom_radius = 15;
        bs.left_bottom_radius  = 0;
    } else {
        bs.left_top_radius = 20;  bs.right_top_radius = 20;
        bs.left_bottom_radius = 20;  bs.right_bottom_radius = 20;
    }

    memset(&st, 0, sizeof(st));
    st.paint = *paint;
    st.width = 14;
    st.opacity = 255;
    st.blend_mode = IPGUI_BLEND_NORMAL;
    ipgui_draw_box_border(&g_surf, 0, &box, &bs, &st);
}

static void shape_text(int cx, int idx, ipgui_paint_t * paint)
{
    ipgui_font_style_t st;

    memset(&st, 0, sizeof(st));
    st.font = &quicksand_medium_36px;
    st.paint = *paint;
    st.opacity = 255;
    st.blend_mode = IPGUI_BLEND_NORMAL;
    ipgui_draw_builtin_text(&g_surf, 0, &st, (const s8_t *)"FILL",
                            cx - 58, 190);
}

static void shape_shadow(int cx, int idx, ipgui_paint_t * paint)
{
    ipgui_aabb_t                    box;
    ipgui_box_style_t               bs;
    ipgui_box_outer_shadow_style_t  sh;
    ipgui_box_bg_style_t            bg;

    box.start.x = cx - 60;  box.start.y = 120;
    box.end.x   = cx + 60;  box.end.y   = 240;

    memset(&bs, 0, sizeof(bs));
    if (idx == 3) {                        /* 图像填充这张：四个角各不相同 */
        bs.left_top_radius     = 40;
        bs.right_top_radius    = 28;
        bs.right_bottom_radius = 14;
        bs.left_bottom_radius  = 0;
    } else {
        bs.left_top_radius = 16;  bs.right_top_radius = 16;
        bs.left_bottom_radius = 16;  bs.right_bottom_radius = 16;
    }

    memset(&sh, 0, sizeof(sh));
    sh.paint    = *paint;
    sh.opacity  = 230;
    sh.blur     = 24;
    sh.spread   = 0;
    sh.offset_x = 0;
    sh.offset_y = 14;
    ipgui_draw_box_outer_shadow(&g_surf, 0, &box, &bs, &sh);

    /* 本体画在上面 */
    memset(&bg, 0, sizeof(bg));
    bg.paint.type = IPGUI_PAINT_COLOR;
    IPGUI_COLOR_SET(bg.paint.src.color, 255, 0x26303E);
    bg.opacity = 255;
    bg.blend_mode = IPGUI_BLEND_NORMAL;
    ipgui_draw_box_background(&g_surf, 0, &box, &bs, &bg);
}

static void demo_fill_source(const char * file, const char * title,
                             const char * sub, shape_fn fn)
{
    int cx[4] = { 160, 400, 640, 880 };
    int i;

    canvas(title, sub);

    for (i = 0; i < 4; i ++) {
        ipgui_paint_t paint;

        memset(&paint, 0, sizeof(paint));
        if (i == 0)      paint_linear(&paint, cx[i], 210, 90);
        else if (i == 1) paint_radial(&paint, cx[i], 210, 95);
        else if (i == 2) paint_conic(&paint, cx[i], 210);
        else             paint_image(&paint, cx[i] - 100, 110, cx[i] + 100, 310);

        fn(cx[i], i, &paint);
    }

    label(160, "线性填充", C(LB_R, LB_G, LB_B));
    label(400, "径向填充", C(LB_R, LB_G, LB_B));
    label(640, "锥形填充", C(LB_R, LB_G, LB_B));
    label(880, "图像填充", C(LB_R, LB_G, LB_B));

    save_bmp(file);
}

int main(void)
{
    if (ipgui_init() != IPGUI_ERR_OK) {
        printf("ipgui_init failed\n");
        return 1;
    }

    g_cap = fopen("tools/captions.txt", "wb");
    if (!g_cap) printf("captions.txt open fail\n");

    build_fill_image();

    demo_pixel();
    demo_line();
    demo_triangle();
    demo_circle();
    demo_arc();
    demo_polygon();
    demo_box_bg();
    demo_box_border();
    demo_shadow();
    demo_image();
    demo_image_api();
    demo_image_tiled();
    demo_icon();
    demo_icon_api();
    demo_text();
    demo_text_shadow();
    demo_gradient();
    demo_blend();
    demo_mask();

    demo_fill_source("docs/images/02b-line-fill.bmp", "直线 · ipgui_draw_line_generic", "同一个图元，填充源依次为 线性填充 / 径向填充 / 锥形填充 / 图像填充", shape_line);
    demo_fill_source("docs/images/03b-triangle-fill.bmp", "三角形 · ipgui_draw_triangle", "同一个图元，填充源依次为 线性填充 / 径向填充 / 锥形填充 / 图像填充", shape_tri);
    demo_fill_source("docs/images/04b-circle-fill.bmp", "实心圆 · ipgui_draw_filled_circle", "同一个图元，填充源依次为 线性填充 / 径向填充 / 锥形填充 / 图像填充", shape_circle);
    demo_fill_source("docs/images/05b-arc-fill.bmp", "圆弧 · ipgui_draw_arc", "起始角度 23°，逆时针画 215°；填充源依次为 线性填充 / 径向填充 / 锥形填充 / 图像填充", shape_arc);
    demo_fill_source("docs/images/07b-box-bg-fill.bmp", "圆角背景 · ipgui_draw_box_background", "同一个图元，填充源依次为 线性填充 / 径向填充 / 锥形填充 / 图像填充", shape_box);
    demo_fill_source("docs/images/08b-box-border-fill.bmp", "圆角边框 · ipgui_draw_box_border", "同一个图元，填充源依次为 线性填充 / 径向填充 / 锥形填充 / 图像填充", shape_border);
    demo_fill_source("docs/images/09b-shadow-fill.bmp", "外阴影 · ipgui_draw_box_outer_shadow", "同一个图元，填充源依次为 线性填充 / 径向填充 / 锥形填充 / 图像填充", shape_shadow);
    demo_fill_source("docs/images/11b-text-fill.bmp", "文字 · ipgui_draw_builtin_text", "同一个图元，填充源依次为 线性填充 / 径向填充 / 锥形填充 / 图像填充", shape_text);

    if (g_cap) fclose(g_cap);
    return 0;
}
