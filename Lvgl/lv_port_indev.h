
/**
 * @file lv_port_indev_templ.h
 *
 */

/*Copy this file as "lv_port_indev.h" and set this value to "1" to enable content*/
#if 1

#ifndef LV_PORT_INDEV_TEMPL_H
#define LV_PORT_INDEV_TEMPL_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/
void lv_port_indev_init(void);

/* 获取最后一次触摸的原始坐标（用于在屏幕上显示和主界面 hit test） */
void lv_port_indev_get_raw(int32_t * raw_x, int32_t * raw_y, bool * pressed);

/* 获取 LVGL 转换后的坐标（用于子页面触摸检测，与 LVGL 坐标系一致） */
void lv_port_indev_get_lvgl(int32_t * x, int32_t * y, bool * pressed);

/* 命中检测：根据 raw 坐标判断按了哪个按钮（返回0~8，-1=没按到） */
int lv_port_indev_hit_test(int32_t raw_x, int32_t raw_y);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_PORT_INDEV_TEMPL_H*/

#endif /*Disable/Enable content*/
