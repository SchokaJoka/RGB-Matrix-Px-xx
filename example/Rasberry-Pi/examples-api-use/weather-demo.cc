// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-

#include "weather-demo.h"

#include "led-matrix.h"
#include "graphics.h"
#include "demo-runner.h"

#include <algorithm> // for array/string manipulations (like map/filter/sort)
#include <cctype>    // for character utilities (like tolower/toupper)
#include <cstdio>    // standard I/O (like console.log/printf)
#include <sstream>   // string stream (helps parse strings like streams/buffers)
#include <string>    // C++ string class (like JS String)
#include <unistd.h>  // Unix system calls (like sleep/usleep)
#include <vector>    // dynamic arrays (like JS Array/List)

using namespace rgb_matrix;

namespace {

// Helper: Convert string to lowercase (JS equivalent: value.toLowerCase())
static std::string ToLowerCopy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return value;
}

// Helper: Convert string to uppercase (JS equivalent: value.toUpperCase())
static std::string ToUpperCopy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return std::toupper(c); });
  return value;
}

// Helper: Maps SwissMetNet abbreviations to City names wttr.in understands
static std::string MapAbbrToCity(const std::string &abbr) {
  std::string upper = ToUpperCopy(abbr);
  if (upper == "LUZ") return "Luzern";
  if (upper == "BAS") return "Basel";
  if (upper == "BER") return "Bern";
  if (upper == "ZRH" || upper == "ZEH") return "Zurich";
  if (upper == "GVE") return "Geneva";
  if (upper == "LUG") return "Lugano";
  return abbr; // fallback
}

// Helper: Extract JSON value from raw JSON string (since we don't have a JSON library)
static std::string ExtractJsonValue(const std::string &json, const std::string &key, size_t start_pos = 0) {
  // Look for "key": "
  std::string target = "\"" + key + "\": \"";
  size_t idx = json.find(target, start_pos);
  if (idx == std::string::npos) {
    // Maybe without space: "key":"
    target = "\"" + key + "\":\"";
    idx = json.find(target, start_pos);
    if (idx == std::string::npos) {
      // Maybe it's a number/boolean: "key": value
      target = "\"" + key + "\": ";
      idx = json.find(target, start_pos);
      if (idx == std::string::npos) {
        // Maybe "key":value
        target = "\"" + key + "\":";
        idx = json.find(target, start_pos);
        if (idx == std::string::npos) return "";
      }
    }
  }
  
  idx += target.length();
  size_t end = json.find_first_of("\",]}\n", idx);
  if (end == std::string::npos) return "";
  std::string val = json.substr(idx, end - idx);
  // Trim trailing quotes or whitespace
  while (!val.empty() && (val.back() == '"' || val.back() == ' ' || val.back() == '\r')) {
    val.pop_back();
  }
  return val;
}

// Helper: Run a terminal command and get output (JS equivalent: execSync(command))
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

// Enum: Weather types for icons
enum WeatherType {
  WEATHER_SUN,
  WEATHER_CLOUD,
  WEATHER_RAIN
};

// Map wttr.in weatherCode to our simplified WeatherType enum
static WeatherType GetWeatherType(const std::string &code_str) {
  int code = atoi(code_str.c_str());
  if (code == 113) {
    return WEATHER_SUN;
  }
  // Rain codes (heavy, moderate, light, showers, thunderstorms)
  if (code == 176 || (code >= 263 && code <= 308) || (code >= 353 && code <= 359) || code == 386 || code == 389) {
    return WEATHER_RAIN;
  }
  // Default to cloud/overcast/fog/mist
  return WEATHER_CLOUD;
}

// Struct: Weather data model
struct WeatherReading {
  std::string station_name;
  std::string current_temp;
  std::string current_time;
  std::string current_code;
  std::string tomorrow_min;
  std::string tomorrow_max;
  std::string tomorrow_code;
};

