# MQTT Testing Commands

## 1. Start Mosquitto Broker

In **Terminal 1**, start the MQTT broker:

```bash
# Start mosquitto broker (runs in foreground, press Ctrl+C to stop)
mosquitto -c /opt/homebrew/etc/mosquitto/mosquitto.conf

# OR start in background:
mosquitto -c /opt/homebrew/etc/mosquitto/mosquitto.conf -d
```

To stop the background broker:
```bash
pkill mosquitto
```

## 2. Subscribe to MQTT Messages

In **Terminal 2**, subscribe to the lighting events topic:

```bash
# Simple subscriber (shows raw messages)
mosquitto_sub -h localhost -p 1883 -t "beat/events/schedule" -v

# Pretty-print JSON (if you have Python installed)
mosquitto_sub -h localhost -p 1883 -t "beat/events/schedule" -v | while read line; do
    echo "$line" | python3 -m json.tool 2>/dev/null || echo "$line"
done
```

## 3. Run ess_stream

In **Terminal 3**, run the streaming pipeline:

```bash
cd /Users/kevmanbox/embedSys/finalProj/src/cpp/ess_stream/build

# Run with default 20 second timeout
./ess_stream output.yaml

# Run with custom timeout (e.g., 60 seconds)
./ess_stream output.yaml 60
```

## Quick Test Script

You can also use the provided test script:

```bash
# In Terminal 1: Start broker and subscriber together
./test_mqtt_setup.sh
```

## Expected MQTT Message Format

The MQTT publisher will send messages like:

```json
{
  "unix_time": 1234567890,
  "microseconds": 123456,
  "r": 1,
  "g": 0,
  "b": 0,
  "event_id": "kick_1.234_0.85"
}
```

Where:
- `r`, `g`, `b` are 0 or 1 (RGB values)
- `unix_time` and `microseconds` are the predicted event time
- `event_id` is a unique identifier for the event

## Troubleshooting

If mosquitto fails to start:
```bash
# Check if port 1883 is already in use
lsof -i :1883

# Try starting with verbose logging
mosquitto -c /opt/homebrew/etc/mosquitto/mosquitto.conf -v
```

If you see connection errors in ess_stream:
- Make sure mosquitto is running
- Check that the broker is listening on localhost:1883
- Verify firewall settings if needed

