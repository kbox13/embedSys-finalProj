/*
 * Copyright (C) 2006-2025  Music Technology Group - Universitat Pompeu Fabra
 *
 * This file is inspired by Essentia's Trimmer implementation (standard & streaming).
 * It provides a SumRange algorithm in both "standard" and "streaming" APIs.
 */

#ifndef ESSENTIA_SUMRANGE_H
#define ESSENTIA_SUMRANGE_H

#include "algorithm.h"

namespace essentia {
namespace standard {

/**
 * SumRange (standard)
 * Sums elements in [lo, hi] of a single input vector<Real> and outputs a scalar Real.
 */
class SumRange : public Algorithm {

 private:
  Input<std::vector<Real>>  _in;
  Output<Real>              _out;
  int _lo;
  int _hi;

 public:
  SumRange()
    : _lo(0), _hi(0) {
    declareInput(_in,  "in",  "input vector");
    declareOutput(_out, "out", "sum over [lo, hi]");
  }

  void declareParameters() {
    declareParameter("lo", "start index (inclusive)", "[0,inf)", 0);
    declareParameter("hi", "end index (inclusive)",  "[0,inf)", 10);
  }

  void configure();
  void compute();

  static const char* name;
  static const char* category;
  static const char* description;
};

} // namespace standard
} // namespace essentia


#include "streamingalgorithm.h"

namespace essentia {
namespace streaming {

/**
 * SumRange (streaming)
 * For each incoming token of type vector<Real> (size arbitrary),
 * emits one scalar Real = sum(v[lo..hi]) with bounds clamped to the input size.
 *
 * Input  : Sink<std::vector<Real>>  "in"   (1 token per tick)
 * Output : Source<Real>             "out"  (1 token per tick)
 */
class SumRange : public Algorithm {
 protected:
  // One vector token in, one scalar token out, per process() call.
  Sink<std::vector<Real>> _in;
  Source<Real>            _out;

  // Parameter cache
  int _lo;
  int _hi;

 public:
  SumRange()
    : Algorithm(), _lo(0), _hi(0) {
    // Acquire/release exactly one token each tick.
    declareInput(_in, 1, "in",  "input vector");
    declareOutput(_out, 1, "out", "sum over [lo, hi]");
  }

  void declareParameters() {
    declareParameter("lo", "start index (inclusive)", "[0,inf)", 0);
    declareParameter("hi", "end index (inclusive)",  "[0,inf)", 10);
  }

  void configure();
  AlgorithmStatus process();
  void reset();

  static const char* name;
  static const char* category;
  static const char* description;
};

} // namespace streaming
} // namespace essentia

#endif // ESSENTIA_SUMRANGE_H
