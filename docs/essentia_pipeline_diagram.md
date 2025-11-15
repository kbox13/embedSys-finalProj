# Essentia Pipeline Block Diagram

## Complete Pipeline Flow

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         AUDIO INPUT                                          │
│                    (PortAudio / BlackHole)                                  │
│                    Sample Rate: 44.1 kHz, Mono                              │
└────────────────────────────┬────────────────────────────────────────────────┘
                              │
                              ↓
┌─────────────────────────────────────────────────────────────────────────────┐
│                    RING BUFFER (Lock-Free)                                   │
│                    Circular buffer: ~5 seconds                               │
│                    Producer: PortAudio callback                              │
│                    Consumer: Essentia feeder thread                          │
└────────────────────────────┬────────────────────────────────────────────────┘
                              │
                              ↓
┌─────────────────────────────────────────────────────────────────────────────┐
│                    RING BUFFER INPUT                                         │
│                    (Essentia Source)                                        │
│                    Buffer Size: frameSize × 10                              │
└────────────────────────────┬────────────────────────────────────────────────┘
                              │
                              ↓
┌─────────────────────────────────────────────────────────────────────────────┐
│                    FRAME CUTTER                                              │
│                    Frame Size: 1024 samples                                  │
│                    Hop Size: 256 samples                                      │
│                    Silent Frames: "noise"                                    │
└────────────────────────────┬────────────────────────────────────────────────┘
                              │
                              ↓
┌─────────────────────────────────────────────────────────────────────────────┐
│                    WINDOWING                                                 │
│                    Type: Blackman-Harris 62                                  │
│                    (Reduces spectral leakage)                                │
└────────────────────────────┬────────────────────────────────────────────────┘
                              │
                              ↓
┌─────────────────────────────────────────────────────────────────────────────┐
│                    SPECTRUM (FFT)                                           │
│                    Converts time-domain to frequency-domain                  │
│                    Output: Magnitude spectrum                                │
└────────────────────────────┬────────────────────────────────────────────────┘
                              │
                              ↓
┌─────────────────────────────────────────────────────────────────────────────┐
│                    MEL BANDS                                                 │
│                    Number of Bands: 64                                        │
│                    Sample Rate: 44.1 kHz                                     │
│                    (Mimics human auditory perception)                        │
└────────────────────────────┬────────────────────────────────────────────────┘
                              │
                              ↓
┌─────────────────────────────────────────────────────────────────────────────┐
│                    INSTRUMENT SUM                                            │
│                    Aggregates 64 mel bands → 5 instruments                  │
│                    Lobe Rolloff: 0.15                                        │
│                    Output: [Kick, Snare, Clap, CHat, OHatCrash]            │
└────────────────────────────┬────────────────────────────────────────────────┘
                              │
                              ├──────────────────────────────────────────────┐
                              │                                              │
                              ↓                                              ↓
                    ┌─────────────────┐                          ┌─────────────────┐
                    │  VECTOR INDEX   │                          │  VECTOR INDEX   │
                    │  (Kick: idx 0)  │                          │  (Snare: idx 1) │
                    └────────┬────────┘                          └────────┬────────┘
                             │                                            │
                             ↓                                            ↓
                    ┌─────────────────┐                          ┌─────────────────┐
                    │  HIT GATE ONSET │                          │  HIT GATE ONSET │
                    │  (Kick Gate)     │                          │  (Snare Gate)   │
                    │  Method: HFC     │                          │  Method: Flux   │
                    │  Threshold: 10   │                          │  Threshold: 1.4 │
                    │  Refractory: 30  │                          │  Refractory: 4   │
                    └────────┬────────┘                          └────────┬────────┘
                             │                                            │
                             │                                            │
                    ┌────────┴────────┐                          ┌────────┴────────┐
                    │                 │                          │                 │
                    ↓                 ↓                          ↓                 ↓
         ┌──────────────────┐  ┌──────────────┐      ┌──────────────────┐  ┌──────────────┐
         │  ZERO MQ PUB      │  │ GATE LOGGER  │      │  ZERO MQ PUB      │  │ GATE LOGGER  │
         │  (Port 5555)      │  │   SINK       │      │  (Port 5555)      │  │   SINK       │
         │  Topic: gate.kick │  │  (File Log)  │      │  Topic: gate.snare│  │  (File Log)  │
         └──────────────────┘  └──────────────┘      └──────────────────┘  └──────────────┘
                             │                                            │
                             │                                            │
                    ┌────────┴────────────────────────────────────────────┴────────┐
                    │                                                               │
                    │  [Similar parallel paths for Clap, CHat, OHatCrash]          │
                    │                                                               │
                    ↓                                                               ↓
         ┌──────────────────────────────────────────────────────────────────────┐
         │                    VECTOR PACK 5                                     │
         │                    Combines all 5 gate outputs                       │
         │                    Output: [kick, snare, clap, chat, ohc]           │
         └────────────────────────────┬─────────────────────────────────────────┘
                                      │
                                      ↓
         ┌──────────────────────────────────────────────────────────────────────┐
         │                    INSTRUMENT PREDICTOR                              │
         │                    - Analyzes gate patterns                          │
         │                    - Predicts future hits (~100ms lookahead)         │
         │                    - BPM range: 60-200                                │
         │                    - Horizon: 2.0 seconds                            │
         │                    - Min hits for seed: 8                            │
         │                    - Output: PredictionOutput struct                  │
         └────────────────────────────┬─────────────────────────────────────────┘
                                      │
                                      ├──────────────────────────────────────────┐
                                      │                                          │
                                      ↓                                          ↓
         ┌──────────────────────────────────────────────────────┐  ┌──────────────────────────────────────┐
         │                    ZERO MQ PUBLISHER                  │  │        LIGHTING ENGINE                │
         │                    (Predictions)                     │  │        - Filters by confidence         │
         │                    Endpoint: tcp://localhost:5556    │  │        - Filters by latency window     │
         │                    Publishes predicted events        │  │        - Duplicate detection           │
         │                    (JSON format)                    │  │        - Maps instruments to RGB        │
         └──────────────────────────────────────────────────────┘  └──────────────────┬───────────────────┘
                                                                                        │
                                                                                        ↓
                                                                    ┌──────────────────────────────────────┐
                                                                    │        MQTT PUBLISHER                │
                                                                    │        - Converts to Unix timestamps │
                                                                    │        - Async, non-blocking publish │
                                                                    │        - Topic: beat/events/schedule  │
                                                                    │        - QoS: 1                       │
                                                                    └──────────────────┬───────────────────┘
                                                                                       │
                                                                                       ↓
                                                                    ┌──────────────────────────────────────┐
                                                                    │        MQTT BROKER                    │
                                                                    │        (Mosquitto)                    │
                                                                    └──────────────────────────────────────┘
                                      │
                                      ↓
         ┌──────────────────────────────────────────────────────────────────────┐
         │                    POOL STORAGE                                        │
         │                    - Stores all features for analysis                 │
         │                    - Aggregated statistics (mean, var, min, max)      │
         │                    - Written to YAML output file                      │
         └──────────────────────────────────────────────────────────────────────┘
