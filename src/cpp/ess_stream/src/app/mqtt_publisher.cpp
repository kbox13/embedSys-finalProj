#include "mqtt_publisher.h"
#include "prediction_types.h"
#include <mqtt/async_client.h>
#include <mqtt/connect_options.h>
#include <mqtt/message.h>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <iostream>
#include <cstring>
#include <cmath>

namespace essentia {
namespace streaming {

const char* MQTTPublisher::name = "MQTTPublisher";
const char* MQTTPublisher::category = "Streaming";
const char* MQTTPublisher::description =
  "Publishes lighting commands to MQTT broker with Unix timestamp conversion.";

MQTTPublisher::MQTTPublisher() : Algorithm() {
  declareInput(_in, "in", "lighting commands from LightingEngine");
  _in.setAcquireSize(1);
  _in.setReleaseSize(1);
  
  _mqttConnected = false;
  _timeInitialized = false;
  _startTimeSec = 0.0;
}

MQTTPublisher::~MQTTPublisher() {
  cleanupMQTT();
}

void MQTTPublisher::configure() {
  _brokerHost = parameter("broker_host").toString();
  _brokerPort = parameter("broker_port").toInt();
  _topic = parameter("topic").toString();
  _clientId = parameter("client_id").toString();
  // Note: batch_size and batch_interval_ms parameters kept for compatibility but not used
  // All commands are published immediately upon arrival
  
  reset();
}

void MQTTPublisher::reset() {
  Algorithm::reset();
  _timeInitialized = false;
  
  initializeTime();
  initializeMQTT();
}

void MQTTPublisher::initializeTime() {
  struct timeval tv;
  if (gettimeofday(&tv, nullptr) == 0) {
    _startUnixTime = tv.tv_sec;
    _startMicroseconds = tv.tv_usec;
    _startTimeSec = static_cast<Real>(tv.tv_sec) + static_cast<Real>(tv.tv_usec) / 1000000.0;
    _timeInitialized = true;
    std::cout << "MQTTPublisher: Time initialized - Unix time: " << _startUnixTime 
              << ", microseconds: " << _startMicroseconds << std::endl;
  } else {
    std::cerr << "MQTTPublisher: Failed to get system time" << std::endl;
    _timeInitialized = false;
  }
}

void MQTTPublisher::initializeMQTT() {
  try {
    // Create broker URI
    std::ostringstream uri;
    uri << "tcp://" << _brokerHost << ":" << _brokerPort;
    
    // Create async client
    _mqttClient = std::make_unique<mqtt::async_client>(uri.str(), _clientId);
    
    // Set connection options
    _connOpts = std::make_unique<mqtt::connect_options>();
    _connOpts->set_clean_session(true);
    _connOpts->set_automatic_reconnect(true);
    
    // Connect to broker
    std::cout << "MQTTPublisher: Connecting to broker at " << uri.str() << "..." << std::endl;
    _mqttClient->connect(*_connOpts)->wait();
    _mqttConnected = true;
    std::cout << "MQTTPublisher: Connected to MQTT broker" << std::endl;
    
  } catch (const mqtt::exception& e) {
    std::cerr << "MQTTPublisher: MQTT connection error: " << e.what() << std::endl;
    _mqttConnected = false;
  } catch (const std::exception& e) {
    std::cerr << "MQTTPublisher: Error initializing MQTT: " << e.what() << std::endl;
    _mqttConnected = false;
  }
}

void MQTTPublisher::cleanupMQTT() {
  if (_mqttClient && _mqttConnected) {
    try {
      _mqttClient->disconnect()->wait();
      _mqttConnected = false;
      std::cout << "MQTTPublisher: Disconnected from MQTT broker" << std::endl;
    } catch (const std::exception& e) {
      std::cerr << "MQTTPublisher: Error disconnecting: " << e.what() << std::endl;
    }
  }
  _mqttClient.reset();
  _connOpts.reset();
}

AlgorithmStatus MQTTPublisher::process() {
  AlgorithmStatus status = acquireData();
  if (status != OK) return status;

  const std::vector<std::vector<LightingCommand>>& inTokens = _in.tokens();
  
  // If no input or empty input, just release and return
  if (inTokens.empty() || inTokens[0].empty()) {
    releaseData();
    return OK;
  }
  
  // Get lighting commands directly (no parsing needed!)
  const std::vector<LightingCommand>& commands = inTokens[0];
  
  // Publish each command immediately as it arrives (no batching, no delay)
  for (const auto& cmd : commands) {
    publishSingleCommand(cmd);
  }
  
  releaseData();
  return OK;
}


void MQTTPublisher::convertToUnixTime(const LightingCommand& cmd, time_t& unixTime, long& microseconds) {
  if (!_timeInitialized) {
    // Fallback: use current time
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    unixTime = tv.tv_sec;
    microseconds = tv.tv_usec;
    return;
  }
  
  // The predictor's t_pred_sec is relative to when processing started (in seconds since start)
  // We need to add it to the Unix time when processing started to get absolute Unix time
  
  // Split cmd.tPredSec into integer seconds and fractional microseconds
  // This avoids floating point precision issues with large Unix timestamps
  Real tPredSec = cmd.tPredSec;
  
  // Get integer seconds from prediction time
  time_t predSeconds = static_cast<time_t>(std::floor(tPredSec));
  
  // Get fractional seconds and convert to microseconds with high precision
  // Use a more precise method: multiply first, then extract integer part
  Real fractionalSec = tPredSec - static_cast<Real>(predSeconds);
  
  // Convert fractional seconds to microseconds with proper rounding
  // Multiply by 1e6 and round to nearest integer
  long predMicroseconds = static_cast<long>(std::round(fractionalSec * 1000000.0));
  
  // Add start time components separately to avoid precision loss
  // Start with seconds
  unixTime = _startUnixTime + predSeconds;
  
  // Add microseconds (handling overflow)
  microseconds = _startMicroseconds + predMicroseconds;
  
  // Handle overflow: if microseconds >= 1000000, carry over to seconds
  if (microseconds >= 1000000) {
    time_t carrySeconds = microseconds / 1000000;
    unixTime += carrySeconds;
    microseconds = microseconds % 1000000;
  }
  
  // Handle underflow: if microseconds < 0, borrow from seconds
  // (shouldn't happen in normal operation, but handle for safety)
  if (microseconds < 0) {
    time_t borrowSeconds = (microseconds - 999999) / 1000000; // Negative division
    unixTime += borrowSeconds;
    microseconds = microseconds - (borrowSeconds * 1000000);
  }
  
  // Final safety check
  if (microseconds < 0 || microseconds >= 1000000) {
    // This should never happen, but if it does, normalize
    time_t adjust = microseconds / 1000000;
    unixTime += adjust;
    microseconds = microseconds - (adjust * 1000000);
    if (microseconds < 0) {
      unixTime--;
      microseconds += 1000000;
    }
  }
}

std::string MQTTPublisher::serializeMQTTMessage(time_t unixTime, long microseconds,
                                                 Real confidence, int r, int g, int b, const std::string& eventId) {
  std::ostringstream oss;
  oss << std::fixed;
  oss << "{\"unix_time\":" << unixTime
      << ",\"microseconds\":" << microseconds
      << ",\"confidence\":" << confidence
      << ",\"r\":" << r
      << ",\"g\":" << g
      << ",\"b\":" << b
      << ",\"event_id\":\"" << eventId << "\"}";
  return oss.str();
}

void MQTTPublisher::publishSingleCommand(const LightingCommand& cmd) {
  if (!_mqttConnected || !_mqttClient) {
    return;
  }
  
  try {
    time_t unixTime;
    long microseconds;
    convertToUnixTime(cmd, unixTime, microseconds);
    
    std::string payload = serializeMQTTMessage(unixTime, microseconds, cmd.confidence, cmd.r, cmd.g, cmd.b, cmd.eventId);
    
    // Create MQTT message
    mqtt::message_ptr msg = mqtt::make_message(_topic, payload);
    msg->set_qos(1); // QoS 1 for reliability
    
    // Publish (truly async, non-blocking - fire and forget)
    // Don't wait for completion to avoid blocking the pipeline
    _mqttClient->publish(msg);
    
    // Reduced console output - only log occasionally to avoid blocking
    // Commented out for performance - uncomment for debugging
    // std::cout << "MQTTPublisher: Published event - ID=" << cmd.eventId 
    //           << ", RGB=(" << cmd.r << "," << cmd.g << "," << cmd.b << ")"
    //           << ", time=" << unixTime << "." << std::setfill('0') << std::setw(6) << microseconds << std::endl;
    
  } catch (const mqtt::exception& e) {
    std::cerr << "MQTTPublisher: MQTT publish error: " << e.what() << std::endl;
  } catch (const std::exception& e) {
    std::cerr << "MQTTPublisher: Error publishing command: " << e.what() << std::endl;
  }
}

// publishBatch() removed - all commands are now published immediately

} // namespace streaming
} // namespace essentia

