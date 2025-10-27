#ifndef ESSENTIA_STREAMING_ZMQPUBLISHER_H
#define ESSENTIA_STREAMING_ZMQPUBLISHER_H

/*
 * ZeroMQPublisher (streaming)
 * - Publishes a single audio feature stream to ZeroMQ subscribers
 * - Uses PUSH-PULL pattern for multiple publishers on same port
 * - Serializes data as JSON with feature name and values
 * - Simple, focused design with one input stream
 *
 * Input:  TOKEN stream of Real (scalar) per frame
 * Output: None (publishes to ZeroMQ)
 *
 * This follows the Essentia streaming style and integrates with the existing pipeline
 */

#include "streamingalgorithm.h"
#include <zmq.hpp>
#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>

namespace essentia {
namespace streaming {

class ZeroMQPublisher : public Algorithm {
public:
    ZeroMQPublisher();
    ~ZeroMQPublisher();

    void declareParameters() {
        declareParameter("endpoint", "ZeroMQ endpoint to publish to", "", "tcp://*:5555");
        declareParameter("feature_name", "Name of the feature being published", "", "audio_feature");
        declareParameter("buffer_size", "Internal buffer size for batching", "[1,inf)", 10);
        declareParameter("threshold", "Only send when value >= threshold", "[0,inf)", 0.0);
        declareParameter("threshold_mode", "Threshold mode: 'always', 'above', 'below'", "{always,above,below}", "always");
    }

    void configure();
    AlgorithmStatus process();
    void reset();

    static const char* name;
    static const char* category;
    static const char* description;

private:
    // Single input connector
    Sink<Real> _input;

    // Parameters
    std::string _endpoint;
    std::string _feature_name;
    int _buffer_size;
    Real _threshold;
    std::string _threshold_mode;

    // ZeroMQ context and socket
    std::unique_ptr<zmq::context_t> _context;
    std::unique_ptr<zmq::socket_t> _socket;
    
    // Internal buffering for batching
    std::vector<Real> _buffer;
    
    // Frame counter for timestamps
    int _frame_count;
    
    // Helper methods
    void initializeZeroMQ();
    void publishBufferedData();
    std::string serializeFeatures();
    void cleanupZeroMQ();
};

} // namespace streaming
} // namespace essentia

#endif // ESSENTIA_STREAMING_ZMQPUBLISHER_H
