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

  static const rgb_matrix::Color SBB_RED(220, 30, 30);
  static const rgb_matrix::Color SBB_WHITE(240, 240, 240);
  static const rgb_matrix::Color SBB_BLACK(10, 10, 10);
  static const rgb_matrix::Color SBB_GREY(80, 80, 80);
  static const rgb_matrix::Color PANTOGRAPH_GREY(150, 150, 150);

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

      key += 6;

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
    
    // Gesamtlänge des SBB Giruno Zuges: 36 (Schnauze) + 28 (Mitte) + 36 (Schnauze) = 100 Pixel
    const int train_total_width = 100;

    enum TrainState
    {
      MOVING_RIGHT,
      WAIT_RIGHT,
      MOVING_LEFT,
      WAIT_LEFT
    };

    TrainState train_state_ = MOVING_RIGHT;

    int wait_counter_ = 0;
    const int wait_frames = 40; // ~2s

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
      train_x_ = -train_total_width; 
      train_y_ = matrix_->height() - 15;

      while (!interrupt_received)
      {
        RenderFrame(ok, trains, error, show_delay_mode);
        offscreen_ = matrix_->SwapOnVSync(offscreen_);

        switch (train_state_)
        {
        case MOVING_RIGHT:
          train_x_++;
          if (train_x_ > matrix_->width() + 40) 
          {
            train_state_ = WAIT_RIGHT;
            wait_counter_ = 0;
          }
          break;

        case WAIT_RIGHT:
          wait_counter_++;
          if (wait_counter_ > wait_frames)
          {
            train_state_ = MOVING_LEFT;
            train_x_ = matrix_->width(); 
          }
          break;

        case MOVING_LEFT:
          train_x_--;
          if (train_x_ < -train_total_width) 
          {
            train_state_ = WAIT_LEFT;
            wait_counter_ = 0;
          }
          break;

        case WAIT_LEFT:
          wait_counter_++;
          if (wait_counter_ > wait_frames)
          {
            train_state_ = MOVING_RIGHT;
            train_x_ = -train_total_width; 
          }
          break;
        }

        usleep(50000);
      }
    }

    // --- SBB Giruno Endwagen (Schnauze) ---
    void DrawGirunoSchnauze(FrameCanvas *canvas, int x, int y, bool flipped)
    {
      const char *sprite[] = {
          "               RRRRRRRRRRRRRRRRRRRRR",
          "         RRRRRRWWWWWWWWWWWWWWWWWWWWW",
          "     RRRRWWWWWWWWWWWWWWWWWWWWWWWWWWW",
          "   RRWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
          "  RWWWWWWBBBBWWWWWWWWWWWWWWWWWWWWWWW",
          "  WWWWWWBBBBWWWWWWWWWWWWWWWWWWWWWWWW",
          "  WWWWWWWWWWWWWWWWWWWWWWWWBBBBBBBBBB",
          "  WWWWWWWWWWWWWWWWRRWWWWWWBBBBBBBBBB",
          "  GGGGGGGGGGGGGGGGRRGGGGGGGGGGGGGGGG",
          "   GGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGG",
          "    X X X X X X X X X X X X X X X   ",
          "     O O                     O O    ",
          "     O O                     O O    "};
      const int h = sizeof(sprite) / sizeof(sprite[0]);
      const int width = 36;

      for (int row = 0; row < h; ++row)
      {
        for (int col = 0; col < width; ++col)
        {
          char pixel = flipped ? sprite[row][width - 1 - col] : sprite[row][col];
          if (pixel == '\0' || pixel == ' ')
            continue;

          Color pColor;
          switch (pixel)
          {
          case 'R': pColor = SBB_RED; break;
          case 'W': pColor = SBB_WHITE; break;
          case 'B': pColor = SBB_BLACK; break;
          case 'G': pColor = SBB_GREY; break;
          case 'X': pColor = Color(40, 40, 40); break;
          case 'O': pColor = SBB_BLACK; break;
          default: continue;
          }
          canvas->SetPixel(x + col, y + row, pColor.r, pColor.g, pColor.b);
        }
      }
    }

    // --- SBB Giruno Mittelwagen ---
    void DrawGirunoMittelwagen(FrameCanvas *canvas, int x, int y)
    {
      const char *sprite[] = {
          "RRRRRRRRRRRRRRRRRRRRRRRRRRRR",
          "WWWWWWWWWWWWWWWWWWWWWWWWWWWW",
          "WWWWWWWWWWWWWWWWWWWWWWWWWWWW",
          "WWWWWWWWWWWWWWWWWWWWWWWWWWWW",
          "WWWWWWWWWWWWWWWWWWWWWWWWWWWW",
          "WWWWWWWWWWWWWWWWWWWWWWWWWWWW",
          "BBBBBBBBBBBBBBBBBBBBBBBBBBBB",
          "BBBBBBBBBBBBBBBBBBBBBBBBBBBB",
          "GGGGGGGGGGGGGGGGGGGGGGGGGGGG",
          "GGGGGGGGGGGGGGGGGGGGGGGGGGGG",
          " X X X X X X X X X X X X X  ",
          "   O O                 O O  ",
          "   O O                 O O  "};
      const int h = sizeof(sprite) / sizeof(sprite[0]);
      const int width = 28;

      for (int row = 0; row < h; ++row)
      {
        for (int col = 0; col < width; ++col)
        {
          char pixel = sprite[row][col];
          if (pixel == '\0' || pixel == ' ')
            continue;

          Color pColor;
          switch (pixel)
          {
          case 'R': pColor = SBB_RED; break;
          case 'W': pColor = SBB_WHITE; break;
          case 'B': pColor = SBB_BLACK; break;
          case 'G': pColor = SBB_GREY; break;
          case 'X': pColor = Color(40, 40, 40); break;
          case 'O': pColor = SBB_BLACK; break;
          default: continue;
          }
          canvas->SetPixel(x + col, y + row, pColor.r, pColor.g, pColor.b);
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
        int train_base_y = matrix_->height() - 15;
        bool is_moving_left = (train_state_ == MOVING_LEFT);

        if (!is_moving_left)
        {
          // --- ZUG FÄHRT NACH RECHTS --->
          // Heck: Muss nach links schauen (Standard/unflipped)
          DrawGirunoSchnauze(offscreen_, train_x_, train_base_y, false);
          // Mitte
          DrawGirunoMittelwagen(offscreen_, train_x_ + 36, train_base_y);
          // Front: Muss nach rechts schauen (gespiegelt/flipped)
          DrawGirunoSchnauze(offscreen_, train_x_ + 36 + 28, train_base_y, true);
        }
        else
        {
          // <--- ZUG FÄHRT NACH LINKS ---
          // Front: Muss nach links schauen (Standard/unflipped)
          DrawGirunoSchnauze(offscreen_, train_x_, train_base_y, false);
          // Mitte
          DrawGirunoMittelwagen(offscreen_, train_x_ + 36, train_base_y);
          // Heck: Muss nach rechts schauen (gespiegelt/flipped)
          DrawGirunoSchnauze(offscreen_, train_x_ + 36 + 28, train_base_y, true);
        }
      }
      
      // =========================
      // 🕒 GROSSE UHR OBEN
      // =========================
      std::string current_time = GetCurrentTime();

      int clock_char_width = 10; 
      int clock_text_width = current_time.size() * clock_char_width;

      int x_clock = (matrix_->width() - clock_text_width) / 2;
      int y_clock = 15; 

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
      // 🚆 ZUGLIST START
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