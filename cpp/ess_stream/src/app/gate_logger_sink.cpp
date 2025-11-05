#include "gate_logger_sink.h"
#include "hit_prediction_logger.h"

namespace essentia {
namespace streaming {

// Static logger instance (set by register_logger)
::HitPredictionLogger* GateLoggerSink::_logger_instance = nullptr;

const char* GateLoggerSink::name = "GateLoggerSink";
const char* GateLoggerSink::category = "Streaming";
const char* GateLoggerSink::description = 
    "Logs gate values to file for hit detection analysis. Only logs when gate fires (value >= 0.5).";

GateLoggerSink::GateLoggerSink() : Algorithm() {
    declareInput(_in, "in", "gate value (0.0 or 1.0)");
    _in.setAcquireSize(1);
    _in.setReleaseSize(1);
    _instrument_index = 0;
}

void GateLoggerSink::configure() {
    _instrument_index = parameter("instrument_index").toInt();
    if (_instrument_index < 0 || _instrument_index > 4) {
        _instrument_index = 0;  // Default to kick
    }
    reset();
}

AlgorithmStatus GateLoggerSink::process() {
    AlgorithmStatus status = acquireData();
    if (status != OK) return status;
    
    const std::vector<essentia::Real>& inBuf = _in.tokens();
    if (!inBuf.empty()) {
        essentia::Real gate_value = inBuf[0];
        
        // Get logger from static registry
        ::HitPredictionLogger* logger = get_logger();
        
        if (logger && logger->is_enabled()) {
            // Only kick logger (index 0) increments frame counter to avoid double-counting
            // All gate loggers process the same frame simultaneously, so we only need one increment
            int frame;
            if (_instrument_index == 0) {
                // Kick logger: increment and get current frame
                frame = logger->get_and_increment_frame();
            } else {
                // Other loggers: just get current frame (already incremented by kick logger)
                frame = logger->get_frame();
            }
            
            // Only log when gate fires (value >= 0.5)
            if (gate_value >= 0.5) {
                logger->log_gate_value(_instrument_index, gate_value, frame);
            }
        }
    }
    
    releaseData();
    return OK;
}

void GateLoggerSink::register_logger(::HitPredictionLogger* logger) {
    _logger_instance = logger;
}

::HitPredictionLogger* GateLoggerSink::get_logger() {
    return _logger_instance;
}

} // namespace streaming
} // namespace essentia

