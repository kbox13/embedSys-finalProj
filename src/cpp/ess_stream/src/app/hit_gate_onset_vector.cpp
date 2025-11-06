#include "hit_gate_onset_vector.h"
#include <essentia/algorithmfactory.h>
#include <algorithm>

namespace essentia {
namespace streaming {

const char* HitGateOnsetVector::name = "HitGateOnsetVector";
const char* HitGateOnsetVector::category = "Streaming";
const char* HitGateOnsetVector::description =
  "Vectorized per-band onset gating with adaptive thresholds and edge-triggering.";

HitGateOnsetVector::HitGateOnsetVector() : Algorithm() {
  declareInput (_in,  "in",  "band energies (vector per frame)");
  declareOutput(_out, "out", "per-band hits (vector of 0/1)");
  // TOKEN mode: one token per call
  _in.setAcquireSize(1);
  _out.setAcquireSize(1);
  _in.setReleaseSize(1);
  _out.setReleaseSize(1);
}

void HitGateOnsetVector::configure() {
  _method = parameter("method").toString();
  _threshold = parameter("threshold").toReal();
  _refractory = parameter("refractory").toInt();
  _warmup = parameter("warmup").toInt();
  _sensitivity = parameter("sensitivity").toReal();
  _smoothWindow = parameter("smooth_window").toInt();
  _odfWindow = parameter("odf_window").toInt();

  reset();
}

void HitGateOnsetVector::reset() {
  Algorithm::reset();
  _framesSeen = 0;
  _detectionEnabled = false;
  _numBands = 0; // set on first process() based on input
  _refCountPerBand.clear();
  _odfHistoryPerBand.clear();
  _odfThreshHistoryPerBand.clear();
  _prevSmoothedPerBand.clear();
  _wasAbovePerBand.clear();
}

void HitGateOnsetVector::ensureBandState(size_t numBands) {
  if (_numBands == numBands && !_odfHistoryPerBand.empty()) return;
  _numBands = numBands;
  _refCountPerBand.assign(_numBands, 0);
  _odfHistoryPerBand.assign(_numBands, std::vector<Real>());
  _odfThreshHistoryPerBand.assign(_numBands, std::vector<Real>());
  _prevSmoothedPerBand.assign(_numBands, 0.0f);
  _wasAbovePerBand.assign(_numBands, false);
  for (size_t b = 0; b < _numBands; ++b) {
    _odfHistoryPerBand[b].reserve(_smoothWindow * 2);
    _odfThreshHistoryPerBand[b].reserve(_odfWindow * 2);
  }
}

AlgorithmStatus HitGateOnsetVector::process() {
  AlgorithmStatus status = acquireData();
  if (status != OK) return status;

  const std::vector<Real>& bands = _in.tokens().at(0);
  if (bands.empty()) {
    // pass through empty
    _out.tokens().resize(1);
    _out.tokens()[0].clear();
    releaseData();
    return OK;
  }

  ensureBandState(bands.size());
  ++_framesSeen;
  if (_framesSeen >= _warmup) _detectionEnabled = true;

  std::vector<Real>& hitsVec = _out.tokens();
  if (hitsVec.size() < 1) hitsVec.resize(1);
  hitsVec[0].assign(_numBands, 0.0f);

  // update refractory
  for (size_t b = 0; b < _numBands; ++b) {
    if (_refCountPerBand[b] > 0) --_refCountPerBand[b];
  }

  if (_detectionEnabled) {
    for (size_t b = 0; b < _numBands; ++b) {
      if (_refCountPerBand[b] > 0) {
        // still allow history updates for smoother/thresholds
      }

      // Compute simple ODF per band using delta energy for hfc/flux, rms otherwise
      Real odfValue = 0.0f;
      if (_method == "hfc" || _method == "flux") {
        std::vector<Real>& h = _odfHistoryPerBand[b];
        if (h.size() >= 1) {
          Real prev = h.back();
          odfValue = std::max(0.0f, bands[b] - prev);
        } else {
          odfValue = 0.0f;
        }
      } else if (_method == "rms") {
        odfValue = bands[b];
      } else {
        std::vector<Real>& h = _odfHistoryPerBand[b];
        if (h.size() >= 1) {
          Real prev = h.back();
          odfValue = std::max(0.0f, bands[b] - prev);
        } else {
          odfValue = 0.0f;
        }
      }

      // Smooth ODF
      Real smoothed = smoothODF(_odfHistoryPerBand[b], odfValue);

      // Adaptive threshold history
      std::vector<Real>& th = _odfThreshHistoryPerBand[b];
      th.push_back(smoothed);
      if ((int)th.size() > _odfWindow) th.erase(th.begin());

      // Compute dynamic threshold
      Real dynamicThreshold = _threshold; // fallback
      if (th.size() >= 8) {
        auto stats = computeMedianAndMAD(th);
        Real median = stats.first;
        Real mad = stats.second;
        Real madFloor = std::max(1e-6f, mad);
        dynamicThreshold = median + (_threshold > 0 ? _threshold : 1.0f) * madFloor;
      }

      // Edge-triggered decision
      bool above = smoothed > dynamicThreshold;
      bool rising = smoothed >= _prevSmoothedPerBand[b];
      if (_refCountPerBand[b] == 0 && above && !_wasAbovePerBand[b] && rising) {
        hitsVec[0][b] = 1.0f;
        _refCountPerBand[b] = _refractory;
      }
      _wasAbovePerBand[b] = above;
      _prevSmoothedPerBand[b] = smoothed;
    }
  } else {
    // still advance histories for initial stabilization
    for (size_t b = 0; b < _numBands; ++b) {
      std::vector<Real>& h = _odfHistoryPerBand[b];
      Real odfValue = (h.empty() ? 0.0f : std::max(0.0f, bands[b] - h.back()));
      (void)smoothODF(h, odfValue);
      std::vector<Real>& th = _odfThreshHistoryPerBand[b];
      th.push_back(h.empty() ? 0.0f : h.back());
      if ((int)th.size() > _odfWindow) th.erase(th.begin());
      _prevSmoothedPerBand[b] = (h.empty() ? 0.0f : h.back());
      _wasAbovePerBand[b] = false;
    }
  }

  releaseData();
  return OK;
}

Real HitGateOnsetVector::smoothODF(std::vector<Real>& history, Real odfValue) const {
  history.push_back(odfValue);
  if ((int)history.size() > _smoothWindow) history.erase(history.begin());
  if (history.empty()) return 0.0f;
  Real sum = 0.0f;
  for (Real v : history) sum += v;
  return sum / history.size();
}

std::pair<Real, Real> HitGateOnsetVector::computeMedianAndMAD(const std::vector<Real>& values) const {
  if (values.empty()) return {0.0f, 0.0f};
  std::vector<Real> copyVals(values.begin(), values.end());
  size_t n = copyVals.size();
  size_t mid = n / 2;
  std::nth_element(copyVals.begin(), copyVals.begin() + mid, copyVals.end());
  Real median = copyVals[mid];
  if ((n % 2) == 0) {
    auto maxLower = *std::max_element(copyVals.begin(), copyVals.begin() + mid);
    median = (median + maxLower) * 0.5f;
  }
  std::vector<Real> devs;
  devs.reserve(n);
  for (Real v : values) devs.push_back(std::abs(v - median));
  size_t midDev = devs.size() / 2;
  std::nth_element(devs.begin(), devs.begin() + midDev, devs.end());
  Real mad = devs[midDev];
  if ((devs.size() % 2) == 0) {
    auto maxLowerDev = *std::max_element(devs.begin(), devs.begin() + midDev);
    mad = (mad + maxLowerDev) * 0.5f;
  }
  return {median, mad};
}

} // namespace streaming
} // namespace essentia


