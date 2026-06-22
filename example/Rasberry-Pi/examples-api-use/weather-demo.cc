// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-

#include "weather-demo.h"

#include "led-matrix.h"
#include "graphics.h"
#include "demo-runner.h"

#include <algorithm> // for array/string manipulations (like map/filter/sort)
#include <cctype>    // for character utilities (like tolower/toupper)
#include <cmath>     // for std::abs
#include <cstdio>    // standard I/O (like console.log/printf)
#include <ctime>     // for time, gmtime, localtime, strftime, timegm
#include <sstream>   // string stream (helps parse strings like streams/buffers)
#include <string>    // C++ string class (like JS String)
#include <unistd.h>  // Unix system calls (like sleep/usleep)
#include <vector>    // dynamic arrays (like JS Array/List)

using namespace rgb_matrix;

namespace {


// Helper: Convert string to uppercase (JS equivalent: value.toUpperCase())
static std::string ToUpperCopy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return std::toupper(c); });
  return value;
}

// Helper: Maps SwissMetNet abbreviations to Point ID, Point Type, and Display Name
static void MapAbbrToPoint(const std::string &abbr, std::string *point_id, std::string *point_type_id, std::string *display_name) {
  std::string upper = ToUpperCopy(abbr);
  
  // Default values: Lucerne fallback
  *point_id = "68";
  *point_type_id = "1";
  *display_name = "Luzern";
  
  if (upper == "LUZ") {
    *point_id = "68";
    *point_type_id = "1";
    *display_name = "Luzern";
  } else if (upper == "BAS") {
    *point_id = "75";
    *point_type_id = "1";
    *display_name = "Basel";
  } else if (upper == "BER") {
    *point_id = "78";
    *point_type_id = "1";
    *display_name = "Bern";
  } else if (upper == "ZRH" || upper == "ZEH" || upper == "SMA") {
    *point_id = "71";
    *point_type_id = "1";
    *display_name = "Zurich";
  } else if (upper == "GVE") {
    *point_id = "58";
    *point_type_id = "1";
    *display_name = "Geneva";
  } else if (upper == "LUG") {
    *point_id = "47";
    *point_type_id = "1";
    *display_name = "Lugano";
  } else {
    // Check if it's all digits (like a postcode or raw point ID)
    bool is_digits = true;
    for (char c : upper) {
      if (!isdigit(c)) { is_digits = false; break; }
    }
    if (is_digits && !upper.empty()) {
      if (upper.length() == 4) {
        *point_id = upper + "00";
        *point_type_id = "2"; // Postcode
      } else {
        *point_id = upper;
        *point_type_id = "1";
      }
      *display_name = "POI " + upper;
    }
  }
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
    fprintf(stderr, "Command failed: '%s' with exit code/status: %d\n", command.c_str(), rc);
    *error_message = "curl returned a failure status";
    return false;
  }
  return true;
}

// Enum: Weather types for icons
enum WeatherType {
  WEATHER_SUN,
  WEATHER_CLOUD,
  WEATHER_RAIN,
  WEATHER_SUN_CLOUD,
  WEATHER_SNOW,
  WEATHER_THUNDER,
  WEATHER_FOG
};

