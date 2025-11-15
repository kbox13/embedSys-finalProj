#ifndef ESSENTIA_STREAMING_PREDICTION_TYPES_H
#define ESSENTIA_STREAMING_PREDICTION_TYPES_H

/*
 * Shared data structures for passing predictions between components
 * Eliminates JSON parsing overhead and preserves full precision
 */

#include <essentia/types.h>
#include <string>
#include <vector>

namespace essentia {
namespace streaming {

// Forward declarations
struct PredictionHit;
struct InstrumentPrediction;
struct PredictionOutput;
struct LightingCommand;

// Prediction hit structure (from InstrumentPredictor)
struct PredictionHit {
  Real tPredSec;
  Real ciLowSec;
  Real ciHighSec;
  Real confidence;
  int hitIndex;
  
  PredictionHit() : tPredSec(0), ciLowSec(0), ciHighSec(0), confidence(0), hitIndex(1) {}
  
  PredictionHit(Real tPred, Real ciLow, Real ciHigh, Real conf, int idx)
    : tPredSec(tPred), ciLowSec(ciLow), ciHighSec(ciHigh), confidence(conf), hitIndex(idx) {}
};

// Instrument prediction structure (contains instrument info and hits)
struct InstrumentPrediction {
  std::string instrument;
  Real tempoBpm;
  Real periodSec;
  Real phase;
  Real confidenceGlobal;
  bool warmupComplete;
  std::vector<PredictionHit> hits;
  
  InstrumentPrediction() 
    : instrument(""), tempoBpm(0), periodSec(0), phase(0), 
      confidenceGlobal(0), warmupComplete(false) {}
};

// Complete prediction output from InstrumentPredictor
struct PredictionOutput {
  Real timestampSec;
  int frameIdx;
  std::vector<InstrumentPrediction> predictions;
  
  PredictionOutput() : timestampSec(0), frameIdx(0) {}
  
  bool isEmpty() const {
    return predictions.empty();
  }
};

// Lighting command structure (from LightingEngine to MQTTPublisher)
struct LightingCommand {
  std::string instrument;
  Real tPredSec;
  Real confidence;
  int r, g, b;
  std::string eventId;
  
  LightingCommand() 
    : instrument(""), tPredSec(0), confidence(0), r(0), g(0), b(0), eventId("") {}
};

} // namespace streaming
} // namespace essentia

#endif // ESSENTIA_STREAMING_PREDICTION_TYPES_H

