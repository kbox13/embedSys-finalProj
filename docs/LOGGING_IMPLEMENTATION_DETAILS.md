# Logging Implementation Details: Option C (Hybrid Approach)

## Why "Hybrid" is Recommended

The "Hybrid" approach (Option C) is called "hybrid" because it combines:
1. **A simple C++ logger class** (not an Essentia algorithm - just a plain class)
2. **A minimal Essentia sink algorithm** (just forwards data to the logger)
3. **Callback mechanism** in InstrumentPredictor (logs when predictions are generated)

**Why this is better than the alternatives:**

- **vs Option A (Full Algorithm)**: Simpler - don't need complex algorithm logic, just forwarding
- **vs Option B (Direct Hooks)**: Cleaner - separates logging concerns from main pipeline
- **vs Python Side**: Accurate - same frame counter for both hits and predictions

## Architecture Overview

```
Current Pipeline:
  HitGateOnset → VectorPack5 → InstrumentPredictor
                ↓
              Pool (for YAML output)

With Logging (Option C):
  HitGateOnset → GateLoggerSink → (calls logger.log_gate_hit())
                ↓
              VectorPack5 → InstrumentPredictor → (calls logger.log_predictions())
                ↓                                ↓
              Pool                             ZMQ
```

## Exact Code Changes

### 1. Create Simple Logger Class (`hit_prediction_logger.h`)

```cpp
#ifndef HIT_PREDICTION_LOGGER_H
#define HIT_PREDICTION_LOGGER_H

#include <fstream>
#include <string>
#include <mutex>
#include <chrono>
#include <iomanip>

class HitPredictionLogger {
public:
    HitPredictionLogger(Real sampleRate, int hopSize, const std::string& logDir = "logs");
    ~HitPredictionLogger();
    
    // Called by GateLoggerSink algorithm for each frame
    void log_gate_value(int inst_index, Real value, int frame_idx);
    
    // Called by InstrumentPredictor when predictions are generated
    void log_predictions(int frame_idx, Real currentTime, 
                        const std::vector<std::vector<PredictionHit>>& allPredictions);
    
    // Get current frame counter (for gate logging)
    int get_and_increment_frame() { return _shared_frame_counter++; }
    
    bool is_enabled() const { return _log_file.is_open(); }

private:
    std::ofstream _log_file;
    std::mutex _log_mutex;  // Thread-safe logging
    int _shared_frame_counter;
    Real _sample_rate;
    int _hop_size;
    std::string _log_filename;
    
    Real frame_to_audio_time(int frame_idx) const {
        return static_cast<Real>(frame_idx) * static_cast<Real>(_hop_size) / _sample_rate;
    }
    
    std::string get_timestamped_filename(const std::string& logDir);
};

#endif
```

### 2. Create Minimal Gate Logger Sink (`gate_logger_sink.h` and `.cpp`)

This is a simple Essentia algorithm that just forwards gate values to the logger.

**gate_logger_sink.h:**
```cpp
#ifndef ESSENTIA_STREAMING_GATE_LOGGER_SINK_H
#define ESSENTIA_STREAMING_GATE_LOGGER_SINK_H

#include "streamingalgorithm.h"

// Forward declaration
class HitPredictionLogger;

namespace essentia {
namespace streaming {

class GateLoggerSink : public Algorithm {
public:
    GateLoggerSink();
    
    void declareParameters() {
        declareParameter("instrument_index", "Instrument index (0=kick, 1=snare, etc.)", "[0,4]", 0);
        declareParameter("logger_ptr", "Pointer to logger (cast to long)", "", 0);
    }
    
    void configure();
    void reset() { Algorithm::reset(); }
    AlgorithmStatus process();
    
    static const char* name;
    static const char* category;
    static const char* description;

private:
    Sink<Real> _in;
    int _instrument_index;
    HitPredictionLogger* _logger;
    int _local_frame_counter;
};

} // namespace streaming
} // namespace essentia

#endif
```

