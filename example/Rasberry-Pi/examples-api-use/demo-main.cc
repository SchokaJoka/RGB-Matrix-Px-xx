// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
//
// This code is public domain
// (but note, once linked against the led-matrix library, this is
// covered by the GPL v2)
//
// Entry point for the DI Plaza Infoscreen demos. Routes -D <demo-nr> to the
// MeteoSwiss weather (13) or SBB train (14) display.
#include "led-matrix.h"

#include "demo-runner.h"
#include "weather-demo.h"
#include "train-demo.h"

#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

using namespace rgb_matrix;

#define TERM_ERR  "\033[1;31m"
#define TERM_NORM "\033[0m"

volatile bool interrupt_received = false;
static void InterruptHandler(int signo) {
  interrupt_received = true;
}

static int usage(const char *progname,
     const RGBMatrix::Options &matrix_options,
     const RuntimeOptions &runtime_opt) {
  fprintf(stderr, "usage: %s <options> -D <demo-nr> [optional parameter]\n",
          progname);
  fprintf(stderr, "Options:\n");
  fprintf(stderr,
          "\t-D <demo-nr>              : Always needs to be set\n"
          );


  rgb_matrix::PrintMatrixFlags(stderr, matrix_options, runtime_opt);

  fprintf(stderr, "Demos, chosen with -D\n");
  fprintf(stderr,
          "\t13 - MeteoSwiss weather summary (optional station code)\n"
          "\t14 - Train summary (optional station code)\n");
  fprintf(stderr, "Example:\n\t%s -D 13 LUZ\n"
          "Shows the weather summary until Ctrl-C is pressed\n", progname);
  return 1;
}

static void PrintDefaultOptions(const RGBMatrix::Options &matrix_options,
                                const RuntimeOptions &runtime_opt) {
  printf("Default properties and their values when running this script:\n\n");
  printf("Script options:\n");
  printf("  demo                     : -1 (Must be set via -D)\n\n");

  printf("Matrix options:\n");
  printf("  hardware_mapping         : %s\n", matrix_options.hardware_mapping ? matrix_options.hardware_mapping : "");
  printf("  rows                     : %d\n", matrix_options.rows);
  printf("  cols                     : %d\n", matrix_options.cols);
  printf("  chain_length             : %d\n", matrix_options.chain_length);
  printf("  parallel                 : %d\n", matrix_options.parallel);
  printf("  pwm_bits                 : %d\n", matrix_options.pwm_bits);
  printf("  pwm_lsb_nanoseconds      : %d\n", matrix_options.pwm_lsb_nanoseconds);
  printf("  pwm_dither_bits          : %d\n", matrix_options.pwm_dither_bits);
  printf("  brightness               : %d%%\n", matrix_options.brightness);
  printf("  scan_mode                : %d (%s)\n", matrix_options.scan_mode, matrix_options.scan_mode == 0 ? "progressive" : "interlaced");
  printf("  row_address_type         : %d\n", matrix_options.row_address_type);
  printf("  multiplexing             : %d\n", matrix_options.multiplexing);
  printf("  disable_hardware_pulsing : %s\n", matrix_options.disable_hardware_pulsing ? "true" : "false");
  printf("  show_refresh_rate        : %s\n", matrix_options.show_refresh_rate ? "true" : "false");
  printf("  inverse_colors           : %s\n", matrix_options.inverse_colors ? "true" : "false");
  printf("  led_rgb_sequence         : %s\n", matrix_options.led_rgb_sequence ? matrix_options.led_rgb_sequence : "");
  printf("  pixel_mapper_config      : %s\n", matrix_options.pixel_mapper_config ? matrix_options.pixel_mapper_config : "(empty)");
  printf("  panel_type               : %s\n", matrix_options.panel_type ? matrix_options.panel_type : "(empty)");
  printf("  limit_refresh_rate_hz    : %d\n", matrix_options.limit_refresh_rate_hz);
  printf("  disable_busy_waiting     : %s\n\n", matrix_options.disable_busy_waiting ? "true" : "false");

  printf("Runtime options:\n");
  printf("  gpio_slowdown            : %d\n", runtime_opt.gpio_slowdown);
  printf("  rp1_rio                  : %d\n", runtime_opt.rp1_rio);
  printf("  daemon                   : %d (%s)\n", runtime_opt.daemon, runtime_opt.daemon == 0 ? "off" : (runtime_opt.daemon == 1 ? "on" : "disabled"));
  printf("  drop_privileges          : %d (%s)\n", runtime_opt.drop_privileges, runtime_opt.drop_privileges == 0 ? "off" : (runtime_opt.drop_privileges == 1 ? "on" : "disabled"));
  printf("  drop_priv_user           : %s\n", runtime_opt.drop_priv_user ? runtime_opt.drop_priv_user : "");
  printf("  drop_priv_group          : %s\n", runtime_opt.drop_priv_group ? runtime_opt.drop_priv_group : "");
}

