#ifndef ESSENTIA_STREAMING_INSTRUMENT_PREDICTOR_H
#define ESSENTIA_STREAMING_INSTRUMENT_PREDICTOR_H

/*
 * InstrumentPredictor (streaming)
 * - Per-instrument Kalman/PLL tracker for tempo + phase
 * - Predicts next 1-3 hits per instrument with timestamps and CI
 * - Seeded by robust IOI statistics (median/MAD)
 * - Selective emission: on hits + periodic heartbeat
 *
 * Input:  TOKEN stream of std::vector<Real> per frame (5 instrument gates)
 * Output: TOKEN stream of std::vector<Real> per frame (dummy, not used directly)
 *         TOKEN stream of PredictionOutput per frame (for lighting engine)
 */

#include "streamingalgorithm.h"
#include "prediction_types.h"
#include <essentia/algorithmfactory.h>
#include <vector>
#include <deque>
#include <string>
#include <memory>
#include <chrono>
#include <zmq.hpp>

// Forward declaration of HitPredictionLogger (defined in global namespace)
class HitPredictionLogger;

namespace essentia {
namespace streaming {

// Forward declare for helper structs
struct InstrumentState;

class InstrumentPredictor : public Algorithm {
public:
  InstrumentPredictor();
  ~InstrumentPredictor();

  void declareParameters() {
    declareParameter("sampleRate", "Audio sample rate (Hz)", "[8000,192000]", 44100);
    declareParameter("hopSize", "Hop size in samples", "[64,4096]", 256);
    declareParameter("endpoint", "ZeroMQ endpoint to publish predictions", "", "tcp://*:5556");
    declareParameter("min_hits_for_seed", "Minimum hits before predictions enabled", "[3,20]", 8);
    declareParameter("min_bpm", "Minimum tempo (BPM)", "[30,120]", 60);
    declareParameter("max_bpm", "Maximum tempo (BPM)", "[120,300]", 200);
    declareParameter("horizon_seconds", "Prediction horizon", "[0.5,5.0]", 2.0);
    declareParameter("max_predictions_per_instrument", "Max hits to predict", "[1,5]", 2);
    declareParameter("confidence_threshold_min", "Minimum confidence to emit", "[0,1]", 0.3);
    declareParameter("periodic_interval_sec", "Periodic emission interval", "[0.05,1.0]", 0.15);
    declareParameter("q_period", "Process noise for period", "[1e-9,1e-3]", 1e-6);
    declareParameter("q_phase", "Process noise for phase", "[1e-9,1e-3]", 1e-8);
    declareParameter("r_base", "Measurement noise base", "[1e-6,1e-2]", 1e-4);
    declareParameter("confidence_decay_rate", "Recency decay alpha", "[0.01,0.5]", 0.1);
  }

  void configure();
  AlgorithmStatus process();
  void reset();
  
  /**
   * Set logger instance for logging predictions to file
   * @param logger Pointer to HitPredictionLogger instance (can be nullptr)
   */
  void set_logger(::HitPredictionLogger* logger);

  static const char* name;
  static const char* category;
  static const char* description;

private:
  // IO connectors
  Sink<std::vector<Real>> _in;
  Source<std::vector<Real>> _out;  // Dummy output for Essentia graph
  Source<PredictionOutput> _predictionOut; // Prediction output for lighting engine

  // Parameters
  Real _sampleRate;
  int _hopSize;
  std::string _endpoint;
  int _minHitsForSeed;
  Real _minBpm;
  Real _maxBpm;
  Real _horizonSeconds;
  int _maxPredictionsPerInstrument;
  Real _confidenceThresholdMin;
  Real _periodicIntervalSec;
  Real _qPeriod;
  Real _qPhase;
  Real _rBase;
  Real _confidenceDecayRate;

  // Per-instrument state (5 instruments: kick, snare, clap, chat, ohc)
  static constexpr int NUM_INSTRUMENTS = 5;
  std::vector<std::unique_ptr<InstrumentState>> _instruments;

  // Timing
  Real _frameTimeSec;
  int _frameCount;
  Real _lastEmissionTime;
  bool _zmqInitialized;

  // Last prediction output (for output emission)
  PredictionOutput _lastPredictionOutput;

  // ZeroMQ
  std::unique_ptr<zmq::context_t> _zmqContext;
  std::unique_ptr<zmq::socket_t> _zmqSocket;
  
  // Logger (optional, set via set_logger)
  ::HitPredictionLogger* _logger;

  // Helpers
  void initializeZeroMQ();
  void ensureInstrumentStates();
  void updateInstrumentState(int idx, bool hit, Real currentTime);
  void generatePredictions(Real currentTime, bool forceEmit);
  std::vector<PredictionHit> predictForInstrument(int idx, Real currentTime);
  Real computeConfidence(int idx);
  Real computeTimeUncertainty(int idx);
  PredictionOutput buildPredictionOutput(const std::vector<std::vector<PredictionHit>> &allPredictions, Real currentTime);
  std::string serializePredictionsForZMQ(const PredictionOutput &output); // For ZeroMQ JSON output
  void publishZeroMQ(const std::string& json);
  
  // Kalman/PLL helpers
  void kalmanPredict(int idx, Real dt);
  void kalmanUpdate(int idx, Real phaseResidual);
  void updateIOIStatistics(int idx);
  Real wrapPhase(Real phase);
  Real wrapPhaseResidual(Real residual);
  
  // Math helpers
  Real computeMedian(const std::vector<Real>& values);
  Real computeMAD(const std::vector<Real>& values, Real median);
};

// Instrument state structure
struct InstrumentState {
  bool warmupComplete;
  std::deque<Real> hitTimes;  // Last N hit timestamps
  std::deque<Real> ioiBuffer; // Last N-1 IOIs
  Real periodMedian;
  Real periodMAD;
  
  // Kalman state: [period, phase]
  Real period;
  Real phase;
  
  // Kalman covariance P (2x2)
  Real P00; // period variance
  Real P01; // period-phase covariance
  Real P10; // same as P01 (symmetric)
  Real P11; // phase variance
  
  Real lastHitTime;
  int lastUpdateFrame;
  int hitsSeen;
  Real confidenceGlobal;
  
  InstrumentState() : warmupComplete(false), periodMedian(0), periodMAD(0),
                       period(0.5), phase(0), P00(0.01), P01(0), P10(0), P11(0.01),
                       lastHitTime(-1), lastUpdateFrame(-1), hitsSeen(0), confidenceGlobal(0) {}
};

// PredictionHit is now defined in prediction_types.h

} // namespace streaming
} // namespace essentia

#endif // ESSENTIA_STREAMING_INSTRUMENT_PREDICTOR_H

