#ifndef ESSENTIA_STREAMING_HITGATEONSET_H
#define ESSENTIA_STREAMING_HITGATEONSET_H

/*
 * HitGateOnset (streaming)
 * - Uses Essentia's built-in onset detection algorithms to detect percussive events
 * - Leverages OnsetDetection streaming algorithm with configurable methods
 * - Provides threshold-based gating on onset detection function values
 * - Supports multiple onset detection methods: hfc, complex, flux, melflux, rms
 * - Configurable sensitivity and refractory period
 *
 * Input:  TOKEN stream of Real (scalar) per frame (frequency band energy)
 * Output: TOKEN stream of Real (scalar) per frame (0.0 or 1.0)
 */

#include "streamingalgorithm.h"
#include <essentia/algorithmfactory.h>
#include <vector>
#include <algorithm>
#include <cmath>

namespace essentia {
namespace streaming {

class HitGateOnset : public Algorithm {
public:
  HitGateOnset();

  void declareParameters() {
    declareParameter("method", "Onset detection method", "{hfc,complex,complex_phase,flux,melflux,rms}", "hfc");
    declareParameter("threshold", "Detection threshold or MAD multiplier (adaptive)", "[0,10]", 0.3);
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
  Sink<Real>   _in;
  Source<Real> _out;

  // Parameters
  std::string _method{"hfc"};
  Real _threshold{0.3f};
  int _refractory{6};
  int _warmup{10};
  Real _sensitivity{1.0f};
  int _smoothWindow{3};
  int _odfWindow{64};

  // State
  int _refCount{0};
  int _framesSeen{0};
  bool _detectionEnabled{false};
  
  // Onset detection function history for smoothing
  std::vector<Real> _odfHistory;
  std::vector<Real> _odfThreshHistory; // rolling history for adaptive thresholding
  Real _prevSmoothed{0.0f};
  bool _wasAbove{false};
  
  // Helper functions
  Real smoothODF(Real odfValue);
  bool detectOnset(Real odfValue);
  std::pair<Real, Real> computeMedianAndMAD(const std::vector<Real>& values) const;
};

} // namespace streaming
} // namespace essentia

#endif // ESSENTIA_STREAMING_HITGATEONSET_H
