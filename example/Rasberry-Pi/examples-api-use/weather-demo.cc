// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-

#include "weather-demo.h"

#include "led-matrix.h"
#include "graphics.h"
#include "demo-runner.h"

// Standard C++ libraries (analogous to JS built-in modules/APIs)
#include <algorithm> // for array/string manipulations (like map/filter/sort)
#include <cctype>    // for character utilities (like tolower/toupper)
#include <cstdio>    // standard I/O (like console.log/printf)
#include <sstream>   // string stream (helps parse strings like streams/buffers)
#include <string>    // C++ string class (like JS String)
#include <unistd.h>  // Unix system calls (like sleep/usleep)
#include <vector>    // dynamic arrays (like JS Array/List)

// Import the rgb_matrix namespace so we don't have to write rgb_matrix:: everywhere
using namespace rgb_matrix;

// Anonymous namespace: variables/functions here are local to this file (similar to ES modules scoping)
namespace {

// Helper: Convert string to lowercase (JS equivalent: value.toLowerCase())
static std::string ToLowerCopy(std::string value) {
  // std::transform works like a JS map() on the characters of the string
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

// Helper: Trim trailing newline (\n) or carriage return (\r) characters
static std::string TrimLine(std::string value) {
  while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
    value.pop_back(); // Pop last char off the string (like JS array.pop())
  }
  return value;
}

// Helper: Split a string by semicolon (JS equivalent: line.split(';'))
static std::vector<std::string> SplitSemicolonLine(const std::string &line) {
  std::vector<std::string> parts; // JS: const parts = [];
  size_t start = 0;
  while (start <= line.size()) {
    const size_t end = line.find(';', start);
    if (end == std::string::npos) {
      parts.push_back(line.substr(start)); // JS: parts.push(line.slice(start))
      break;
    }
    parts.push_back(line.substr(start, end - start));
    start = end + 1;
  }
  return parts;
}

// Helper: Find index of a string in a vector (JS equivalent: columns.indexOf(name))
static int FindColumn(const std::vector<std::string> &columns,
                      const std::string &name) {
  for (size_t i = 0; i < columns.size(); ++i) {
    if (columns[i] == name) return static_cast<int>(i);
  }
  return -1;
}

// Helper: Get value at index from a vector (JS: return values[index])
// In C++, we pass a pointer (std::string *value) to write the result back (output parameter)
static bool GetValueAt(const std::vector<std::string> &values, int index,
                       std::string *value) {
  if (index < 0 || index >= static_cast<int>(values.size())) return false;
  if (values[index].empty()) return false;
  *value = values[index]; // Dereference pointer to store value (JS: value = values[index])
  return true;
}

// Helper: Parse string to double/float (JS equivalent: parseFloat(value))
static bool ParseDouble(const std::string &value, double *result) {
  if (value.empty()) return false;
  char *end = NULL;
  *result = strtod(value.c_str(), &end); // Converts string to double
  return end != value.c_str();          // Returns true if parsing succeeded
}

// Helper: Run a terminal command and get output (JS equivalent: execSync(command))
static bool RunCommand(const std::string &command, std::string *output,
                       std::string *error_message) {
  // popen runs the command and opens a read pipe to capture stdout/stderr
  FILE *pipe = popen(command.c_str(), "r");
  if (pipe == NULL) {
    *error_message = "Unable to start curl";
    return false;
  }

  char buffer[4096];
  // Read output line by line from the pipe
  while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
    output->append(buffer); // JS: output += buffer
  }

  const int rc = pclose(pipe); // Close the pipe and get exit code
  if (rc != 0) {
    *error_message = "curl returned a failure status";
    return false;
  }
  return true;
}

// Struct: A lightweight data model (JS equivalent: plain object/interface with default values)
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

