#include "hit_gate_onset.h"
#include <essentia/algorithmfactory.h>

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
  
  reset();
}

void HitGateOnset::reset() {
  Algorithm::reset();
  _odfHistory.clear();
  _odfHistory.reserve(_smoothWindow * 2);
  _refCount = 0;
  _framesSeen = 0;
  _detectionEnabled = false;
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
  if (_detectionEnabled && _refCount == 0) {
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
    
    // Detect onset based on threshold
    if (detectOnset(smoothedODF)) {
      hit = 1.0f;
      _refCount = _refractory;
    }
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

} // namespace streaming
} // namespace essentia
