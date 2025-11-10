# Embedded Device: Event Scheduler System

## Overview
This Arduino Nano ESP32 implementation receives beat prediction events via MQTT and executes precise LED lighting events with microsecond-level timing accuracy.

## Architecture
- **Core 0**: Communication (WiFi, MQTT, SNTP time sync)
- **Core 1**: Execution (Event scheduler, LED control)

See [ARCHITECTURE.md](ARCHITECTURE.md) for complete details.

## Prerequisites

### Software
- PlatformIO (or Arduino IDE)
- MQTT Broker (Mosquitto) running on host machine

### Hardware
- Arduino Nano ESP32
- WiFi network access

## Setup

### 1. Install MQTT Broker (Mosquitto)

**macOS:**
```bash
brew install mosquitto
mosquitto -c /usr/local/etc/mosquitto/mosquitto.conf
```

**Linux:**
```bash
sudo apt-get install mosquitto mosquitto-clients
sudo systemctl start mosquitto
```

**Windows:**
Download from: https://mosquitto.org/download/

### 2. Configure WiFi and MQTT

**Important:** WiFi credentials are stored in a separate file that is NOT committed to git.

1. Copy the template file:
   ```bash
   cp src/wifi_config.h.template src/wifi_config.h
   ```

2. Edit `src/wifi_config.h` with your credentials:
   ```cpp
   static const char* WIFI_SSID = "YOUR_WIFI_SSID";
   static const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
   static const char* MQTT_BROKER = "192.168.1.100";  // Your broker IP
   ```

The `wifi_config.h` file is automatically ignored by git (see `.gitignore`), so your credentials will never be committed.

### 3. Build and Upload

```bash
# Build
pio run

# Upload
pio run -t upload

# Monitor serial output
pio device monitor
```

## MQTT Topics

### Subscribed Topics (Device listens to):
- `beat/events/schedule` - Schedule LED events
- `beat/time/sync` - Time synchronization
- `beat/commands/all` - Commands for all devices

### Published Topics (Future):
- `beat/devices/{id}/status` - Device status

## Message Formats

### Schedule Event (Single)
```json
{
  "unix_time": 1234567890,
  "microseconds": 123456,
  "r": 1,
  "g": 0,
  "b": 0,
  "event_id": 12345
}
```

### Schedule Event (Batch)
```json
{
  "events": [
    {
      "unix_time": 1234567890,
      "microseconds": 123456,
      "r": 1, "g": 0, "b": 0,
      "event_id": 12345
    },
    {
      "unix_time": 1234567900,
      "microseconds": 123456,
      "r": 0, "g": 1, "b": 0,
      "event_id": 12346
    }
  ],
  "batch_id": 67890
}
```

### Time Sync
```json
{
  "unix_time": 1234567890,
  "microseconds": 123456,
  "sync_source": "host"
}
```

## Testing

### Test MQTT Connection

```bash
# Subscribe to events (to see what device receives)
mosquitto_sub -h localhost -t "beat/events/schedule" -v

# Publish test event
mosquitto_pub -h localhost -t "beat/events/schedule" -m '{
  "unix_time": 1234567890,
  "microseconds": 123456,
  "r": 1,
  "g": 0,
  "b": 0,
  "event_id": 999
}'
```

### Test Time Sync

```bash
# Get current Unix timestamp
python3 -c "import time; print(int(time.time()))"

# Publish time sync
mosquitto_pub -h localhost -t "beat/time/sync" -m '{
  "unix_time": 1234567890,
  "microseconds": 123456,
  "sync_source": "host"
}'
```

## Serial Monitor Output

The device will output:
- WiFi connection status
- MQTT connection status
- SNTP time sync status
- Event scheduling confirmations
- Event execution notifications

## Timing Accuracy

- **Event Execution**: ±1ms (using micros() polling)
- **Time Sync**: ±50ms initial, ±10ms after sync
- **Suitable for**: Beat prediction with ~100ms lookahead

## Troubleshooting

### WiFi Not Connecting
- Check SSID and password
- Verify WiFi network is 2.4GHz (ESP32 doesn't support 5GHz)
- Check Serial Monitor for connection errors

### MQTT Not Connecting
- Verify MQTT broker is running: `mosquitto_sub -h localhost -t "test" -v`
- Check broker IP address in code
- Verify firewall allows port 1883
- Check Serial Monitor for MQTT errors

### Time Sync Issues
- Ensure device has internet access (for NTP)
- Check Serial Monitor for SNTP status
- Time sync may take 2-5 seconds on first boot

### Events Not Executing
- Check Serial Monitor for event scheduling messages
- Verify Unix timestamps are in the future
- Check time sync status
- Verify event queue isn't full

## Future Enhancements

- [ ] Hardware timer interrupts for ±50µs precision
- [ ] Event cancellation via MQTT
- [ ] Device status reporting
- [ ] Pattern sequences
- [ ] Fade effects
- [ ] Multi-device coordination

## Files

- `src/main.cpp` - Main implementation
- `ARCHITECTURE.md` - Complete architecture documentation
- `platformio.ini` - PlatformIO configuration

## License
See project root for license information.

