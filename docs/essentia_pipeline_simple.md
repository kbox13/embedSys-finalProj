# Essentia Pipeline - Simplified Block Diagram (For Presentations)

## Main Pipeline Flow

```
Audio Input (44.1 kHz)
    ↓
Ring Buffer (Thread-Safe)
    ↓
Frame Cutter (1024 samples, 256 hop)
    ↓
Windowing (Blackman-Harris)
    ↓
FFT → Spectrum
    ↓
Mel Bands (64 bands)
    ↓
Instrument Sum (5 instruments)
    ↓
    ├─→ Kick Gate ──┐
    ├─→ Snare Gate ─┤
    ├─→ Clap Gate ──┤──→ Vector Pack ─→ Predictor ─┬→ ZeroMQ (5556) ─→ External Tools
    ├─→ CHat Gate ──┤                                │
    └─→ OHat Gate ──┘                                │
         │                                            │
         └─→ ZeroMQ (5555) ─→ Python Visualization  │
                                                      ↓
                                            Lighting Engine ─→ MQTT Publisher ─→ MQTT Broker
```

## Detailed Component Flow

```
┌──────────────┐
│ Audio Input  │ PortAudio / BlackHole
│ 44.1 kHz    │
└──────┬───────┘
       │
       ↓
┌──────────────┐
│ Ring Buffer  │ Lock-free circular buffer
└──────┬───────┘
       │
       ↓
┌──────────────┐
│Frame Cutter  │ 1024 samples/frame, 256 hop
└──────┬───────┘
       │
       ↓
┌──────────────┐
│  Windowing   │ Blackman-Harris 62
└──────┬───────┘
       │
       ↓
┌──────────────┐
│   Spectrum   │ FFT (Time → Frequency)
└──────┬───────┘
       │
       ↓
┌──────────────┐
│  Mel Bands   │ 64 frequency bands
└──────┬───────┘
       │
       ↓
┌──────────────┐
│Instrument Sum│ Aggregate → 5 instruments
└──────┬───────┘
       │
       ├─────────────┬─────────────┬─────────────┬─────────────┐
       ↓             ↓             ↓             ↓             ↓
┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐
│Kick Gate │  │Snare Gate│  │Clap Gate │  │CHat Gate │  │OHat Gate │
│ (HFC)    │  │ (Flux)   │  │ (Flux)   │  │ (HFC)    │  │ (HFC)    │
└────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘
     │             │             │             │             │
     └─────────────┴─────────────┴─────────────┴─────────────┘
                            │
                            ↓
                   ┌─────────────────┐
                   │  Vector Pack 5   │ Combine all gates
                   └────────┬────────┘
                            │
                            ↓
                   ┌─────────────────┐
                   │   Predictor      │ Predict future hits
                   │   (~100ms ahead) │
                   └────────┬─────────┘
                            │
                            ├──────────────────────────────┐
                            │                              │
                            ↓                              ↓
                   ┌─────────────────┐          ┌─────────────────┐
                   │  ZeroMQ Pub      │          │ Lighting Engine  │ Filter & RGB mapping
                   │  Port 5556       │          └────────┬─────────┘
                   │  (Predictions)   │                   │
                   └─────────────────┘                   ↓
                                                 ┌─────────────────┐
                                                 │ MQTT Publisher   │ Unix timestamp conversion
                                                 └────────┬─────────┘
                                                          │
                                                          ↓
                                                 ┌─────────────────┐
                                                 │  MQTT Broker    │
                                                 └─────────────────┘
```

## Key Processing Stages

### Stage 1: Audio Acquisition
- **Input**: System audio via BlackHole (macOS)
- **Format**: 44.1 kHz, Mono
- **Buffer**: Lock-free ring buffer for thread safety

### Stage 2: Spectral Analysis
- **Framing**: 1024 samples per frame, 256 sample hop
- **Windowing**: Blackman-Harris 62 (reduces spectral leakage)
- **FFT**: Time-domain → Frequency-domain
- **Mel Bands**: 64-band mel-scale frequency analysis

### Stage 3: Instrument Detection
- **Aggregation**: 64 mel bands → 5 instruments
  - Kick (low frequency)
  - Snare (mid frequency)
  - Clap (mid-high frequency)
  - Closed Hi-Hat (high frequency)
  - Open Hi-Hat/Crash (very high frequency)

### Stage 4: Percussive Event Detection
- **Gate Algorithms**: 
  - HFC (High-Frequency Content) for Kick, CHat, OHat
  - Flux (Spectral Flux) for Snare, Clap
- **Adaptive Thresholding**: Per-instrument thresholds
- **Refractory Period**: Prevents double-triggers

### Stage 5: Prediction
- **Input**: Gate hit patterns from all 5 instruments
- **Output**: Predicted future hits (~100ms ahead)
- **Method**: Pattern analysis with BPM estimation (60-200 BPM)
- **Horizon**: 2.0 seconds into future
- **Output Format**: `PredictionOutput` struct with instrument predictions and hit timestamps

### Stage 6: Lighting Engine
- **Input**: `PredictionOutput` from InstrumentPredictor
- **Filtering**:
  - Confidence threshold (default: 0.3, configurable)
  - Latency window (min/max prediction latency)
  - Duplicate detection (prevents sending same event twice)
- **Color Mapping**: Maps instruments to RGB values
  - Kick → Red (1, 0, 0)
  - Snare → Green (0, 1, 0)
  - Others → Blue (0, 0, 1)
- **Output**: `LightingCommand` vector with filtered, color-mapped events

### Stage 7: MQTT Publishing
- **Input**: `LightingCommand` vector from LightingEngine
- **Unix Timestamp Conversion**: Converts prediction times to Unix timestamps with microsecond precision
- **MQTT Publishing**: Async, non-blocking publish to MQTT broker
- **Topic**: `beat/events/schedule` (configurable)
- **QoS**: 1 (at least once delivery)
- **Format**: JSON with `unix_time`, `microseconds`, `confidence`, `r`, `g`, `b`, `event_id`

### Stage 8: Communication
- **ZeroMQ Port 5555**: Real-time gate hits (detected events)
- **ZeroMQ Port 5556**: Predicted future events (JSON format)
- **MQTT**: Lighting commands to embedded devices
- **YAML Output**: Aggregated statistics for analysis
- **Log Files**: Timestamped hit/prediction logs

## Processing Latency

- **Frame Processing**: ~5.8ms per frame (256 samples @ 44.1 kHz)
- **Total Pipeline**: ~10-50ms end-to-end
- **Suitable for**: Real-time applications with ~100ms prediction lookahead

