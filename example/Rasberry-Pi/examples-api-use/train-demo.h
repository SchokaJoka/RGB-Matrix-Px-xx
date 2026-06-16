// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-

#ifndef TRAIN_DEMO_H_
#define TRAIN_DEMO_H_

#include <string>

namespace rgb_matrix {
class RGBMatrix;
}

class DemoRunner;

DemoRunner *CreateTrainDemo(rgb_matrix::RGBMatrix *matrix,
                                    const std::string &station_abbr);

#endif  // TRAIN_DEMO_H_