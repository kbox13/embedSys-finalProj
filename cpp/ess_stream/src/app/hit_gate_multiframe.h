#ifndef ESSENTIA_STREAMING_HITGATEMULTIFRAME_H
#define ESSENTIA_STREAMING_HITGATEMULTIFRAME_H

/*
 * HitGateMultiFrame (streaming)
 * - Analyzes multiple frames to detect frequency spectrum spikes
 * - Uses sliding window to compare current frame against recent history
 * - Implements multiple detection strategies:
 *   - Peak detection: current frame significantly higher than recent average
 *   - Trend detection: sustained increase over multiple frames
 *   - Variance detection: sudden increase in signal variance
 * - Configurable window size and sensitivity parameters
 *
 * Input:  TOKEN stream of Real (scalar) per frame (frequency band energy)
 * Output: TOKEN stream of Real (scalar) per frame (0.0 or 1.0)
 */

#include "streamingalgorithm.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace essentia {
namespace streaming {

class HitGateMultiFrame : public Algorithm {
public:
  HitGateMultiFrame();

  void declareParameters() {
    declareParameter("window_size", "Number of frames to analyze", "[2,inf)", 10);
    declareParameter("peak_threshold", "Peak detection threshold (std devs)", "[0,inf)", 2.5);
    declareParameter("trend_threshold", "Trend detection threshold (ratio)", "[1,inf)", 1.5);
    declareParameter("variance_threshold", "Variance spike threshold (ratio)", "[1,inf)", 2.0);
    declareParameter("refractory", "Refractory period in frames", "[0,inf)", 8);
    declareParameter("warmup", "Frames before detection enabled", "[0,inf)", 20);
    declareParameter("detection_mode", "Detection strategy: 'peak', 'trend', 'variance', 'combined'", "{peak,trend,variance,combined}", "combined");
  }

  void configure();
  AlgorithmStatus process();
  void reset();

  static const char* name;
  static const char* category;
  static const char* description;

private:
  // TOKEN-style connectors for scalar values
  Sink<Real>   _in;
  Source<Real> _out;

  // Parameters
  int _windowSize{10};
  Real _peakThreshold{2.5f};
  Real _trendThreshold{1.5f};
  Real _varianceThreshold{2.0f};
  int _refractory{8};
  int _warmup{20};
  std::string _detectionMode{"combined"};

  // State
  std::vector<Real> _frameHistory;
  int _refCount{0};
  int _framesSeen{0};
  bool _detectionEnabled{false};

  // Detection methods
  bool detectPeak(const std::vector<Real>& history, Real current);
  bool detectTrend(const std::vector<Real>& history, Real current);
  bool detectVariance(const std::vector<Real>& history, Real current);
  
  // Helper functions
  Real calculateMean(const std::vector<Real>& data);
  Real calculateStdDev(const std::vector<Real>& data, Real mean);
  Real calculateTrend(const std::vector<Real>& data);
  Real calculateVariance(const std::vector<Real>& data, Real mean);
};

} // namespace streaming
} // namespace essentia

#endif // ESSENTIA_STREAMING_HITGATEMULTIFRAME_H
