# Logging Plan: Hits and Predictions with Accurate Timing

## Problem Statement
We need to log all gate hits and predictions with accurate, comparable timing to evaluate prediction accuracy. Currently:
- **Gates** are published via ZeroMQPublisher with wall clock timestamps
- **Predictions** are published via InstrumentPredictor with audio-time (frame-based) timestamps
- These use **different timing references**, making accurate comparison difficult

## Current Architecture Analysis

### Gate Flow (Hits)
1. `HitGateOnset` algorithms (kick, snare, clap, chat, ohc) output 0.0 or 1.0 per frame
2. Outputs go to `VectorPack5` which packs them into a vector
3. Individual gates also go to `ZeroMQPublisher` instances (currently mostly commented out)
4. `ZeroMQPublisher` buffers values and publishes when buffer fills
5. **Timing**: Uses wall clock time (milliseconds since epoch) + own frame counter

### Prediction Flow
1. `InstrumentPredictor` receives packed gate vector per frame
2. Updates Kalman/PLL trackers for each instrument on hits
3. Generates predictions periodically or on hits
4. Publishes JSON via ZeroMQ to port 5556
5. **Timing**: Uses audio time (`frameCount * hopSize / sampleRate`) + frame index

### Timing Issues
- **Different time references**: Wall clock vs audio time
- **Different frame counters**: ZeroMQPublisher and InstrumentPredictor maintain separate counters
- **Network latency**: Logging on Python side adds variable delay
- **Batching**: ZeroMQPublisher buffers hits, losing precise frame-level timing

## Recommended Solution: Log on C++ Side (streaming_pipe.cpp)

### Why C++ Side?
1. **Same time reference**: Can compute audio time from a shared frame counter
2. **Frame-level precision**: Log exactly when gates fire (from HitGateOnset) and when predictions are made
3. **No network latency**: Direct access to algorithm outputs
4. **Synchronized timing**: Gates and predictions logged with same frame/time reference
5. **Accurate correlation**: Can match prediction timestamps with actual hit timestamps

### Implementation Approach

#### Option A: Intercept at Algorithm Level (Recommended)
Create a logging algorithm that sits between the algorithms and their outputs:

```
HitGateOnset -> LoggingAlgorithm -> VectorPack5
                                      -> InstrumentPredictor -> LoggingAlgorithm
```

**Pros:**
- Captures exact frame when gate fires
- Can log before batching/buffering
- Clean separation of concerns

**Cons:**
- Requires new Essentia algorithm class
- More code to maintain

#### Option B: Log in streaming_pipe.cpp (Simpler)
Hook into the existing pipeline wiring in `streaming_pipe.cpp`:

1. **For Gates**: Add a logging callback after `HitGateOnset` outputs but before they're used
   - Use Essentia's `Pool` storage to capture gate values per frame
   - Or create a custom sink algorithm that logs to file

2. **For Predictions**: Intercept the JSON string before it's published
   - Modify `InstrumentPredictor` to have a logging callback
   - Or parse predictions from the pool if stored there

**Pros:**
- Simpler implementation
- No new algorithm classes needed
- Can reuse existing BeatLogger pattern

**Cons:**
- Less clean separation
- May need to modify existing algorithms slightly

#### Option C: Hybrid - File Logger in C++ (Best Balance)
Create a simple file logger in C++ that:
- Tracks a shared frame counter (matches audio processing)
- Logs gate hits directly from gate outputs (before batching)
- Logs predictions when they're generated in InstrumentPredictor

### Recommended Implementation: Option C

#### 1. Create HitPredictionLogger Class
```cpp
class HitPredictionLogger {
private:
    std::ofstream log_file;
    int shared_frame_counter;
    Real sample_rate;
    int hop_size;
    
public:
    void log_gate_hit(int inst_index, int frame_idx, Real audio_time);
    void log_prediction(int inst_index, Real pred_time, Real confidence, 
                       Real audio_time, int frame_idx);
    void log_prediction_batch(const std::vector<PredictionHit>& hits, 
                              int inst_index, Real current_time, int frame_idx);
};
```