// Fetch real-time weather readings for the given Swiss station code (e.g. "BAS" for Basel)
static bool FetchWeatherReading(const std::string &station_abbr,
                                WeatherReading *reading,
                                std::string *error_message) {
  const std::string station_lower = ToLowerCopy(station_abbr);
  std::string csv;
  // URL to the Geo.admin.ch real-time weather API (returns a CSV file)
  const std::string url = "https://data.geo.admin.ch/ch.meteoschweiz.ogd-smn/" +
      station_lower + "/ogd-smn_" + station_lower + "_t_now.csv";
  
  // Run `curl -fsSL <url>` to fetch the CSV content
  if (!RunCommand("curl -fsSL " + url, &csv, error_message)) {
    return false;
  }

  // Print fetched data to the console (terminal)
  printf("Fetched weather CSV data from URL:\n%s\n", csv.c_str());

  // Read the CSV data line by line
  std::istringstream input(csv);
  std::string line;
  std::vector<std::string> lines;
  while (std::getline(input, line)) {
    line = TrimLine(line);
    if (!line.empty()) {
      lines.push_back(line);
    }
  }

  // The CSV must have at least the header row and a data row
  if (lines.size() < 2) {
    *error_message = "weather CSV did not contain any data rows";
    return false;
  }

  // Parse header columns and the last row (most recent data reading)
  const std::vector<std::string> header = SplitSemicolonLine(lines.front());
  const std::vector<std::string> row = SplitSemicolonLine(lines.back());

  // Find the column index for each weather parameter in the header
  const int station_index = FindColumn(header, "station_abbr");
  const int timestamp_index = FindColumn(header, "reference_timestamp");
  const int temperature_index = FindColumn(header, "tre200s0");
  const int humidity_index = FindColumn(header, "ure200s0");
  const int pressure_index = FindColumn(header, "prestas0");
  const int wind_index = FindColumn(header, "fkl010z0");
  const int gust_index = FindColumn(header, "fkl010z1");
  const int precipitation_index = FindColumn(header, "rre150z0");

  // Populate basic metadata
  if (!GetValueAt(row, station_index, &reading->station_abbr) ||
      !GetValueAt(row, timestamp_index, &reading->reference_timestamp)) {
    *error_message = "weather CSV row is missing station or timestamp data";
    return false;
  }

  // Parse and set metrics if present in the data row
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

// Helper: Formats weather metric string. E.g. "T23.5C" or "H55%" or "T--C" if no value.
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

// Class: The main weather animation runner (JS equivalent: `class MeteoSwissWeather extends DemoRunner`)
class MeteoSwissWeather : public DemoRunner {
public:
  // Constructor: Ran when instantiating the class
  MeteoSwissWeather(RGBMatrix *matrix, const std::string &station_abbr)
    : DemoRunner(matrix), matrix_(matrix), station_abbr_(station_abbr) {
    // Create an offscreen buffer canvas for double-buffering (prevents screen tearing/flickering)
    offscreen_ = matrix_->CreateFrameCanvas();
    // Choose font size depending on matrix display height
    font_file_ = (matrix_->height() >= 20) ? "../fonts/5x7.bdf"
                                          : "../fonts/4x6.bdf";
    // Load the font
    if (!font_.LoadFont(font_file_.c_str())) {
      fprintf(stderr, "Couldn't load font '%s'\n", font_file_.c_str());
    }
  }

  // The main run loop called by the demo system
  void Run() override {
    while (!interrupt_received) { // Loop runs until user hits Ctrl-C
      WeatherReading reading;
      std::string error_message;
      // Fetch latest weather data (makes curl network request)
      const bool ok = FetchWeatherReading(station_abbr_, &reading,
                                          &error_message);
      // Draw weather text metrics to offscreen canvas
      RenderFrame(ok, reading, error_message);
      // Swap the offscreen canvas to the active display (and get the old screen back as new offscreen)
      offscreen_ = matrix_->SwapOnVSync(offscreen_);
      // Sleep for 10 minutes before fetching and rendering again
      SleepUntilNextRefresh();
    }
  }

private:
  // Sleep helper: sleeps for 10 minutes, but wakes up immediately if Ctrl-C (interrupt) is received
  void SleepUntilNextRefresh() {
    const int refresh_seconds = 10 * 60; // 10 minutes
    for (int i = 0; i < refresh_seconds && !interrupt_received; ++i) {
      sleep(1);
    }
  }

  // Text helper: Draws text at coordinates (x, y) with color
  void DrawLineText(int x, int y, const Color &color, const std::string &text) {
    DrawText(offscreen_, font_, x, y + font_.baseline(), color, NULL,
             text.c_str(), 0);
  }

  // Renders the layout onto the offscreen canvas buffer
  void RenderFrame(bool ok, const WeatherReading &reading,
                   const std::string &error_message) {
    offscreen_->Fill(0, 0, 0); // Clear canvas (JS: context.clearRect)
    const bool compact = matrix_->height() < font_.height() * 3; // Check if display is small

    // Render error message if the fetch failed (e.g. no internet connection)
    if (!ok) {
      DrawLineText(0, 0, Color(255, 0, 0), "MeteoSwiss weather");
      DrawLineText(0, font_.height(), Color(255, 255, 0), station_abbr_);
      if (!compact) {
        DrawLineText(0, font_.height() * 2, Color(255, 255, 255),
                     error_message);
      }
      return;
    }

    // Line 1: Station Code & Time (e.g., "BAS 202606221000")
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "%s %s",
             ToUpperCopy(station_abbr_).c_str(),
             reading.reference_timestamp.c_str());
    DrawLineText(0, 0, Color(255, 255, 0), buffer);

    // Line 2: Temperature, Humidity, and Wind speed (e.g., "T23.5C H55% W3.2m/s")
    snprintf(buffer, sizeof(buffer), "%s %s %s",
             FormatMetric(reading.has_temperature, reading.temperature_c, 1,
                          "T", "C").c_str(),
             FormatMetric(reading.has_humidity, reading.humidity_percent, 0,
                          "H", "%").c_str(),
             FormatMetric(reading.has_wind_speed, reading.wind_speed_ms, 1,
                          "W", "m/s").c_str());
    DrawLineText(0, font_.height(), Color(0, 255, 255), buffer);

    // If the display height is too small, stop here
    if (compact) {
      return;
    }

    // Line 3 (Only for taller screens): Gust speed, Air Pressure, and Precipitation (e.g., "G4.5m/s P1013hPa R0.0mm")
    snprintf(buffer, sizeof(buffer), "%s %s %s",
             FormatMetric(reading.has_gust_speed, reading.gust_speed_ms, 1,
                          "G", "m/s").c_str(),
             FormatMetric(reading.has_pressure, reading.pressure_hpa, 0,
                          "P", "hPa").c_str(),
             FormatMetric(reading.has_precipitation, reading.precipitation_mm, 1,
                          "R", "mm").c_str());
    DrawLineText(0, font_.height() * 2, Color(0, 255, 0), buffer);
  }

  // Member variables (JS: `this.matrix`, `this.offscreen`, etc.)
  RGBMatrix *const matrix_;
  FrameCanvas *offscreen_;
  std::string station_abbr_;
  std::string font_file_;
  Font font_;
};

}  // namespace

// Factory function: Instantiates and returns the class (like standard exports)
DemoRunner *CreateMeteoSwissWeather(RGBMatrix *matrix,
                                    const std::string &station_abbr) {
  return new MeteoSwissWeather(matrix, station_abbr);
}