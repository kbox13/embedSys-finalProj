---
layout: page
title: Essentia Pipeline
---

# Essentia Streaming Pipeline

The Essentia streaming pipeline is the core audio processing component that detects percussive events, predicts future hits, and publishes lighting commands to embedded devices.

## Overview

The pipeline processes real-time audio through multiple stages:
1. Audio capture and framing
2. Spectral analysis (FFT, Mel bands)
3. Instrument classification
4. Percussive event detection (gates)
5. Prediction of future events
6. Lighting command generation and filtering
7. MQTT publishing to embedded devices

## Key Components

### InstrumentPredictor

The prediction engine that analyzes gate hit patterns and predicts future percussive events.

**Features:**
- Kalman filter-based tempo and phase tracking
- BPM estimation (60-200 BPM range)
- Prediction horizon: 2.0 seconds
- Confidence estimation for each prediction
- Outputs `PredictionOutput` struct for downstream processing

**Outputs:**
- ZeroMQ JSON (port 5556) for external tools
- `PredictionOutput` struct stream for LightingEngine

### LightingEngine

Filters predictions and converts them to lighting commands with RGB color mapping.

**Features:**
- Confidence threshold filtering
- Latency window filtering (min/max prediction time)
- Duplicate event detection
- Instrument-to-RGB color mapping
- Outputs `LightingCommand` vector

**Parameters:**
- `confidence_threshold`: Minimum confidence to send (default: 0.3)
- `max_latency_sec`: Maximum prediction latency (default: 2.0)
- `min_latency_sec`: Minimum prediction latency (default: 0.05)
- `duplicate_window_sec`: Duplicate detection window (default: 0.1)

**Color Mapping:**
- Kick → Red (1, 0, 0)
- Snare → Green (0, 1, 0)
- Clap/Chat/OHat → Blue (0, 0, 1)

### MQTTPublisher

Converts lighting commands to Unix timestamps and publishes to MQTT broker.

**Features:**
- Automatic Unix timestamp conversion with microsecond precision
- Async, non-blocking MQTT publishing
- QoS 1 for reliable delivery
- Immediate publishing (no batching delay)

**Parameters:**
- `broker_host`: MQTT broker address (default: "localhost")
- `broker_port`: MQTT broker port (default: 1883)
- `topic`: MQTT topic (default: "beat/events/schedule")
- `client_id`: MQTT client ID (default: "essentia_lighting")

**Message Format:**
```json
{
  "unix_time": 1234567890,
  "microseconds": 123456,
  "confidence": 0.85,
  "r": 1,
  "g": 0,
  "b": 0,
  "event_id": "kick_123.45"
}
```

## Data Structures

### PredictionOutput

Complete prediction output from InstrumentPredictor:
- `timestampSec`: Current time
- `frameIdx`: Frame index
- `predictions`: Vector of `InstrumentPrediction`

### InstrumentPrediction

Per-instrument prediction:
- `instrument`: Instrument name (kick, snare, clap, chat, ohc)
- `tempoBpm`: Estimated tempo
- `periodSec`: Period in seconds
- `phase`: Current phase
- `confidenceGlobal`: Global confidence
- `warmupComplete`: Whether warmup is complete
- `hits`: Vector of `PredictionHit`

### PredictionHit

Individual predicted hit:
- `tPredSec`: Predicted time (seconds)
- `ciLowSec`: Confidence interval lower bound
- `ciHighSec`: Confidence interval upper bound
- `confidence`: Hit confidence
- `hitIndex`: Hit index

### LightingCommand

Lighting command for MQTT:
- `instrument`: Instrument name
- `tPredSec`: Prediction time
- `confidence`: Confidence value
- `r`, `g`, `b`: RGB color values
- `eventId`: Unique event identifier

## Pipeline Flow

```
Audio Input
    ↓
Frame Cutter → Windowing → FFT → Mel Bands
    ↓
Instrument Sum (5 instruments)
    ↓
Gate Detection (5 parallel gates)
    ↓
Vector Pack → InstrumentPredictor
    ↓
    ├─→ ZeroMQ Publisher (JSON)
    └─→ LightingEngine
            ↓
        MQTTPublisher → MQTT Broker
```

## Configuration

See `streaming_pipe.cpp` for configuration:
- LightingEngine: lines 391-395
- MQTTPublisher: lines 396-402

For detailed parameter tuning, see [Instrument Detection Tuning Guide](INSTRUMENT_DETECTION_TUNING_GUIDE.md).

## Dependencies

- Essentia library (built from source)
- Paho MQTT C++ (paho-mqttpp3)
- Paho MQTT C (libpaho-mqtt3a)
- ZeroMQ (libzmq)
- PortAudio

## Build

```bash
cd src/cpp/ess_stream/build
make
```

## Usage

```bash
./ess_stream output.yaml
```

The pipeline will:
1. Capture audio from system audio (BlackHole on macOS)
2. Process in real-time
3. Publish predictions via ZeroMQ (port 5556)
4. Publish lighting commands via MQTT
5. Write aggregated statistics to YAML file
