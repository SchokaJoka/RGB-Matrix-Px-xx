// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
// Displays two centered lines: "Digital" and "Ideation".
//
// This code is public domain
// (but note, that the led-matrix library this depends on is GPL v2)

#include "led-matrix.h"
#include "graphics.h"

#include <algorithm>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

using namespace rgb_matrix;

volatile bool interrupt_received = false;
static void InterruptHandler(int signo) {
  interrupt_received = true;
}

static int usage(const char *progname) {
  fprintf(stderr, "usage: %s [options]\n", progname);
  fprintf(stderr,
          "Displays centered text on two lines:\n"
          "  Digital\n"
          "  Ideation\n"
          "Options:\n"
          "\t-f <font-file>    : BDF font file. Default: ../fonts/7x13.bdf\n"
          "\t-C <r,g,b>        : Text color. Default: 255,255,255\n"
          "\t-B <r,g,b>        : Background color. Default: 0,0,0\n");
  rgb_matrix::PrintMatrixFlags(stderr);
  return 1;
}

static bool parseColor(Color *c, const char *str) {
  return sscanf(str, "%hhu,%hhu,%hhu", &c->r, &c->g, &c->b) == 3;
}

static int TextWidth(const Font &font, const char *text, int letter_spacing) {
  int width = 0;
  for (const char *p = text; *p; ++p) {
    width += font.CharacterWidth(*p);
    if (*(p + 1)) width += letter_spacing;
  }
  return width;
}

int main(int argc, char *argv[]) {
  RGBMatrix::Options matrix_options;
  rgb_matrix::RuntimeOptions runtime_opt;
  if (!rgb_matrix::ParseOptionsFromFlags(&argc, &argv,
                                         &matrix_options, &runtime_opt)) {
    return usage(argv[0]);
  }

  const char *bdf_font_file = "../fonts/7x13.bdf";
  const char *line1 = "Digital";
  const char *line2 = "Ideation";
  const int line_spacing = 1;
  const int letter_spacing = 0;

  Color text_color(255, 255, 255);
  Color bg_color(0, 0, 0);

  int opt;
  while ((opt = getopt(argc, argv, "f:C:B:")) != -1) {
    switch (opt) {
    case 'f':
      bdf_font_file = strdup(optarg);
      break;
    case 'C':
      if (!parseColor(&text_color, optarg)) {
        fprintf(stderr, "Invalid color spec: %s\n", optarg);
        return usage(argv[0]);
      }
      break;
    case 'B':
      if (!parseColor(&bg_color, optarg)) {
        fprintf(stderr, "Invalid background color spec: %s\n", optarg);
        return usage(argv[0]);
      }
      break;
    default:
      return usage(argv[0]);
    }
  }

  Font font;
  if (!font.LoadFont(bdf_font_file)) {
    fprintf(stderr, "Couldn't load font '%s'\n", bdf_font_file);
    return 1;
  }

  RGBMatrix *matrix = RGBMatrix::CreateFromOptions(matrix_options, runtime_opt);
  if (matrix == NULL) {
    return 1;
  }

  signal(SIGTERM, InterruptHandler);
  signal(SIGINT, InterruptHandler);

  FrameCanvas *offscreen_canvas = matrix->CreateFrameCanvas();

  const int text_h = 2 * font.height() + line_spacing;
  const int y_top = std::max(0, (matrix->height() - text_h) / 2);
  const int y1 = y_top + font.baseline();
  const int y2 = y1 + font.height() + line_spacing;

  const int w1 = TextWidth(font, line1, letter_spacing);
  const int w2 = TextWidth(font, line2, letter_spacing);
  const int x1 = std::max(0, (matrix->width() - w1) / 2);
  const int x2 = std::max(0, (matrix->width() - w2) / 2);

  while (!interrupt_received) {
    offscreen_canvas->Fill(bg_color.r, bg_color.g, bg_color.b);
    DrawText(offscreen_canvas, font, x1, y1, text_color, NULL, line1,
             letter_spacing);
    DrawText(offscreen_canvas, font, x2, y2, text_color, NULL, line2,
             letter_spacing);
    offscreen_canvas = matrix->SwapOnVSync(offscreen_canvas);
    usleep(30 * 1000);
  }

  delete matrix;
  return 0;
}
