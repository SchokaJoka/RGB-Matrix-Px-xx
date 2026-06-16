// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-

#include "weather-demo.h"

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

static std::string ToLowerCopy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return value;
}

static std::string ToUpperCopy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return std::toupper(c); });
  return value;
}

static std::string TrimLine(std::string value) {
  while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
    value.pop_back();
  }
  return value;
}

static std::vector<std::string> SplitSemicolonLine(const std::string &line) {
  std::vector<std::string> parts;
  size_t start = 0;
  while (start <= line.size()) {
    const size_t end = line.find(';', start);
    if (end == std::string::npos) {
      parts.push_back(line.substr(start));
      break;
    }
    parts.push_back(line.substr(start, end - start));
    start = end + 1;
  }
  return parts;
}

static int FindColumn(const std::vector<std::string> &columns,
                      const std::string &name) {
  for (size_t i = 0; i < columns.size(); ++i) {
    if (columns[i] == name) return static_cast<int>(i);
  }
  return -1;
}

static bool GetValueAt(const std::vector<std::string> &values, int index,
                       std::string *value) {
  if (index < 0 || index >= static_cast<int>(values.size())) return false;
  if (values[index].empty()) return false;
  *value = values[index];
  return true;
}

static bool ParseDouble(const std::string &value, double *result) {
  if (value.empty()) return false;
  char *end = NULL;
  *result = strtod(value.c_str(), &end);
  return end != value.c_str();
}

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

struct WeatherReading {
  std::string station_abbr;
  std::string reference_timestamp;
  bool has_temperature = false;
  bool has_humidity = false;
  bool has_pressure = false;
  bool has_wind_speed = false;
  bool has_gust_speed = false;
  bool has_precipitation = false;
  double temperature_c = 0.0;
  double humidity_percent = 0.0;
  double pressure_hpa = 0.0;
  double wind_speed_ms = 0.0;
  double gust_speed_ms = 0.0;
  double precipitation_mm = 0.0;
};

static bool FetchWeatherReading(const std::string &station_abbr,
                                WeatherReading *reading,
                                std::string *error_message) {
  const std::string station_lower = ToLowerCopy(station_abbr);
  std::string csv;
  const std::string url = "https://data.geo.admin.ch/ch.meteoschweiz.ogd-smn/" +
      station_lower + "/ogd-smn_" + station_lower + "_t_now.csv";
  if (!RunCommand("curl -fsSL " + url, &csv, error_message)) {
    return false;
  }

  std::istringstream input(csv);
  std::string line;
  std::vector<std::string> lines;
  while (std::getline(input, line)) {
    line = TrimLine(line);
    if (!line.empty()) {
      lines.push_back(line);
    }
  }

  if (lines.size() < 2) {
    *error_message = "weather CSV did not contain any data rows";
    return false;
  }

  const std::vector<std::string> header = SplitSemicolonLine(lines.front());
  const std::vector<std::string> row = SplitSemicolonLine(lines.back());

  const int station_index = FindColumn(header, "station_abbr");
  const int timestamp_index = FindColumn(header, "reference_timestamp");
  const int temperature_index = FindColumn(header, "tre200s0");
  const int humidity_index = FindColumn(header, "ure200s0");
  const int pressure_index = FindColumn(header, "prestas0");
  const int wind_index = FindColumn(header, "fkl010z0");
  const int gust_index = FindColumn(header, "fkl010z1");
  const int precipitation_index = FindColumn(header, "rre150z0");

  if (!GetValueAt(row, station_index, &reading->station_abbr) ||
      !GetValueAt(row, timestamp_index, &reading->reference_timestamp)) {
    *error_message = "weather CSV row is missing station or timestamp data";
    return false;
  }

  std::string value;
  if (GetValueAt(row, temperature_index, &value) &&
      ParseDouble(value, &reading->temperature_c)) {
    reading->has_temperature = true;
  }
  if (GetValueAt(row, humidity_index, &value) &&
      ParseDouble(value, &reading->humidity_percent)) {
    reading->has_humidity = true;
  }
  if (GetValueAt(row, pressure_index, &value) &&
      ParseDouble(value, &reading->pressure_hpa)) {
    reading->has_pressure = true;
  }
  if (GetValueAt(row, wind_index, &value) &&
      ParseDouble(value, &reading->wind_speed_ms)) {
    reading->has_wind_speed = true;
  }
  if (GetValueAt(row, gust_index, &value) &&
      ParseDouble(value, &reading->gust_speed_ms)) {
    reading->has_gust_speed = true;
  }
  if (GetValueAt(row, precipitation_index, &value) &&
      ParseDouble(value, &reading->precipitation_mm)) {
    reading->has_precipitation = true;
  }

  return true;
}

