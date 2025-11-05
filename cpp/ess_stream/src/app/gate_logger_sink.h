#ifndef ESSENTIA_STREAMING_GATE_LOGGER_SINK_H
#define ESSENTIA_STREAMING_GATE_LOGGER_SINK_H

/*
 * GateLoggerSink (streaming)
 * - Minimal sink algorithm that logs gate values to HitPredictionLogger
 * - Only logs when gate value >= 0.5 (actual hits)
 * - Uses shared frame counter from logger for accurate timing
 */

#include "streamingalgorithm.h"

// Forward declaration
class HitPredictionLogger;

namespace essentia {
namespace streaming {

class GateLoggerSink : public Algorithm {
public:
    GateLoggerSink();
    
    void declareParameters() {
        declareParameter("instrument_index", "Instrument index (0=kick, 1=snare, 2=clap, 3=chat, 4=ohc)", "[0,4]", 0);
    }
    
    void configure();
    void reset() { Algorithm::reset(); }
    AlgorithmStatus process();
    
    static const char* name;
    static const char* category;
    static const char* description;
    
    /**
     * Register logger instance (called from streaming_pipe.cpp)
     * This uses a global registry pattern to avoid passing pointers through Essentia parameters
     */
    static void register_logger(::HitPredictionLogger* logger);
    
    /**
     * Get registered logger instance
     */
    static ::HitPredictionLogger* get_logger();

private:
    Sink<essentia::Real> _in;
    int _instrument_index;
    static ::HitPredictionLogger* _logger_instance;  // Global logger instance
};

} // namespace streaming
} // namespace essentia

#endif // ESSENTIA_STREAMING_GATE_LOGGER_SINK_H

