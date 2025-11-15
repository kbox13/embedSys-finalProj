# Embedded Device Architecture: Event Scheduler System

## Overview
This document describes the complete architecture for the Arduino Nano ESP32 event scheduling system that receives beat prediction events via MQTT and executes precise LED lighting events using hardware timer interrupts.

---

## System Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                    Host System (Python)                        │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ Beat Prediction System                                    │  │
│  │  - Predicts beats ~100ms in future                       │  │
│  │  - Uses NTP time synchronization (ntplib)                │  │
│  │  - Publishes events via MQTT                              │  │
│  └──────────────────────────────────────────────────────────┘  │
│                          ↓ (MQTT Publish)                       │
└─────────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────────┐
│                    MQTT Broker (Mosquitto)                     │
│  - Runs on host machine (localhost:1883)                       │
│  - Routes messages to all subscribers                          │
│  - Handles QoS, persistence, topic routing                    │
└─────────────────────────────────────────────────────────────────┘
                          ↓ (MQTT Subscribe)
┌─────────────────────────────────────────────────────────────────┐
│              Arduino Nano ESP32 (ESP32-S3 Dual-Core)            │
│                                                                 │
│  ┌──────────────────────────────────────────────────────────┐ │
│  │ CORE 0: Communication Core                                │ │
│  │  ┌────────────────────────────────────────────────────┐  │ │
│  │  │ WiFi Task                                           │  │ │
│  │  │  - Manages WiFi connection                          │  │ │
│  │  │  - Handles connection/reconnection                  │  │ │
│  │  └────────────────────────────────────────────────────┘  │ │
│  │  ┌────────────────────────────────────────────────────┐  │ │
│  │  │ MQTT Client Task                                     │  │ │
│  │  │  - Subscribes to MQTT topics                        │  │ │
│  │  │  - Receives messages from broker                    │  │ │
│  │  │  - Parses JSON messages                            │  │ │
│  │  │  - Enqueues events to shared queue                  │  │ │
│  │  └────────────────────────────────────────────────────┘  │ │
│  │  ┌────────────────────────────────────────────────────┐  │ │
│  │  │ SNTP Time Sync                                      │  │ │
│  │  │  - Syncs with NTP server                            │  │ │
│  │  │  - Updates time offset                              │  │ │
│  │  │  - Handles periodic re-sync                         │  │ │
│  │  └────────────────────────────────────────────────────┘  │ │
│  └──────────────────────────────────────────────────────────┘ │
│                          ↓ (FreeRTOS Queue + Mutex)            │
│  ┌──────────────────────────────────────────────────────────┐ │
│  │ Thread-Safe Event Queue                                  │ │
│  │  - Priority queue (sorted by execute_time_us)            │ │
│  │  - Protected by mutex                                    │ │
│  │  - Max size: 50-100 events                               │ │
│  └──────────────────────────────────────────────────────────┘ │
│                          ↓                                     │
│  ┌──────────────────────────────────────────────────────────┐ │
│  │ CORE 1: Execution Core                                    │ │
│  │  ┌────────────────────────────────────────────────────┐  │ │
│  │  │ Event Scheduler Task                                │  │ │
│  │  │  - Monitors event queue                             │  │ │
│  │  │  - Configures hardware timer for next event         │  │ │
│  │  │  - Handles event cleanup                            │  │ │
│  │  └────────────────────────────────────────────────────┘  │ │
│  │                          ↓                                 │ │
│  │  ┌────────────────────────────────────────────────────┐  │ │
│  │  │ Hardware Timer (HW Timer 0)                         │  │ │
│  │  │  - Fires at precise event time                      │  │ │
│  │  │  - Triggers ISR (Interrupt Service Routine)          │  │ │
│  │  └────────────────────────────────────────────────────┘  │ │
│  │                          ↓                                 │ │
│  │  ┌────────────────────────────────────────────────────┐  │ │
│  │  │ ISR: Event Execution                                  │  │ │
│  │  │  - Executes in interrupt context                      │  │ │
│  │  │  - Direct GPIO writes (fast, safe)                   │  │ │
│  │  │  - Sets LED states                                   │  │ │
│  │  │  - Sets flag for scheduler cleanup                    │  │ │
│  │  └────────────────────────────────────────────────────┘  │ │
│  └──────────────────────────────────────────────────────────┘ │
│                          ↓                                     │
│  ┌──────────────────────────────────────────────────────────┐ │
│  │ Hardware: RGB LED (Active-Low)                           │ │
│  │  - GPIO 14 (Red)                                          │ │
│  │  - GPIO 15 (Green)                                       │ │
│  │  - GPIO 16 (Blue)                                         │ │
│  └──────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
```

---

## Component Details

### 1. Hardware Components

#### Arduino Nano ESP32 (ESP32-S3)
- **Dual-Core**: Core 0 (Communication), Core 1 (Execution)
- **WiFi**: 802.11 b/g/n
- **Memory**: 512KB SRAM, sufficient for event queue
- **Timers**: 4 hardware timers (64-bit, 16-bit prescaler)

#### RGB LED
- **Type**: Onboard RGB LED (active-low)
- **Pins**: 
  - Red: GPIO 14 (LED_RED constant)
  - Green: GPIO 15 (LED_GREEN constant)
  - Blue: GPIO 16 (LED_BLUE constant)
- **Behavior**: LOW = ON, HIGH = OFF

### 2. Software Architecture

#### Core 0: Communication Core
**Responsibilities:**
- WiFi connection management
- MQTT client connection and subscription
- Message reception and JSON parsing
- Time synchronization (SNTP)
- Event queue insertion (thread-safe)

**Tasks:**
1. **WiFi Task** (Priority: 1)
   - Manages WiFi connection
   - Handles reconnection on failure
   - Monitors connection status

2. **MQTT Client Task** (Priority: 1)
   - Connects to MQTT broker
   - Subscribes to topics:
     - `beat/events/schedule` - Event scheduling
     - `beat/time/sync` - Time synchronization
     - `beat/commands/all` - Commands
   - Receives messages
   - Parses JSON
   - Enqueues events to shared queue

3. **SNTP Sync** (Background)
   - Initializes SNTP client
   - Syncs with NTP server (pool.ntp.org)
   - Updates time offset
   - Periodic re-sync (hourly)

#### Core 1: Execution Core
**Responsibilities:**
- Event queue monitoring
- Hardware timer configuration
- Event execution timing
- LED state management

**Tasks:**
1. **Event Scheduler Task** (Priority: 2, higher priority)
   - Continuously monitors event queue
   - Removes executed events
   - Configures hardware timer for next event
   - Handles event cleanup after execution
   - Minimal delay (<1ms) for tight timing

2. **Hardware Timer ISR** (Interrupt Context)
   - Executes at precise event time
   - Direct GPIO writes (fast, safe)
   - Sets completion flag
   - Minimal processing (<100µs)

### 3. Data Structures

#### ScheduledEvent
```cpp
struct ScheduledEvent {
    unsigned long execute_time_us;  // Microsecond precision
    bool red;                        // RGB LED states
    bool green;
    bool blue;
    uint8_t event_id;                // For cancellation
    // Future: event_type, duration, pattern, etc.
};
```

#### Time Synchronization
```cpp
// Time sync state
struct TimeSyncState {
    bool synced;                     // SNTP sync status
    time_t sync_epoch;               // Unix epoch at sync point
    unsigned long sync_micros;       // micros() at sync point
    unsigned long time_offset_us;    // Calculated offset
};
```

#### Event Queue
- **Type**: Priority queue (sorted array or min-heap)
- **Max Size**: 50-100 events
- **Protection**: FreeRTOS mutex
- **Operations**: 
  - Insert: O(n) or O(log n)
  - Extract: O(1)
  - Peek: O(1)

### 4. Communication Protocol

#### MQTT Topics
```
beat/events/schedule      # Schedule LED events (QoS 1)
beat/time/sync            # Time synchronization (QoS 1)
beat/commands/all          # Commands for all devices (QoS 1)
beat/devices/+/status     # Device status (QoS 0)
```

#### Message Formats

**Event Schedule (Single):**
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

**Event Schedule (Batch):**
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

**Time Sync:**
```json
{
  "unix_time": 1234567890,
  "microseconds": 123456,
  "sync_source": "host"
}
```

### 5. Time Synchronization Flow

```
1. ESP32 boots → Connects to WiFi
2. SNTP initializes → Syncs with NTP server
3. Time offset calculated:
   offset = current_micros() - (unix_time * 1000000 + microseconds)
