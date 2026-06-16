// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-

#include "train-demo.h"

#include "led-matrix.h"
#include "graphics.h"
#include "demo-runner.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

using namespace rgb_matrix;

namespace {

static bool RunCommand(const std::string &command, std::string *output,
                       std::string *error_message) {
  FILE *pipe = popen(command.c_str(), "r");
  if (pipe == NULL) {
    *error_message = "Unable to start curl";
    return false;
  }

  char buffer[4096];
  while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
    output->append(buffer);
  }

  const int rc = pclose(pipe);
  if (rc != 0) {
    *error_message = "curl returned a failure status";
    return false;
  }
  return true;
}

struct TrainDeparture {
  std::string direction;
  std::string departure_time;
  std::string platform;

  bool has_direction = false;
  bool has_time = false;
  bool has_platform = false;
};

static bool FetchTrainData(const std::string &station,
                           std::vector<TrainDeparture> *out,
                           std::string *error_message) {
  std::string json;

  std::string url =
      "https://transport.opendata.ch/v1/stationboard?station=" +
      station + "&limit=4";

  if (!RunCommand("curl -fsSL \"" + url + "\"", &json, error_message)) {
    return false;
  }

  size_t pos = 0;

  while ((pos = json.find("\"to\":\"", pos)) != std::string::npos) {
    TrainDeparture d;

    pos += 7;
    size_t end = json.find("\"", pos);
    d.direction = json.substr(pos, end - pos);
    d.has_direction = true;

    size_t dep = json.find("\"departure\":\"", end);
    if (dep != std::string::npos) {
      dep += 13;
      size_t dep_end = json.find("\"", dep);
      d.departure_time = json.substr(dep, dep_end - dep);
      d.has_time = true;
    }

    size_t plat = json.find("\"platform\":\"", end);
    if (plat != std::string::npos) {
      plat += 12;
      size_t plat_end = json.find("\"", plat);
      d.platform = json.substr(plat, plat_end - plat);
      d.has_platform = true;
    }

    out->push_back(d);
    pos = end;
  }

  return !out->empty();
}

class TrainStationBoardDemo : public DemoRunner {
public:
  TrainStationBoardDemo(RGBMatrix *matrix, const std::string &station_abbr)
    : DemoRunner(matrix), matrix_(matrix), station_abbr_(station_abbr) {
    offscreen_ = matrix_->CreateFrameCanvas();
    font_file_ = "../fonts/4x6.bdf";
    if (!font_.LoadFont(font_file_.c_str())) {
      fprintf(stderr, "Couldn't load font '%s'\n", font_file_.c_str());
    }
  }

  void Run() override {
    while (!interrupt_received) {

      std::vector<TrainDeparture> trains;
      std::string error;

      bool ok = FetchTrainData(station_abbr_, &trains, &error);

      RenderFrame(ok, trains, error);

      offscreen_ = matrix_->SwapOnVSync(offscreen_);
      sleep(30);
    }
  }

private:
  void DrawLineText(int x, int y, const Color &color, const std::string &text) {
    DrawText(offscreen_, font_, x, y + font_.baseline(), color, NULL,
             text.c_str(), 0);
  }

  void RenderFrame(bool ok,
                   const std::vector<TrainDeparture> &trains,
                   const std::string &error_message) {
    offscreen_->Fill(0, 0, 0);

    const int x_offset = 2;

    // 👉 Spaltenlayout
    const int x_dest = 2;
    const int x_time = 80;
    const int x_plat = 110;

    if (!ok || trains.empty()) {
      DrawLineText(x_offset, 0, Color(255, 0, 0), "Train board error");
      DrawLineText(x_offset, font_.height(), Color(255, 255, 0), station_abbr_);
      DrawLineText(x_offset, font_.height() * 2, Color(255, 255, 255), error_message);
      return;
    }

    // Header
    DrawLineText(x_dest, 0, Color(255, 255, 0), "DEST");
    DrawLineText(x_time, 0, Color(255, 255, 0), "TIME");
    DrawLineText(x_plat, 0, Color(255, 255, 0), "GL");

    int y = font_.height();

    for (size_t i = 0; i < trains.size() && y < matrix_->height(); i++) {

      const auto &t = trains[i];

      std::string time = "--:--";
      if (t.departure_time.size() >= 16)
        time = t.departure_time.substr(11, 5);

      char dest[64];
      if(t.direction == "enzburg") {
        snprintf(dest, sizeof(dest), "Lenzburg");
      } else
      snprintf(dest, sizeof(dest), "%.12s", t.direction.c_str());

      char plat[8];
      snprintf(plat, sizeof(plat), "%s", t.platform.c_str());

      DrawLineText(x_dest, y, Color(0, 255, 255), dest);
      DrawLineText(x_time, y, Color(255, 255, 255), time);
      DrawLineText(x_plat, y, Color(255, 200, 0), plat);

      y += font_.height() - 1;
    }
  }

  RGBMatrix *const matrix_;
  FrameCanvas *offscreen_;
  std::string station_abbr_;
  std::string font_file_;
  Font font_;
};

}  // namespace

DemoRunner *CreateTrainDemo(RGBMatrix *matrix,
                            const std::string &station_abbr) {
  return new TrainStationBoardDemo(matrix, station_abbr);
}