```

## Pipeline Parameters

| Component | Parameter | Value |
|-----------|-----------|-------|
| **Audio Input** | Sample Rate | 44.1 kHz |
| | Channels | Mono |
| | Source | BlackHole (macOS) |
| **Frame Processing** | Frame Size | 1024 samples |
| | Hop Size | 256 samples |
| | Window Type | Blackman-Harris 62 |
| **Spectral Analysis** | Mel Bands | 64 bands |
| **Instrument Detection** | Instruments | 5 (Kick, Snare, Clap, CHat, OHatCrash) |
| **Gate Detection** | Methods | HFC (Kick, CHat, OHatCrash), Flux (Snare, Clap) |
| **Prediction** | Lookahead | ~100ms |
| | BPM Range | 60-200 BPM |
| | Horizon | 2.0 seconds |
| **Lighting Engine** | Confidence Threshold | 0.3 (default, configurable) |
| | Max Latency | 2.0 seconds (default) |
| | Min Latency | 0.05 seconds (default) |
| | Duplicate Window | 0.1 seconds (default) |
| **MQTT Publisher** | Broker Host | localhost (default, configurable) |
| | Broker Port | 1883 (default) |
| | Topic | beat/events/schedule (default, configurable) |
| | Client ID | essentia_lighting (default) |
| **Communication** | ZeroMQ Gates | Port 5555 |
| | ZeroMQ Predictions | Port 5556 |
| | MQTT | Configurable broker/topic |

## Instrument Gate Parameters

| Instrument | Method | Threshold | Refractory | Sensitivity | ODF Window |
|------------|--------|-----------|------------|-------------|------------|
| **Kick** | HFC | 10.0 | 30 | 5.0 | 64 |
| **Snare** | Flux | 1.4 | 4 | 1.8 | 64 |
| **Clap** | Flux | 1.4 | 3 | 1.8 | 48 |
| **CHat** | HFC | 1.6 | 3 | 1.6 | 48 |
| **OHatCrash** | HFC | 1.5 | 4 | 1.6 | 64 |

## Data Flow Summary

1. **Audio Capture**: PortAudio captures from BlackHole device
2. **Buffering**: Lock-free ring buffer for thread-safe audio transfer
3. **Framing**: Audio split into overlapping frames (1024 samples, 256 hop)
4. **Windowing**: Blackman-Harris window reduces spectral leakage
5. **FFT**: Time-domain → Frequency-domain conversion
6. **Mel Bands**: 64-band mel-scale frequency analysis
7. **Instrument Aggregation**: Mel bands mapped to 5 percussion instruments
8. **Gate Detection**: Adaptive onset detection per instrument
9. **Hit Logging**: Gates logged to file for analysis
10. **ZeroMQ Publishing**: Real-time gate events published (port 5555)
11. **Prediction**: Future hits predicted from gate patterns
12. **Prediction Publishing**: Predicted events published via ZeroMQ (port 5556) and as PredictionOutput struct
13. **Lighting Engine**: Filters predictions by confidence, latency, and duplicates; maps to RGB colors
14. **MQTT Publishing**: Converts lighting commands to Unix timestamps and publishes to MQTT broker
15. **Storage**: All features aggregated and saved to YAML

## Output Channels

- **ZeroMQ Port 5555**: Real-time gate hits (detected percussive events)
- **ZeroMQ Port 5556**: Predicted future events (JSON format for external tools)
- **MQTT**: Lighting commands with Unix timestamps (for embedded devices)
- **YAML File**: Aggregated statistics and frame-by-frame data
- **Log Files**: Timestamped hit and prediction logs

