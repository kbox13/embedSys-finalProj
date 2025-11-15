---
layout: page
title: Project Overview
permalink: /
exclude: true
---
<!-- Website must have initial sections for goals, specific aims, approach, deliverables, and timeline-->
# Predicting Percussive Events to Trigger Lighting Events on an Embedded Device

## Project Abstract
This project is meant to synchronize lighting to music to create interactive visuals. The project has 2 parts, first determining events in the music, then using the events to trigger time synchronized events on an embedded device. To do this events will need to be predicted such that the latency between the platform doing the processing and the embedded device can be accounted for.

## Project Goals
The goals of the project can be divided into 2 parts, the signal processing side and the scheduling and embedded device action side  

### Percussive Event Prediction
- Detect Percussive Events in Music 
- Use a prediction algorithm to predict future events 100ms into the future.
- Visualize events and predicted events on host laptop
- Measure accuracy of predicted events to actual detected events.
- Make time synchronized predicted events availible for communication to embedded device.

### Embedded Device Triggering
- Synchronize and Measure time/clock difference between host laptop and embedded device
- Send and execute time syncronized commands from host laptop to embedded device
- Use 3rd embedded platform to measure accuracy of timed event execution

## Approach
My approach to complete this project is broken up logically into the 2 parts. Create the detectiion and prediction pipeline. Then create and meaasure accuracy of the embedded platform event execution. Then integrate the predictions with the embedded platform execution. 


## Timeline

### Completed
- Week 3: Installed and made BeatNet functional (exploratory)
- Week 4: Installed and created basic streaming pipeline in Essentia
- Week 5: Completed full Essentia streaming pipeline with predictions and visualizations
  - FFT-based audio analysis
  - Percussive event detection (gates)
  - Instrument classification (Kick, Snare, Clap, Hi-Hat, Crash)
  - ZeroMQ publishing for Python integration
  - Kalman filter-based prediction (~100ms lookahead)
  - Real-time visualization using Vispy
- Week 6: Began work on embedded side
  - Arduino Nano ESP32 firmware development
  - MQTT communication implementation
  - SNTP/NTP time synchronization
  - Basic event scheduling
- **Week 7**: Midterm Checkpoint
  - Integration of Essentia pipeline → Arduino
  - MQTT event publishing and scheduling
- Week 8:  Finish integration and improved timing accuracy
  - Finish Integration
  - Create accuracy testing framework
- Week 9: Enhanced system reliability and documentation
  - Expand lighting side past triggering a single event. 
  - Make lighting more dynamic.
- Week 10: Final System
  - Wrap up development into a polished product
  - Complete documentation
- Finals Week: Final presentation and demo
  - Complete system demonstration
  - Timing accuracy measurements
  - Performance analysis
  - Project documentation

## Current System Status

### ✅ Completed Components

1. **Essentia Streaming Pipeline (C++)**
   - Real-time audio capture and FFT analysis
   - Multiple gate algorithms for percussive detection
   - Instrument classification
   - Kalman filter-based prediction engine (InstrumentPredictor)
   - Lighting engine with confidence filtering and RGB color mapping
   - Direct MQTT publishing with Unix timestamp conversion
   - ZeroMQ publishing for visualization/external tools

2. **Python Visualization & Testing (Optional)**
   - ZeroMQ subscriber for Essentia events
   - Real-time visualization
   - MQTT testing utilities

3. **Embedded Device (Arduino Nano ESP32)**
   - Dual-core FreeRTOS architecture
   - MQTT subscription and event scheduling
   - SNTP/NTP time synchronization
   - Hardware timer interrupts for precise execution
   - RGB LED control

### System Performance

- **Event Detection**: Real-time with ~10-50ms processing latency
- **Prediction Lookahead**: ~100ms
- **Time Sync Accuracy**: ±50ms initial, ±10ms after sync
- **Event Execution Accuracy**: ±50µs (hardware timer)
- **MQTT Latency**: 10-50ms (network dependent)

### Architecture Highlights

- **Separation of Concerns**: C++ for audio processing, prediction, and MQTT communication; Python optional for visualization
  - **Production-ready**: Direct C++ MQTT publishing eliminates Python dependency for core functionality
- **Standard Protocols**: ZeroMQ for visualization/external tools, MQTT for device communication, NTP for time sync
- **Precise Timing**: Hardware timer interrupts ensure microsecond-level accuracy
- **Unix Timestamp Conversion**: High-precision conversion from prediction times to Unix timestamps with microsecond accuracy
- **Scalability**: MQTT pub/sub allows multiple devices
- **Reliability**: Dual-core isolation, thread-safe operations, error handling, non-blocking async MQTT publishing 
  