#ifndef ESSENTIA_STREAMING_MQTT_PUBLISHER_H
#define ESSENTIA_STREAMING_MQTT_PUBLISHER_H

/*
 * MQTTPublisher (streaming)
 * - Converts lighting commands to Unix timestamps
 * - Publishes to MQTT broker for embedded device
 *
 * Input:  TOKEN stream of std::vector<LightingCommand> per frame (from LightingEngine)
 * Output: None (sink algorithm)
 */

#include "streamingalgorithm.h"
#include "prediction_types.h"
#include <essentia/algorithmfactory.h>
#include <string>
#include <memory>
#include <vector>
#include <sys/time.h>
#include <time.h>

// Forward declare MQTT client (Paho MQTT C++)
namespace mqtt {
  class async_client;
  class connect_options;
}

namespace essentia {
namespace streaming {

// LightingCommand is now defined in prediction_types.h

class MQTTPublisher : public Algorithm {
public:
  MQTTPublisher();
  ~MQTTPublisher();

  void declareParameters() {
    declareParameter("broker_host", "MQTT broker hostname", "", "localhost");
    declareParameter("broker_port", "MQTT broker port", "[1,65535]", 1883);
    declareParameter("topic", "MQTT topic for events", "", "beat/events/schedule");
    declareParameter("client_id", "MQTT client ID", "", "essentia_lighting");
    // Note: batch_size and batch_interval_ms kept for backward compatibility but not used
    // All commands are published immediately upon arrival
    declareParameter("batch_size", "Number of events to batch (unused - immediate publish)", "[1,50]", 1);
    declareParameter("batch_interval_ms", "Maximum time to wait before sending batch (unused - immediate publish)", "[10,1000]", 50);
  }

  void configure();
  AlgorithmStatus process();
  void reset();

  static const char* name;
  static const char* category;
  static const char* description;

private:
  // IO connector
  Sink<std::vector<LightingCommand>> _in;

  // Parameters
  std::string _brokerHost;
  int _brokerPort;
  std::string _topic;
  std::string _clientId;
  // Note: _batchSize and _batchIntervalMs removed - not used (immediate publishing)

  // MQTT client
  std::unique_ptr<mqtt::async_client> _mqttClient;
  std::unique_ptr<mqtt::connect_options> _connOpts;
  bool _mqttConnected;
  
  // Time synchronization
  time_t _startUnixTime;
  long _startMicroseconds;
  Real _startTimeSec;
  bool _timeInitialized;

  // Helper methods
  void initializeMQTT();
  void cleanupMQTT();
  void convertToUnixTime(const LightingCommand& cmd, time_t& unixTime, long& microseconds);
  std::string serializeMQTTMessage(time_t unixTime, long microseconds, Real confidence, int r, int g, int b, const std::string& eventId);
  void publishSingleCommand(const LightingCommand& cmd);
  void initializeTime();
};

} // namespace streaming
} // namespace essentia

#endif // ESSENTIA_STREAMING_MQTT_PUBLISHER_H

