# Hardware Timer Interrupt Implementation Plan

## Current State
- **Polling-based**: Event scheduler task polls every 1ms to check if events are ready
- **Timing accuracy**: ~1ms jitter/uncertainty
- **CPU usage**: Continuous polling on Core 1

## Target State
- **Hardware timer interrupts**: Use ESP32 hardware timer to fire ISR at exact event time
- **Timing accuracy**: ±10µs (hardware-based precision)
- **CPU usage**: Minimal - timer wakes up only when needed

## Architecture Changes

### 1. Hardware Timer Selection
- **ESP32 has 4 hardware timers** (Timer Group 0 and 1, each with 2 timers)
- **Recommendation**: Use Timer Group 0, Timer 0 (TG0_TIMER0)
- **Alternative**: Use Arduino `hw_timer_t` API (simpler, but less control)

### 2. Timer Configuration
```
- Timer Mode: One-shot (alarm mode)
- Prescaler: 80 (for 1MHz = 1µs resolution)
- Counter: 64-bit (can handle long delays)
- Interrupt: Attach ISR callback
```

### 3. Key Components to Add/Modify

#### A. Timer Initialization (`initHardwareTimer()`)
- Initialize hardware timer
- Configure prescaler for 1µs resolution
- Set up interrupt handler
- Start timer (but don't set alarm yet)

#### B. Timer Alarm Configuration (`configureTimerForEvent()`)
- Calculate delay from `micros()` to event time
- Handle edge cases:
  - Events in the past → execute immediately
  - Events too far in future → handle timer overflow
  - Minimum delay (e.g., 10µs) to ensure ISR can be set up
- Set timer alarm value
- Enable timer interrupt

#### C. Timer ISR Handler (`IRAM_ATTR timerISR()`)
- **Constraints**: Must be in IRAM, no Serial, no mutexes, minimal processing
- Execute the event (LED control)
- Schedule turn-off event if needed
- Signal main task to handle cleanup
- Disable timer (one-shot mode)

#### D. Event Scheduler Task (Modified)
- **New role**: Queue management and timer configuration only
- Remove polling loop for event execution
- When new event arrives:
  - If queue was empty or new event is sooner → reconfigure timer
  - Otherwise, timer will fire for current event first
- After timer fires, configure for next event

#### E. Communication Between ISR and Task
- **Option 1**: Use FreeRTOS queue from ISR (requires `xQueueSendFromISR`)
- **Option 2**: Use flag + task notification
- **Option 3**: Direct execution in ISR (fastest, but limited)

### 4. Implementation Details

#### Timer API Choice
**Option A: ESP-IDF Timer API** (More control)
```cpp
#include "driver/timer.h"
- timer_group_t, timer_idx_t
- timer_init(), timer_set_alarm_value()
- timer_isr_register()
```

**Option B: Arduino hw_timer** (Simpler)
```cpp
#include "esp32-hal-timer.h"
- hw_timer_t *timer = timerBegin(0, 80, true)
- timerAttachInterrupt(timer, ISR, true)
- timerAlarmWrite(timer, delay_us, false)  // one-shot
```

**Recommendation**: Start with Option B (Arduino API) for simplicity, can migrate to Option A if needed.

#### ISR Implementation
```cpp
void IRAM_ATTR timerISR() {
    // Get next event (already stored in volatile variable)
    ScheduledEvent* event = const_cast<ScheduledEvent*>(nextEvent);
    
    // Execute event (fast GPIO writes only)
    executeEventInISR(event);
    
    // Signal scheduler task to handle cleanup
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(eventSchedulerTaskHandle, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
```

#### Event Execution in ISR
- **Can do**: Fast GPIO writes (digitalWrite is OK, but direct register access is faster)
- **Cannot do**: Serial.print, mutex operations, malloc, complex operations
- **Solution**: Do minimal work in ISR, defer cleanup to task

### 5. Edge Cases to Handle

#### A. Events in the Past
- If `event_time < micros()`, execute immediately in scheduler task
- Don't set timer for past events

#### B. Timer Overflow
- ESP32 timer is 64-bit, but alarm value might be limited
- If delay > max alarm value, schedule in chunks or use different approach
- Check: `timerAlarmWrite()` max value

#### C. Very Short Delays (< 10µs)
- Timer setup overhead might be longer than delay
- Execute immediately instead of setting timer

#### D. Timer Already Set
- If new event arrives and timer is already set:
  - If new event is sooner → reconfigure timer (stop, set new alarm, restart)
  - If new event is later → leave timer as-is, it will be set after current fires

#### E. Multiple Events at Same Time
- Queue handles this (sorted by time)
- Timer fires, execute first event, then immediately check for next event at same time

### 6. Code Structure Changes

#### Global Variables (Add)
```cpp
hw_timer_t *eventTimer = NULL;
TaskHandle_t eventSchedulerTaskHandle = NULL;
volatile bool timerArmed = false;
```

#### Functions to Modify
1. `initHardwareTimer()` - Initialize timer, attach ISR
2. `configureTimerForEvent()` - Calculate delay, set alarm
3. `eventSchedulerTask()` - Remove polling, add timer management
4. `executeEvent()` - Split into ISR version and normal version

#### Functions to Add
1. `IRAM_ATTR timerISR()` - Timer interrupt handler
2. `IRAM_ATTR executeEventInISR()` - Fast event execution
3. `scheduleNextEvent()` - Configure timer for next event in queue

### 7. Testing Strategy

1. **Basic functionality**: Single event fires at correct time
2. **Multiple events**: Queue of events fires in order
3. **Past events**: Events scheduled in past execute immediately
4. **Rapid events**: Events scheduled close together (< 1ms apart)
5. **Long delays**: Events scheduled far in future (> 1 second)
6. **Timer reconfiguration**: New event arrives while timer is armed

### 8. Performance Expectations

- **Timing accuracy**: ±10µs (hardware timer precision)
- **ISR latency**: < 5µs (fast GPIO writes)
- **CPU usage**: Near zero when idle (timer wakes up only when needed)
- **Event scheduling overhead**: ~10-50µs (timer configuration)

### 9. Migration Steps

1. **Phase 1**: Add timer initialization, keep polling as fallback
2. **Phase 2**: Implement ISR, test with single events
3. **Phase 3**: Integrate with event queue, remove polling
4. **Phase 4**: Optimize and handle edge cases
5. **Phase 5**: Test and validate timing accuracy

### 10. Rollback Plan

- Keep old polling code commented out
- Add compile-time flag to switch between timer and polling
- Test both modes to ensure compatibility

## Implementation Order

1. ✅ Add timer initialization code
2. ✅ Implement timer ISR handler
3. ✅ Modify `configureTimerForEvent()` to use hardware timer
4. ✅ Update `eventSchedulerTask()` to manage timer instead of polling
5. ✅ Add edge case handling
6. ✅ Test and validate
7. ✅ Remove polling code (or keep as fallback)