**gate_logger_sink.cpp:**
```cpp
#include "gate_logger_sink.h"
#include "../../hit_prediction_logger.h"  // Path relative to src/app

namespace essentia {
namespace streaming {

const char* GateLoggerSink::name = "GateLoggerSink";
const char* GateLoggerSink::category = "Streaming";
const char* GateLoggerSink::description = "Logs gate values to file for hit detection analysis";

GateLoggerSink::GateLoggerSink() : Algorithm() {
    declareInput(_in, "in", "gate value (0.0 or 1.0)");
    _in.setAcquireSize(1);
    _in.setReleaseSize(1);
    _logger = nullptr;
    _local_frame_counter = 0;
}

void GateLoggerSink::configure() {
    _instrument_index = parameter("instrument_index").toInt();
    // Note: Passing logger pointer via parameter is tricky in Essentia
    // We'll use a static/global registry instead (see below)
    reset();
}

AlgorithmStatus GateLoggerSink::process() {
    AlgorithmStatus status = acquireData();
    if (status != OK) return status;
    
    const std::vector<Real>& inBuf = _in.tokens();
    if (!inBuf.empty() && _logger && _logger->is_enabled()) {
        Real gate_value = inBuf[0];
        // Only log when gate fires (value >= 0.5)
        if (gate_value >= 0.5) {
            int frame = _logger->get_and_increment_frame();
            _logger->log_gate_value(_instrument_index, gate_value, frame);
        }
    }
    
    releaseData();
    return OK;
}

} // namespace streaming
} // namespace essentia
```

**Better approach**: Instead of passing logger pointer through parameters, use a global registry:

```cpp
// In hit_prediction_logger.h, add:
namespace {
    HitPredictionLogger* g_logger_instance = nullptr;
    
    void register_logger(HitPredictionLogger* logger) {
        g_logger_instance = logger;
    }
    
    HitPredictionLogger* get_logger() {
        return g_logger_instance;
    }
}
```

Then in `GateLoggerSink::process()`:
```cpp
_logger = get_logger();  // Get from global registry
```

### 3. Modify InstrumentPredictor to Support Logging

**instrument_predictor.h** - Add logger callback:

```cpp
// Add to private section:
HitPredictionLogger* _logger;

// Add method:
void set_logger(HitPredictionLogger* logger) { _logger = logger; }
```

**instrument_predictor.cpp** - Modify `generatePredictions()`:

```cpp
void InstrumentPredictor::generatePredictions(Real currentTime, bool forceEmit) {
  if (!_zmqInitialized) return;
  
  std::vector<std::vector<PredictionHit>> allPredictions(NUM_INSTRUMENTS);
  
  for (int i = 0; i < NUM_INSTRUMENTS; ++i) {
    allPredictions[i] = predictForInstrument(i, currentTime);
  }
  
  std::string json = serializePredictions(allPredictions, currentTime);
  
  // NEW: Log predictions to file
  if (_logger && _logger->is_enabled()) {
    _logger->log_predictions(_frameCount, currentTime, allPredictions);
  }
  
  publishZeroMQ(json);
}
```

### 4. Wire Everything in `streaming_pipe.cpp`

**Add includes:**
```cpp
#include "hit_prediction_logger.h"
#include "gate_logger_sink.h"
```

**After creating gates (around line 300):**
```cpp
// Create logger instance
HitPredictionLogger logger(sampleRate, hopSize, "logs");
register_logger(&logger);  // Register for GateLoggerSink access

// Create gate logger sinks (one per instrument)
Algorithm *kick_gate_logger = F.create("GateLoggerSink",
                                      "instrument_index", 0);
Algorithm *snare_gate_logger = F.create("GateLoggerSink",
                                        "instrument_index", 1);
Algorithm *clap_gate_logger = F.create("GateLoggerSink",
                                      "instrument_index", 2);
Algorithm *chat_gate_logger = F.create("GateLoggerSink",
                                      "instrument_index", 3);
Algorithm *ohc_gate_logger = F.create("GateLoggerSink",
                                     "instrument_index", 4);

// Wire gates to loggers (parallel to existing connections)
kick_gate->output("out") >> kick_gate_logger->input("in");
snare_gate->output("out") >> snare_gate_logger->input("in");
clap_gate->output("out") >> clap_gate_logger->input("in");
chat_gate->output("out") >> chat_gate_logger->input("in");
ohc_gate->output("out") >> ohc_gate_logger->input("in");
```

**After creating predictor (around line 340):**
```cpp
// Set logger in predictor
static_cast<InstrumentPredictor*>(predictor)->set_logger(&logger);
```

**Register algorithm (around line 177):**
```cpp
AlgorithmFactory::Registrar<streaming::GateLoggerSink> regGateLoggerSink;
```

### 5. Implement Logger Methods

**hit_prediction_logger.cpp:**

