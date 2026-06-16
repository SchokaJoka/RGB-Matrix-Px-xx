// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-

#ifndef WEATHER_DEMO_H_
#define WEATHER_DEMO_H_

#include <string>

namespace rgb_matrix {
class RGBMatrix;
}

class DemoRunner;

DemoRunner *CreateMeteoSwissWeather(rgb_matrix::RGBMatrix *matrix,
                                    const std::string &station_abbr);

#endif  // WEATHER_DEMO_H_