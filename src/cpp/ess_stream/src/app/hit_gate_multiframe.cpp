#include "hit_gate_multiframe.h"
#include <essentia/algorithmfactory.h>

namespace essentia {
namespace streaming {

const char* HitGateMultiFrame::name = "HitGateMultiFrame";
const char* HitGateMultiFrame::category = "Streaming";
const char* HitGateMultiFrame::description =
  "Multi-frame hit detector that analyzes sliding window of frames to detect frequency spectrum spikes.\n"
  "Supports peak detection, trend analysis, and variance-based detection strategies.";

HitGateMultiFrame::HitGateMultiFrame() : Algorithm() {
  declareInput (_in,  "in",  "frequency band energy (scalar per frame)");
  declareOutput(_out, "out", "hit detection (scalar; 0 or 1)");
  // TOKEN mode: one token per call
  _in.setAcquireSize(1);
  _out.setAcquireSize(1);
  _in.setReleaseSize(1);
  _out.setReleaseSize(1);
}

void HitGateMultiFrame::configure() {
  _windowSize = parameter("window_size").toInt();
  _peakThreshold = parameter("peak_threshold").toReal();
  _trendThreshold = parameter("trend_threshold").toReal();
  _varianceThreshold = parameter("variance_threshold").toReal();
  _refractory = parameter("refractory").toInt();
  _warmup = parameter("warmup").toInt();
  _detectionMode = parameter("detection_mode").toString();
  
  reset();
}

void HitGateMultiFrame::reset() {
  Algorithm::reset();
  _frameHistory.clear();
  _frameHistory.reserve(_windowSize * 2); // Reserve extra space for efficiency
  _refCount = 0;
  _framesSeen = 0;
  _detectionEnabled = false;
}

AlgorithmStatus HitGateMultiFrame::process() {
  AlgorithmStatus status = acquireData();
  if (status != OK) return status;

  const std::vector<Real>& inBuf = _in.tokens();
  std::vector<Real>& outBuf = _out.tokens();

  const Real currentFrame = inBuf[0];
  ++_framesSeen;

  // Add current frame to history
  _frameHistory.push_back(currentFrame);
  
  // Maintain sliding window size
  if (_frameHistory.size() > _windowSize) {
    _frameHistory.erase(_frameHistory.begin());
  }

  // Update refractory period
  if (_refCount > 0) --_refCount;

  Real hit = 0.0f;

  // Enable detection after warmup period
  if (_framesSeen >= _warmup) {
    _detectionEnabled = true;
  }

  // Perform detection if enabled and we have enough history
  if (_detectionEnabled && _refCount == 0 && _frameHistory.size() >= _windowSize) {
    bool detected = false;

    if (_detectionMode == "peak") {
      detected = detectPeak(_frameHistory, currentFrame);
    } else if (_detectionMode == "trend") {
      detected = detectTrend(_frameHistory, currentFrame);
    } else if (_detectionMode == "variance") {
      detected = detectVariance(_frameHistory, currentFrame);
    } else if (_detectionMode == "combined") {
      // Use multiple detection methods for more robust detection
      bool peakDetected = detectPeak(_frameHistory, currentFrame);
      bool trendDetected = detectTrend(_frameHistory, currentFrame);
      bool varianceDetected = detectVariance(_frameHistory, currentFrame);
      
      // Require at least 2 out of 3 detection methods to agree
      int detectionCount = (peakDetected ? 1 : 0) + (trendDetected ? 1 : 0) + (varianceDetected ? 1 : 0);
      detected = detectionCount >= 2;
    }

    if (detected) {
      hit = 1.0f;
      _refCount = _refractory;
    }
  }

  outBuf[0] = hit;

  releaseData();
  return OK;
}

bool HitGateMultiFrame::detectPeak(const std::vector<Real>& history, Real current) {
  if (history.size() < 3) return false;
  
  // Calculate mean and standard deviation of recent frames (excluding current)
  std::vector<Real> recentFrames(history.begin(), history.end() - 1);
  Real mean = calculateMean(recentFrames);
  Real stdDev = calculateStdDev(recentFrames, mean);
  
  if (stdDev < 1e-6f) return false; // Avoid division by zero
  
  // Check if current frame is significantly above recent average
  Real zScore = (current - mean) / stdDev;
  return zScore > _peakThreshold;
}

bool HitGateMultiFrame::detectTrend(const std::vector<Real>& history, Real current) {
  if (history.size() < 4) return false;
  
  // Calculate trend over the window
  Real trend = calculateTrend(history);
  
  // Check if there's a sustained upward trend
  return trend > _trendThreshold;
}

bool HitGateMultiFrame::detectVariance(const std::vector<Real>& history, Real current) {
  if (history.size() < 6) return false;
  
  // Split history into two halves
  int midPoint = history.size() / 2;
  std::vector<Real> firstHalf(history.begin(), history.begin() + midPoint);
  std::vector<Real> secondHalf(history.begin() + midPoint, history.end() - 1);
  
  // Add current frame to second half
  secondHalf.push_back(current);
  
  Real mean1 = calculateMean(firstHalf);
  Real mean2 = calculateMean(secondHalf);
  
  Real var1 = calculateVariance(firstHalf, mean1);
  Real var2 = calculateVariance(secondHalf, mean2);
  
  if (var1 < 1e-6f) return false; // Avoid division by zero
  
  // Check if variance has increased significantly
  Real varianceRatio = var2 / var1;
  return varianceRatio > _varianceThreshold;
}

Real HitGateMultiFrame::calculateMean(const std::vector<Real>& data) {
  if (data.empty()) return 0.0f;
  
  Real sum = std::accumulate(data.begin(), data.end(), 0.0f);
  return sum / data.size();
}

Real HitGateMultiFrame::calculateStdDev(const std::vector<Real>& data, Real mean) {
  if (data.size() < 2) return 0.0f;
  
  Real sumSquaredDiff = 0.0f;
  for (Real value : data) {
    Real diff = value - mean;
    sumSquaredDiff += diff * diff;
  }
  
  return std::sqrt(sumSquaredDiff / (data.size() - 1));
}

Real HitGateMultiFrame::calculateTrend(const std::vector<Real>& data) {
  if (data.size() < 2) return 1.0f;
  
  // Simple linear trend calculation
  // Compare average of first half vs second half
  int midPoint = data.size() / 2;
  
  std::vector<Real> firstHalf(data.begin(), data.begin() + midPoint);
  std::vector<Real> secondHalf(data.begin() + midPoint, data.end());
  
  Real avgFirst = calculateMean(firstHalf);
  Real avgSecond = calculateMean(secondHalf);
  
  if (avgFirst < 1e-6f) return 1.0f; // Avoid division by zero
  
  return avgSecond / avgFirst;
}

Real HitGateMultiFrame::calculateVariance(const std::vector<Real>& data, Real mean) {
  if (data.size() < 2) return 0.0f;
  
  Real sumSquaredDiff = 0.0f;
  for (Real value : data) {
    Real diff = value - mean;
    sumSquaredDiff += diff * diff;
  }
  
  return sumSquaredDiff / (data.size() - 1);
}

} // namespace streaming
} // namespace essentia
