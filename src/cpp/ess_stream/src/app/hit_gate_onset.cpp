#include "hit_gate_onset.h"
#include <essentia/algorithmfactory.h>
#include <algorithm>
#include <vector>

namespace essentia {
namespace streaming {

const char* HitGateOnset::name = "HitGateOnset";
const char* HitGateOnset::category = "Streaming";
const char* HitGateOnset::description =
  "Hit detector using Essentia's onset detection algorithms to identify percussive events.\n"
  "Supports multiple onset detection methods with configurable sensitivity and thresholds.";

HitGateOnset::HitGateOnset() : Algorithm() {
  declareInput (_in,  "in",  "frequency band energy (scalar per frame)");
  declareOutput(_out, "out", "hit detection (scalar; 0 or 1)");
  // TOKEN mode: one token per call
  _in.setAcquireSize(1);
  _out.setAcquireSize(1);
  _in.setReleaseSize(1);
  _out.setReleaseSize(1);
}

void HitGateOnset::configure() {
  _method = parameter("method").toString();
  _threshold = parameter("threshold").toReal();
  _refractory = parameter("refractory").toInt();
  _warmup = parameter("warmup").toInt();
  _sensitivity = parameter("sensitivity").toReal();
  _smoothWindow = parameter("smooth_window").toInt();
  _odfWindow = parameter("odf_window").toInt();

  reset();
}

void HitGateOnset::reset() {
  Algorithm::reset();
  _odfHistory.clear();
  _odfHistory.reserve(_smoothWindow * 2);
  _odfThreshHistory.clear();
  _odfThreshHistory.reserve(_odfWindow * 2);
  _refCount = 0;
  _framesSeen = 0;
  _detectionEnabled = false;
  _prevSmoothed = 0.0f;
  _wasAbove = false;
}

AlgorithmStatus HitGateOnset::process() {
  AlgorithmStatus status = acquireData();
  if (status != OK) return status;

  const std::vector<Real>& inBuf = _in.tokens();
  std::vector<Real>& outBuf = _out.tokens();

  const Real currentFrame = inBuf[0];
  ++_framesSeen;

  // Update refractory period
  if (_refCount > 0) --_refCount;

  Real hit = 0.0f;

  // Enable detection after warmup period
  if (_framesSeen >= _warmup) {
    _detectionEnabled = true;
  }

  // Perform onset detection if enabled
  if (_detectionEnabled)
  {
    // Since we're working with frequency band energy (scalar), we'll implement
    // a simplified onset detection based on energy changes and peaks
    
    // Calculate onset detection function value
    Real odfValue = 0.0f;
    
    if (_method == "hfc" || _method == "flux") {
      // High Frequency Content / Flux: detect rapid increases in energy
      if (_odfHistory.size() >= 2) {
        Real current = currentFrame;
        Real previous = _odfHistory.back();
        odfValue = std::max(0.0f, current - previous);
      }
    } else if (_method == "rms") {
      // RMS-based: use current frame energy as ODF
      odfValue = currentFrame;
    } else {
      // Default: use energy difference
      if (_odfHistory.size() >= 1) {
        Real current = currentFrame;
        Real previous = _odfHistory.back();
        odfValue = std::max(0.0f, current - previous);
      }
    }
    
    // Apply smoothing to the ODF
    Real smoothedODF = smoothODF(odfValue);

    // Maintain rolling history for adaptive threshold
    _odfThreshHistory.push_back(smoothedODF);
    if ((int)_odfThreshHistory.size() > _odfWindow)
    {
      _odfThreshHistory.erase(_odfThreshHistory.begin());
    }

    // Adaptive threshold: median + k * MAD
    Real dynamicThreshold = _threshold; // fallback
    if (_odfThreshHistory.size() >= 8)
    {
      auto stats = computeMedianAndMAD(_odfThreshHistory);
      Real median = stats.first;
      Real mad = stats.second;
      // Use _threshold as multiplier (k), clamp MAD floor to avoid zero
      Real madFloor = std::max(1e-6f, mad);
      dynamicThreshold = median + (_threshold > 0 ? _threshold : 0.3f) * madFloor;
    }

    // Edge-triggered peak pick: crossing above threshold while rising
    bool above = smoothedODF > dynamicThreshold;
    bool rising = smoothedODF >= _prevSmoothed;
    if (above && !_wasAbove && rising)
    {
      hit = 1.0f;
    }
    _wasAbove = above;
    _prevSmoothed = smoothedODF;
  }

  // modify the refractory logic, we want to always compute just not output a hit if it exists inside the refractory period
  // keeps track of history well
  // check the impact of this.
  if (_refCount != 0)
  {
    hit = 0.0f;
  }
  if (hit == 1.0f)
  {
    _refCount = _refractory;
  }

  outBuf[0] = hit;

  releaseData();
  return OK;
}

Real HitGateOnset::smoothODF(Real odfValue) {
  // Add current ODF value to history
  _odfHistory.push_back(odfValue);
  
  // Maintain smoothing window size
  if (_odfHistory.size() > _smoothWindow) {
    _odfHistory.erase(_odfHistory.begin());
  }
  
  // Calculate smoothed ODF (simple moving average)
  if (_odfHistory.empty()) return 0.0f;
  
  Real sum = 0.0f;
  for (Real value : _odfHistory) {
    sum += value;
  }
  
  return sum / _odfHistory.size();
}

bool HitGateOnset::detectOnset(Real odfValue) {
  // Apply sensitivity scaling
  Real scaledODF = odfValue * _sensitivity;
  
  // Simple threshold-based detection
  // In a more sophisticated implementation, we could use peak detection
  // or adaptive thresholds based on the ODF history
  return scaledODF > _threshold;
}

std::pair<Real, Real> HitGateOnset::computeMedianAndMAD(const std::vector<Real> &values) const
{
  if (values.empty())
    return {0.0f, 0.0f};
  std::vector<Real> copyVals(values.begin(), values.end());
  size_t n = copyVals.size();
  size_t mid = n / 2;
  std::nth_element(copyVals.begin(), copyVals.begin() + mid, copyVals.end());
  Real median = copyVals[mid];
  if ((n % 2) == 0)
  {
    auto maxLower = *std::max_element(copyVals.begin(), copyVals.begin() + mid);
    median = (median + maxLower) * 0.5f;
  }
  // Compute absolute deviations
  std::vector<Real> devs;
  devs.reserve(n);
  for (Real v : values)
    devs.push_back(std::abs(v - median));
  size_t midDev = devs.size() / 2;
  std::nth_element(devs.begin(), devs.begin() + midDev, devs.end());
  Real mad = devs[midDev];
  if ((devs.size() % 2) == 0)
  {
    auto maxLowerDev = *std::max_element(devs.begin(), devs.begin() + midDev);
    mad = (mad + maxLowerDev) * 0.5f;
  }
  return {median, mad};
}

} // namespace streaming
} // namespace essentia
