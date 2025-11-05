#ifndef ESSENTIA_STREAMING_HITGATEONSETVECTOR_H
#define ESSENTIA_STREAMING_HITGATEONSETVECTOR_H

/*
 * HitGateOnsetVector (streaming)
 * - Vectorized per-band onset gating for mel (or any) band energies
 * - Causal, per-frame processing with adaptive thresholds (median + k*MAD)
 * - Edge-triggered detections with refractory periods per band
 *
 * Input:  TOKEN stream of std::vector<Real> per frame (length = numBands)
 * Output: TOKEN stream of std::vector<Real> per frame (0.0 or 1.0 per band)
 */

#include "streamingalgorithm.h"
#include <essentia/algorithmfactory.h>
#include <vector>
#include <algorithm>
#include <cmath>

namespace essentia {
namespace streaming {

class HitGateOnsetVector : public Algorithm {
public:
  HitGateOnsetVector();

  void declareParameters() {
    declareParameter("method", "Onset detection method", "{hfc,complex,complex_phase,flux,melflux,rms}", "hfc");
    declareParameter("threshold", "MAD multiplier (adaptive) or fixed fallback", "[0,10]", 1.0);
    declareParameter("refractory", "Refractory period in frames", "[0,inf)", 6);
    declareParameter("warmup", "Frames before detection enabled", "[0,inf)", 10);
    declareParameter("sensitivity", "Detection sensitivity multiplier", "[0.1,10]", 1.0);
    declareParameter("smooth_window", "Smoothing window size for detection function", "[1,inf)", 3);
    declareParameter("odf_window", "Rolling window (frames) for adaptive thresholding", "[8,inf)", 64);
  }

  void configure();
  AlgorithmStatus process();
  void reset();

  static const char* name;
  static const char* category;
  static const char* description;

private:
  // TOKEN-style connectors
  Sink<std::vector<Real>>   _in;
  Source<std::vector<Real>> _out;

  // Parameters
  std::string _method{"hfc"};
  Real _threshold{1.0f};
  int _refractory{6};
  int _warmup{10};
  Real _sensitivity{1.0f};
  int _smoothWindow{3};
  int _odfWindow{64};

  // Derived / state per band
  size_t _numBands{0};
  std::vector<int> _refCountPerBand;
  int _framesSeen{0};
  bool _detectionEnabled{false};

  // Histories per band
  std::vector<std::vector<Real>> _odfHistoryPerBand;      // for smoothing
  std::vector<std::vector<Real>> _odfThreshHistoryPerBand; // for adaptive thresholding
  std::vector<Real> _prevSmoothedPerBand;
  std::vector<bool> _wasAbovePerBand;

  // Helpers
  void ensureBandState(size_t numBands);
  Real smoothODF(std::vector<Real>& history, Real odfValue) const;
  std::pair<Real, Real> computeMedianAndMAD(const std::vector<Real>& values) const;
};

} // namespace streaming
} // namespace essentia

#endif // ESSENTIA_STREAMING_HITGATEONSETVECTOR_H


