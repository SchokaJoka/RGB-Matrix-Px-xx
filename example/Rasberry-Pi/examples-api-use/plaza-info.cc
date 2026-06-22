// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-

#include "led-matrix.h"
#include "graphics.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <sstream>
#include <string>
#include <vector>
#include <unistd.h>
#include <signal.h>
#include <thread>
#include <mutex>
#include <atomic>

using namespace rgb_matrix;

// Global signal interrupt flag
volatile bool interrupt_received = false;
static void InterruptHandler(int signo) {
  interrupt_received = true;
}

// Helper: Convert string to uppercase
static std::string ToUpperCopy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return std::toupper(c); });
  return value;
}

// Run a terminal command and return output
static bool RunCommand(const std::string &command, std::string *output,
                       std::string *error_message) {
  FILE *pipe = popen(command.c_str(), "r");
  if (pipe == NULL) {
    *error_message = "Unable to start command";
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

// ============================================================================
// WEATHER CODE IMPLEMENTATION
// ============================================================================

enum WeatherType {
  WEATHER_SUN,
  WEATHER_CLOUD,
  WEATHER_RAIN,
  WEATHER_SUN_CLOUD,
  WEATHER_SNOW,
  WEATHER_THUNDER,
  WEATHER_FOG
};

static WeatherType GetWeatherType(const std::string &code_str) {
  int code = atoi(code_str.c_str());
  if (code > 100) {
    code = code - 100;
  }
  
  switch (code) {
    case 1:
    case 26:
      return WEATHER_SUN;

    case 2:
    case 3:
      return WEATHER_SUN_CLOUD;
      
    case 7:
    case 8:
    case 14:
    case 18:
    case 29:
    case 30:
    case 31:
    case 32:
    case 33:
    case 34:
    case 35:
      return WEATHER_RAIN;

    case 9:
    case 10:
    case 15:
    case 16:
    case 19:
    case 20:
      return WEATHER_SNOW;

    case 11:
    case 12:
    case 13:
    case 21:
    case 22:
    case 39:
    case 40:
      return WEATHER_THUNDER;

    case 6:
      return WEATHER_FOG;
      
    default:
      return WEATHER_CLOUD;
  }
}

struct WeatherReading {
  std::string station_name;
  std::string current_temp;
  std::string current_time;
  std::string current_code;
  std::string tomorrow_min;
  std::string tomorrow_max;
  std::string tomorrow_code;
};

static std::string GetDateStringOffset(int days_offset) {
  time_t t = time(NULL);
  t += days_offset * 24 * 3600;
  struct tm *tm_info = localtime(&t);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y%m%d", tm_info);
  return std::string(buf);
}

static bool DiscoverLatestRun(std::string *item_id, std::string *latest_run, std::string *error_message) {
  std::string json;
  bool fetched = false;
  
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
  
  size_t pos = 0;
  std::string max_run = "";
  while ((pos = json.find("vnut12.lssw.", pos)) != std::string::npos) {
    pos += 12;
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

static bool FetchParameterRow(const std::string &item_id, const std::string &latest_run,
                              const std::string &param, const std::string &point_id,
                              const std::string &point_type_id, bool is_by_date,
                              const std::string &max_date, std::string *result_rows,
                              std::string *error_message) {
  std::string url = "https://data.geo.admin.ch/ch.meteoschweiz.ogd-local-forecasting/" + item_id + "/vnut12.lssw." + latest_run + "." + param + ".csv";
  std::string cmd = "curl -fsSL -k --compressed --connect-timeout 10 -m 30 \"" + url + "\" | awk -F';' -v pid=\"" + point_id + "\" -v ptype=\"" + point_type_id + "\" -v is_by_date=" + (is_by_date ? "1" : "0") + " -v max_date=\"" + max_date + "\" 'NR == 1 { next } is_by_date && $3 > max_date { exit } !is_by_date && matched && ($1 != pid || $2 != ptype) { exit } $1 == pid && $2 == ptype { print; matched = 1 }' || true";
  
  result_rows->clear();
  std::string cmd_err;
  if (!RunCommand(cmd, result_rows, &cmd_err)) {
    *error_message = "Failed to download/filter " + param;
    return false;
  }
  return true;
}

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

static std::string FormatUtcToLocalTime(const std::string &utc_date_str) {
  time_t epoch = ParseDateToEpoch(utc_date_str);
  struct tm *local_tm = localtime(&epoch);
  char buf[16];
  strftime(buf, sizeof(buf), "%H:%M", local_tm);
  return std::string(buf);
}

static std::string RoundTempString(const std::string &val_str) {
  if (val_str.empty()) return "";
  char *endptr;
  double val = strtod(val_str.c_str(), &endptr);
  if (endptr == val_str.c_str()) {
    return val_str;
  }
  int rounded = (int)round(val);
  return std::to_string(rounded);
}

// Maps input parameter (e.g. LUZ) to point details and train station name
static void MapAbbrToInfo(const std::string &abbr,
                          std::string *weather_point_id,
                          std::string *weather_point_type_id,
                          std::string *weather_display_name,
                          std::string *train_station_query) {
  std::string upper = ToUpperCopy(abbr);
  
  // SBB Train departures are always hardcoded to Emmenbrücke
  *train_station_query = "Emmenbrücke";
  
  // Default values: Lucerne fallback
  *weather_point_id = "68";
  *weather_point_type_id = "1";
  *weather_display_name = "Luzern";
  
  if (upper == "LUZ") {
    *weather_point_id = "68";
    *weather_point_type_id = "1";
    *weather_display_name = "Luzern";
  } else if (upper == "BAS") {
    *weather_point_id = "75";
    *weather_point_type_id = "1";
    *weather_display_name = "Basel";
  } else if (upper == "BER") {
    *weather_point_id = "78";
    *weather_point_type_id = "1";
    *weather_display_name = "Bern";
  } else if (upper == "ZRH" || upper == "ZEH" || upper == "SMA") {
    *weather_point_id = "71";
    *weather_point_type_id = "1";
    *weather_display_name = "Zurich";
  } else if (upper == "GVE") {
    *weather_point_id = "58";
    *weather_point_type_id = "1";
    *weather_display_name = "Geneva";
  } else if (upper == "LUG") {
    *weather_point_id = "47";
    *weather_point_type_id = "1";
    *weather_display_name = "Lugano";
  } else {
    bool is_digits = true;
    for (char c : upper) {
      if (!isdigit(c)) { is_digits = false; break; }
    }
    if (is_digits && !upper.empty()) {
      if (upper.length() == 4) {
        *weather_point_id = upper + "00";
        *weather_point_type_id = "2";
      } else {
        *weather_point_id = upper;
        *weather_point_type_id = "1";
      }
      *weather_display_name = "POI " + upper;
    }
  }
}

static bool FetchWeatherReading(const std::string &station_abbr,
                                WeatherReading *reading,
                                std::string *error_message) {
  printf("MeteoSwiss API: Starting weather fetch for %s...\n", station_abbr.c_str());
  fflush(stdout);
  
  std::string point_id, point_type_id, display_name, train_station_query;
  MapAbbrToInfo(station_abbr, &point_id, &point_type_id, &display_name, &train_station_query);
  
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
  
  time_t now = time(NULL);
  
  struct tm *utc_now = gmtime(&now);
  char cur_utc[16];
  snprintf(cur_utc, sizeof(cur_utc), "%04d%02d%02d%02d00",
           utc_now->tm_year + 1900, utc_now->tm_mon + 1, utc_now->tm_mday, utc_now->tm_hour);
  std::string target_utc_str(cur_utc);
  
  time_t max_hourly_time = now + 6 * 3600;
  struct tm *utc_max_hourly = gmtime(&max_hourly_time);
  char max_hourly_buf[16];
  snprintf(max_hourly_buf, sizeof(max_hourly_buf), "%04d%02d%02d%02d00",
           utc_max_hourly->tm_year + 1900, utc_max_hourly->tm_mon + 1, utc_max_hourly->tm_mday, utc_max_hourly->tm_hour);
  std::string max_hourly_str(max_hourly_buf);
  
  time_t tomorrow = now + 24 * 3600;
  struct tm *local_tomorrow = localtime(&tomorrow);
  char tom_local[16];
  strftime(tom_local, sizeof(tom_local), "%Y%m%d0000", local_tomorrow);
  std::string target_tom_str(tom_local);

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

// ============================================================================
// TRAIN CODE IMPLEMENTATION
// ============================================================================

struct TrainDeparture {
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
                           std::string *error_message) {
  std::string json;
  
  // URL escape spaces in station names
  std::string escaped_station = station;
  size_t start_pos = 0;
  while ((start_pos = escaped_station.find(" ", start_pos)) != std::string::npos) {
    escaped_station.replace(start_pos, 1, "%20");
    start_pos += 3;
  }

  std::string url = "https://transport.opendata.ch/v1/stationboard?station=" + escaped_station + "&limit=4";
  std::string cmd = "curl -fsSL -k --connect-timeout 10 -m 20 \"" + url + "\"";

  printf("SBB API: Starting train fetch for %s...\n", station.c_str());
  fflush(stdout);

  if (!RunCommand(cmd, &json, error_message)) {
    printf("SBB API: Fetch failed: %s\n", error_message->c_str());
    fflush(stdout);
    return false;
  }

  size_t pos = 0;
  while (true) {
    size_t key = json.find("\"to\":\"", pos);
    if (key == std::string::npos)
      break;

    key += 6; // Length of "to":"
    size_t end = json.find("\"", key);
    if (end == std::string::npos)
      break;

    TrainDeparture d;
    d.direction = json.substr(key, end - key);
    d.has_direction = true;

    // --- FIND departure ---
    size_t dep = json.find("\"departure\":\"", end);
    if (dep != std::string::npos) {
      dep += 13;
      size_t dep_end = json.find("\"", dep);
      if (dep_end != std::string::npos) {
        d.departure_time = json.substr(dep, dep_end - dep);
        d.has_time = true;
      }
    }

    // --- FIND platform ---
    size_t plat = json.find("\"platform\":\"", end);
    if (plat != std::string::npos) {
      plat += 12;
      size_t plat_end = json.find("\"", plat);
      if (plat_end != std::string::npos) {
        d.platform = json.substr(plat, plat_end - plat);
        d.has_platform = true;
      }
    }

    // --- FIND delay ---
    size_t del = json.find("\"delay\":", end);
    if (del != std::string::npos) {
      del += 8;
      size_t del_end = json.find(",", del);
      if (del_end != std::string::npos) {
        d.delay = json.substr(del, del_end - del);
        d.has_delay = true;
      }
    }

    out->push_back(d);
    pos = end;
  }

  printf("SBB API: Success. Fetched %zu departures.\n", out->size());
  fflush(stdout);
  return !out->empty();
}

// ============================================================================
// CONCURRENCY & APP STATE MANAGEMENT
// ============================================================================

struct AppState {
  std::mutex mutex;
  
  // Weather
  WeatherReading weather;
  bool weather_ok = false;
  std::string weather_err;
  
  // Trains
  std::vector<TrainDeparture> trains;
  bool trains_ok = false;
  std::string trains_err;
};

static void WeatherFetchWorker(const std::string &station_abbr, AppState *state) {
  while (!interrupt_received) {
    WeatherReading reading;
    std::string error_message;
    bool ok = FetchWeatherReading(station_abbr, &reading, &error_message);
    
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      state->weather = reading;
      state->weather_ok = ok;
      state->weather_err = error_message;
    }
    
    // Sleep for 10 minutes, waking up to check interrupt periodically
    for (int i = 0; i < 600 && !interrupt_received; ++i) {
      usleep(1000 * 1000);
    }
  }
}

static void TrainFetchWorker(const std::string &train_station_query, AppState *state) {
  while (!interrupt_received) {
    std::vector<TrainDeparture> departures;
    std::string error_message;
    bool ok = FetchTrainData(train_station_query, &departures, &error_message);
    
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      state->trains = departures;
      state->trains_ok = ok;
      state->trains_err = error_message;
    }
    
    // Sleep for 30 seconds, waking up to check interrupt periodically
    for (int i = 0; i < 30 && !interrupt_received; ++i) {
      usleep(1000 * 1000);
    }
  }
}

// ============================================================================
// DRAWING HELPERS
// ============================================================================

static void DrawLineText(FrameCanvas *canvas, const Font &font, int x, int y, const Color &color, const std::string &text) {
  DrawText(canvas, font, x, y + font.baseline(), color, NULL, text.c_str(), 0);
}

static void DrawCenteredText(FrameCanvas *canvas, const Font &font, int center_x, int y, const Color &color, const std::string &text) {
  int text_width = MeasureText(font, text.c_str());
  int x = center_x - text_width / 2;
  DrawText(canvas, font, x, y + font.baseline(), color, NULL, text.c_str(), 0);
}

static void DrawWeatherIcon(FrameCanvas *offscreen, int x, int y, WeatherType type, int tick) {
  static const int drift[] = {0, 0, 1, 1, 2, 2, 1, 1, 0, 0, -1, -1, -2, -2, -1, -1};
  if (type == WEATHER_SUN) {
    bool frame = (tick / 10) % 2;
    static const char *sprite_a[] = {
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
    static const char *sprite_b[] = {
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
          offscreen->SetPixel(x + c, y + r, 255, 215, 0); // Gold/Yellow
        }
      }
    }
  } else if (type == WEATHER_CLOUD) {
    int x_offset = drift[(tick / 16) % 16];
    static const char *sprite[] = {
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
          offscreen->SetPixel(x + x_offset + c, y + r, 220, 220, 220); // Light Grey
        } else if (sprite[r][c] == 'D') {
          offscreen->SetPixel(x + x_offset + c, y + r, 130, 130, 130); // Shadow
        }
      }
    }
  } else if (type == WEATHER_RAIN) {
    int x_offset = drift[(tick / 16) % 16];
    static const char *cloud_sprite[] = {
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
          offscreen->SetPixel(x + x_offset + c, y + r, 200, 200, 200); // Grey cloud
        } else if (cloud_sprite[r][c] == 'D') {
          offscreen->SetPixel(x + x_offset + c, y + r, 110, 110, 110); // Shadow
        }
      }
    }

    int rain_frame = (tick / 4) % 3;
    static const char *rain_sprite[] = {
      "....B....B....B.",
      "...B....B....B..",
      "..B....B....B...",
    };
    for (int r = 0; r < 3; ++r) {
      int src_row = (r - rain_frame + 3) % 3;
      for (int c = 0; rain_sprite[src_row][c] != '\0'; ++c) {
        if (rain_sprite[src_row][c] == 'B') {
          offscreen->SetPixel(x + x_offset + c, y + 7 + r, 0, 191, 255); // Deep Sky Blue rain
        }
      }
    }
  } else if (type == WEATHER_SUN_CLOUD) {
    bool frame = (tick / 10) % 2;
    static const char *sun_a[] = {
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
    static const char *sun_b[] = {
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
          offscreen->SetPixel(x - 2 + c, y - 2 + r, 255, 200, 0); // Gold/Yellow
        }
      }
    }

    int x_offset = drift[(tick / 16) % 16];
    static const char *cloud[] = {
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
          offscreen->SetPixel(x + 2 + x_offset + c, y + 3 + r, 220, 220, 220); // Light Grey
        } else if (cloud[r][c] == 'D') {
          offscreen->SetPixel(x + 2 + x_offset + c, y + 3 + r, 130, 130, 130); // Shadow
        }
      }
    }
  } else if (type == WEATHER_SNOW) {
    int x_offset = drift[(tick / 16) % 16];
    static const char *cloud_sprite[] = {
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
          offscreen->SetPixel(x + x_offset + c, y + r, 220, 220, 220); // Light Grey
        } else if (cloud_sprite[r][c] == 'D') {
          offscreen->SetPixel(x + x_offset + c, y + r, 130, 130, 130); // Shadow
        }
      }
    }

    int snow_frame = (tick / 8) % 4;
    static const char *snow_sprite[] = {
      "....W....W....W.",
      "..W....W....W...",
      "......W....W....",
      ".W....W....W....",
    };
    for (int r = 0; r < 3; ++r) {
      int src_row = (r - snow_frame + 4) % 4;
      for (int c = 0; snow_sprite[src_row][c] != '\0'; ++c) {
        if (snow_sprite[src_row][c] == 'W') {
          offscreen->SetPixel(x + x_offset + c, y + 7 + r, 255, 255, 255); // White
        }
      }
    }
  } else if (type == WEATHER_THUNDER) {
    int x_offset = drift[(tick / 16) % 16];
    static const char *cloud_sprite[] = {
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
          offscreen->SetPixel(x + x_offset + c, y + r, 100, 100, 110); // Dark cloud
        } else if (cloud_sprite[r][c] == 'D') {
          offscreen->SetPixel(x + x_offset + c, y + r, 60, 60, 70); // Darker shadow
        }
      }
    }

    bool flash = (tick % 24 == 0) || (tick % 24 == 4);
    if (flash) {
      static const char *lightning[] = {
        ".......Y........",
        "......YY........",
        ".....YYY........",
        ".......Y........",
        "......Y.........",
      };
      for (int r = 0; r < 5; ++r) {
        for (int c = 0; lightning[r][c] != '\0'; ++c) {
          if (lightning[r][c] == 'Y') {
            offscreen->SetPixel(x + x_offset + c, y + 7 + r, 255, 255, 0); // Yellow
          }
        }
      }
    }
  } else if (type == WEATHER_FOG) {
    int drift1 = ((tick / 8) % 12) - 6;
    int drift2 = -(((tick / 12) % 12) - 6);
    int drift3 = ((tick / 10) % 10) - 5;
    
    for (int c = 2; c < 14; ++c) {
      offscreen->SetPixel(x + drift1 + c, y + 2, 160, 160, 160);
    }
    for (int c = 0; c < 16; ++c) {
      offscreen->SetPixel(x + drift2 + c, y + 5, 120, 120, 120);
    }
    for (int c = 3; c < 13; ++c) {
      offscreen->SetPixel(x + drift3 + c, y + 8, 160, 160, 160);
    }
    for (int c = 1; c < 15; ++c) {
      offscreen->SetPixel(x + drift1 + c, y + 11, 100, 100, 100);
    }
  }
}

