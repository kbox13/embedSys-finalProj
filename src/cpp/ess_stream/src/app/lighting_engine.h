#ifndef ESSENTIA_STREAMING_LIGHTING_ENGINE_H
#define ESSENTIA_STREAMING_LIGHTING_ENGINE_H

/*
 * LightingEngine (streaming)
 * - Filters predictions from InstrumentPredictor
 * - Maps instruments to RGB colors
 * - Outputs lighting commands for MQTT publisher
 *
 * Input:  TOKEN stream of PredictionOutput per frame (from InstrumentPredictor)
 * Output: TOKEN stream of std::vector<LightingCommand> per frame (for MQTTPublisher)
 */

#include "streamingalgorithm.h"
#include "prediction_types.h"
#include <essentia/algorithmfactory.h>
#include <string>
#include <unordered_set>
#include <map>
#include <chrono>
#include <vector>

namespace essentia {
namespace streaming {

// Structure for tracking sent events (for duplicate detection)
struct SentEvent {
  std::string eventId;
  Real tPredSec;  // Prediction time (when the event should occur)
  
  SentEvent() : eventId(""), tPredSec(0.0) {}
  SentEvent(const std::string& id, Real tPred) : eventId(id), tPredSec(tPred) {}
};

// LightingCommand is now defined in prediction_types.h

class LightingEngine : public Algorithm {
public:
  LightingEngine();
  ~LightingEngine();

  void declareParameters() {
    declareParameter("confidence_threshold", "Minimum confidence to send", "[0,1]", 0.3);
    declareParameter("max_latency_sec", "Maximum prediction latency", "[0.1,10.0]", 2.0);
    declareParameter("min_latency_sec", "Minimum prediction latency", "[0.01,1.0]", 0.05);
    declareParameter("duplicate_window_sec", "Time window for duplicate detection", "[0.01,1.0]", 0.1);
  }

  void configure();
  AlgorithmStatus process();
  void reset();

  static const char* name;
  static const char* category;
  static const char* description;

private:
  // IO connectors
  Sink<PredictionOutput> _in;
  Source<std::vector<LightingCommand>> _out;

  // Parameters
  Real _confidenceThreshold;
  Real _maxLatencySec;
  Real _minLatencySec;
  Real _duplicateWindowSec;

  // Internal state
  Real _currentTimeSec;
  int _frameCount;
  std::map<std::string, SentEvent> _sentEvents;  // eventId -> SentEvent
  int _eventIdCounter;
  int _cleanupCounter;  // Counter for batched cleanup
  static const int CLEANUP_INTERVAL = 50;  // Cleanup every 50 frames

  // Helper methods
  void processPredictionOutput(const PredictionOutput& output, std::vector<LightingCommand>& commands);
  bool shouldSendCommand(const LightingCommand& cmd, const std::string& eventId);
  std::string generateEventId(const LightingCommand& cmd);
  void cleanupOldEvents(Real currentTime);
  void mapInstrumentToColor(const std::string& instrument, int& r, int& g, int& b);
};

} // namespace streaming
} // namespace essentia

#endif // ESSENTIA_STREAMING_LIGHTING_ENGINE_H

