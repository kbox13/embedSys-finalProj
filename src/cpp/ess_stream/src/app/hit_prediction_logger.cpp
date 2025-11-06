#include "hit_prediction_logger.h"
#include "instrument_predictor.h"  // For PredictionHit definition
#include <sstream>
#include <iostream>
#include <sys/stat.h>  // For mkdir on Unix/macOS
#include <sys/types.h>
#include <cerrno>  // For errno

// Platform-specific directory creation
#ifdef _WIN32
#include <direct.h>
#define mkdir(dir, mode) _mkdir(dir)
#endif

HitPredictionLogger::HitPredictionLogger(essentia::Real sampleRate, int hopSize, const std::string& logDir)
    : _sample_rate(sampleRate), _hop_size(hopSize), _shared_frame_counter(0),
      _start_time(std::chrono::steady_clock::now()) {
    
    // Create log directory if it doesn't exist
    ensure_log_directory(logDir);
    
    // Get timestamped filename
    _log_filename = get_timestamped_filename(logDir);
    _log_file.open(_log_filename, std::ios::out | std::ios::app);
    
    if (_log_file.is_open()) {
        // Write header with metadata
        _log_file << "# Hit and Prediction Log\n";
        _log_file << "# Sample Rate: " << static_cast<int>(sampleRate) << " Hz\n";
        _log_file << "# Hop Size: " << hopSize << " samples\n";
        _log_file << "# Format: JSON Lines (one object per line)\n";
        _log_file << "# Fields: frame, audio_time, wall_time_ms, wall_time_rel, type, instrument, ...\n";
        _log_file << "#\n";
        std::cerr << "HitPredictionLogger: Writing to " << _log_filename << std::endl;
    } else {
        std::cerr << "HitPredictionLogger: Warning - Failed to open log file: " << _log_filename << std::endl;
    }
}

HitPredictionLogger::~HitPredictionLogger() {
    if (_log_file.is_open()) {
        std::lock_guard<std::mutex> lock(_log_mutex);
        _log_file << "# Log ended. Total frames logged: " << _shared_frame_counter << "\n";
        _log_file.close();
        std::cerr << "HitPredictionLogger: Closed log file" << std::endl;
    }
}

void HitPredictionLogger::log_gate_value(int inst_index, essentia::Real value, int frame_idx) {
    if (!_log_file.is_open()) return;
    
    // Only log when gate fires (value >= 0.5)
    if (value < 0.5) return;
    
    std::lock_guard<std::mutex> lock(_log_mutex);
    
    static const char* inst_names[] = {"kick", "snare", "clap", "chat", "ohc"};
    if (inst_index < 0 || inst_index >= 5) {
        return;  // Invalid instrument index
    }
    
    essentia::Real audio_time = frame_to_audio_time(frame_idx);
    int64_t wall_time_ms = get_wall_time_ms();
    essentia::Real wall_time_rel = get_relative_wall_time();
    
    _log_file << std::fixed << std::setprecision(6);
    _log_file << "{\"frame\":" << frame_idx
              << ",\"audio_time\":" << audio_time
              << ",\"wall_time_ms\":" << wall_time_ms
              << ",\"wall_time_rel\":" << wall_time_rel
              << ",\"type\":\"hit\""
              << ",\"instrument\":\"" << inst_names[inst_index] << "\""
              << ",\"value\":" << value
              << "}\n";
    _log_file.flush();  // Ensure immediate write for debugging
}

void HitPredictionLogger::log_predictions(int frame_idx, essentia::Real currentTime,
                                         const std::vector<std::vector<essentia::streaming::PredictionHit>>& allPredictions) {
    if (!_log_file.is_open()) return;
    
    std::lock_guard<std::mutex> lock(_log_mutex);
    
    static const char* inst_names[] = {"kick", "snare", "clap", "chat", "ohc"};
    int64_t wall_time_ms = get_wall_time_ms();
    essentia::Real wall_time_rel = get_relative_wall_time();
    
    _log_file << std::fixed << std::setprecision(6);
    
    for (size_t i = 0; i < allPredictions.size() && i < 5; ++i) {
        for (const auto& hit : allPredictions[i]) {
            _log_file << "{\"frame\":" << frame_idx
                      << ",\"audio_time\":" << currentTime
                      << ",\"wall_time_ms\":" << wall_time_ms
                      << ",\"wall_time_rel\":" << wall_time_rel
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
    
#ifdef _WIN32
    struct tm tm_buf;
    localtime_s(&tm_buf, &time_t);
#else
    struct tm tm_buf;
    localtime_r(&time_t, &tm_buf);
#endif
    
    std::ostringstream oss;
    oss << logDir << "/hits_predictions_"
        << std::put_time(&tm_buf, "%Y%m%d_%H%M%S") << ".log";
    return oss.str();
}

void HitPredictionLogger::ensure_log_directory(const std::string& logDir) {
    // Try to create directory (mkdir returns 0 on success, -1 if exists or error)
    int result = mkdir(logDir.c_str(), 0755);
    if (result != 0) {
        // Check if directory already exists (errno == EEXIST on Unix)
#ifdef _WIN32
        // On Windows, check GetLastError or just ignore
#else
        if (errno != EEXIST) {
            std::cerr << "HitPredictionLogger: Warning - Could not create log directory: " 
                      << logDir << std::endl;
        }
#endif
    }
}