static const rgb_matrix::Color SBB_RED(220, 30, 30);
static const rgb_matrix::Color SBB_WHITE(240, 240, 240);
static const rgb_matrix::Color SBB_BLACK(10, 10, 10);
static const rgb_matrix::Color SBB_GREY(80, 80, 80);
static const rgb_matrix::Color PANTOGRAPH_GREY(150, 150, 150);

static void DrawGirunoSchnauze(FrameCanvas *canvas, int x, int y, bool flipped) {
  static const char *sprite[] = {
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

  for (int row = 0; row < h; ++row) {
    for (int col = 0; col < width; ++col) {
      char pixel = flipped ? sprite[row][width - 1 - col] : sprite[row][col];
      if (pixel == '\0' || pixel == ' ')
        continue;

      Color pColor;
      switch (pixel) {
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

static void DrawGirunoMittelwagen(FrameCanvas *canvas, int x, int y) {
  static const char *sprite[] = {
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

  for (int row = 0; row < h; ++row) {
    for (int col = 0; col < width; ++col) {
      char pixel = sprite[row][col];
      if (pixel == '\0' || pixel == ' ')
        continue;

      Color pColor;
      switch (pixel) {
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

static std::string GetCurrentTime() {
  time_t now = time(nullptr);
  struct tm *lt = localtime(&now);
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", lt->tm_hour, lt->tm_min);
  return std::string(buf);
}

// Print command-line usage
static int usage(const char *progname) {
  fprintf(stderr, "Usage: sudo %s [options] <station-code>\n", progname);
  fprintf(stderr, "Example: sudo %s LUZ\n", progname);
  fprintf(stderr, "\nGeneral LED matrix options:\n");
  fprintf(stderr, "  --led-rows=<rows>        : Panel rows (default: 64)\n");
  fprintf(stderr, "  --led-cols=<cols>        : Panel columns (default: 64)\n");
  fprintf(stderr, "  --led-chain=<chain>      : Number of daisy-chained panels (default: 2)\n");
  fprintf(stderr, "  --led-slowdown-gpio=<n>  : GPIO slowdown for Pi 4/5 (default: 4)\n");
  fprintf(stderr, "  --led-gpio-mapping=<map> : GPIO mapping (e.g. classic, adafruit-hat-pwm)\n");
  return 1;
}

// ============================================================================
// MAIN ENTRYPOINT
// ============================================================================

int main(int argc, char *argv[]) {
  RGBMatrix::Options matrix_options;
  rgb_matrix::RuntimeOptions runtime_opt;

  // Set defaults matching layout needs (128x64 display, chain=2)
  matrix_options.rows = 64;
  matrix_options.cols = 64;
  matrix_options.chain_length = 2;
  matrix_options.parallel = 1;
  matrix_options.disable_hardware_pulsing = true;
  runtime_opt.gpio_slowdown = 4;

  if (!ParseOptionsFromFlags(&argc, &argv, &matrix_options, &runtime_opt)) {
    return usage(argv[0]);
  }

  std::string station_code = "LUZ";
  if (optind < argc) {
    station_code = argv[optind];
  }

  printf("Starting Plaza Info for station parameter: %s\n", station_code.c_str());
  fflush(stdout);

  // Initialize display matrix
  RGBMatrix *matrix = RGBMatrix::CreateFromOptions(matrix_options, runtime_opt);
  if (matrix == NULL) {
    fprintf(stderr, "Error: Failed to create LED matrix.\n");
    return 1;
  }

  FrameCanvas *offscreen = matrix->CreateFrameCanvas();

  // Load fonts
  Font font;
  if (!font.LoadFont("../fonts/4x6.bdf")) {
    fprintf(stderr, "Error: Couldn't load font '../fonts/4x6.bdf'\n");
    delete matrix;
    return 1;
  }
  
  Font clock_font;
  if (!clock_font.LoadFont("../fonts/10x20.bdf")) {
    fprintf(stderr, "Error: Couldn't load clock font '../fonts/10x20.bdf'\n");
    delete matrix;
    return 1;
  }

  // Map inputs for both APIs
  std::string weather_point_id, weather_point_type_id, weather_display_name, train_station_query;
  MapAbbrToInfo(station_code, &weather_point_id, &weather_point_type_id, &weather_display_name, &train_station_query);

  // Initialize shared AppState and start background workers
  AppState app_state;
  std::thread weather_thread(WeatherFetchWorker, station_code, &app_state);
  std::thread train_thread(TrainFetchWorker, train_station_query, &app_state);

  // Set up interrupt signal handlers
  signal(SIGTERM, InterruptHandler);
  signal(SIGINT, InterruptHandler);

  printf("Press <CTRL-C> to exit cleanly and reset LEDs.\n");
  fflush(stdout);

  // Animation and state variables
  int tick = 0;
  
  // Train animation state
  enum TrainState {
    MOVING_RIGHT,
    WAIT_RIGHT,
    MOVING_LEFT,
    WAIT_LEFT
  };
  TrainState train_state = MOVING_RIGHT;
  const int train_total_width = 100;
  int train_x = -train_total_width;
  int train_y = matrix->height() - 15; // 49
  int wait_counter = 0;
  const int wait_frames = 40; // 40 * 50ms = 2s

  // Main UI update loop
  while (!interrupt_received) {
    // Clear back buffer
    offscreen->Fill(0, 0, 0);

    // ==========================================
    // 1. Thread-safe snapshot of latest data
    // ==========================================
    WeatherReading cur_weather;
    bool cur_weather_ok = false;
    std::string cur_weather_err;
    std::vector<TrainDeparture> cur_trains;
    bool cur_trains_ok = false;
    std::string cur_trains_err;
    
    {
      std::lock_guard<std::mutex> lock(app_state.mutex);
      cur_weather = app_state.weather;
      cur_weather_ok = app_state.weather_ok;
      cur_weather_err = app_state.weather_err;
      cur_trains = app_state.trains;
      cur_trains_ok = app_state.trains_ok;
      cur_trains_err = app_state.trains_err;
    }

    // ==========================================
    // 2. Center Clock (Top)
    // ==========================================
    std::string time_str = GetCurrentTime();
    int clock_text_width = MeasureText(clock_font, time_str.c_str());
    int x_clock = (matrix->width() - clock_text_width) / 2; // (128 - 50) / 2 = 39
    int y_clock = 15;
    DrawText(offscreen, clock_font, x_clock, y_clock, Color(255, 255, 255), NULL, time_str.c_str());

    // ==========================================
    // 3. Left Panel (0-63) - Weather Info
    // ==========================================
    if (!cur_weather_ok) {
      // Draw error message on left panel
      DrawLineText(offscreen, font, 2, clock_font.height() + 2, Color(255, 0, 0), "Weather Err");
      DrawLineText(offscreen, font, 2, clock_font.height() + font.height() + 4, Color(255, 255, 0), station_code);
      std::string short_err = cur_weather_err.substr(0, 15);
      DrawLineText(offscreen, font, 2, clock_font.height() + font.height() * 2 + 6, Color(255, 255, 255), short_err);
    } else {
      const int y_icon = 18;
      const int y_name = 30;
      const int y_temp = 38;

      // NOW Column (centered at x = 16)
      DrawWeatherIcon(offscreen, 16 - 8, y_icon, GetWeatherType(cur_weather.current_code), tick);
      DrawCenteredText(offscreen, font, 16, y_name, Color(0, 255, 255), "HEUTE");
      DrawCenteredText(offscreen, font, 16, y_temp, Color(255, 255, 255), cur_weather.current_temp + "\xc2\xb0" "C");

      // TOMORROW Column (centered at x = 48)
      DrawWeatherIcon(offscreen, 48 - 8, y_icon, GetWeatherType(cur_weather.tomorrow_code), tick);
      DrawCenteredText(offscreen, font, 48, y_name, Color(0, 255, 255), "MORGEN");
      DrawCenteredText(offscreen, font, 48, y_temp, Color(255, 255, 255), cur_weather.tomorrow_max + "\xc2\xb0" "C");
    }

    // ==========================================
    // 4. Right Panel (64-127) - Train Board
    // ==========================================
    const int x_base = 64;
    const int x_dest = x_base + 2;   // 66
    const int x_time = x_base + 36;  // 100
    const int x_plat = x_time + 23;  // 123
    
    if (!cur_trains_ok) {
      // Draw error message on right panel
      DrawLineText(offscreen, font, x_base + 2, clock_font.height() + 2, Color(255, 0, 0), "Train Board");
      DrawLineText(offscreen, font, x_base + 2, clock_font.height() + font.height() + 4, Color(255, 255, 0), train_station_query.substr(0, 15));
      std::string short_err = cur_trains_err.substr(0, 15);
      DrawLineText(offscreen, font, x_base + 2, clock_font.height() + font.height() * 2 + 6, Color(255, 255, 255), short_err);
    } else {
      // Toggle between departure time and delay every 200 frames (10 seconds)
      bool show_delay_mode = (tick % 400) >= 200;
      int y = clock_font.height() - 2; // y = 18
      
      for (size_t i = 0; i < cur_trains.size() && i < 4 && y < 52; i++) {
        const auto &t = cur_trains[i];

        std::string dep_time = "--:--";
        if (t.departure_time.size() >= 16) {
          dep_time = t.departure_time.substr(11, 5);
        }

        char dest[64];
        snprintf(dest, sizeof(dest), "%.12s", t.direction.c_str());

        // Direction column (Cyan)
        DrawLineText(offscreen, font, x_dest, y, Color(0, 255, 255), dest);
        
        // Time/Delay column
        if (!show_delay_mode) {
          DrawLineText(offscreen, font, x_time, y, Color(255, 255, 255), dep_time);
        } else {
          std::string delay = (t.has_delay && t.delay != "null" && t.delay != "0") 
                            ? ("+" + t.delay) 
                            : " ";
          DrawLineText(offscreen, font, x_time, y, Color(255, 80, 80), delay);
        }

        // Platform column (Orange)
        DrawLineText(offscreen, font, x_plat, y, Color(255, 200, 0), t.has_platform ? t.platform : "-");

        y += font.height() + 1; // spacing
      }
    }

    // ==========================================
    // 5. SBB Train (Bottom, train_y = 49)
    // ==========================================
    // Draw Giruno (Front-Schnauze, Mittelwagen, Heck-Schnauze)
    DrawGirunoSchnauze(offscreen, train_x, train_y, false);
    DrawGirunoMittelwagen(offscreen, train_x + 36, train_y);
    DrawGirunoSchnauze(offscreen, train_x + 64, train_y, true);

    // Update train state machine
    switch (train_state) {
      case MOVING_RIGHT:
        train_x++;
        if (train_x > matrix->width() + 40) {
          train_state = WAIT_RIGHT;
          wait_counter = 0;
        }
        break;

      case WAIT_RIGHT:
        wait_counter++;
        if (wait_counter > wait_frames) {
          train_state = MOVING_LEFT;
          train_x = matrix->width();
        }
        break;

      case MOVING_LEFT:
        train_x--;
        if (train_x < -train_total_width) {
          train_state = WAIT_LEFT;
          wait_counter = 0;
        }
        break;

      case WAIT_LEFT:
        wait_counter++;
        if (wait_counter > wait_frames) {
          train_state = MOVING_RIGHT;
          train_x = -train_total_width;
        }
        break;
    }

    // Swap double buffers
    offscreen = matrix->SwapOnVSync(offscreen);

    // Sleep for 50ms (Frame time)
    usleep(50 * 1000);
    tick++;
  }

  printf("Clean shutdown requested. Waiting for background threads to join...\n");
  fflush(stdout);
  
  // Wait for worker threads to finish
  if (weather_thread.joinable()) {
    weather_thread.join();
  }
  if (train_thread.joinable()) {
    train_thread.join();
  }

  // Clear screen and clean up matrix
  offscreen->Fill(0, 0, 0);
  matrix->SwapOnVSync(offscreen);
  delete matrix;

  printf("Exiting. Goodbye!\n");
  fflush(stdout);
  return 0;
}
