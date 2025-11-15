#include "lighting_engine.h"
#include "prediction_types.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>

namespace essentia {
namespace streaming {

const char* LightingEngine::name = "LightingEngine";
const char* LightingEngine::category = "Streaming";
const char* LightingEngine::description =
  "Filters predictions and converts them to lighting commands with RGB color mapping.";

LightingEngine::LightingEngine() : Algorithm() {
  declareInput(_in, "in", "prediction output from InstrumentPredictor");
  declareOutput(_out, "out", "lighting commands");
  _in.setAcquireSize(1);
  _out.setAcquireSize(1);
  _in.setReleaseSize(1);
  _out.setReleaseSize(1);
  
  _currentTimeSec = 0.0;
  _frameCount = 0;
  _eventIdCounter = 0;
  _cleanupCounter = 0;
}

LightingEngine::~LightingEngine() {
}

void LightingEngine::configure() {
  _confidenceThreshold = parameter("confidence_threshold").toReal();
  _maxLatencySec = parameter("max_latency_sec").toReal();
  _minLatencySec = parameter("min_latency_sec").toReal();
  _duplicateWindowSec = parameter("duplicate_window_sec").toReal();
  
  reset();
}

void LightingEngine::reset() {
  Algorithm::reset();
  _currentTimeSec = 0.0;
  _frameCount = 0;
  _sentEvents.clear();
  _eventIdCounter = 0;
  _cleanupCounter = 0;
}

AlgorithmStatus LightingEngine::process() {
  AlgorithmStatus status = acquireData();
  if (status != OK) return status;

  const std::vector<PredictionOutput>& inTokens = _in.tokens();
  std::vector<std::vector<LightingCommand>>& outTokens = _out.tokens();
  
  if (outTokens.size() < 1) outTokens.resize(1);
  
  // If no input or empty input, emit empty vector
  if (inTokens.empty() || inTokens[0].isEmpty()) {
    outTokens[0].clear();
    _frameCount++;
    releaseData();
    return OK;
  }
  
  // Process prediction output and generate lighting commands
  std::vector<LightingCommand> commands;
  processPredictionOutput(inTokens[0], commands);
  
  // Output commands (they've already been filtered during processing)
  outTokens[0] = commands;
  
  _frameCount++;
  releaseData();
  return OK;
}

void LightingEngine::processPredictionOutput(const PredictionOutput& output, std::vector<LightingCommand>& commands) {
  commands.clear();
  
  // Extract timestamp
  _currentTimeSec = output.timestampSec;
  
  // Batched cleanup: only run every CLEANUP_INTERVAL frames
  _cleanupCounter++;
  if (_cleanupCounter >= CLEANUP_INTERVAL) {
    cleanupOldEvents(_currentTimeSec);
    _cleanupCounter = 0;
  }
  
  // Process each instrument prediction
  for (const auto& pred : output.predictions) {
    // Process each hit for this instrument
    for (const auto& hit : pred.hits) {
      // Create lighting command
      LightingCommand cmd;
      cmd.instrument = pred.instrument;
      cmd.tPredSec = hit.tPredSec;  // Full precision preserved!
      cmd.confidence = hit.confidence;
      mapInstrumentToColor(pred.instrument, cmd.r, cmd.g, cmd.b);
      
      // Generate event ID for all commands
      cmd.eventId = generateEventId(cmd);
      
      // Check if we should send this command (confidence, latency, duplicates)
      if (!shouldSendCommand(cmd, cmd.eventId)) {
        continue; // Skip this command
      }
      
      // All checks passed - add command
      if (cmd.instrument == "kick") {
        commands.push_back(cmd);
      }
      
      // Track this event with its prediction time (not sent time)
      // This allows us to catch duplicates until after the prediction time has passed
      _sentEvents[cmd.eventId] = SentEvent(cmd.eventId, cmd.tPredSec);
    }
  }
}

bool LightingEngine::shouldSendCommand(const LightingCommand& cmd, const std::string& eventId) {
  // Check confidence threshold
  if (cmd.confidence < _confidenceThreshold) {
    return false;
  }
  
  // Check latency (prediction time relative to current time)
  Real latency = cmd.tPredSec - _currentTimeSec;
  if (latency < _minLatencySec || latency > _maxLatencySec) {
    return false;
  }
  
  // Check for duplicates using the provided eventId
  auto it = _sentEvents.find(eventId);
  if (it != _sentEvents.end()) {
    // Check if prediction time hasn't passed yet (or just passed within window)
    Real timeDiff = cmd.tPredSec - it->second.tPredSec;
    if (timeDiff < _duplicateWindowSec) {
      return false; // Duplicate detected
    }
  }
  
  return true;
}

std::string LightingEngine::generateEventId(const LightingCommand& cmd) {
  // Generate event ID from instrument, rounded time
  // Round time to 10ms precision to catch near-duplicates
  Real roundedTime = std::round(cmd.tPredSec * 100.0) / 100.0;  // 10ms precision
  
  std::ostringstream oss;
  oss << cmd.instrument << "_"
      << std::fixed << std::setprecision(2) << roundedTime
      ;
  return oss.str();
}

void LightingEngine::cleanupOldEvents(Real currentTime) {
  // Remove events only after their prediction time has passed (plus window)
  // This ensures we catch duplicates that arrive late
  auto it = _sentEvents.begin();
  while (it != _sentEvents.end()) {
    Real timeSincePrediction = currentTime - it->second.tPredSec;
    // Keep events until prediction time has passed plus duplicate window
    if (timeSincePrediction > _duplicateWindowSec) {
      it = _sentEvents.erase(it);
    } else {
      ++it;
    }
  }
}


void LightingEngine::mapInstrumentToColor(const std::string& instrument, int& r, int& g, int& b) {
  // Fixed mapping: kick=red, snare=green, others=blue
  if (instrument == "kick") {
    r = 1; g = 0; b = 0;
  } else if (instrument == "snare") {
    r = 0; g = 1; b = 0;
  } else {
    // clap, chat, ohc -> blue
    r = 0; g = 0; b = 1;
  }
}

} // namespace streaming
} // namespace essentia

