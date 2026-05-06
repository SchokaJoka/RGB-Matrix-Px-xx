#pragma once
#include "esp_err.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lvgl_port_cfg_t lvgl_port_cfg;
    size_t buffer_size;
    bool double_buffer;
    struct {
        unsigned int buff_dma: 1;
        unsigned int buff_spiram: 1;
    } flags;
} display_cfg;

typedef enum {
    DISPLAY_MAP_EXTEND = 0,
    DISPLAY_MAP_MIRROR = 1,
} display_map_mode_t;

esp_err_t init_display(void);

lv_display_t *display_start(void);
esp_err_t display_stop(void); 
bool display_lock(uint32_t timeout_ms);
void display_unlock(void);

/*
 * @brief start display driver with config
 * @param cfg display config
 * @return lv_display_t* display driver instance
 */
lv_display_t *display_start_with_config(const display_cfg *cfg); 
/*
 * @brief set display brightness
 * @param brightness_percent brightness percent
 * @return esp_err_t ESP_OK if success
 */
esp_err_t display_brightness_set(int brightness_percent);
/*
 * @brief set display map mode
 * @param mode display map mode
 * @return uint16_t display map mode
 */
uint16_t display_set_map_mode(display_map_mode_t mode);
/*
 * @brief rotate display
 * @param disp display instance
 * @param rotation rotation
 * @return void
 */
void display_rotate(lv_display_t *disp, lv_disp_rotation_t rotation);
/**
 * @brief Set label text with gradient color.
 * @param label LVGL label object.
 * @param text Text to set.
 * @param start_color Start color of the gradient.
 * @param end_color End color of the gradient.
 * @return void
 */
void set_label_gradient_text(lv_obj_t *label, const char *text,
                             uint32_t start_color, uint32_t end_color);
#ifdef __cplusplus
}
#endif