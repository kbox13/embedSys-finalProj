# Python Scripts: Prediction, Visualization, and Communication

This directory contains Python scripts for real-time percussive event prediction, visualization, and communication with the embedded device.

## Overview

The Python layer sits between the C++ Essentia streaming pipeline and the Arduino embedded device:
- **Input**: ZeroMQ messages from Essentia pipeline (C++)
- **Processing**: Kalman filter-based prediction, time synchronization
- **Output**: MQTT messages to Arduino, real-time visualization

## Scripts

### Core Scripts

#### `zmq_hit_visualizer.py`
**Main visualization and prediction script**

Real-time visualization of percussive events from the Essentia pipeline with beat prediction.

**Features:**
- Subscribes to Essentia pipeline via ZeroMQ
- Real-time visualization using Vispy
- Kalman filter-based beat prediction (~100ms lookahead)
- Instrument classification visualization (Kick, Snare, Clap, Hi-Hat, Crash)
- MQTT event publishing to Arduino devices
- NTP time synchronization

**Usage:**
```bash
# Default (subscribes to tcp://*:5555 for gates, tcp://*:5556 for predictions)
python zmq_hit_visualizer.py

# Custom endpoints
python zmq_hit_visualizer.py tcp://*:5555 tcp://*:5556

# With MQTT publishing
python zmq_hit_visualizer.py --mqtt-broker 192.168.1.100
```

#### `zmq_audio_subscriber.py`
**ZeroMQ subscriber for Essentia pipeline**

Simple subscriber that receives and displays events from the Essentia pipeline.

**Usage:**
```bash
python zmq_audio_subscriber.py
```

#### `beat_predictor_kf.py`
**Kalman filter-based beat prediction**

Standalone prediction module using Kalman filtering to predict future beat events.

**Features:**
- Kalman filter for tempo and phase estimation
- ~100ms prediction lookahead
- Handles tempo variations and phase drift

**Usage:**
```python
from beat_predictor_kf import BeatPredictorKF

predictor = BeatPredictorKF()
predicted_time = predictor.predict_next_beat(detected_beats)
```

#### `test_arduino_mqtt.py`
**MQTT test client for Arduino communication**

Test script to verify MQTT connectivity and event scheduling with Arduino devices.

**Features:**
- NTP time synchronization
- Single and batch event scheduling
- Time sync messages
- Event timing verification

**Usage:**
```bash
# Test with default broker (localhost)
python test_arduino_mqtt.py

# Custom broker
python test_arduino_mqtt.py --broker 192.168.1.100 --port 1883

# Schedule single event
python test_arduino_mqtt.py --single-event

# Schedule batch of events
python test_arduino_mqtt.py --batch-events 10
```

### Supporting Scripts

#### `beatnet_streaming.py`
**BeatNet streaming mode (exploratory)**

Real-time beat tracking using BeatNet. This was explored early in the project but is not the primary detection method (Essentia pipeline is used instead).

**Note**: BeatNet focuses on beat locations rather than percussive events, so it's not ideal for lighting synchronization.

**Usage:**
```bash
# List audio devices
python beatnet_streaming.py --list-devices

# Run with visualization
python beatnet_streaming.py --plot
```

#### `setup_macos_audio.py`
**macOS audio setup helper**

Checks audio routing configuration and permissions for macOS.

**Usage:**
```bash
python setup_macos_audio.py
```

#### `beat_logger.py`
**Event logging utility**

Logs detected and predicted events to files for analysis.

#### `beat_visualizer.py`
**Visualization framework**

Core visualization classes using Vispy for real-time event display.

#### `movement_planning.py`
**Movement planning utilities**

(If applicable - check file for details)

## Communication Protocols

### ZeroMQ (Input from C++ Pipeline)

**Endpoints:**
- `tcp://*:5555` - Gate hits (detected percussive events)
- `tcp://*:5556` - Predictions (future event predictions)

**Message Format:**
```json
{
  "timestamp": 1234567890.123,
  "instrument": "kick",
  "confidence": 0.95,
  "gate_type": "onset"
}
```

### MQTT (Output to Arduino)

**Broker:** Localhost or specified IP (default: `localhost:1883`)

**Topics:**
- `beat/events/schedule` - Schedule LED events
- `beat/time/sync` - Time synchronization messages

**Message Format:**
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

## Dependencies

All dependencies are listed in `requirements.txt` at the project root.

**Key dependencies:**
- `paho-mqtt` - MQTT client
- `pyzmq` - ZeroMQ Python bindings
- `ntplib` - NTP time synchronization
- `vispy` - Real-time visualization
- `numpy` - Numerical computing
- `scipy` - Scientific computing (for Kalman filter)

**Installation:**
```bash
# From project root
pip install -r requirements.txt
```

## Quick Start

### 1. Start Essentia Pipeline (C++)
```bash
cd ../cpp/ess_stream/build
./ess_stream output.yaml
```

### 2. Start Visualization
```bash
python zmq_hit_visualizer.py
```

### 3. Test MQTT (Optional)
```bash
# In another terminal
python test_arduino_mqtt.py --broker localhost
```

## Architecture

```
┌─────────────────────────────────────┐
│  Essentia Pipeline (C++)            │
│  - Publishes via ZeroMQ             │
└──────────────┬──────────────────────┘
               │ ZeroMQ
               ↓
┌─────────────────────────────────────┐
│  zmq_hit_visualizer.py              │
│  - Subscribes to ZeroMQ             │
│  - Kalman filter prediction         │
│  - Visualization                    │
│  - MQTT publishing                 │
└──────────────┬──────────────────────┘
               │ MQTT
               ↓
┌─────────────────────────────────────┐
│  Arduino Nano ESP32                 │
│  - Receives events via MQTT         │
│  - Executes with hardware timer     │
└─────────────────────────────────────┘
```

## Troubleshooting

### ZeroMQ Connection Issues
- Verify Essentia pipeline is running and publishing
- Check firewall settings for port 5555/5556
- Ensure endpoints match between publisher and subscriber

### MQTT Connection Issues
- Verify MQTT broker is running: `mosquitto_sub -h localhost -t "test" -v`
- Check broker IP address in script arguments
- Verify firewall allows port 1883

### Visualization Not Appearing
- Check that Vispy backend is installed (PyQt6, PySide2, or glfw)
- Verify events are being received (check console output)
- Try different backends: `vispy.use('PyQt6')`

### Time Synchronization Issues
- Ensure internet connection for NTP
- Check `ntplib` is installed: `pip install ntplib`
- Verify NTP server is accessible

## Performance

- **ZeroMQ Latency**: <1ms (local)
- **Prediction Latency**: <10ms
- **MQTT Latency**: 10-50ms (network dependent)
- **Visualization Update Rate**: 60 FPS (configurable)

## Notes

- The Kalman filter predictor requires initial beat detections to establish tempo
- Prediction accuracy improves with consistent tempo
- MQTT events are scheduled with Unix timestamps for precise timing
- Visualization can handle high event rates (1000+ events/second)