```cpp
#include "hit_prediction_logger.h"
#include <sstream>
#include <iomanip>
#include <ctime>
#include <sys/stat.h>  // For mkdir on Unix

HitPredictionLogger::HitPredictionLogger(Real sampleRate, int hopSize, const std::string& logDir)
    : _sample_rate(sampleRate), _hop_size(hopSize), _shared_frame_counter(0) {
    
    // Create log directory if it doesn't exist
    mkdir(logDir.c_str(), 0755);
    
    // Get timestamped filename
    _log_filename = get_timestamped_filename(logDir);
    _log_file.open(_log_filename, std::ios::out | std::ios::app);
    
    if (_log_file.is_open()) {
        // Write header
        _log_file << "# Hit and Prediction Log\n";
        _log_file << "# Sample Rate: " << sampleRate << " Hz\n";
        _log_file << "# Hop Size: " << hopSize << " samples\n";
        _log_file << "# Format: JSON Lines (one object per line)\n";
        _log_file << "#\n";
    }
}

HitPredictionLogger::~HitPredictionLogger() {
    if (_log_file.is_open()) {
        _log_file.close();
    }
}

void HitPredictionLogger::log_gate_value(int inst_index, Real value, int frame_idx) {
    if (!_log_file.is_open()) return;
    
    std::lock_guard<std::mutex> lock(_log_mutex);
    
    static const char* inst_names[] = {"kick", "snare", "clap", "chat", "ohc"};
    Real audio_time = frame_to_audio_time(frame_idx);
    
    _log_file << "{\"frame\":" << frame_idx
              << ",\"audio_time\":" << std::fixed << std::setprecision(6) << audio_time
              << ",\"type\":\"hit\""
              << ",\"instrument\":\"" << inst_names[inst_index] << "\""
              << ",\"value\":" << value
              << "}\n";
    _log_file.flush();  // Ensure immediate write
}

void HitPredictionLogger::log_predictions(int frame_idx, Real currentTime,
                                         const std::vector<std::vector<PredictionHit>>& allPredictions) {
    if (!_log_file.is_open()) return;
    
    std::lock_guard<std::mutex> lock(_log_mutex);
    
    static const char* inst_names[] = {"kick", "snare", "clap", "chat", "ohc"};
    
    for (int i = 0; i < (int)allPredictions.size(); ++i) {
        for (const auto& hit : allPredictions[i]) {
            _log_file << "{\"frame\":" << frame_idx
                      << ",\"audio_time\":" << std::fixed << std::setprecision(6) << currentTime
                      << ",\"type\":\"prediction\""
                      << ",\"instrument\":\"" << inst_names[i] << "\""
                      << ",\"predicted_time\":" << hit.tPredSec
                      << ",\"confidence\":" << hit.confidence
                      << ",\"ci_low\":" << hit.ciLowSec
                      << ",\"ci_high\":" << hit.ciHighSec
                      << ",\"hit_index\":" << hit.hitIndex
                      << "}\n";
        }
    }
    _log_file.flush();
}

std::string HitPredictionLogger::get_timestamped_filename(const std::string& logDir) {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
    localtime_r(&time_t, &tm_buf);  // Use localtime_s on Windows
    
    std::ostringstream oss;
    oss << logDir << "/hits_predictions_"
        << std::put_time(&tm_buf, "%Y%m%d_%H%M%S") << ".log";
    return oss.str();
}
```

### 6. Update CMakeLists.txt

Add logger source files:
```cmake
set(SOURCES
    ...
    src/app/hit_prediction_logger.cpp
    src/app/gate_logger_sink.cpp
    ...
)
```

## Why This Approach Works

1. **Shared Frame Counter**: Logger maintains `_shared_frame_counter` that increments for each gate hit. Predictions use `_frameCount` from InstrumentPredictor, but we can synchronize them.

2. **Frame-Level Precision**: Each gate value is logged exactly when `HitGateOnset` outputs it (before buffering).

3. **Same Time Reference**: Both hits and predictions use `frame * hopSize / sampleRate` for audio time.

4. **Thread-Safe**: Mutex ensures log writes don't interfere with each other.

5. **Non-Intrusive**: Existing pipeline remains unchanged, just adds parallel logging branches.

## Log Output Format

Each line is a JSON object:

**Hit:**
```json
{"frame":1024,"audio_time":0.580000,"type":"hit","instrument":"kick","value":1.0}
```

**Prediction:**
```json
{"frame":1024,"audio_time":0.580000,"type":"prediction","instrument":"snare","predicted_time":0.650000,"confidence":0.85,"ci_low":0.645000,"ci_high":0.655000,"hit_index":1}
```

## Synchronization Note

The `_shared_frame_counter` in logger increments for each gate hit logged. The `_frameCount` in InstrumentPredictor increments every frame. These may not perfectly align because:
- Gate logger only logs when gate fires (value >= 0.5)
- Predictor processes every frame

For accurate correlation, we should use the InstrumentPredictor's `_frameCount` for both, or track frames independently. The logger can query the predictor's frame count, or we can pass frame info through the callback.


