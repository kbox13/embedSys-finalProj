# Predicting Percussive Events to Trigger Lighting Events on an Embedded Device
### ECE M202A: Embedded Systems with Professor Mani Srivastava

This is the final project for a graduate level embedded systems course at UCLA. This project synchronizes lighting to music by detecting percussive events in real-time audio, predicting future events, and triggering precise LED lighting events on an embedded device with microsecond-level timing accuracy.

## Project Overview

The system consists of three main components:

1. **Audio Processing Pipeline (C++)**: Real-time audio analysis using Essentia to detect percussive events, predict future hits, filter and map to lighting commands, and publish directly to MQTT
2. **Visualization & Testing (Python, Optional)**: Real-time visualization and testing tools for development and debugging
3. **Embedded Device (Arduino Nano ESP32)**: Hardware timer-based event scheduler with dual-core architecture

## System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  Audio Source (System Audio / Microphone)                   │
└───────────────────────┬─────────────────────────────────────┘
                        │
                        ↓
┌─────────────────────────────────────────────────────────────┐
│  C++ Essentia Streaming Pipeline                            │
│  - FFT-based audio analysis                                 │
│  - Percussive event detection (gates)                       │
│  - Instrument classification                                │
│  - Prediction Engine (InstrumentPredictor)                  │
│  - Lighting Engine (filters & RGB mapping)                  │
│  - MQTT Publisher (Unix timestamp conversion)               │
└───────────────────────┬─────────────────────────────────────┘
                        │ (MQTT)
                        ↓
┌─────────────────────────────────────────────────────────────┐
│  MQTT Broker (Mosquitto)                                    │
└───────────────────────┬─────────────────────────────────────┘
                        │
                        ↓
┌─────────────────────────────────────────────────────────────┐
│  Arduino Nano ESP32 (Dual-Core)                              │
│  - Core 0: WiFi, MQTT, SNTP time sync                       │
│  - Core 1: Event scheduler, hardware timer, LED control     │
│  - Timing accuracy: ±50µs (hardware timer)                  │
└─────────────────────────────────────────────────────────────┘
```

## Components

### 1. Essentia Streaming Pipeline (C++)

Located in `src/cpp/ess_stream/`, this C++ application processes audio in real-time using the Essentia library.

**Features:**
- Real-time audio capture and FFT analysis
- Percussive event detection using multiple gate algorithms
- Instrument classification (Kick, Snare, Clap, Hi-Hat, Crash)
- Kalman filter-based prediction engine (~100ms lookahead)
- Lighting engine with confidence filtering and RGB color mapping
- Direct MQTT publishing to embedded devices (no Python layer needed)
- ZeroMQ publishing for external visualization/analysis
- Configurable via YAML files and algorithm parameters

**Build & Run:**
```bash
cd src/cpp/ess_stream/build
make
./ess_stream output.yaml
```

See `docs/essentia_pipe.md` for detailed pipeline documentation.

### 2. Python Visualization & Testing (Optional)

Located in `src/python/`, this component provides visualization and testing tools. **Note:** The main prediction and MQTT communication is now handled directly in C++.

**Key Scripts:**
- `zmq_audio_subscriber.py` - Subscribes to Essentia pipeline via ZeroMQ
- `zmq_hit_visualizer.py` - Real-time visualization of detected events
- `test_arduino_mqtt.py` - MQTT test client for Arduino communication
- `beatnet_streaming.py` - BeatNet streaming mode (exploratory, not primary)

**Features:**
- Real-time visualization using Vispy
- Event logging and analysis
- MQTT testing utilities

**Usage:**
```bash
# Start visualization (subscribes to Essentia pipeline)
python src/python/zmq_hit_visualizer.py

# Test Arduino MQTT connection
python src/python/test_arduino_mqtt.py --broker 192.168.1.100
```

### 3. Embedded Device (Arduino Nano ESP32)

Located in `src/cpp/arduino/EmbeddedDevice/`, this is the embedded event scheduler.

**Architecture:**
- **Core 0**: Communication tasks (WiFi, MQTT, SNTP)
- **Core 1**: Execution tasks (Event scheduler, hardware timer ISR)
- **Hardware Timer**: ESP32 hardware timer for microsecond-precise event execution
- **Event Queue**: Thread-safe priority queue for scheduled events

**Features:**
- MQTT subscription for event scheduling
- SNTP/NTP time synchronization
- Hardware timer interrupts for ±50µs timing accuracy
- RGB LED control (GPIO 14, 15, 16)
- Dual-core FreeRTOS task separation

**Setup:**
1. Install PlatformIO
2. Configure WiFi in `src/wifi_config.h` (copy from template)
3. Build and upload: `pio run -t upload`

See `src/cpp/arduino/EmbeddedDevice/README.md` and `ARCHITECTURE.md` for complete details.

## Installation

### Prerequisites

- Python 3.7+ (Python 3.13 tested)
- PlatformIO (for Arduino development)
- MQTT Broker (Mosquitto)
- Essentia library (built from source, see `essentia/`)
- CMake (for C++ build)

### Python Dependencies

```bash
# Create virtual environment
python3 -m venv venv
source venv/bin/activate