#### 2. Hook into Gate Outputs
In `streaming_pipe.cpp`, after creating gates:
```cpp
// Create logger with shared frame counter
HitPredictionLogger logger(sampleRate, hopSize, "hits_predictions.log");

// Wire gates to logger (parallel to existing wiring)
kick_gate->output("out") >> logger.input_gate(0);  // 0=kick
snare_gate->output("out") >> logger.input_gate(1);  // 1=snare
// ... etc
```

#### 3. Hook into Predictions
Modify `InstrumentPredictor::generatePredictions()` to accept a logger callback:
```cpp
void InstrumentPredictor::generatePredictions(Real currentTime, bool forceEmit) {
    // ... existing code ...
    std::string json = serializePredictions(allPredictions, currentTime);
    
    // Log to file
    if (logger) {
        for (int i = 0; i < NUM_INSTRUMENTS; ++i) {
            logger->log_prediction_batch(allPredictions[i], i, 
                                        currentTime, _frameCount);
        }
    }
    
    publishZeroMQ(json);
}
```

#### 4. Log Format
Use structured format for easy parsing:
```
FRAME|TIME|TYPE|INST|DATA
```
- `FRAME`: Shared frame counter
- `TIME`: Audio time in seconds (frame * hopSize / sampleRate)
- `TYPE`: "HIT" or "PRED"
- `INST`: Instrument index (0-4) or name
- `DATA`: JSON for predictions (confidence, CI, etc.)

Example:
```
1024|0.580|HIT|kick|{}
1024|0.580|PRED|snare|{"t_pred":0.650,"confidence":0.85,"ci_low":0.645,"ci_high":0.655}
```

### Alternative: Log on Python Side (If C++ Logging Too Complex)

If implementing C++ logging is too complex, we can log on Python side but with improvements:

#### Improvements Needed:
1. **Normalize timestamps**: Convert gate wall-clock times to audio time using frame counts
2. **Order messages**: Handle out-of-order arrival
3. **Frame mapping**: Map ZeroMQPublisher frame_count to actual audio frame
4. **Delay compensation**: Account for network latency

#### Implementation:
In `zmq_hit_visualizer.py`, enhance logging:
- Parse frame_count from gate messages
- Convert wall clock timestamp to audio time: `audio_time = (frame_count * hop_size) / sample_rate`
- Store predictions with their audio time from message
- Log both with normalized audio time

**Drawback**: Still has network latency, but timing should be accurate enough if we use frame-based audio time.

## Decision Matrix

| Approach | Accuracy | Complexity | Maintainability | Recommendation |
|----------|----------|------------|-----------------|----------------|
| C++ Intercept (A) | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐⭐⭐ | Best for accuracy |
| C++ Hook (B) | ⭐⭐⭐⭐ | ⭐⭐ | ⭐⭐⭐ | Good balance |
| C++ File Logger (C) | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐⭐⭐ | **Recommended** |
| Python Side | ⭐⭐⭐ | ⭐ | ⭐⭐⭐⭐ | Fallback if C++ too hard |

## Recommended Next Steps

1. **Implement Option C** (C++ File Logger):
   - Create `HitPredictionLogger` class
   - Hook into gate outputs in `streaming_pipe.cpp`
   - Add logging callback to `InstrumentPredictor`
   - Use shared frame counter for accurate timing

2. **Log Format**: JSON Lines (one object per line) for easy parsing:
   ```json
   {"frame":1024,"audio_time":0.580,"type":"hit","instrument":"kick"}
   {"frame":1024,"audio_time":0.580,"type":"prediction","instrument":"snare","predicted_time":0.650,"confidence":0.85}
   ```

3. **Timing Precision**: 
   - Use `frame_idx * hopSize / sampleRate` for both hits and predictions
   - Frame-level precision is sufficient (256 samples = ~5.8ms at 44.1kHz)

4. **File Naming**: Timestamped like existing beat_logger: `hits_predictions_YYYYMMDD_HHMMSS.log`

## Questions to Consider
1. Do we need wall clock time in addition to audio time? (For correlation with external events)
2. Should we log every frame or only when gates fire? (Only hits is more efficient)
3. Do we need real-time analysis or post-processing? (Post-processing is simpler)
4. Should logger be optional/configurable? (Yes, via command-line flag)


