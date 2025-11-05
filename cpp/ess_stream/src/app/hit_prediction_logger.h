#ifndef HIT_PREDICTION_LOGGER_H
#define HIT_PREDICTION_LOGGER_H

/*
 * HitPredictionLogger
 * - Logs gate hits and predictions to JSON Lines format
 * - Thread-safe file logging with both audio time and wall clock time
 * - Maintains shared frame counter for accurate timing correlation
 */

#include <essentia/types.h>
#include <fstream>
#include <string>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <ctime>
#include <vector>

// Forward declaration of PredictionHit (defined in instrument_predictor.h)
namespace essentia {
namespace streaming {
struct PredictionHit;
}
}

class HitPredictionLogger {
public:
    /**
     * Constructor
     * @param sampleRate Audio sample rate in Hz
     * @param hopSize Hop size in samples
     * @param logDir Directory for log files (created if doesn't exist)
     */
    HitPredictionLogger(essentia::Real sampleRate, int hopSize, const std::string& logDir = "logs");
    
    /**
     * Destructor - closes log file
     */
    ~HitPredictionLogger();
    
    /**
     * Log a gate hit event
     * @param inst_index Instrument index (0=kick, 1=snare, 2=clap, 3=chat, 4=ohc)
     * @param value Gate value (typically 0.0 or 1.0)
     * @param frame_idx Frame index for audio time calculation
     */
    void log_gate_value(int inst_index, essentia::Real value, int frame_idx);
    
    /**
     * Log prediction events from InstrumentPredictor
     * @param frame_idx Frame index when predictions were generated
     * @param currentTime Audio time (seconds) when predictions were generated
     * @param allPredictions Vector of prediction vectors, one per instrument
     */
    void log_predictions(int frame_idx, essentia::Real currentTime,
                        const std::vector<std::vector<essentia::streaming::PredictionHit>>& allPredictions);
    
    /**
     * Get the current frame counter without incrementing
     */
    int get_frame() const { return _shared_frame_counter; }
    
    /**
     * Increment the shared frame counter (call once per frame by one logger)
     */
    void increment_frame() { _shared_frame_counter++; }
    
    /**
     * Get and increment the shared frame counter
     * Used by a single designated logger to track frame progression
     */
    int get_and_increment_frame() { 
        int current = _shared_frame_counter;
        _shared_frame_counter++;
        return current;
    }
    
    /**
     * Check if logger is enabled (file is open)
     */
    bool is_enabled() const { return _log_file.is_open(); }
    
    /**
     * Get the log filename (for debugging/info)
     */
    const std::string& get_log_filename() const { return _log_filename; }

private:
    std::ofstream _log_file;
    std::mutex _log_mutex;  // Thread-safe logging
    int _shared_frame_counter;
    essentia::Real _sample_rate;
    int _hop_size;
    std::string _log_filename;
    std::chrono::steady_clock::time_point _start_time;  // For relative wall time
    
    /**
     * Convert frame index to audio time in seconds
     */
    essentia::Real frame_to_audio_time(int frame_idx) const {
        return static_cast<essentia::Real>(frame_idx) * static_cast<essentia::Real>(_hop_size) / _sample_rate;
    }
    
    /**
     * Get current wall clock time in milliseconds since epoch
     */
    int64_t get_wall_time_ms() const {
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
    }
    
    /**
     * Get relative wall time in seconds since logger creation
     */
    essentia::Real get_relative_wall_time() const {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            now - _start_time).count();
        return static_cast<essentia::Real>(elapsed) / 1000000.0;
    }
    
    /**
     * Generate timestamped log filename
     */
    std::string get_timestamped_filename(const std::string& logDir);
    
    /**
     * Helper to ensure log directory exists (platform-specific)
     */
    void ensure_log_directory(const std::string& logDir);
};

#endif // HIT_PREDICTION_LOGGER_H