static std::string FormatMetric(bool has_value, double value, int decimals,
                                const char *prefix, const char *suffix) {
  char buffer[64];
  if (!has_value) {
    snprintf(buffer, sizeof(buffer), "%s--%s", prefix, suffix);
  } else {
    if (decimals == 0) {
      snprintf(buffer, sizeof(buffer), "%s%.0f%s", prefix, value, suffix);
    } else {
      snprintf(buffer, sizeof(buffer), "%s%.*f%s", prefix, decimals, value,
               suffix);
    }
  }
  return buffer;
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

  void Run() override {
    while (!interrupt_received) {
      WeatherReading reading;
      std::string error_message;
      const bool ok = FetchWeatherReading(station_abbr_, &reading,
                                          &error_message);
      RenderFrame(ok, reading, error_message);
      offscreen_ = matrix_->SwapOnVSync(offscreen_);
      SleepUntilNextRefresh();
    }
  }

private:
  void SleepUntilNextRefresh() {
    const int refresh_seconds = 10 * 60;
    for (int i = 0; i < refresh_seconds && !interrupt_received; ++i) {
      sleep(1);
    }
  }

  void DrawLineText(int x, int y, const Color &color, const std::string &text) {
    DrawText(offscreen_, font_, x, y + font_.baseline(), color, NULL,
             text.c_str(), 0);
  }

  void RenderFrame(bool ok, const WeatherReading &reading,
                   const std::string &error_message) {
    offscreen_->Fill(0, 0, 0);
    const bool compact = matrix_->height() < font_.height() * 3;

    if (!ok) {
      DrawLineText(0, 0, Color(255, 0, 0), "MeteoSwiss weather");
      DrawLineText(0, font_.height(), Color(255, 255, 0), station_abbr_);
      if (!compact) {
        DrawLineText(0, font_.height() * 2, Color(255, 255, 255),
                     error_message);
      }
      return;
    }

    char buffer[128];
    snprintf(buffer, sizeof(buffer), "%s %s",
             ToUpperCopy(station_abbr_).c_str(),
             reading.reference_timestamp.c_str());
    DrawLineText(0, 0, Color(255, 255, 0), buffer);

    snprintf(buffer, sizeof(buffer), "%s %s %s",
             FormatMetric(reading.has_temperature, reading.temperature_c, 1,
                          "T", "C").c_str(),
             FormatMetric(reading.has_humidity, reading.humidity_percent, 0,
                          "H", "%").c_str(),
             FormatMetric(reading.has_wind_speed, reading.wind_speed_ms, 1,
                          "W", "m/s").c_str());
    DrawLineText(0, font_.height(), Color(0, 255, 255), buffer);

    if (compact) {
      return;
    }

    snprintf(buffer, sizeof(buffer), "%s %s %s",
             FormatMetric(reading.has_gust_speed, reading.gust_speed_ms, 1,
                          "G", "m/s").c_str(),
             FormatMetric(reading.has_pressure, reading.pressure_hpa, 0,
                          "P", "hPa").c_str(),
             FormatMetric(reading.has_precipitation, reading.precipitation_mm, 1,
                          "R", "mm").c_str());
    DrawLineText(0, font_.height() * 2, Color(0, 255, 0), buffer);
  }

  RGBMatrix *const matrix_;
  FrameCanvas *offscreen_;
  std::string station_abbr_;
  std::string font_file_;
  Font font_;
};

}  // namespace

DemoRunner *CreateMeteoSwissWeather(RGBMatrix *matrix,
                                    const std::string &station_abbr) {
  return new MeteoSwissWeather(matrix, station_abbr);
}