# Install dependencies
pip install -r requirements.txt
```

### C++ Dependencies

The Essentia library is included as a submodule. Build instructions:
```bash
cd essentia
# Follow Essentia build instructions
```

**MQTT Libraries (macOS):**
```bash
# Install Paho MQTT C++ and C libraries
brew install paho-mqtt-cpp
brew install paho-mqtt-c
```

The build system will automatically find these libraries in `/opt/homebrew/lib` (Apple Silicon) or `/usr/local/lib` (Intel).

### MQTT Broker

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

## Quick Start

### 1. Start MQTT Broker
```bash
mosquitto -c /usr/local/etc/mosquitto/mosquitto.conf
```
Notes: ensure that mosquitto is configured to listen at the correct address

### 2. Build and Run Essentia Pipeline
```bash
cd src/cpp/ess_stream/build
make
./ess_stream output.yaml
```

### 3. Start Python Visualization
```bash
cd src/python
python zmq_hit_visualizer.py
```

### 4. Upload Arduino Firmware
```bash
cd src/cpp/arduino/EmbeddedDevice
pio run -t upload
pio device monitor  # Monitor serial output
```

### 5. Test End-to-End
```bash
# In another terminal, test MQTT communication
python src/python/test_arduino_mqtt.py --broker "correct broker ip"
```

## Project Structure

```
finalProj/
├── README.md                          # This file
├── requirements.txt                    # Python dependencies
├── essentia/                          # Essentia library (submodule)
├── BeatNet/                           # BeatNet library (submodule)
├── madmom/                            # Madmom library (submodule)
├── docs/                              # Documentation
│   ├── index.md                       # Project overview and timeline
│   ├── essentia_pipe.md               # Essentia pipeline details
│   ├── INSTRUMENT_DETECTION_TUNING_GUIDE.md
│   └── LOGGING_IMPLEMENTATION_DETAILS.md
├── src/
│   ├── cpp/
│   │   ├── ess_stream/                # Essentia streaming pipeline
│   │   │   ├── src/app/               # Application code
│   │   │   └── build/                 # Build directory
│   │   └── arduino/
│   │       └── EmbeddedDevice/        # Arduino Nano ESP32 firmware
│   │           ├── src/main.cpp       # Main firmware code
│   │           ├── ARCHITECTURE.md     # Complete architecture docs
│   │           └── HARDWARE_TIMER_PLAN.md
│   └── python/                        # Python scripts
│       ├── zmq_hit_visualizer.py       # Main visualization
│       ├── zmq_audio_subscriber.py    # ZeroMQ subscriber
│       ├── beat_predictor_kf.py       # Kalman filter predictor
│       ├── test_arduino_mqtt.py       # MQTT test client
│       └── README.md                  # Python scripts documentation
└── data/                              # Data files
└── logs/                              # Log files
```

## Communication Protocols

### ZeroMQ (C++ → External Consumers)
- **Endpoint**: `tcp://*:5555` (gates), `tcp://*:5556` (predictions)
- **Format**: JSON messages with event timestamps and metadata
- **Purpose**: Visualization, logging, and external analysis tools

### MQTT (C++ → Arduino)
- **Broker**: Configurable (default: `localhost:1883`)
- **Topic**: `beat/events/schedule` (configurable)
- **Format**: JSON with Unix timestamps and microsecond precision
- **Client**: Paho MQTT C++ (async client, non-blocking)
- **Features**:
  - Automatic Unix timestamp conversion from prediction times
  - Immediate publishing (no batching delay)
  - QoS 1 for reliable delivery

## Timing & Accuracy

- **Event Detection**: Real-time (processing latency ~10-50ms)
- **Prediction Lookahead**: ~100ms (configurable via LightingEngine parameters)
- **MQTT Latency**: 10-50ms (network dependent, non-blocking async publish)
- **Time Sync Accuracy**: ±50ms initial, ±10ms after sync
- **Event Execution Accuracy**: ±50µs (hardware timer on ESP32)
- **Unix Timestamp Conversion**: High-precision conversion with microsecond accuracy

## Development History

### Initial Exploration
- **BeatNet**: Explored BeatNet for beat detection, but found it insufficient for percussive event detection
- BeatNet focuses on beat locations rather than percussive events

### Current Implementation
- **Essentia Pipeline**: FFT-based analysis with custom gate algorithms
- **Kalman Filter Prediction**: Predicts future events with ~100ms lookahead
- **Lighting Engine**: Filters predictions by confidence, latency, and duplicates; maps instruments to RGB colors
- **Direct MQTT Publishing**: C++ MQTT publisher eliminates Python dependency for production use
- **Hardware Timer**: ESP32 hardware timer for microsecond-precise execution
- **Dual-Core Architecture**: Separation of communication and execution tasks

## Documentation

- [Project Timeline & Status](docs/index.md)
- [Essentia Pipeline Details](docs/essentia_pipe.md)
- [Embedded Device Architecture](src/cpp/arduino/EmbeddedDevice/ARCHITECTURE.md)
- [Hardware Timer Implementation](src/cpp/arduino/EmbeddedDevice/HARDWARE_TIMER_PLAN.md)
- [Python Scripts Guide](src/python/README.md)
- [Instrument Detection Tuning](docs/INSTRUMENT_DETECTION_TUNING_GUIDE.md)

## License

See individual component licenses:
- Essentia: GPL v3
- BeatNet: See BeatNet/LICENSE
- Project code: See LICENSE file

## Acknowledgments

- Essentia library: https://essentia.upf.edu/
- BeatNet: https://github.com/mjhydri/BeatNet
- Arduino ESP32: https://github.com/espressif/arduino-esp32
