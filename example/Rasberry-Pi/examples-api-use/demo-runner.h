// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-

#ifndef DEMO_RUNNER_H_
#define DEMO_RUNNER_H_

#include "led-matrix.h"

extern volatile bool interrupt_received;

class DemoRunner {
protected:
  DemoRunner(rgb_matrix::Canvas *canvas) : canvas_(canvas) {}
  inline rgb_matrix::Canvas *canvas() { return canvas_; }

public:
  virtual ~DemoRunner() {}
  virtual void Run() = 0;

private:
  rgb_matrix::Canvas *const canvas_;
};

#endif  // DEMO_RUNNER_H_