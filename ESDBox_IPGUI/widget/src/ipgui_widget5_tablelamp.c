#include "ipgui_draw_image_api.h"
#include "ipgui_widget.h"
#include "ipgui_blend.h"
#include "ipgui_pattle.h"
#include "ipgui_draw_box_background.h"
#include "ipgui_draw_box_border.h"
#include "ipgui_draw_builtin_font.h"
#include "quicksand_medium.h"
#include "ipgui_draw_box_shadow.h"
#include "ipgui_time.h"
extern ipgui_image_data_t tablelamp_img;
extern ipgui_image_data_t tablelamp_on_img;

static int tablelamp_is_on = 0;
static int tablelamp_hovered = 0;

void tablelamp_render(struct ipgui_widget * widget, ipgui_widget_render_ctx_t * ctx)
{
    u8_t bg_alpha = tablelamp_hovered ? 80 : 0;
    ipgui_aabb_t box;
    box.start.x = 5;
    box.start.y = 5;
    box.end.x = 95;
    box.end.y = 85;

    ipgui_box_style_t box_style;
    box_style.bottom_padding = 0;
    box_style.left_padding = 0;
    box_style.top_padding = 0;
    box_style.right_padding = 0;
    box_style.left_top_radius = 10;
    box_style.right_top_radius = 10;
    box_style.left_bottom_radius = 10;
    box_style.right_bottom_radius = 10;

    ipgui_box_outer_shadow_style_t shadow_style;
    shadow_style.blur = 5;
    shadow_style.offset_x = 0;
    shadow_style.offset_y = 0;
    shadow_style.spread = 0;
    shadow_style.opacity = 100;
    shadow_style.paint.type = IPGUI_PAINT_COLOR;
    IPGUI_COLOR_SET(shadow_style.paint.src.color, 255, IPGUI_COLOR_526);

    // ipgui_draw_box_outer_shadow(
    //     ctx->surf,
    //     (ipgui_aabb_t *)0,
    //     &box,
    //     &box_style,
    //     &shadow_style);

    ipgui_box_bg_style_t box_bg_style;
    box_bg_style.blend_mode = IPGUI_BLEND_NORMAL;
    box_bg_style.paint.type = IPGUI_PAINT_COLOR;
    if (tablelamp_is_on) {
        box_bg_style.opacity = 150 - bg_alpha;
        IPGUI_COLOR_SET(box_bg_style.paint.src.color, 255, IPGUI_COLOR_BLACK);
    } else {
        box_bg_style.opacity = 200 - bg_alpha;
        IPGUI_COLOR_SET(box_bg_style.paint.src.color, 255, IPGUI_COLOR_244);
    }

    ipgui_draw_box_background(
        ctx->surf,
        (ipgui_aabb_t *)0,
        &box,
        &box_style,
        &box_bg_style);

    ipgui_box_border_style_t box_border_style;
    box_border_style.blend_mode = IPGUI_BLEND_NORMAL;
    box_border_style.opacity = 200;

    if (tablelamp_is_on) {
        box_border_style.width = 1;
        box_border_style.paint.type = IPGUI_PAINT_COLOR;
        IPGUI_COLOR_SET(box_border_style.paint.src.color, 255, IPGUI_COLOR_471);
    } else {
        box_border_style.width = 4;
        box_border_style.paint.type = IPGUI_PAINT_GRADIENT;
        box_border_style.paint.src.grad_src.grad_type = IPGUI_GRADIENT_TYPE_CONIC;
        ipgui_conic_gradient_init(
            &box_border_style.paint.src.grad_src.grad.conic_grad,
            50, 50, (s32_t)(ipgui_sys_tick % 480) * 360 / 480);  /* 锥形渐变旋转 */

        ipgui_gradient_color_stop_t stop0;
        stop0.pos = 0;
        IPGUI_COLOR_SET(stop0.color, 80, 0xe74c3c);
        ipgui_conic_gradient_add_stop(&box_border_style.paint.src.grad_src.grad.conic_grad, &stop0);

        ipgui_gradient_color_stop_t stop1;
        stop1.pos = 64;
        IPGUI_COLOR_SET(stop1.color, 255, 0xf1c40f);
        ipgui_conic_gradient_add_stop(&box_border_style.paint.src.grad_src.grad.conic_grad, &stop1);

        ipgui_gradient_color_stop_t stop2;
        stop2.pos = 128;
        IPGUI_COLOR_SET(stop2.color, 255, 0x2ecc71);
        ipgui_conic_gradient_add_stop(&box_border_style.paint.src.grad_src.grad.conic_grad, &stop2);

        ipgui_gradient_color_stop_t stop3;
        stop3.pos = 192;
        IPGUI_COLOR_SET(stop3.color, 255, 0x3498db);
        ipgui_conic_gradient_add_stop(&box_border_style.paint.src.grad_src.grad.conic_grad, &stop3);

        ipgui_gradient_color_stop_t stop255;
        stop255.pos = 255;
        IPGUI_COLOR_SET(stop255.color, 80, 0xe74c3c);
        ipgui_conic_gradient_add_stop(&box_border_style.paint.src.grad_src.grad.conic_grad, &stop255);
    }

    ipgui_draw_box_border(
        ctx->surf,
        (ipgui_aabb_t *)0,
        &box,
        &box_style,
        &box_border_style);

    ipgui_image_draw_style_t img_style;
    img_style.blend_mode = IPGUI_BLEND_NORMAL;
    img_style.opacity = 200;

    ipgui_draw_image_at(
        ctx->surf,
        tablelamp_is_on ? &tablelamp_img : &tablelamp_on_img,
        32,
        15,
        &img_style,
        IPGUI_IMAGE_QUALITY_HIGH);

    ipgui_font_style_t font_style;
    font_style.blend_mode = IPGUI_BLEND_NORMAL;
    font_style.opacity = 200;
    font_style.line_spacing = 0;
    font_style.font = &quicksand_medium_15px;
    font_style.paint.type = IPGUI_PAINT_COLOR;
    if (tablelamp_is_on) {
        IPGUI_COLOR_SET(font_style.paint.src.color, 255, IPGUI_COLOR_WHITE);
    } else {
        IPGUI_COLOR_SET(font_style.paint.src.color, 255, IPGUI_COLOR_BLACK);
    }

    ipgui_draw_builtin_text(
        ctx->surf,
        (ipgui_aabb_t *)0,
        &font_style,
        tablelamp_is_on ? "Light On" : "Light Off",
        20,
        60);
}

