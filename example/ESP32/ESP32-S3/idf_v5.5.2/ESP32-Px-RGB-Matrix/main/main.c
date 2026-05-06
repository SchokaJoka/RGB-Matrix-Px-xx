#include "display.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "font/font_5x7.h"
#include <stdbool.h>

static const char *TAG = "RGBW";

#define FONT_LINE_COUNT 4
#define STEP_DELAY_MS 2000


typedef struct {
  const char *name;
  uint32_t hex_color;
} rgbw_color_t;

typedef struct {
  const char *text;
  uint32_t start_color;
  uint32_t end_color;
} gradient_line_t;

static const rgbw_color_t rgbw_colors[] = {{"Red", 0xFF0000},
                                           {"Green", 0x00FF00},
                                           {"Blue", 0x0000FF},
                                           {"White", 0xFFFFFF}};

static const gradient_line_t welcome_lines[FONT_LINE_COUNT] = {
    {"Welcome", 0xFF0000, 0xFFFF00},
    {"Waveshare", 0x00FF00, 0x00FFFF},
    {"RGB MATRIX", 0x00A0FF, 0xFF00A0},
    {"P4 64x32", 0xFF00FF, 0xFFFFFF},
};

static void rgbw_set_color(uint32_t hex_color) {
  lv_obj_t *screen = lv_scr_act();
  lv_obj_set_style_bg_color(screen, lv_color_hex(hex_color), 0);
}

static lv_obj_t *font_ui_lines[FONT_LINE_COUNT];

static void get_screen_size(lv_obj_t *screen, lv_coord_t *width, lv_coord_t *height) {
  if (!screen || !width || !height) return;

  *width = lv_obj_get_width(screen);
  *height = lv_obj_get_height(screen);
}

static void clear_screen_black(lv_obj_t *screen) {
  if (!screen) return;
  lv_obj_clean(screen);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
}

static void font_5x7_ui_init(lv_obj_t *scr) {
  if (!scr) return;

  lv_coord_t screen_height = lv_obj_get_height(scr);
  lv_coord_t row_height = screen_height / FONT_LINE_COUNT;

  for (int i = 0; i < FONT_LINE_COUNT; i++) {
    lv_obj_t *label = lv_label_create(scr);
    font_ui_lines[i] = label;
    lv_obj_set_width(label, lv_pct(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(label, &lv_font_5x7, 0);
    lv_label_set_recolor(label, true);
    lv_obj_set_style_text_line_space(label, 0, 0);
    lv_obj_set_style_text_letter_space(label, 1, 0);
    lv_obj_set_style_pad_all(label, 0, 0);
    lv_obj_set_pos(label, 0, row_height * i);
  }
}

static void font_5x7_ui_apply(const gradient_line_t *lines, size_t count) {
  for (size_t i = 0; i < count && i < FONT_LINE_COUNT; i++) {
    if (!font_ui_lines[i]) continue;
    set_label_gradient_text(font_ui_lines[i], lines[i].text ? lines[i].text : "",
                            lines[i].start_color, lines[i].end_color);
  }
}

static void font_5x7_ui_deinit(void) {
  for (int i = 0; i < FONT_LINE_COUNT; i++) {
    if (font_ui_lines[i]) {
      lv_obj_del(font_ui_lines[i]);
      font_ui_lines[i] = NULL;
    }
  }
}

static void show_four_lines_text(void) {
  bool locked = display_lock(1000);
  if (!locked) {
    return;
  }

  lv_obj_t *scr = lv_scr_act();
  if (!scr) {
    display_unlock();
    return;
  }

  clear_screen_black(scr);
  font_5x7_ui_init(scr);
  font_5x7_ui_apply(welcome_lines, FONT_LINE_COUNT);
  display_unlock();

  vTaskDelay(pdMS_TO_TICKS(STEP_DELAY_MS));

  locked = display_lock(1000);
  if (locked) {
    font_5x7_ui_deinit();
    display_unlock();
  }
}


void draw_circle_square(){
    bool locked = display_lock(1000);
    if (!locked) {
        return;
    }

    lv_obj_t *screen = lv_scr_act();
    if (!screen) {
        display_unlock();
        return;
    }

    clear_screen_black(screen);

    lv_coord_t screen_width = 0;
    lv_coord_t screen_height = 0;
    lv_coord_t shape_size = 24;
    lv_coord_t shape_gap = 10;
    lv_coord_t total_width = (shape_size * 2) + shape_gap;
    lv_coord_t start_x = 0;
    lv_coord_t start_y = 0;

    get_screen_size(screen, &screen_width, &screen_height);
    start_x = (screen_width - total_width) / 2;
    start_y = (screen_height - shape_size) / 2;

    lv_obj_t *square = lv_obj_create(screen);
    lv_obj_clear_flag(square, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(square, shape_size, shape_size);
    lv_obj_set_pos(square, start_x, start_y);
    lv_obj_set_style_radius(square, 0, 0);
    lv_obj_set_style_border_width(square, 0, 0);
    lv_obj_set_style_bg_color(square, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_bg_opa(square, LV_OPA_COVER, 0);

    lv_obj_t *circle = lv_obj_create(screen);
    lv_obj_clear_flag(circle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(circle, shape_size, shape_size);
    lv_obj_set_pos(circle, start_x + shape_size + shape_gap, start_y);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(circle, 0, 0);
    lv_obj_set_style_bg_color(circle, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);

    display_unlock();

    vTaskDelay(pdMS_TO_TICKS(STEP_DELAY_MS));
    lv_obj_set_style_bg_opa(circle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(square, LV_OPA_TRANSP, 0);
}

void app_main(void) {
  init_display();
  int color_index = 0;
  const int num_colors = sizeof(rgbw_colors) / sizeof(rgbw_colors[0]);
  bool locked = display_lock(1000);
  if (locked) {
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    display_unlock();
  }

  while (true) {
    locked = display_lock(1000);
    if (locked) {
      rgbw_set_color(rgbw_colors[color_index].hex_color);
      display_unlock();
    }
    ESP_LOGI(TAG, "Current color: %s", rgbw_colors[color_index].name);
    vTaskDelay(pdMS_TO_TICKS(STEP_DELAY_MS));
    color_index++;
    if (color_index >= num_colors) {
      draw_circle_square();
      show_four_lines_text();
      color_index = 0;
    }
  }
}
