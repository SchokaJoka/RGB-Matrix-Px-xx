/*
 * SPDX-FileCopyrightText: 2022-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "display.h"
#include "hub75_bridge.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sdkconfig.h>

static const char *TAG = "esp32_s3_matrix";
/* Peripheral boundary: Display + LVGL port + HUB75 bridge
 * - Responsibilities:
 *     Manage LVGL display lifecycle and frame buffers.
 *     Bridge LVGL flush pipeline to HUB75 panel driver.
 *     Provide thread-safe display lock/unlock helpers.
 *     Expose brightness/rotation/start/stop public BSP display APIs.
 * - Public APIs:
 *     display_brightness_set(): set panel brightness (0~100% -> 0~255).
 *     display_lock()/display_unlock(): guard LVGL operations in multi-task context.
 *     display_rotate(): rotate target display with lock protection.
 *     display_start()/display_start_with_config(): start display pipeline.
 *     init_display(): compatibility wrapper returning esp_err_t.
 *     display_stop(): stop and release display resources. */
static display_map_mode_t display_map_mode =
#if CONFIG_HUB75_LAYOUT_COLS > 1
    DISPLAY_MAP_EXTEND;
#else
    DISPLAY_MAP_MIRROR;
#endif

static uint8_t *lvgl_buf1 = NULL;
static uint8_t *lvgl_buf2 = NULL;
static lv_display_t *lvgl_disp = NULL;
static bool lvgl_port_inited = false;
static bool use_double_buffer = false;

static void display_free_buffers(void)
{
    if (lvgl_buf1) {
        heap_caps_free(lvgl_buf1);
        lvgl_buf1 = NULL;
    }
    if (lvgl_buf2) {
        heap_caps_free(lvgl_buf2);
        lvgl_buf2 = NULL;
    }
}

static inline int display_logical_width(void)
{
    const bool mirror = (display_map_mode == DISPLAY_MAP_MIRROR);
    if (mirror) return CONFIG_HUB75_PANEL_WIDTH;
    return CONFIG_HUB75_PANEL_WIDTH * CONFIG_HUB75_LAYOUT_COLS;
}

static inline int display_logical_height(void)
{
    return CONFIG_HUB75_PANEL_HEIGHT * CONFIG_HUB75_LAYOUT_ROWS;
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    if (!disp || !area || !px_map) {
        return;
    }

    const uint16_t w = (uint16_t)((area->x2 - area->x1) + 1);
    const uint16_t h = (uint16_t)((area->y2 - area->y1) + 1);
    const uint16_t panel_w = (uint16_t)CONFIG_HUB75_PANEL_WIDTH;
    const uint16_t panel_count = (uint16_t)CONFIG_HUB75_LAYOUT_COLS;
    const bool mirror = (display_map_mode == DISPLAY_MAP_MIRROR);
    if (!mirror || panel_count <= 1) {
        hub75_bridge_draw((uint16_t)area->x1, (uint16_t)area->y1, w, h, px_map, false);
    }
    uint16_t col = 0;
    while (mirror && col < panel_count) {
        const uint16_t x = (uint16_t)((uint16_t)area->x1 + (uint16_t)(col * panel_w));
        hub75_bridge_draw(x, (uint16_t)area->y1, w, h, px_map, false);
        ++col;
    }
#if defined(CONFIG_HUB75_DOUBLE_BUFFER)
    if (use_double_buffer) {
        hub75_bridge_flip();
    }
#endif
    lv_display_flush_ready(disp);
}

esp_err_t display_brightness_set(int brightness_percent)
{
    if (brightness_percent > 100) brightness_percent = 100;
    if (brightness_percent < 0) brightness_percent = 0;

    const uint32_t b = (uint32_t)((brightness_percent * 255 + 50) / 100);
    hub75_bridge_set_brightness((uint8_t)b);
    return ESP_OK;
}

uint16_t display_set_map_mode(display_map_mode_t mode)
{
    const bool mode_ok = (mode == DISPLAY_MAP_EXTEND || mode == DISPLAY_MAP_MIRROR);
    if (!mode_ok) return ESP_ERR_INVALID_ARG;
    if (lvgl_disp) return ESP_ERR_INVALID_STATE;

    display_map_mode = mode;
    return display_map_mode;
}

bool display_lock(uint32_t timeout_ms)
{
    if (!lvgl_port_inited) return false;
    return lvgl_port_lock(timeout_ms);
}

void display_unlock(void)
{
    if (!lvgl_port_inited) return;
    lvgl_port_unlock();
}