4. Events received with Unix timestamps
5. Conversion to micros():
   execute_time_us = (unix_time * 1000000) + microseconds - offset
6. Periodic re-sync (hourly) to handle drift
```

### 6. Event Execution Flow

```
1. MQTT message received → JSON parsed
2. Event created with Unix timestamp
3. Converted to micros() equivalent
4. Enqueued to priority queue
5. Scheduler task picks up event
6. Configures hardware timer:
   timerAlarmWrite(timer, delay_us, false)
7. Timer fires at precise time
8. ISR executes:
   - Direct GPIO writes
   - Sets completion flag
9. Scheduler task removes executed event
10. Process repeats for next event
```

### 7. Memory Management

```
Stack Allocations:
- Core 0 WiFi Task: ~8KB
- Core 0 MQTT Task: ~8KB
- Core 1 Scheduler Task: ~4KB
- Total Stack: ~20KB

Heap Allocations:
- Event Queue: ~50 events × 16 bytes = 800 bytes
- MQTT Client: ~5KB
- WiFi: ~20KB
- JSON Parsing: ~1KB (temporary)
- Total: ~27KB

Total: ~47KB (ESP32-S3 has 512KB SRAM, plenty of headroom)
```

### 8. Timing Guarantees

| Component | Precision | Jitter | Notes |
|-----------|-----------|--------|-------|
| Hardware Timer | 1µs | ±10µs | Hardware-based, very precise |
| ISR Execution | <100µs | ±50µs | Fast GPIO writes |
| Event Scheduling | ~1ms | ±1ms | Depends on queue processing |
| MQTT Reception | Variable | Network dependent | Handled on separate core |
| Time Sync | ±50ms | ±100ms | Initial sync, then <10ms |

**Overall System Timing:**
- Event execution: ±50µs accuracy
- Suitable for beat prediction (100ms lookahead)
- Hardware timer provides precise execution

### 9. Error Handling

#### WiFi Connection Loss
- Automatic reconnection attempt
- Events continue to execute (time sync may drift)
- MQTT reconnects when WiFi restored

#### MQTT Connection Loss
- Automatic reconnection
- Re-subscribe to topics
- Events scheduled before loss still execute

#### Time Sync Loss
- Use last known good sync offset
- Continue executing scheduled events
- Attempt re-sync periodically
- Log warning if sync lost

#### Event Queue Full
- Reject new events
- Return error via MQTT (if possible)
- Log warning

#### Hardware Timer Miss
- Extremely rare (hardware-based)
- If missed, execute immediately
- Log warning

### 10. Task Priorities

```
Priority 2: Event Scheduler Task (Core 1)
  - Highest priority for timing-critical operations
  