// Fetch weather readings from wttr.in JSON API
static bool FetchWeatherReading(const std::string &station_abbr,
                                WeatherReading *reading,
                                std::string *error_message) {
  const std::string city = MapAbbrToCity(station_abbr);
  std::string json;
  const std::string url = "https://wttr.in/" + city + "?format=j1";
  
  if (!RunCommand("curl -fsSL \"" + url + "\"", &json, error_message)) {
    return false;
  }

  // Print fetched data to the console (terminal)
  printf("Fetched weather JSON data from URL:\n%s\n", json.c_str());

  // Extract current condition fields
  reading->station_name = city;
  reading->current_temp = ExtractJsonValue(json, "temp_C");
  reading->current_time = ExtractJsonValue(json, "observation_time");
  reading->current_code = ExtractJsonValue(json, "weatherCode");

  // Extract tomorrow's fields from the forecast array
  size_t weather_arr_pos = json.find("\"weather\":");
  if (weather_arr_pos == std::string::npos) {
    *error_message = "Missing weather forecast array in response";
    return false;
  }

  // Find today's date block
  size_t today_date_pos = json.find("\"date\":", weather_arr_pos);
  
  // Find tomorrow's date block
  size_t tomorrow_date_pos = json.find("\"date\":", today_date_pos + 1);
  if (tomorrow_date_pos == std::string::npos) {
    *error_message = "Missing tomorrow's forecast date in response";
    return false;
  }

  // Tomorrow max/min temperatures
  reading->tomorrow_max = ExtractJsonValue(json, "maxtempC", tomorrow_date_pos);
  reading->tomorrow_min = ExtractJsonValue(json, "mintempC", tomorrow_date_pos);

  // Tomorrow weather code at midday (12:00)
  size_t tomorrow_midday_pos = json.find("\"time\": \"1200\"", tomorrow_date_pos);
  reading->tomorrow_code = "113";
  if (tomorrow_midday_pos != std::string::npos) {
    reading->tomorrow_code = ExtractJsonValue(json, "weatherCode", tomorrow_midday_pos);
  }

  return true;
}

class MeteoSwissWeather : public DemoRunner {
public:
  MeteoSwissWeather(RGBMatrix *matrix, const std::string &station_abbr)
    : DemoRunner(matrix), matrix_(matrix), station_abbr_(station_abbr) {
    offscreen_ = matrix_->CreateFrameCanvas();
    font_file_ = (matrix_->height() >= 20) ? "../fonts/5x7.bdf"
                                          : "../fonts/4x6.bdf";
    if (!font_.LoadFont(font_file_.c_str())) {
      fprintf(stderr, "Couldn't load font '%s'\n", font_file_.c_str());
    }
  }

  // The main run loop
  void Run() override {
    int tick = 6000; // Force immediate fetch on startup
    WeatherReading reading;
    std::string error_message;
    bool ok = false;

    while (!interrupt_received) {
      if (tick >= 6000) { // Fetch every 10 minutes (6000 * 100ms)
        ok = FetchWeatherReading(station_abbr_, &reading, &error_message);
        tick = 0;
      }
      
      // Render screen (Toggle between NOW and TOMORROW every 2.5s if narrow)
      RenderFrame(ok, reading, error_message, (tick % 50) >= 25);
      offscreen_ = matrix_->SwapOnVSync(offscreen_);
      
      usleep(100 * 1000); // Sleep for 100ms
      tick++;
    }
  }

private:
  void DrawLineText(int x, int y, const Color &color, const std::string &text) {
    DrawText(offscreen_, font_, x, y + font_.baseline(), color, NULL,
             text.c_str(), 0);
  }

