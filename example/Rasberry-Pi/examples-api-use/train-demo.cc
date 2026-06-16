// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-

#include "train-demo.h"

#include "led-matrix.h"
#include "graphics.h"
#include "demo-runner.h"

#include <algorithm>
#include <cstdio>
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
};

static bool FetchTrainData(const std::string &station,
                           std::vector<TrainDeparture> *out,
                           std::string *error_message) {
  std::string json;

  std::string url =
      "https://transport.opendata.ch/v1/stationboard?station=" +
      station + "&limit=10";

  if (!RunCommand("curl -fsSL \"" + url + "\"", &json, error_message)) {
    return false;
  }

  size_t pos = 0;

  while ((pos = json.find("\"to\":\"", pos)) != std::string::npos) {
    TrainDeparture d;

    pos += 7;
    size_t end = json.find("\"", pos);
    d.direction = json.substr(pos, end - pos);

    size_t dep = json.find("\"departure\":\"", end);
    if (dep != std::string::npos) {
      dep += 13;
      size_t dep_end = json.find("\"", dep);
      d.departure_time = json.substr(dep, dep_end - dep);
    }

    size_t plat = json.find("\"platform\":\"", end);
    if (plat != std::string::npos) {
      plat += 12;
      size_t plat_end = json.find("\"", plat);
      d.platform = json.substr(plat, plat_end - plat);
    }

    out->push_back(d);
    pos = end;
  }

  return !out->empty();
}

class TrainBoardDemo : public DemoRunner {
public:
  TrainBoardDemo(RGBMatrix *matrix, const std::string &station_abbr)
    : DemoRunner(matrix),
      matrix_(matrix),
      station_abbr_(station_abbr) {

    offscreen_ = matrix_->CreateFrameCanvas();

    // 👉 kleiner Font
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

    // 👉 rechte Seite nutzen
    int x_offset = matrix_->width() / 2;

    if (!ok || trains.empty()) {
      DrawLineText(x_offset, 0, Color(255, 0, 0), "Train error");
      DrawLineText(x_offset, font_.height(), Color(255, 255, 0), station_abbr_);
      DrawLineText(x_offset, font_.height() * 2, Color(255, 255, 255), error_message);
      return;
    }

    int y = 0;

    for (size_t i = 0; i < trains.size() && y < matrix_->height(); i++) {

      const auto &t = trains[i];

      char buffer[128];
      snprintf(buffer, sizeof(buffer), "%s %s %s",
               t.direction.c_str(),
               (t.departure_time.size() >= 16)
                 ? t.departure_time.substr(11, 5).c_str()
                 : "--:--",
               t.platform.c_str());

      DrawLineText(x_offset, y, Color(0, 255, 255), buffer);

      // 👉 kompakter Abstand
      y += font_.height() - 1;
    }
  }

  RGBMatrix *const matrix_;
  FrameCanvas *offscreen_;
  std::string station_abbr_;
  std::string font_file_;
  Font font_;
};

} // namespace

DemoRunner *CreateTrainBoardDemo(RGBMatrix *matrix,
                                 const std::string &station_abbr) {
  return new TrainBoardDemo(matrix, station_abbr);
}