// Map MeteoSwiss pictogram weather code to our simplified WeatherType enum
static WeatherType GetWeatherType(const std::string &code_str) {
  int code = atoi(code_str.c_str());
  // Normalize night-time codes (100+) to daytime equivalent
  if (code > 100) {
    code = code - 100;
  }
  
  switch (code) {
    case 1:  // Sunny
    case 26: // High clouds
      return WEATHER_SUN;

    case 2:  // Mostly sunny
    case 3:  // Partly sunny
      return WEATHER_SUN_CLOUD;
      
    case 7:  // Rain showers
    case 8:  // Heavy rain
    case 14: // Rain
    case 18: // Sunny intervals and rain
    case 29: // Rain / showers
    case 30: // Rain / showers
    case 31: // Rain / showers
    case 32: // Rain / showers
    case 33: // Rain / showers
    case 34: // Rain / showers
    case 35: // Rain / showers
      return WEATHER_RAIN;

    case 9:  // Rain and snow showers
    case 10: // Snow showers
    case 15: // Snow
    case 16: // Rain and snow
    case 19: // Sunny intervals and snow
    case 20: // Sunny intervals, rain and snow
      return WEATHER_SNOW;

    case 11: // Thunderstorm
    case 12: // Sunny intervals, chance of thunderstorms
    case 13: // Sunny intervals, possible thunderstorms
    case 21: // Thunderstorm
    case 22: // Thunderstorm
    case 39: // Snow/Rain/Storm
    case 40: // Snow/Rain/Storm
      return WEATHER_THUNDER;

    case 6:  // Fog
      return WEATHER_FOG;
      
    default:
      // Default/fallback is WEATHER_CLOUD for codes like 4 (Overcast), 5 (Bedeckt), 23, 24, 25, 27, 28, etc.
      return WEATHER_CLOUD;
  }
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

// Helper: Get YYYYMMDD date string with offset in days
static std::string GetDateStringOffset(int days_offset) {
  time_t t = time(NULL);
  t += days_offset * 24 * 3600;
  struct tm *tm_info = localtime(&t);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y%m%d", tm_info);
  return std::string(buf);
}

// Helper: Discover the latest forecast run date and timestamp from the daily STAC items
static bool DiscoverLatestRun(std::string *item_id, std::string *latest_run, std::string *error_message) {
  std::string json;
  bool fetched = false;
  
  // Try today, yesterday, then 2 days ago
  for (int offset = 0; offset >= -2; --offset) {
    std::string date_str = GetDateStringOffset(offset);
    std::string cur_item_id = date_str + "-ch";
    std::string url = "https://data.geo.admin.ch/api/stac/v1/collections/ch.meteoschweiz.ogd-local-forecasting/items/" + cur_item_id;
    
    std::string cmd = "curl -fsSL -k --compressed --connect-timeout 10 -m 20 \"" + url + "\"";
    json.clear();
    std::string cmd_err;
    if (RunCommand(cmd, &json, &cmd_err) && !json.empty()) {
      *item_id = cur_item_id;
      fetched = true;
      break;
    }
  }
  
  if (!fetched) {
    *error_message = "Failed to fetch daily STAC items";
    return false;
  }
  
  // Search for the highest 12-digit timestamp matching "vnut12.lssw.YYYYMMDDHHMM"
  size_t pos = 0;
  std::string max_run = "";
  while ((pos = json.find("vnut12.lssw.", pos)) != std::string::npos) {
    pos += 12; // Length of "vnut12.lssw."
    if (pos + 12 <= json.length()) {
      std::string run = json.substr(pos, 12);
      bool is_digits = true;
      for (char c : run) {
        if (!isdigit(c)) { is_digits = false; break; }
      }
      if (is_digits) {
        if (run > max_run) {
          max_run = run;
        }
      }
    }
  }
  
  if (max_run.empty()) {
    *error_message = "No forecast runs found in STAC item";
    return false;
  }
  
  *latest_run = max_run;
  return true;
}

// Helper: Fetch a single parameter's filtered row(s) from the STAC asset
static bool FetchParameterRow(const std::string &item_id, const std::string &latest_run,
                              const std::string &param, const std::string &point_id,
                              const std::string &point_type_id, bool is_by_date,
                              const std::string &max_date, std::string *result_rows,
                              std::string *error_message) {
  std::string url = "https://data.geo.admin.ch/ch.meteoschweiz.ogd-local-forecasting/" + item_id + "/vnut12.lssw." + latest_run + "." + param + ".csv";
  // Use awk to stop fetching as soon as possible.
  // - If is_by_date is true, we exit when the Date (col 3) exceeds max_date.
  // - If is_by_date is false (sorted by point_id first), we exit when we finish matching the target point.
  std::string cmd = "curl -fsSL -k --compressed --connect-timeout 10 -m 30 \"" + url + "\" | awk -F';' -v pid=\"" + point_id + "\" -v ptype=\"" + point_type_id + "\" -v is_by_date=" + (is_by_date ? "1" : "0") + " -v max_date=\"" + max_date + "\" 'NR == 1 { next } is_by_date && $3 > max_date { exit } !is_by_date && matched && ($1 != pid || $2 != ptype) { exit } $1 == pid && $2 == ptype { print; matched = 1 }' || true";
  
  result_rows->clear();
  std::string cmd_err;
  if (!RunCommand(cmd, result_rows, &cmd_err)) {
    *error_message = "Failed to download/filter " + param;
    return false;
  }
  return true;
}

// Helper: Parse CSV rows into date-value string pairs
static std::vector<std::pair<std::string, std::string>> ParseCsvRows(const std::string &csv_content) {
  std::vector<std::pair<std::string, std::string>> rows;
  std::stringstream ss(csv_content);
  std::string line;
  while (std::getline(ss, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
      line.pop_back();
    }
    if (line.empty()) continue;
    
    std::vector<std::string> tokens;
    std::stringstream line_ss(line);
    std::string token;
    while (std::getline(line_ss, token, ';')) {
      tokens.push_back(token);
    }
    
    if (tokens.size() >= 4) {
      rows.push_back(std::make_pair(tokens[2], tokens[3]));
    }
  }
  return rows;
}

// Helper: Convert YYYYMMDDHHMM UTC date string into epoch time
static time_t ParseDateToEpoch(const std::string &date_str) {
  if (date_str.length() < 12) return 0;
  struct tm tm_info = {};
  tm_info.tm_year = atoi(date_str.substr(0, 4).c_str()) - 1900;
  tm_info.tm_mon = atoi(date_str.substr(4, 2).c_str()) - 1;
  tm_info.tm_mday = atoi(date_str.substr(6, 2).c_str());
  tm_info.tm_hour = atoi(date_str.substr(8, 2).c_str());
  tm_info.tm_min = atoi(date_str.substr(10, 2).c_str());
  return timegm(&tm_info);
}

// Helper: Find value from vector of rows closest to a target date string
static std::string FindClosestValue(const std::vector<std::pair<std::string, std::string>> &rows,
                                    const std::string &target_date, std::string *actual_date) {
  if (rows.empty()) return "";
  
  size_t best_idx = 0;
  double min_diff = -1;
  time_t target_epoch = ParseDateToEpoch(target_date);
  
  for (size_t i = 0; i < rows.size(); ++i) {
    time_t row_epoch = ParseDateToEpoch(rows[i].first);
    double diff = std::abs(difftime(row_epoch, target_epoch));
    
    if (min_diff < 0 || diff < min_diff) {
      min_diff = diff;
      best_idx = i;
    }
  }
  
  *actual_date = rows[best_idx].first;
  return rows[best_idx].second;
}

// Helper: Convert UTC date string to formatted local time string
static std::string FormatUtcToLocalTime(const std::string &utc_date_str) {
  time_t epoch = ParseDateToEpoch(utc_date_str);
  struct tm *local_tm = localtime(&epoch);
  char buf[16];
  strftime(buf, sizeof(buf), "%H:%M", local_tm);
  return std::string(buf);
}

// Helper: Round a temperature string to no decimals (using nearest integer rounding)
static std::string RoundTempString(const std::string &val_str) {
  if (val_str.empty()) return "";
  char *endptr;
  double val = strtod(val_str.c_str(), &endptr);
  if (endptr == val_str.c_str()) {
    return val_str; // not a number, return as is
  }
  int rounded = (int)round(val);
  return std::to_string(rounded);
}

// Fetch weather readings from MeteoSwiss OGD API
static bool FetchWeatherReading(const std::string &station_abbr,
                                WeatherReading *reading,
                                std::string *error_message) {
  printf("MeteoSwiss API: Starting weather fetch for %s...\n", station_abbr.c_str());
  fflush(stdout);
  
  std::string point_id, point_type_id, display_name;
  MapAbbrToPoint(station_abbr, &point_id, &point_type_id, &display_name);
  
  printf("MeteoSwiss API: Mapped to point_id=%s, point_type_id=%s, name=%s\n",
         point_id.c_str(), point_type_id.c_str(), display_name.c_str());
  fflush(stdout);
  
  std::string item_id, latest_run;
  if (!DiscoverLatestRun(&item_id, &latest_run, error_message)) {
    printf("MeteoSwiss API: Run discovery failed: %s\n", error_message->c_str());
    fflush(stdout);
    return false;
  }
  
  printf("MeteoSwiss API: Found latest run=%s in item=%s\n", latest_run.c_str(), item_id.c_str());
  fflush(stdout);
  
  // Calculate date variables first
  time_t now = time(NULL);
  
  // Find current UTC hour string
  struct tm *utc_now = gmtime(&now);
  char cur_utc[16];
  snprintf(cur_utc, sizeof(cur_utc), "%04d%02d%02d%02d00",
           utc_now->tm_year + 1900, utc_now->tm_mon + 1, utc_now->tm_mday, utc_now->tm_hour);
  std::string target_utc_str(cur_utc);
  
  // Calculate max date threshold for hourly parameters (current UTC + 6 hours)
  time_t max_hourly_time = now + 6 * 3600;
  struct tm *utc_max_hourly = gmtime(&max_hourly_time);
  char max_hourly_buf[16];
  snprintf(max_hourly_buf, sizeof(max_hourly_buf), "%04d%02d%02d%02d00",
           utc_max_hourly->tm_year + 1900, utc_max_hourly->tm_mon + 1, utc_max_hourly->tm_mday, utc_max_hourly->tm_hour);
  std::string max_hourly_str(max_hourly_buf);
  
  // Find tomorrow's local date string
  time_t tomorrow = now + 24 * 3600;
  struct tm *local_tomorrow = localtime(&tomorrow);
  char tom_local[16];
  strftime(tom_local, sizeof(tom_local), "%Y%m%d0000", local_tomorrow);
  std::string target_tom_str(tom_local);

  // Calculate max date threshold for daily parameters (current local time + 48 hours)
  time_t max_daily_time = now + 48 * 3600;
  struct tm *local_max_daily = localtime(&max_daily_time);
  char max_daily_buf[16];
  strftime(max_daily_buf, sizeof(max_daily_buf), "%Y%m%d0000", local_max_daily);
  std::string max_daily_str(max_daily_buf);

  std::string temp_csv, picto_csv, tmin_csv, tmax_csv, dpicto_csv;
  
  printf("MeteoSwiss API: Downloading hourly temperature (tre200h0)...\n"); fflush(stdout);
  if (!FetchParameterRow(item_id, latest_run, "tre200h0", point_id, point_type_id, true, max_hourly_str, &temp_csv, error_message)) return false;
  
  printf("MeteoSwiss API: Downloading hourly pictograms (jww003i0)...\n"); fflush(stdout);
  if (!FetchParameterRow(item_id, latest_run, "jww003i0", point_id, point_type_id, false, max_hourly_str, &picto_csv, error_message)) return false;
  
  printf("MeteoSwiss API: Downloading daily min temp (tre200dn)...\n"); fflush(stdout);
  if (!FetchParameterRow(item_id, latest_run, "tre200dn", point_id, point_type_id, true, max_daily_str, &tmin_csv, error_message)) return false;
  
  printf("MeteoSwiss API: Downloading daily max temp (tre200dx)...\n"); fflush(stdout);
  if (!FetchParameterRow(item_id, latest_run, "tre200dx", point_id, point_type_id, true, max_daily_str, &tmax_csv, error_message)) return false;
  
  printf("MeteoSwiss API: Downloading daily pictograms (jp2000d0)...\n"); fflush(stdout);
  if (!FetchParameterRow(item_id, latest_run, "jp2000d0", point_id, point_type_id, false, max_daily_str, &dpicto_csv, error_message)) return false;
  
  printf("MeteoSwiss API: Parsing CSV files...\n"); fflush(stdout);
  auto temp_rows = ParseCsvRows(temp_csv);
  auto picto_rows = ParseCsvRows(picto_csv);
  auto tmin_rows = ParseCsvRows(tmin_csv);
  auto tmax_rows = ParseCsvRows(tmax_csv);
  auto dpicto_rows = ParseCsvRows(dpicto_csv);
  
  if (temp_rows.empty() || picto_rows.empty() || tmin_rows.empty() || tmax_rows.empty() || dpicto_rows.empty()) {
    *error_message = "No forecast data found for this point";
    printf("MeteoSwiss API: Parse failed: empty rows\n"); fflush(stdout);
    return false;
  }
  
  std::string actual_temp_date, actual_picto_date, actual_tmin_date, actual_tmax_date, actual_dpicto_date;
  
  std::string temp_val = FindClosestValue(temp_rows, target_utc_str, &actual_temp_date);
  std::string picto_val = FindClosestValue(picto_rows, target_utc_str, &actual_picto_date);
  std::string tmin_val = FindClosestValue(tmin_rows, target_tom_str, &actual_tmin_date);
  std::string tmax_val = FindClosestValue(tmax_rows, target_tom_str, &actual_tmax_date);
  std::string dpicto_val = FindClosestValue(dpicto_rows, target_tom_str, &actual_dpicto_date);
  
  reading->station_name = display_name;
  reading->current_temp = RoundTempString(temp_val);
  reading->current_time = FormatUtcToLocalTime(actual_temp_date);
  reading->current_code = picto_val;
  reading->tomorrow_min = RoundTempString(tmin_val);
  reading->tomorrow_max = RoundTempString(tmax_val);
  reading->tomorrow_code = dpicto_val;
  
  printf("MeteoSwiss API: Success. temp=%s, time=%s, code=%s, tmin=%s, tmax=%s, dcode=%s\n",
         reading->current_temp.c_str(), reading->current_time.c_str(), reading->current_code.c_str(),
         reading->tomorrow_min.c_str(), reading->tomorrow_max.c_str(), reading->tomorrow_code.c_str());
  fflush(stdout);
  
  return true;
}

class MeteoSwissWeather : public DemoRunner {
public:
  MeteoSwissWeather(RGBMatrix *matrix, const std::string &station_abbr)
    : DemoRunner(matrix), matrix_(matrix), station_abbr_(station_abbr) {
    setvbuf(stdout, NULL, _IONBF, 0); // Turn off stdout buffering
    offscreen_ = matrix_->CreateFrameCanvas();
    font_file_ = "../fonts/4x6.bdf";
    if (!font_.LoadFont(font_file_.c_str())) {
      fprintf(stderr, "Couldn't load font '%s'\n", font_file_.c_str());
    }
    if (!clock_font.LoadFont("../fonts/10x20.bdf")) {
      fprintf(stderr, "Couldn't load clock font\n");
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
      RenderFrame(ok, reading, error_message, (tick % 50) >= 25, tick);
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

  void DrawCenteredText(int center_x, int y, const Color &color, const std::string &text) {
    int text_width = MeasureText(font_, text.c_str());
    int x = center_x - text_width / 2;
    DrawText(offscreen_, font_, x, y + font_.baseline(), color, NULL, text.c_str(), 0);
  }

  // Draws an animated pixel-art weather icon on the canvas at (x,y)
  void DrawWeatherIcon(int x, int y, WeatherType type, int tick) {
    if (type == WEATHER_SUN) {
      // Alternate sun rays every 500ms (5 ticks)
      bool frame = (tick / 5) % 2;
      const char *sprite_a[] = {
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
      const char *sprite_b[] = {
        ".....Y.Y.Y....",
        "....YYYYYY....",
        "...YYYYYYYY...",
        "Y.YYYYYYYYYY.Y",
        "..YYYYYYYYYY..",
        "..YYYYYYYYYY..",
        "Y.YYYYYYYYYY.Y",
        "...YYYYYYYY...",
        "....YYYYYY....",
        "....Y.Y.Y.....",
      };
      const char **sprite = frame ? sprite_b : sprite_a;
      for (int r = 0; r < 10; ++r) {
        for (int c = 0; sprite[r][c] != '\0'; ++c) {
          if (sprite[r][c] == 'Y') {
            offscreen_->SetPixel(x + c, y + r, 255, 215, 0); // Gold/Yellow
          }
        }
      }
    } else if (type == WEATHER_CLOUD) {
      // Gently drift the cloud left-and-right slowly (range -2 to +2 pixels)
      int drift[] = {0, 0, 1, 1, 2, 2, 1, 1, 0, 0, -1, -1, -2, -2, -1, -1};
      int x_offset = drift[(tick / 8) % 16];

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
            offscreen_->SetPixel(x + x_offset + c, y + r, 220, 220, 220); // Light Grey
          } else if (sprite[r][c] == 'D') {
            offscreen_->SetPixel(x + x_offset + c, y + r, 130, 130, 130); // Shadow
          }
        }
      }
    } else if (type == WEATHER_RAIN) {
      // Float cloud and animate falling raindrops
      int drift[] = {0, 0, 1, 1, 2, 2, 1, 1, 0, 0, -1, -1, -2, -2, -1, -1};
      int x_offset = drift[(tick / 8) % 16];

      const char *cloud_sprite[] = {
        "......CCCC......",
        "....CCCCCCCCC...",
        "...CCCCCCCCCCC..",
        "..CCCCCCCCCCCCC.",
        ".CCCCCCCCCCCCCCC",
        "CCCCCCCCCCCCCCCC",
        "DDDDDDDDDDDDDDDD",
      };
      for (int r = 0; r < 7; ++r) {
        for (int c = 0; cloud_sprite[r][c] != '\0'; ++c) {
          if (cloud_sprite[r][c] == 'C') {
            offscreen_->SetPixel(x + x_offset + c, y + r, 200, 200, 200); // Grey cloud
          } else if (cloud_sprite[r][c] == 'D') {
            offscreen_->SetPixel(x + x_offset + c, y + r, 110, 110, 110); // Shadow
          }
        }
      }

      // 3-frame vertical scroll for falling rain
      int rain_frame = (tick / 2) % 3;
      const char *rain_sprite[] = {
        "....B....B....B.",
        "...B....B....B..",
        "..B....B....B...",
      };
      for (int r = 0; r < 3; ++r) {
        int src_row = (r - rain_frame + 3) % 3;
        for (int c = 0; rain_sprite[src_row][c] != '\0'; ++c) {
          if (rain_sprite[src_row][c] == 'B') {
            offscreen_->SetPixel(x + x_offset + c, y + 7 + r, 0, 191, 255); // Deep Sky Blue rain
          }
        }
      }
    } else if (type == WEATHER_SUN_CLOUD) {
      // Draw Sun in background (stationary, but rays alternate)
      bool frame = (tick / 5) % 2;
      const char *sun_a[] = {
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
      const char *sun_b[] = {
        ".....Y.Y.Y....",
        "....YYYYYY....",
        "...YYYYYYYY...",
        "Y.YYYYYYYYYY.Y",
        "..YYYYYYYYYY..",
        "..YYYYYYYYYY..",
        "Y.YYYYYYYYYY.Y",
        "...YYYYYYYY...",
        "....YYYYYY....",
        "....Y.Y.Y.....",
      };
      const char **sun = frame ? sun_b : sun_a;
      for (int r = 0; r < 10; ++r) {
        for (int c = 0; sun[r][c] != '\0'; ++c) {
          if (sun[r][c] == 'Y') {
            offscreen_->SetPixel(x - 2 + c, y - 2 + r, 255, 200, 0); // Gold/Yellow (sun)
          }
        }
      }

      // Draw Cloud floating in foreground
      int drift[] = {0, 0, 1, 1, 2, 2, 1, 1, 0, 0, -1, -1, -2, -2, -1, -1};
      int x_offset = drift[(tick / 8) % 16];

      const char *cloud[] = {
        "......CCCC......",
        "....CCCCCCCCC...",
        "...CCCCCCCCCCC..",
        "..CCCCCCCCCCCCC.",
        ".CCCCCCCCCCCCCCC",
        "CCCCCCCCCCCCCCCC",
        "DDDDDDDDDDDDDDDD",
      };
      for (int r = 0; r < 7; ++r) {
        for (int c = 0; cloud[r][c] != '\0'; ++c) {
          if (cloud[r][c] == 'C') {
            offscreen_->SetPixel(x + 2 + x_offset + c, y + 3 + r, 220, 220, 220); // Light Grey
          } else if (cloud[r][c] == 'D') {
            offscreen_->SetPixel(x + 2 + x_offset + c, y + 3 + r, 130, 130, 130); // Shadow
          }
        }
      }
    } else if (type == WEATHER_SNOW) {
      // Float cloud
      int drift[] = {0, 0, 1, 1, 2, 2, 1, 1, 0, 0, -1, -1, -2, -2, -1, -1};
      int x_offset = drift[(tick / 8) % 16];

      const char *cloud_sprite[] = {
        "......CCCC......",
        "....CCCCCCCCC...",
        "...CCCCCCCCCCC..",
        "..CCCCCCCCCCCCC.",
        ".CCCCCCCCCCCCCCC",
        "CCCCCCCCCCCCCCCC",
        "DDDDDDDDDDDDDDDD",
      };
      for (int r = 0; r < 7; ++r) {
        for (int c = 0; cloud_sprite[r][c] != '\0'; ++c) {
          if (cloud_sprite[r][c] == 'C') {
            offscreen_->SetPixel(x + x_offset + c, y + r, 220, 220, 220); // Light Grey
          } else if (cloud_sprite[r][c] == 'D') {
            offscreen_->SetPixel(x + x_offset + c, y + r, 130, 130, 130); // Shadow
          }
        }
      }

      // Animate snow falling down slowly (4 frames)
      int snow_frame = (tick / 4) % 4;
      const char *snow_sprite[] = {
        "....W....W....W.",
        "..W....W....W...",
        "......W....W....",
        ".W....W....W....",
      };
      for (int r = 0; r < 3; ++r) {
        int src_row = (r - snow_frame + 4) % 4;
        for (int c = 0; snow_sprite[src_row][c] != '\0'; ++c) {
          if (snow_sprite[src_row][c] == 'W') {
            offscreen_->SetPixel(x + x_offset + c, y + 7 + r, 255, 255, 255); // Pure White snow
          }
        }
      }
    } else if (type == WEATHER_THUNDER) {
      // Float dark storm cloud
      int drift[] = {0, 0, 1, 1, 2, 2, 1, 1, 0, 0, -1, -1, -2, -2, -1, -1};
      int x_offset = drift[(tick / 8) % 16];

      const char *cloud_sprite[] = {
        "......CCCC......",
        "....CCCCCCCCC...",
        "...CCCCCCCCCCC..",
        "..CCCCCCCCCCCCC.",
        ".CCCCCCCCCCCCCCC",
        "CCCCCCCCCCCCCCCC",
        "DDDDDDDDDDDDDDDD",
      };
      for (int r = 0; r < 7; ++r) {
        for (int c = 0; cloud_sprite[r][c] != '\0'; ++c) {
          if (cloud_sprite[r][c] == 'C') {
            offscreen_->SetPixel(x + x_offset + c, y + r, 100, 100, 110); // Dark storm cloud
          } else if (cloud_sprite[r][c] == 'D') {
            offscreen_->SetPixel(x + x_offset + c, y + r, 60, 60, 70); // Darker shadow
          }
        }
      }

      // Flash lightning bolt
      bool flash = (tick % 12 == 0) || (tick % 12 == 2); // natural double flash
      if (flash) {
        const char *lightning[] = {
          ".......Y........",
          "......YY........",
          ".....YYY........",
          ".......Y........",
          "......Y.........",
        };
        for (int r = 0; r < 5; ++r) {
          for (int c = 0; lightning[r][c] != '\0'; ++c) {
            if (lightning[r][c] == 'Y') {
              offscreen_->SetPixel(x + x_offset + c, y + 7 + r, 255, 255, 0); // Bright Yellow lightning
            }
          }
        }
      }
    } else if (type == WEATHER_FOG) {
      // Draw 4 drifting fog/mist bands
      int drift1 = ((tick / 4) % 12) - 6; // range -6 to 5
      int drift2 = -(((tick / 6) % 12) - 6);
      int drift3 = ((tick / 5) % 10) - 5;
      
      // Fog band 1
      for (int c = 2; c < 14; ++c) {
        offscreen_->SetPixel(x + drift1 + c, y + 2, 160, 160, 160);
      }
      // Fog band 2
      for (int c = 0; c < 16; ++c) {
        offscreen_->SetPixel(x + drift2 + c, y + 5, 120, 120, 120);
      }
      // Fog band 3
      for (int c = 3; c < 13; ++c) {
        offscreen_->SetPixel(x + drift3 + c, y + 8, 160, 160, 160);
      }
      // Fog band 4
      for (int c = 1; c < 15; ++c) {
        offscreen_->SetPixel(x + drift1 + c, y + 11, 100, 100, 100);
      }
    }
  }

  // Renders the weather visual information
  void RenderFrame(bool ok, const WeatherReading &reading,
                   const std::string &error_message, bool toggle_tomorrow, int tick) {
    offscreen_->Fill(0, 0, 0); // Clear screen

    // =========================
    // 🕒 GROSSE UHR OBEN
    // =========================
    std::string current_time = GetCurrentTime();
    int clock_text_width = MeasureText(clock_font, current_time.c_str());
    int x_clock = (128 - clock_text_width) / 2; // Centered on the middle of the two boards (0-127)
    int y_clock = 15; // baseline y-coordinate

    DrawText(offscreen_, clock_font,
             x_clock,
             y_clock,
             Color(255, 255, 255),
             NULL,
             current_time.c_str());

    // =========================
    // ❌ ERROR STATE
    // =========================
    if (!ok) {
      DrawLineText(2, clock_font.height() + 2,
                   Color(255, 0, 0), "Weather Error");
      DrawLineText(2, clock_font.height() + font_.height() + 4,
                   Color(255, 255, 0), station_abbr_);
      DrawLineText(2, clock_font.height() + font_.height() * 2 + 6,
                   Color(255, 255, 255), error_message);
      return;
    }

    // =========================
    // 🧱 WEATHER COLUMNS (NOW and TOMORROW side-by-side on the first panel)
    // =========================
    const int y_icon = 17;
    const int y_name = 31;
    const int y_temp = 38;

    // NOW Column (centered at x = 12)
    DrawWeatherIcon(12 - 8, y_icon, GetWeatherType(reading.current_code), tick);
    DrawCenteredText(12, y_name, Color(0, 255, 255), "NOW");
    DrawCenteredText(12, y_temp, Color(255, 255, 255), reading.current_temp + "\xc2\xb0" "C");

    // TOMORROW Column (centered at x = 43)
    DrawWeatherIcon(43 - 8, y_icon, GetWeatherType(reading.tomorrow_code), tick);
    DrawCenteredText(43, y_name, Color(0, 255, 255), "TOMORROW");
    
    std::string tom_temp = reading.tomorrow_min + "-" + reading.tomorrow_max + "\xc2\xb0" "C";
    DrawCenteredText(43, y_temp, Color(255, 255, 255), tom_temp);
  }

  static std::string GetCurrentTime() {
    time_t now = time(nullptr);
    struct tm *lt = localtime(&now);

    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d",
             lt->tm_hour,
             lt->tm_min);

    return std::string(buf);
  }

  RGBMatrix *const matrix_;
  FrameCanvas *offscreen_;
  std::string station_abbr_;
  std::string font_file_;
  Font font_;
  Font clock_font;
};

}  // namespace

// Factory function
DemoRunner *CreateMeteoSwissWeather(RGBMatrix *matrix,
                                    const std::string &station_abbr) {
  return new MeteoSwissWeather(matrix, station_abbr);
}