int main(int argc, char *argv[]) {
  int demo = -1;

  const char *demo_parameter = NULL;
  RGBMatrix::Options matrix_options;
  rgb_matrix::RuntimeOptions runtime_opt;

  // Default to a 64x64 matrix split across two chained panels.
  matrix_options.rows = 64;
  matrix_options.cols = 64;
  matrix_options.chain_length = 2;
  matrix_options.parallel = 1;
  matrix_options.disable_hardware_pulsing = true;
  runtime_opt.gpio_slowdown = 4;

  // Check if user requested showing defaults
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--show-defaults") == 0 || strcmp(argv[i], "--defaults") == 0) {
      PrintDefaultOptions(matrix_options, runtime_opt);
      return 0;
    }
  }

  // First things first: extract the command line flags that contain
  // relevant matrix options.
  if (!ParseOptionsFromFlags(&argc, &argv, &matrix_options, &runtime_opt)) {
    return usage(argv[0], matrix_options, runtime_opt);
  }

  int opt;
  while ((opt = getopt(argc, argv, "dD:")) != -1) {
    switch (opt) {
    case 'D':
      demo = atoi(optarg);
      break;

    default: /* '?' */
      return usage(argv[0], matrix_options, runtime_opt);
    }
  }

  if (optind < argc) {
    demo_parameter = argv[optind];
  }

  if (demo < 0) {
    fprintf(stderr, TERM_ERR "Expected required option -D <demo>\n" TERM_NORM);
    return usage(argv[0], matrix_options, runtime_opt);
  }

  RGBMatrix *matrix = RGBMatrix::CreateFromOptions(matrix_options, runtime_opt);
  if (matrix == NULL)
    return 1;

  printf("Size: %dx%d. Hardware gpio mapping: %s\n",
         matrix->width(), matrix->height(), matrix_options.hardware_mapping);

  // The DemoRunner objects are filling the matrix continuously.
  DemoRunner *demo_runner = NULL;
  switch (demo) {
    case 13:
      demo_runner = CreateMeteoSwissWeather(matrix,
                                            demo_parameter ? demo_parameter
                                                           : "LUZ");
      break;

    case 14:
      demo_runner = CreateTrainDemo(matrix, "Emmenbrücke");
      break;
  }

  if (demo_runner == NULL)
    return usage(argv[0], matrix_options, runtime_opt);

  // Set up an interrupt handler to be able to stop animations while they go
  // on. Each demo tests for while (!interrupt_received) {},
  // so they exit as soon as they get a signal.
  signal(SIGTERM, InterruptHandler);
  signal(SIGINT, InterruptHandler);

  printf("Press <CTRL-C> to exit and reset LEDs\n");

  // Now, run our particular demo; it will exit when it sees interrupt_received.
  demo_runner->Run();

  delete demo_runner;
  delete matrix;

  printf("Received CTRL-C. Exiting.\n");
  return 0;
}
