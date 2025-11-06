#include "instrument_sum.h"
#include <algorithm>
#include <cmath>

namespace essentia {
namespace streaming {

const char* InstrumentSum::name = "InstrumentSum";
const char* InstrumentSum::category = "Streaming";
const char* InstrumentSum::description =
  "Aggregate mel band energies into 5 EDM instrument sums using weighted masks.";

InstrumentSum::InstrumentSum() : Algorithm() {
  declareInput (_in,  "in",  "mel band energies (vector per frame)");
  declareOutput(_out, "out", "instrument sums [Kick, Snare, Clap, CHat, OHatCrash]");
  _in.setAcquireSize(1);
  _out.setAcquireSize(1);
  _in.setReleaseSize(1);
  _out.setReleaseSize(1);
}

void InstrumentSum::configure() {
  _sampleRate = parameter("sampleRate").toReal();
  Real nyq = parameter("nyquist").toReal();
  _nyquist = (nyq > 0 ? nyq : _sampleRate * 0.5f);
  _expectedBands = parameter("expectedBands").toInt();
  _lobeRolloff = parameter("lobeRolloff").toReal();

  reset();
}

void InstrumentSum::reset() {
  Algorithm::reset();
  _numBands = 0;
  _weightsReady = false;
  _weights.clear();
}

AlgorithmStatus InstrumentSum::process() {
  AlgorithmStatus status = acquireData();
  if (status != OK) return status;

  const std::vector<std::vector<Real>>& inTokens = _in.tokens();
  std::vector<std::vector<Real>>& outTokens = _out.tokens();
  
  if (inTokens.empty()) {
    if (outTokens.size() < 1) outTokens.resize(1);
    outTokens[0].clear();
    releaseData();
    return OK;
  }
  
  const std::vector<Real>& bands = inTokens[0];

  if (bands.empty()) {
    if (outTokens.size() < 1) outTokens.resize(1);
    outTokens[0].clear();
    releaseData();
    return OK;
  }

  if (_expectedBands > 0 && (int)bands.size() != _expectedBands) {
    // If expected doesn't match, rebuild weights to actual size once
    _weightsReady = false;
  }

  if (!_weightsReady || _numBands != bands.size()) {
    _numBands = bands.size();
    ensureWeights(_numBands);
  }

  // Compute instrument sums: outTokens[0] holds vector of size 5
  if (outTokens.size() < 1) outTokens.resize(1);
  outTokens[0].assign(5, 0.0f);
  for (size_t k = 0; k < 5; ++k) {
    const std::vector<Real>& w = _weights[k];
    Real s = 0.0f;
    for (size_t b = 0; b < _numBands; ++b) s += w[b] * bands[b];
    outTokens[0][k] = s;
  }

  releaseData();
  return OK;
}

void InstrumentSum::ensureWeights(size_t numBands) {
  _weights.assign(5, std::vector<Real>(numBands, 0.0f));
  buildDefaultMasks(numBands, _weights);
  _weightsReady = true;
}

// mel helpers
Real InstrumentSum::hzToMel(Real hz) {
  return 2595.0f * std::log10(1.0f + hz / 700.0f);
}

Real InstrumentSum::melToHz(Real mel) {
  return 700.0f * (std::pow(10.0f, mel / 2595.0f) - 1.0f);
}

void InstrumentSum::buildDefaultMasks(size_t numBands,
                                      std::vector<std::vector<Real>>& weights) const {
  // Compute band center freqs assuming linear spacing on mel scale between 0..Nyquist
  std::vector<Real> centersHz(numBands, 0.0f);
  Real mel0 = hzToMel(0.0f);
  Real melN = hzToMel(_nyquist);
  for (size_t i = 0; i < numBands; ++i) {
    Real m = mel0 + (melN - mel0) * (static_cast<Real>(i) + 0.5f) / static_cast<Real>(numBands);
    centersHz[i] = melToHz(m);
  }

  auto addInstrument = [&](size_t idx,
                           std::initializer_list<std::tuple<Real, Real, Real>> lobes) {
    std::vector<Real>& dest = weights[idx];
    for (auto& l : lobes) {
      Real f1 = std::get<0>(l);
      Real f2 = std::get<1>(l);
      Real w  = std::get<2>(l);
      addHannLobe(dest, centersHz, f1, f2, w, _lobeRolloff);
    }
    normalize(dest);
  };

  // Kick: Focused on fundamental sub-bass (40-75 Hz) and narrow click (3-4k Hz)
  // Removed 110-180 Hz range to avoid bass/snare overlap
  // Narrowed high freq to 3-4k to avoid snare attack overlap (snare is 2-5k)
  addInstrument(0, { {40.0f, 75.0f, 0.75f} });

  // Snare: 180–280 (0.35), 350–600 (0.10), 2–5k (0.35), 6–10k (0.20)
  addInstrument(1, { {180.0f, 280.0f, 0.35f}, {350.0f, 600.0f, 0.10f}, {2000.0f, 5000.0f, 0.35f}, {6000.0f, 10000.0f, 0.20f} });

  // Clap: 800–1.6k (0.30), 2–6k (0.50), 6–10k (0.20)
  addInstrument(2, { {800.0f, 1600.0f, 0.30f}, {2000.0f, 6000.0f, 0.50f}, {6000.0f, 10000.0f, 0.20f} });

  // Closed Hat: 3–6k (0.25), 7–12k (0.55), 12–16k (0.20)
  addInstrument(3, { {3000.0f, 6000.0f, 0.25f}, {7000.0f, 12000.0f, 0.55f}, {12000.0f, 16000.0f, 0.20f} });

  // Open Hat / Crash: 3–6k (0.25), 6–12k (0.50), 12–16k (0.25)
  addInstrument(4, { {3000.0f, 6000.0f, 0.25f}, {6000.0f, 12000.0f, 0.50f}, {12000.0f, 16000.0f, 0.25f} });
}

void InstrumentSum::addHannLobe(std::vector<Real>& dest,
                                const std::vector<Real>& bandCentersHz,
                                Real f1, Real f2, Real weight, Real rolloffFrac) {
  if (f2 <= f1) return;
  Real span = f2 - f1;
  Real edge = std::max(0.0f, std::min(span * rolloffFrac, span * 0.49f));
  Real core1 = f1 + edge;
  Real core2 = f2 - edge;
  for (size_t i = 0; i < bandCentersHz.size(); ++i) {
    Real f = bandCentersHz[i];
    Real w = 0.0f;
    if (f >= core1 && f <= core2) {
      w = 1.0f; // flat core
    } else if (f >= f1 && f < core1) {
      Real x = (f - f1) / std::max(1e-9f, edge);
      w = 0.5f * (1.0f - std::cos(3.14159265358979323846f * x));
    } else if (f > core2 && f <= f2) {
      Real x = (f2 - f) / std::max(1e-9f, edge);
      w = 0.5f * (1.0f - std::cos(3.14159265358979323846f * x));
    }
    dest[i] += weight * w;
  }
}

void InstrumentSum::normalize(std::vector<Real>& v) {
  Real s = 0.0f;
  for (Real x : v) s += x;
  if (s <= 0.0f) return;
  Real inv = 1.0f / s;
  for (Real& x : v) x *= inv;
}

} // namespace streaming
} // namespace essentia