Priority 1: MQTT Client Task (Core 0)
  - Normal priority for communication
  
Priority 1: WiFi Task (Core 0)
  - Normal priority for connection management
  
Priority 0: Idle Task (Both cores)
  - Background/idle
```

### 11. Thread Safety

#### Shared Resources
1. **Event Queue**: Protected by FreeRTOS mutex
2. **Time Offset**: Protected by mutex (read from Core 1, write from Core 0)
3. **LED State**: Atomic operations (ISR writes, task reads)

#### Synchronization Primitives
- `xQueue`: FreeRTOS queue for event queue
- `xSemaphoreMutex`: Mutex for critical sections
- `volatile`: For ISR-shared variables
- No blocking in ISR context

### 12. Future-Proofing Considerations

#### Extensible Event Types
```cpp
enum EventType {
    EVENT_RGB_SIMPLE,      // Current: simple RGB on/off
    EVENT_RGB_PATTERN,     // Future: pattern sequence
    EVENT_RGB_FADE,        // Future: fade effect
    EVENT_SEQUENCE          // Future: multi-step sequence
};
```

#### Event Metadata
```cpp
struct ScheduledEvent {
    // ... existing fields ...
    uint8_t event_type;
    void* metadata;  // Future: pattern data, fade parameters, etc.
};
```

#### Additional Topics
```
beat/events/cancel/{id}    # Cancel specific event
beat/events/clear          # Clear all events
beat/patterns/define       # Define reusable patterns
beat/status/request        # Request device status
```

---

## Implementation Phases

### Phase 1: Foundation (MVP)
- [ ] WiFi connection
- [ ] SNTP time synchronization
- [ ] Basic MQTT client (subscribe only)
- [ ] Event queue (simple array)
- [ ] Hardware timer setup
- [ ] Basic ISR for LED control
- [ ] Single event scheduling

### Phase 2: Full Integration
- [ ] Dual-core task separation
- [ ] Thread-safe event queue
- [ ] Batch event support
- [ ] Time sync via MQTT
- [ ] Error handling and recovery
- [ ] Status reporting

### Phase 3: Advanced Features
- [ ] Event cancellation
- [ ] Pattern sequences
- [ ] Device status reporting
- [ ] Performance monitoring
- [ ] Advanced timing modes

---

## Testing Strategy

### Unit Tests
- Event queue insertion/extraction
- Time conversion accuracy
- JSON parsing

### Integration Tests
- MQTT message flow
- Timer execution timing
- Multi-event scheduling

### System Tests
- End-to-end from Python to LED
- Timing accuracy measurement
- Multiple device synchronization
- Stress testing (burst events)

---

## Configuration

### WiFi
```cpp
const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";
```

### MQTT
```cpp
const char* mqtt_broker = "192.168.1.100";  // Host machine
const int mqtt_port = 1883;
const char* mqtt_client_id = "arduino_esp32_1";
```

### SNTP
```cpp
const char* ntp_server1 = "pool.ntp.org";
const char* ntp_server2 = "time.nist.gov";
const char* timezone = "UTC";
```

### Queue
```cpp
#define MAX_EVENT_QUEUE_SIZE 50
#define EVENT_TIMEOUT_MS 1000
```

---

## Performance Characteristics

- **Event Processing**: <1ms per event
- **MQTT Latency**: 10-50ms (network dependent)
- **Time Sync Accuracy**: ±50ms initial, ±10ms after sync
- **Event Execution Accuracy**: ±50µs (hardware timer)
- **Queue Throughput**: 1000+ events/second
- **Memory Usage**: ~47KB total

---

## Summary

This architecture provides:
1. **Precise Timing**: Hardware timer interrupts for microsecond accuracy
2. **Scalability**: MQTT pub/sub for multiple devices
3. **Reliability**: Dual-core isolation, thread-safe operations
4. **Standard Protocols**: MQTT, SNTP/NTP (no custom solutions)
5. **Future-Proof**: Extensible design for advanced features

The system is designed to handle beat prediction events with tight timing guarantees while maintaining clean separation of concerns and standard communication protocols.