void tablelamp_event_handler(struct ipgui_widget * widget, ipgui_widget_evt_t * evt)
{
    if (evt->type == IPGUI_WIDGET_EVENT_RELEASED) {
        tablelamp_is_on = !tablelamp_is_on;
        ipgui_widget_mark_dirty(widget);
    } else if (evt->type == IPGUI_WIDGET_EVENT_HOVER) {
        tablelamp_hovered = 1;
        ipgui_widget_mark_dirty(widget);
    }
}

/* OFF 状态下需要持续重绘（锥形渐变动画） */
static int tablelamp_needs_anim(void)
{
    return !tablelamp_is_on;
}

/* 每帧开始前复位，返回旧值 */
static int tablelamp_hover_reset(void)
{
    int prev = tablelamp_hovered;
    tablelamp_hovered = 0;
    return prev;
}

void widget_tablelamp_update(ipgui_widget_t * w)
{
    if (tablelamp_hover_reset())
        ipgui_widget_mark_dirty(w);
    if (tablelamp_needs_anim())
        ipgui_widget_mark_dirty(w);
}

/* 客厅标签悬停状态 */
static int livingroom_hovered = 0;

void _label_livingroom_render(struct ipgui_widget * widget, ipgui_widget_render_ctx_t * ctx)
{
    const ipgui_font_t * font = livingroom_hovered ? &quicksand_medium_28px : &quicksand_medium_25px;

    ipgui_font_style_t font_style;
    font_style.blend_mode = IPGUI_BLEND_NORMAL;
    font_style.opacity = 170;
    font_style.line_spacing = 0;
    font_style.font = font;
    font_style.paint.type = IPGUI_PAINT_COLOR;
    IPGUI_COLOR_SET(font_style.paint.src.color, 255, IPGUI_COLOR_WHITE);

    /* 从中心放大：根据当前字体宽度居中 */
    ipgui_coord_t tw = ipgui_builtin_text_width(font, (const s8_t *)"Living Room");
    ipgui_coord_t x = (widget->w - tw) / 2 - 20;

    ipgui_draw_builtin_text(
        ctx->surf,
        (ipgui_aabb_t *)0,
        &font_style,
        "Living Room",
        x,
        0);
}

void livingroom_event_handler(struct ipgui_widget * widget, ipgui_widget_evt_t * evt)
{
    if (evt->type == IPGUI_WIDGET_EVENT_HOVER) {
        livingroom_hovered = 1;
        ipgui_widget_mark_dirty(widget);
    } else if (evt->type == IPGUI_WIDGET_EVENT_PRESSED || evt->type == IPGUI_WIDGET_EVENT_RELEASED) {
        livingroom_hovered = 0;
        ipgui_widget_mark_dirty(widget);
    }
}

/* 每帧开始前复位，返回旧值 */
static int livingroom_hover_reset(void)
{
    int prev = livingroom_hovered;
    livingroom_hovered = 0;
    return prev;
}

void widget_livingroom_update(ipgui_widget_t * w)
{
    if (livingroom_hover_reset())
        ipgui_widget_mark_dirty(w);
}