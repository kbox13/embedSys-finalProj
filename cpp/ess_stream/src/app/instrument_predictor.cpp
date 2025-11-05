#include "instrument_predictor.h"
#include "hit_prediction_logger.h"  // For HitPredictionLogger
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <cstring>

namespace essentia {
namespace streaming {

const char* InstrumentPredictor::name = "InstrumentPredictor";
const char* InstrumentPredictor::category = "Streaming";
const char* InstrumentPredictor::description =
  "Predicts next 1-3 hits per instrument using Kalman/PLL tempo+phase tracking.";

InstrumentPredictor::InstrumentPredictor() : Algorithm() {
  declareInput(_in, "in", "instrument gate hits (vector of 5)");
  declareOutput(_out, "out", "dummy output (not used)");
  _in.setAcquireSize(1);
  _out.setAcquireSize(1);
  _in.setReleaseSize(1);
  _out.setReleaseSize(1);
  
  _zmqInitialized = false;
  _frameCount = 0;
  _frameTimeSec = 0.0;
  _lastEmissionTime = 0.0;
}

InstrumentPredictor::~InstrumentPredictor() {
  if (_zmqSocket) {
    _zmqSocket->close();
  }
  if (_zmqContext) {
    _zmqContext->close();
  }
}

void InstrumentPredictor::configure() {
  _sampleRate = parameter("sampleRate").toReal();
  _hopSize = parameter("hopSize").toInt();
  _endpoint = parameter("endpoint").toString();
  _minHitsForSeed = parameter("min_hits_for_seed").toInt();
  _minBpm = parameter("min_bpm").toReal();
  _maxBpm = parameter("max_bpm").toReal();
  _horizonSeconds = parameter("horizon_seconds").toReal();
  _maxPredictionsPerInstrument = parameter("max_predictions_per_instrument").toInt();
  _confidenceThresholdMin = parameter("confidence_threshold_min").toReal();
  _periodicIntervalSec = parameter("periodic_interval_sec").toReal();
  _qPeriod = parameter("q_period").toReal();
  _qPhase = parameter("q_phase").toReal();
  _rBase = parameter("r_base").toReal();
  _confidenceDecayRate = parameter("confidence_decay_rate").toReal();
  
  reset();
}

void InstrumentPredictor::reset() {
  Algorithm::reset();
  _instruments.clear();
  _instruments.resize(NUM_INSTRUMENTS);
  for (int i = 0; i < NUM_INSTRUMENTS; ++i) {
    _instruments[i] = std::make_unique<InstrumentState>();
  }
  _frameCount = 0;
  _frameTimeSec = 0.0;
  _lastEmissionTime = 0.0;
  _zmqInitialized = false;
  _logger = nullptr;
  
  initializeZeroMQ();
}

void InstrumentPredictor::initializeZeroMQ() {
  try {
    _zmqContext = std::make_unique<zmq::context_t>(1);
    _zmqSocket = std::make_unique<zmq::socket_t>(*_zmqContext, ZMQ_PUSH);
    // PUSH sockets connect (client side), receiver binds (server side)
    _zmqSocket->connect(_endpoint);
    _zmqInitialized = true;
    std::cout << "InstrumentPredictor connected to: " << _endpoint << std::endl;
  } catch (const std::exception& e) {
    // Silent fail - predictions will work but won't publish
    _zmqInitialized = false;
    std::cerr << "InstrumentPredictor ZMQ connection error: " << e.what() << std::endl;
  }
}

void InstrumentPredictor::ensureInstrumentStates() {
  if (_instruments.empty()) {
    _instruments.resize(NUM_INSTRUMENTS);
    for (int i = 0; i < NUM_INSTRUMENTS; ++i) {
      _instruments[i] = std::make_unique<InstrumentState>();
    }
  }
}

AlgorithmStatus InstrumentPredictor::process() {
  AlgorithmStatus status = acquireData();
  if (status != OK) return status;

  // For Sink<std::vector<Real>>, tokens() returns std::vector<std::vector<Real>>&
  const std::vector<std::vector<Real>>& inTokens = _in.tokens();
  std::vector<std::vector<Real>>& outTokens = _out.tokens();
  
  if (inTokens.empty()) {
    if (outTokens.size() < 1) outTokens.resize(1);
    outTokens[0].clear();
    releaseData();
    return OK;
  }
  
  const std::vector<Real>& gates = inTokens[0];
  
  if (gates.size() < NUM_INSTRUMENTS) {
    if (outTokens.size() < 1) outTokens.resize(1);
    outTokens[0].clear();
    releaseData();
    return OK;
  }

  ensureInstrumentStates();
  
  // Compute frame time
  _frameTimeSec = static_cast<Real>(_frameCount) * static_cast<Real>(_hopSize) / _sampleRate;
  Real dt = static_cast<Real>(_hopSize) / _sampleRate;
  
  bool anyHit = false;
  
  // Update each instrument state
  for (int i = 0; i < NUM_INSTRUMENTS; ++i) {
    bool hit = (gates[i] >= 0.5);
    if (hit) anyHit = true;
    
    // Always run predict step
    kalmanPredict(i, dt);
    
    // Update on hits
    if (hit) {
      updateInstrumentState(i, true, _frameTimeSec);
    }
  }
  
  // Check periodic emission or emit on hits
  Real elapsedSinceLastEmit = _frameTimeSec - _lastEmissionTime;
  bool shouldEmit = anyHit || (elapsedSinceLastEmit >= _periodicIntervalSec);
  
  if (shouldEmit) {
    generatePredictions(_frameTimeSec, shouldEmit);
    _lastEmissionTime = _frameTimeSec;
  }
  
  // Dummy output (not used)
  if (outTokens.size() < 1) outTokens.resize(1);
  outTokens[0].assign(NUM_INSTRUMENTS, 0.0f);
  
  _frameCount++;
  releaseData();
  return OK;
}

void InstrumentPredictor::updateInstrumentState(int idx, bool hit, Real currentTime) {
  auto& inst = *_instruments[idx];
  
  if (hit) {
    inst.hitTimes.push_back(currentTime);
    inst.lastHitTime = currentTime;
    inst.lastUpdateFrame = _frameCount;
    inst.hitsSeen++;
    
    // Maintain sliding window (keep last 20 hits)
    const int MAX_HITS = 20;
    if (inst.hitTimes.size() > MAX_HITS) {
      inst.hitTimes.pop_front();
    }
    
    // Update IOI statistics if we have enough hits
    if (inst.hitTimes.size() >= 2) {
      updateIOIStatistics(idx);
      
      // Check if warmup complete
      if (!inst.warmupComplete && inst.hitsSeen >= _minHitsForSeed && inst.ioiBuffer.size() >= (_minHitsForSeed - 1)) {
        inst.warmupComplete = true;
        // Initialize Kalman state from IOI stats
        inst.period = inst.periodMedian;
        inst.phase = 0.0; // Reset to preferred impact phase
        inst.P00 = inst.periodMAD * inst.periodMAD; // Variance from MAD
        inst.P11 = 0.01; // Initial phase uncertainty
      }
    }
    
    // Kalman update if warmup complete
    if (inst.warmupComplete) {
      Real phaseResidual = wrapPhaseResidual(inst.phase - 0.0); // Preferred phase = 0
      kalmanUpdate(idx, phaseResidual);
      
      // Clamp period to BPM bounds
      Real minPeriod = 60.0 / _maxBpm;
      Real maxPeriod = 60.0 / _minBpm;
      inst.period = std::max(minPeriod, std::min(maxPeriod, inst.period));
    }
  }
}

void InstrumentPredictor::kalmanPredict(int idx, Real dt) {
  auto& inst = *_instruments[idx];
  
  if (!inst.warmupComplete) return;
  
  // Predict step: period with tiny drift, phase advances
  // Period: x[0] = x[0] + process_noise (tiny)
  // Phase: x[1] = (x[1] + dt/period) mod 1
  
  // Process noise on period (very small drift)
  inst.P00 += _qPeriod * dt;
  
  // Phase advances
  if (inst.period > 1e-6) {
    inst.phase = wrapPhase(inst.phase + dt / inst.period);
  }
  
  // Process noise on phase (propagates through period uncertainty)
  Real dphase_dperiod = -dt / (inst.period * inst.period);
  inst.P11 += _qPhase * dt + dphase_dperiod * dphase_dperiod * inst.P00;
  inst.P01 += dphase_dperiod * inst.P00;
  inst.P10 = inst.P01; // Symmetric
}

void InstrumentPredictor::kalmanUpdate(int idx, Real phaseResidual) {
  auto& inst = *_instruments[idx];
  
  // Measurement: we observe phase residual (difference from preferred phase 0)
  // H = [0, 1] (we only observe phase, not period directly)
  Real H0 = 0.0; // Measurement sensitivity to period
  Real H1 = 1.0; // Measurement sensitivity to phase
  
  // Measurement noise (scales with IOI jitter)
  Real R = _rBase * (1.0 + inst.periodMAD / inst.period);
  
  // Innovation covariance
  Real S = H0 * H0 * inst.P00 + 2.0 * H0 * H1 * inst.P01 + H1 * H1 * inst.P11 + R;
  
  if (S < 1e-9) return; // Numerical stability
  
  // Kalman gain
  Real K0 = (H0 * inst.P00 + H1 * inst.P01) / S;
  Real K1 = (H0 * inst.P01 + H1 * inst.P11) / S;
  
  // State update
  inst.period -= K0 * phaseResidual;
  inst.phase -= K1 * phaseResidual;
  inst.phase = wrapPhase(inst.phase);
  
  // Covariance update
  Real P00_new = inst.P00 - K0 * S * K0;
  Real P01_new = inst.P01 - K0 * S * K1;
  Real P11_new = inst.P11 - K1 * S * K1;
  
  inst.P00 = std::max(1e-6f, P00_new); // Ensure positive
  inst.P01 = P01_new;
  inst.P10 = P01_new;
  inst.P11 = std::max(1e-6f, P11_new);
  
  // Period adjustment based on residual (if phase consistently off, adjust period)
  if (std::abs(phaseResidual) > 0.1) {
    Real periodCorrection = -phaseResidual * inst.period * 0.1; // Damped correction
    inst.period += periodCorrection;
  }
}

void InstrumentPredictor::updateIOIStatistics(int idx) {
  auto& inst = *_instruments[idx];
  
  if (inst.hitTimes.size() < 2) return;
  
  inst.ioiBuffer.clear();
  for (size_t i = 1; i < inst.hitTimes.size(); ++i) {
    Real ioi = inst.hitTimes[i] - inst.hitTimes[i-1];
    // Filter outliers (IOIs should be in reasonable range)
    Real minPeriod = 60.0 / _maxBpm;
    Real maxPeriod = 60.0 / _minBpm;
    if (ioi >= minPeriod && ioi <= maxPeriod * 4) { // Allow up to 4x period for gaps
      inst.ioiBuffer.push_back(ioi);
    }
  }
  
  if (inst.ioiBuffer.size() >= 2) {
    // Convert deque to vector for computeMedian/computeMAD
    std::vector<Real> ioiVec(inst.ioiBuffer.begin(), inst.ioiBuffer.end());
    inst.periodMedian = computeMedian(ioiVec);
    inst.periodMAD = computeMAD(ioiVec, inst.periodMedian);
  }
}

Real InstrumentPredictor::computeMedian(const std::vector<Real>& values) {
  if (values.empty()) return 0.0;
  if (values.size() == 1) return values[0];
  
  std::vector<Real> sorted(values.begin(), values.end());
  std::sort(sorted.begin(), sorted.end());
  
  size_t mid = sorted.size() / 2;
  if (sorted.size() % 2 == 0) {
    return (sorted[mid - 1] + sorted[mid]) * 0.5;
  } else {
    return sorted[mid];
  }
}

Real InstrumentPredictor::computeMAD(const std::vector<Real>& values, Real median) {
  if (values.empty()) return 0.0;
  
  std::vector<Real> deviations;
  deviations.reserve(values.size());
  for (Real v : values) {
    deviations.push_back(std::abs(v - median));
  }
  
  Real mad = computeMedian(deviations);
  return 1.4826 * mad; // Scale to match standard deviation for normal distribution
}

Real InstrumentPredictor::wrapPhase(Real phase) {
  // Wrap to [0, 1)
  while (phase >= 1.0) phase -= 1.0;
  while (phase < 0.0) phase += 1.0;
  return phase;
}

Real InstrumentPredictor::wrapPhaseResidual(Real residual) {
  // Wrap to [-0.5, 0.5)
  while (residual >= 0.5) residual -= 1.0;
  while (residual < -0.5) residual += 1.0;
  return residual;
}

void InstrumentPredictor::generatePredictions(Real currentTime, bool forceEmit) {
  if (!_zmqInitialized) return;
  
  std::vector<std::vector<PredictionHit>> allPredictions(NUM_INSTRUMENTS);
  
  for (int i = 0; i < NUM_INSTRUMENTS; ++i) {
    allPredictions[i] = predictForInstrument(i, currentTime);
  }
  
  std::string json = serializePredictions(allPredictions, currentTime);
  
  // Log predictions to file if logger is set
  if (_logger && _logger->is_enabled()) {
    _logger->log_predictions(_frameCount, currentTime, allPredictions);
  }
  
  publishZeroMQ(json);
}

std::vector<PredictionHit> InstrumentPredictor::predictForInstrument(int idx, Real currentTime) {
  std::vector<PredictionHit> hits;
  auto& inst = *_instruments[idx];
  
  if (!inst.warmupComplete || inst.period < 1e-6) {
    return hits; // Empty
  }
  
  // Project next 1-3 hits
  Real phaseRemaining = 1.0 - inst.phase;
  Real tNext = currentTime + phaseRemaining * inst.period;
  
  int hitIndex = 1;
  while (hitIndex <= _maxPredictionsPerInstrument && tNext <= currentTime + _horizonSeconds) {
    PredictionHit hit;
    hit.tPredSec = tNext;
    hit.hitIndex = hitIndex;
    
    // Compute confidence
    hit.confidence = computeConfidence(idx);
    
    // Compute CI
    Real timeUncertainty = computeTimeUncertainty(idx);
    hit.ciLowSec = tNext - 1.96 * timeUncertainty; // ~95% CI
    hit.ciHighSec = tNext + 1.96 * timeUncertainty;
    
    // Filter by confidence threshold
    if (hit.confidence >= _confidenceThresholdMin) {
      hits.push_back(hit);
    }
    
    // Next hit
    tNext += inst.period;
    hitIndex++;
  }
  
  return hits;
}

Real InstrumentPredictor::computeConfidence(int idx) {
  auto& inst = *_instruments[idx];
  
  // IOI stability
  Real cIOI = 0.0;
  if (inst.period > 1e-6 && inst.periodMAD > 0) {
    cIOI = std::max(0.0f, 1.0f - inst.periodMAD / inst.period);
    cIOI = std::min(1.0f, cIOI);
  }
  
  // Phase variance
  Real cPhase = 0.0;
  if (inst.P11 > 0) {
    Real phaseStd = std::sqrt(inst.P11);
    cPhase = std::max(0.0f, 1.0f - phaseStd * 10.0f); // Scale factor
    cPhase = std::min(1.0f, cPhase);
  }
  
  // Recency
  Real cRecency = 1.0;
  if (inst.lastHitTime > 0 && inst.period > 1e-6) {
    Real dt = _frameTimeSec - inst.lastHitTime;
    cRecency = std::exp(-dt / (_confidenceDecayRate * inst.period));
  }
  
  // Combine (weighted average)
  Real confidence = 0.4 * cPhase + 0.3 * cIOI + 0.3 * cRecency;
  inst.confidenceGlobal = confidence;
  
  return confidence;
}

Real InstrumentPredictor::computeTimeUncertainty(int idx) {
  auto& inst = *_instruments[idx];
  
  // Time uncertainty from phase and period uncertainty
  Real phaseStd = std::sqrt(inst.P11);
  Real periodStd = std::sqrt(inst.P00);
  
  // Propagation: t = phase * period
  Real timeUncertainty = std::sqrt(
    (inst.phase * periodStd) * (inst.phase * periodStd) +
    (inst.period * phaseStd) * (inst.period * phaseStd)
  );
  
  // Add IOI jitter component
  if (inst.periodMAD > 0) {
    timeUncertainty = std::sqrt(timeUncertainty * timeUncertainty + 0.25 * inst.periodMAD * inst.periodMAD);
  }
  
  return std::max(0.001f, timeUncertainty); // Minimum 1ms
}

std::string InstrumentPredictor::serializePredictions(const std::vector<std::vector<PredictionHit>>& allPredictions, Real currentTime) {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(6);
  
  static const char* instNames[] = {"kick", "snare", "clap", "chat", "ohc"};
  
  oss << "{\"timestamp_sec\":" << currentTime
      << ",\"frame_idx\":" << _frameCount
      << ",\"predictions\":[";
  
  for (int i = 0; i < NUM_INSTRUMENTS; ++i) {
    auto& inst = *_instruments[i];
    auto& hits = allPredictions[i];
    
    if (i > 0) oss << ",";
    oss << "{\"instrument\":\"" << instNames[i] << "\""
        << ",\"tempo_bpm\":" << (inst.period > 1e-6 ? 60.0 / inst.period : 0.0)
        << ",\"period_sec\":" << inst.period
        << ",\"phase\":" << inst.phase
        << ",\"confidence_global\":" << inst.confidenceGlobal
        << ",\"warmup_complete\":" << (inst.warmupComplete ? "true" : "false")
        << ",\"hits\":[";
    
    for (size_t j = 0; j < hits.size(); ++j) {
      if (j > 0) oss << ",";
      oss << "{\"t_pred_sec\":" << hits[j].tPredSec
          << ",\"ci_low_sec\":" << hits[j].ciLowSec
          << ",\"ci_high_sec\":" << hits[j].ciHighSec
          << ",\"confidence\":" << hits[j].confidence
          << ",\"hit_index\":" << hits[j].hitIndex << "}";
    }
    
    oss << "]}";
  }
  
  oss << "]}";
  return oss.str();
}

void InstrumentPredictor::publishZeroMQ(const std::string& json) {
  if (!_zmqInitialized || !_zmqSocket) return;
  
  try {
    zmq::message_t msg(json.c_str(), json.size());
    _zmqSocket->send(msg, zmq::send_flags::dontwait);
  } catch (const std::exception&) {
    // Silent fail on publish errors
  }
}

void InstrumentPredictor::set_logger(::HitPredictionLogger* logger) {
  _logger = logger;
}

} // namespace streaming
} // namespace essentia

