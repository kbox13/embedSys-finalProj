#include "zeromq_publisher.h"
#include <essentia/algorithmfactory.h>
#include <iostream>
#include <sstream>
#include <iomanip>

namespace essentia {
namespace streaming {

const char* ZeroMQPublisher::name = "ZeroMQPublisher";
const char* ZeroMQPublisher::category = "Streaming";
const char* ZeroMQPublisher::description =
    "Publishes audio features to ZeroMQ subscribers using PUSH-PULL pattern.\n"
    "Supports multiple publishers on same port and JSON serialization for Python consumers.";

ZeroMQPublisher::ZeroMQPublisher() : Algorithm() {
    // Declare single input connector
    declareInput(_input, "in", "audio feature value");

    // TOKEN mode: one token per call
    _input.setAcquireSize(1);
    _input.setReleaseSize(1);

    // Initialize buffer
    _buffer.reserve(100);
    _frame_count = 0;
}

ZeroMQPublisher::~ZeroMQPublisher() {
    cleanupZeroMQ();
}

void ZeroMQPublisher::configure() {
    _endpoint = parameter("endpoint").toString();
    _feature_name = parameter("feature_name").toString();
    _buffer_size = parameter("buffer_size").toInt();
    _threshold = parameter("threshold").toReal();
    _threshold_mode = parameter("threshold_mode").toString();
    
    reset();
}

void ZeroMQPublisher::reset() {
    Algorithm::reset();
    
    // Clear buffer
    _buffer.clear();
    _frame_count = 0;
    
    // Initialize ZeroMQ
    initializeZeroMQ();
}

void ZeroMQPublisher::initializeZeroMQ() {
    try {
        _context = std::make_unique<zmq::context_t>(1);
        _socket = std::make_unique<zmq::socket_t>(*_context, ZMQ_PUSH);
        
        // Connect to the endpoint (PUSH connects, doesn't bind)
        _socket->connect(_endpoint.c_str());
        
        // Set socket options for better performance
        _socket->set(zmq::sockopt::linger, 0);
        
        std::cout << "ZeroMQ Publisher connected to: " << _endpoint << std::endl;
        
        // Give time for connection to establish
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
    } catch (const zmq::error_t& e) {
        std::cerr << "ZeroMQ initialization error: " << e.what() << std::endl;
        throw;
    }
}

AlgorithmStatus ZeroMQPublisher::process() {
    AlgorithmStatus status = acquireData();
    if (status != OK) return status;

    // Read input data
    const std::vector<Real>& input_data = _input.tokens();

    // Buffer the data only if it meets threshold criteria
    if (!input_data.empty()) {
        Real value = input_data[0];
        bool should_buffer = false;
        
        if (_threshold_mode == "always") {
            should_buffer = true;
        } else if (_threshold_mode == "above") {
            should_buffer = (value >= _threshold);
        } else if (_threshold_mode == "below") {
            should_buffer = (value <= _threshold);
        }
        
        if (should_buffer) {
            _buffer.push_back(value);
        }
    }

    _frame_count++;

    // Publish when buffer is full
    if (_buffer.size() >= _buffer_size) {
        std::cout << "Publishing " << _buffer.size() << " frames" << std::endl;
        publishBufferedData();
    }

    releaseData();
    return OK;
}

void ZeroMQPublisher::publishBufferedData() {
    if (!_socket) return;

    try {
        std::string json_data = serializeFeatures();
        
        // Send as single message (PUSH-PULL pattern)
        zmq::message_t data(json_data.c_str(), json_data.size());
        _socket->send(data, zmq::send_flags::dontwait);
        
        // Clear buffer after successful send
        _buffer.clear();
        
    } catch (const zmq::error_t& e) {
        std::cerr << "ZeroMQ publish error: " << e.what() << std::endl;
    }
}

std::string ZeroMQPublisher::serializeFeatures() {
    std::ostringstream json;
    json << std::fixed << std::setprecision(6);
    
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    
    json << "{\n";
    json << "  \"feature_name\": \"" << _feature_name << "\",\n";
    json << "  \"timestamp\": " << timestamp << ",\n";
    json << "  \"frame_count\": " << _frame_count << ",\n";
    json << "  \"values\": [";
    
    for (size_t i = 0; i < _buffer.size(); ++i) {
        if (i > 0) json << ", ";
        json << _buffer[i];
    }
    
    json << "]\n";
    json << "}";
    
    return json.str();
}

void ZeroMQPublisher::cleanupZeroMQ() {
    if (_socket) {
        _socket->close();
        _socket.reset();
    }
    if (_context) {
        _context->close();
        _context.reset();
    }
}

} // namespace streaming
} // namespace essentia