void display_rotate(lv_display_t *disp, lv_disp_rotation_t rotation)
{
    lv_display_t *target = disp ? disp : lvgl_disp;
    if (!target) return;
    bool locked = display_lock(1000);
    lv_disp_set_rotation(target, rotation);
    if (locked) {
        display_unlock();
    }
}

void set_label_gradient_text(lv_obj_t *label, const char *text, uint32_t color_start, uint32_t color_end)
{
    size_t text_len = strlen(text);
    if (text_len == 0) {
        lv_label_set_text(label, "");
        return;
    }

    static char gradient_buf[256];
    char *buf_ptr = gradient_buf;
    char *buf_end = gradient_buf + sizeof(gradient_buf) - 1;

    uint8_t r_start = (color_start >> 16) & 0xFF;
    uint8_t g_start = (color_start >> 8)  & 0xFF;
    uint8_t b_start =  color_start        & 0xFF;
    uint8_t r_end = (color_end >> 16)     & 0xFF;
    uint8_t g_end = (color_end >> 8)      & 0xFF;
    uint8_t b_end =  color_end            & 0xFF;

    for (size_t idx = 0; idx < text_len && buf_ptr < buf_end; idx++) {
        float ratio = (text_len > 1) ? (float)idx / (float)(text_len - 1) : 0.0f;

        uint8_t r = (uint8_t)(r_start + (r_end - r_start) * ratio);
        uint8_t g = (uint8_t)(g_start + (g_end - g_start) * ratio);
        uint8_t b = (uint8_t)(b_start + (b_end - b_start) * ratio);

        int written = snprintf(buf_ptr, buf_end - buf_ptr, "#%02X%02X%02X %c#", r, g, b, text[idx]);
        if (written <= 0) break;
        buf_ptr += written;
    }

    *buf_ptr = '\0';
    lv_label_set_text(label, gradient_buf);
}

lv_display_t *display_start(void)
{
    const int disp_w = display_logical_width();
    const int disp_h = display_logical_height();
    const display_cfg cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = (size_t)disp_w * (size_t)disp_h,
        .double_buffer = true,
        .flags = {
            .buff_dma = true,
            .buff_spiram = true,
        },
    };
    return display_start_with_config(&cfg);
}

lv_display_t *display_start_with_config(const display_cfg *cfg)
{
    if (!cfg) return NULL;

    const int disp_w = display_logical_width();
    const int disp_h = display_logical_height();

    if (!lvgl_port_inited) {
        esp_err_t r = lvgl_port_init(&cfg->lvgl_port_cfg);
        if (r != ESP_OK) return NULL;
        lvgl_port_inited = true;
    }

    if (lvgl_disp) return lvgl_disp;

    const size_t buffer_pixels = cfg->buffer_size ? cfg->buffer_size : (size_t)disp_w * (size_t)disp_h;
    const size_t buf_bytes = buffer_pixels * 2;

    lvgl_buf1 = (uint8_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!lvgl_buf1) {
        lvgl_buf1 = (uint8_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!lvgl_buf1) {
        return NULL;
    }

    if (cfg->double_buffer) {
        lvgl_buf2 = (uint8_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!lvgl_buf2) {
            lvgl_buf2 = (uint8_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        }
        if (!lvgl_buf2) {
            display_free_buffers();
            return NULL;
        }
    }

    if (!hub75_bridge_init()) {
        display_free_buffers();
        return NULL;
    }
    hub75_bridge_set_brightness(CONFIG_HUB75_BRIGHTNESS);

    bool locked = display_lock(1000);
    lvgl_disp = lv_display_create(disp_w, disp_h);
    if (!lvgl_disp) {
        if (locked) display_unlock();
        hub75_bridge_deinit();
        display_free_buffers();
        return NULL;
    }
    use_double_buffer = cfg->double_buffer;
    lv_display_set_color_format(lvgl_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(lvgl_disp, flush_cb);
    lv_display_set_buffers(lvgl_disp,
                           lvgl_buf1,
                           use_double_buffer ? lvgl_buf2 : NULL,
                           buf_bytes,
                           use_double_buffer ? LV_DISPLAY_RENDER_MODE_FULL : LV_DISPLAY_RENDER_MODE_PARTIAL);
    if (locked) display_unlock();

    return lvgl_disp;
}

esp_err_t init_display(void)
{
    return display_start() ? ESP_OK : ESP_FAIL;
}

esp_err_t display_stop(void)
{
    bool locked = display_lock(1000);
    if (lvgl_disp) {
        lv_display_delete(lvgl_disp);
        lvgl_disp = NULL;
    }
    if (locked) {
        display_unlock();
    }
    use_double_buffer = false;
    display_free_buffers();
    hub75_bridge_deinit();
    return ESP_OK;
}