  // Draws a pixel-art weather icon on the canvas at (x,y)
  void DrawWeatherIcon(int x, int y, WeatherType type) {
    if (type == WEATHER_SUN) {
      const char *sprite[] = {
        "....Y.YY.Y....",
        ".....YYYY.....",
        "..Y.YYYYYY.Y..",
        "..YYYYYYYYYY..",
        "Y.YYYYYYYYYY.Y",
        "Y.YYYYYYYYYY.Y",
        "..YYYYYYYYYY..",
        "..Y.YYYYYY.Y..",
        ".....YYYY.....",
        "....Y.YY.Y....",
      };
      for (int r = 0; r < 10; ++r) {
        for (int c = 0; sprite[r][c] != '\0'; ++c) {
          if (sprite[r][c] == 'Y') {
            offscreen_->SetPixel(x + c, y + r, 255, 215, 0); // Gold/Yellow
          }
        }
      }
    } else if (type == WEATHER_CLOUD) {
      const char *sprite[] = {
        "......CCCC......",
        "....CCCCCCCCC...",
        "...CCCCCCCCCCC..",
        "..CCCCCCCCCCCCC.",
        ".CCCCCCCCCCCCCCC",
        "CCCCCCCCCCCCCCCC",
        "DDDDDDDDDDDDDDDD",
      };
      for (int r = 0; r < 7; ++r) {
        for (int c = 0; sprite[r][c] != '\0'; ++c) {
          if (sprite[r][c] == 'C') {
            offscreen_->SetPixel(x + c, y + r, 220, 220, 220); // Light Grey
          } else if (sprite[r][c] == 'D') {
            offscreen_->SetPixel(x + c, y + r, 130, 130, 130); // Shadow
          }
        }
      }
    } else if (type == WEATHER_RAIN) {
      const char *sprite[] = {
        "......CCCC......",
        "....CCCCCCCCC...",
        "...CCCCCCCCCCC..",
        "..CCCCCCCCCCCCC.",
        ".CCCCCCCCCCCCCCC",
        "CCCCCCCCCCCCCCCC",
        "DDDDDDDDDDDDDDDD",
        "....B....B....B.",
        "...B....B....B..",
        "..B....B....B...",
      };
      for (int r = 0; r < 10; ++r) {
        for (int c = 0; sprite[r][c] != '\0'; ++c) {
          if (sprite[r][c] == 'C') {
            offscreen_->SetPixel(x + c, y + r, 200, 200, 200); // Grey cloud
          } else if (sprite[r][c] == 'D') {
            offscreen_->SetPixel(x + c, y + r, 110, 110, 110); // Shadow
          } else if (sprite[r][c] == 'B') {
            offscreen_->SetPixel(x + c, y + r, 0, 191, 255); // Deep Sky Blue rain
          }
        }
      }
    }
  }

  // Renders the weather visual information
  void RenderFrame(bool ok, const WeatherReading &reading,
                   const std::string &error_message, bool toggle_tomorrow) {
    offscreen_->Fill(0, 0, 0); // Clear screen

    if (!ok) {
      DrawLineText(2, 2, Color(255, 0, 0), "Weather Error");
      DrawLineText(2, font_.height() + 4, Color(255, 255, 0), station_abbr_);
      DrawLineText(2, font_.height() * 2 + 6, Color(255, 255, 255), error_message);
      return;
    }

    const int w = matrix_->width();
    const int h = matrix_->height();

    // If screen is wide (e.g. 128px), show side-by-side
    if (w >= 128) {
      // Draw NOW (Left side)
      DrawLineText(4, 2, Color(0, 191, 255), "NOW (" + reading.current_time + ")");
      DrawWeatherIcon(12, 16, GetWeatherType(reading.current_code));
      DrawLineText(32, 18, Color(255, 215, 0), reading.current_temp + " C");

      // Draw vertical separator
      for (int y = 0; y < h; ++y) {
        offscreen_->SetPixel(64, y, 60, 60, 60);
      }

      // Draw TOMORROW (Right side)
      DrawLineText(68, 2, Color(0, 255, 127), "TOMORROW");
      DrawWeatherIcon(76, 16, GetWeatherType(reading.tomorrow_code));
      DrawLineText(96, 18, Color(255, 215, 0), reading.tomorrow_min + " - " + reading.tomorrow_max + " C");
    } else {
      // Screen is narrow (e.g. 64px), toggle view every 2.5 seconds
      if (!toggle_tomorrow) {
        // Draw NOW
        DrawLineText(2, 1, Color(0, 191, 255), "NOW (" + reading.current_time + ")");
        DrawWeatherIcon(4, 11, GetWeatherType(reading.current_code));
        DrawLineText(24, 13, Color(255, 215, 0), reading.current_temp + " C");
      } else {
        // Draw TOMORROW
        DrawLineText(2, 1, Color(0, 255, 127), "TOMORROW");
        DrawWeatherIcon(4, 11, GetWeatherType(reading.tomorrow_code));
        DrawLineText(24, 13, Color(255, 215, 0), reading.tomorrow_min + "-" + reading.tomorrow_max + " C");
      }
    }
  }

  RGBMatrix *const matrix_;
  FrameCanvas *offscreen_;
  std::string station_abbr_;
  std::string font_file_;
  Font font_;
};

}  // namespace

// Factory function
DemoRunner *CreateMeteoSwissWeather(RGBMatrix *matrix,
                                    const std::string &station_abbr) {
  return new MeteoSwissWeather(matrix, station_abbr);
}