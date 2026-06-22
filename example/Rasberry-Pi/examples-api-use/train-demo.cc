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
#include <ctime>

using namespace rgb_matrix;

namespace
{

  static bool RunCommand(const std::string &command, std::string *output,
                         std::string *error_message)
  {
    FILE *pipe = popen(command.c_str(), "r");
    if (pipe == NULL)
    {
      *error_message = "Unable to start curl";
      return false;
    }

    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != NULL)
    {
      output->append(buffer);
    }

    const int rc = pclose(pipe);
    if (rc != 0)
    {
      *error_message = "curl returned a failure status";
      return false;
    }
    return true;
  }

  struct TrainDeparture
  {
    std::string direction;
    std::string departure_time;
    std::string platform;
    std::string delay;

    bool has_direction = false;
    bool has_time = false;
    bool has_platform = false;
    bool has_delay = false;
  };

  static bool FetchTrainData(const std::string &station,
                             std::vector<TrainDeparture> *out,
                             std::string *error_message)
  {
    std::string json;

    std::string url =
        "https://transport.opendata.ch/v1/stationboard?station=" +
        station + "&limit=4";

    if (!RunCommand("curl -fsSL \"" + url + "\"", &json, error_message))
    {
      return false;
    }

    size_t pos = 0;

    while (true)
    {
      // --- FIND "to" ---
      size_t key = json.find("\"to\":\"", pos);
      if (key == std::string::npos)
        break;

      key += 6; // korrekt: Länge von "to":"

      size_t end = json.find("\"", key);
      if (end == std::string::npos)
        break;

      TrainDeparture d;
      d.direction = json.substr(key, end - key);
      d.has_direction = true;

      // --- FIND departure ---
      size_t dep = json.find("\"departure\":\"", end);
      if (dep != std::string::npos)
      {
        dep += 13;
        size_t dep_end = json.find("\"", dep);
        if (dep_end != std::string::npos)
        {
          d.departure_time = json.substr(dep, dep_end - dep);
          d.has_time = true;
        }
      }

      // --- FIND platform ---
      size_t plat = json.find("\"platform\":\"", end);
      if (plat != std::string::npos)
      {
        plat += 12;
        size_t plat_end = json.find("\"", plat);
        if (plat_end != std::string::npos)
        {
          d.platform = json.substr(plat, plat_end - plat);
          d.has_platform = true;
        }
      }

      // --- FIND delay ---
      size_t del = json.find("\"delay\":", end);
      if (del != std::string::npos)
      {
        del += 8;
        size_t del_end = json.find(",", del);
        if (del_end != std::string::npos)
        {
          d.delay = json.substr(del, del_end - del);
          d.has_delay = true;
        }
      }

      out->push_back(d);

      // wichtig: weiter im JSON suchen
      pos = end;
    }

    return !out->empty();
  }

  class TrainStationBoardDemo : public DemoRunner
  {
    bool show_delay_mode = false;
    bool train_animating_ = false;
    int train_x_ = 0;
    int train_y_ = 0;
    bool train_moving_right_ = true;

  public:
    TrainStationBoardDemo(RGBMatrix *matrix, const std::string &station_abbr)
        : DemoRunner(matrix), matrix_(matrix), station_abbr_(station_abbr)
    {
      offscreen_ = matrix_->CreateFrameCanvas();
      font_file_ = "../fonts/4x6.bdf";

      if (!font_.LoadFont(font_file_.c_str()))
      {
        fprintf(stderr, "Couldn't load font '%s'\n", font_file_.c_str());
      }

      // 👇 richtig: KEIN Font davor!
      if (!clock_font.LoadFont("../fonts/10x20.bdf"))
      {
        fprintf(stderr, "Couldn't load clock font\n");
      }
    }

    static std::string GetCurrentTime()
    {
      time_t now = time(nullptr);
      struct tm *lt = localtime(&now);

      char buf[6];
      snprintf(buf, sizeof(buf), "%02d:%02d",
               lt->tm_hour,
               lt->tm_min);

      return std::string(buf);
    }

    void Run() override
    {
      std::vector<TrainDeparture> trains;
      std::string error;

      bool ok = FetchTrainData(station_abbr_, &trains, &error);

      train_animating_ = true;
      train_x_ = -20;
      train_y_ = matrix_->height() - 12;

      const int train_width = 17;

      while (!interrupt_received)
      {
        RenderFrame(ok, trains, error, show_delay_mode);

        offscreen_ = matrix_->SwapOnVSync(offscreen_);

        if (train_moving_right_)
        {
          train_x_++;

          if (train_x_ >= matrix_->width() - train_width)
            train_moving_right_ = false;
        }
        else
        {
          train_x_--;

          if (train_x_ <= 0)
            train_moving_right_ = true;
        }

        usleep(50000);
      }
    }
    void DrawSteamTrain(int x, int y, bool flipped)
    {
      const char *sprite[] = {
          "....SSSS.........",
          "...SSSSSS.SS.....",
          "..SSSSSSSSSSSS...",
          "..SSSS..SSSSSSS..",
          "....XXXX.........",
          "..XXXXX.XXXXXX...",
          ".XXXXXXXXXXXXXXX.",
          "XXWXXXXXXXXXXXXX.",
          "XXXXXXXXXXXXXXXXX",
          "XXXXXXXXXXXXXXXX.",
          "XXRRXXRRXXRRXX...",
          "................",
      };

      const int h = sizeof(sprite) / sizeof(sprite[0]);

      for (int row = 0; row < h; ++row)
      {
        int width = 17;

        for (int col = 0; col < width; ++col)
        {
          char pixel = flipped
                           ? sprite[row][width - 1 - col]
                           : sprite[row][col];

          switch (pixel)
          {
          case 'X':
            offscreen_->SetPixel(x + col, y + row, 30, 35, 45);
            break;
          case 'W':
            offscreen_->SetPixel(x + col, y + row, 180, 240, 255);
            break;
          case 'R':
            offscreen_->SetPixel(x + col, y + row, 220, 60, 60);
            break;
          case 'S':
            offscreen_->SetPixel(x + col, y + row, 170, 170, 160);
            break;
          }
        }
      }
    }

  private:
    void DrawLineText(int x, int y, const Color &color, const std::string &text)
    {
      DrawText(offscreen_, font_, x, y + font_.baseline(), color, NULL,
               text.c_str(), 0);
    }

    void RenderFrame(bool ok,
                     const std::vector<TrainDeparture> &trains,
                     const std::string &error_message,
                     bool show_delay_mode)
    {
      offscreen_->Fill(0, 0, 0);
      if (train_animating_)
      {
        DrawSteamTrain(train_x_, train_y_, train_moving_right_);
      }
      // =========================
      // 🕒 GROSSE UHR OBEN
      // =========================
      std::string current_time = GetCurrentTime();

      int clock_char_width = 10; // grob für 10x20 Font
      int clock_text_width = current_time.size() * clock_char_width;

      int x_clock = (matrix_->width() - clock_text_width) / 2;
      int y_clock = 15; // kleiner Abstand nach oben

      DrawText(offscreen_, clock_font,
               x_clock,
               y_clock,
               Color(255, 255, 255),
               NULL,
               current_time.c_str());

      // =========================
      // 🧱 LAYOUT VARIABLEN
      // =========================
      const int x_offset = 2;

      const int panel_width = 64;
      const int x_base = matrix_->width() - panel_width;

      const int x_dest = x_base + 2;
      const int x_time = x_base + 36;
      const int x_plat = x_time + 23;

      // =========================
      // ❌ ERROR STATE
      // =========================
      if (!ok || trains.empty())
      {
        DrawLineText(x_offset, clock_font.height() + 2,
                     Color(255, 0, 0), "Train board error");

        DrawLineText(x_offset, clock_font.height() + font_.height() + 4,
                     Color(255, 255, 0), station_abbr_);

        DrawLineText(x_offset, clock_font.height() + font_.height() * 2 + 6,
                     Color(255, 255, 255), error_message);
        return;
      }

      // =========================
      // 🚆 ZUGLIST START (mehr Abstand nach Uhr)
      // =========================
      int y = clock_font.height() - 2;

      for (size_t i = 0; i < trains.size() && y < matrix_->height(); i++)
      {
        const auto &t = trains[i];

        std::string dep_time = "--:--";
        if (t.departure_time.size() >= 16)
          dep_time = t.departure_time.substr(11, 5);

        char dest[64];
        snprintf(dest, sizeof(dest), "%.12s", t.direction.c_str());

        if (!show_delay_mode)
        {
          DrawLineText(x_dest, y, Color(0, 255, 255), dest);
          DrawLineText(x_time, y, Color(255, 255, 255), dep_time);
          DrawLineText(x_plat, y, Color(255, 200, 0),
                       t.has_platform ? t.platform : "-");
        }
        else
        {
          std::string delay =
              t.has_delay && t.delay != "null" && t.delay != "0"
                  ? ("+" + t.delay)
                  : " ";

          DrawLineText(x_dest, y, Color(0, 255, 255), dest);
          DrawLineText(x_time, y, Color(255, 80, 80), delay);
          DrawLineText(x_plat, y, Color(255, 200, 0),
                       t.has_platform ? t.platform : "-");
        }

        y += font_.height() + 1;
      }
    }

    RGBMatrix *const matrix_;
    FrameCanvas *offscreen_;
    std::string station_abbr_;
    std::string font_file_;
    Font font_;
    Font clock_font;
  };

} // namespace

DemoRunner *CreateTrainDemo(RGBMatrix *matrix,
                            const std::string &station_abbr)
{
  return new TrainStationBoardDemo(matrix, station_abbr);
}