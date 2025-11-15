# Instrument Detection Tuning Guide

This guide explains all tunable parameters for instrument detection in the audio pipeline.

## Pipeline Overview

```
Audio → FrameCutter → Windowing → Spectrum → MelBands → InstrumentSum → HitGateOnset → Output
```

## 1. Instrument Frequency Masks (InstrumentSum)

**Location:** `cpp/ess_stream/src/app/instrument_sum.cpp` - `buildDefaultMasks()` function

**Current Defaults:**
- **Kick**: 
  - 45-90 Hz (weight 0.55) - sub-bass body
  - 110-180 Hz (weight 0.15) - low bass
  - 2500-5000 Hz (weight 0.30) - click/punch
  
- **Snare**: 
  - 180-280 Hz (weight 0.35) - body/thump
  - 350-600 Hz (weight 0.10) - low-mid body
  - 2000-5000 Hz (weight 0.35) - attack/crack
  - 6000-10000 Hz (weight 0.20) - air/sizzle

- **Clap**: 
  - 800-1600 Hz (weight 0.30) - mid body
  - 2000-6000 Hz (weight 0.50) - main attack
  - 6000-10000 Hz (weight 0.20) - high end

- **Closed Hat**: 
  - 3000-6000 Hz (weight 0.25) - attack
  - 7000-12000 Hz (weight 0.55) - main energy
  - 12000-16000 Hz (weight 0.20) - shimmer

- **Open Hat/Crash**: 
  - 3000-6000 Hz (weight 0.25) - attack
  - 6000-12000 Hz (weight 0.50) - main energy
  - 12000-16000 Hz (weight 0.25) - shimmer

### Tuning Frequency Masks

To modify frequency masks, edit the `buildDefaultMasks()` function in `instrument_sum.cpp`:

```cpp
// Example: Adjusting Kick drum frequencies
addInstrument(0, { 
  {45.0f, 90.0f, 0.55f},    // Change: Lower cutoff for deeper kick
  {110.0f, 180.0f, 0.15f},  // Change: Increase weight for more body
  {2500.0f, 5000.0f, 0.30f} // Change: Narrow range for cleaner click
});
```

**Parameters:**
- `f1, f2`: Frequency range in Hz (lower, upper)
- `weight`: Relative importance (0.0-1.0+), normalized internally
- Multiple lobes can be added per instrument

**Tips:**
- Lower frequencies (< 500 Hz) capture body/resonance
- Mid frequencies (500-5000 Hz) capture attack/transients
- High frequencies (> 5000 Hz) capture air/high-end detail
- Increase weight on problematic ranges if instrument is leaking into others

### InstrumentSum Parameters

**Location:** `cpp/ess_stream/src/app/streaming_pipe.cpp` lines 168-171

```cpp
Algorithm *instr = F.create("InstrumentSum",
  "sampleRate", sampleRateInt,    // 44100 - should match audio SR
  "expectedBands", 64,            // Must match MelBands numberBands
  "lobeRolloff", 0.15);           // 0.05-0.5: Edge smoothness (higher = sharper)
```

- **`lobeRolloff`** (0.05-0.5): Controls Hann window edge rolloff
  - Lower (0.05-0.10): Sharper cutoff, more selective
  - Higher (0.3-0.5): Smoother transitions, more overlap between instruments
  - **Default:** 0.15 (balanced)

## 2. Hit Detection Parameters (HitGateOnset)

**Location:** `cpp/ess_stream/src/app/streaming_pipe.cpp` lines 235-274

Each instrument has its own `HitGateOnset` instance with independent parameters.

### Current Settings

```cpp
// Kick
"method", "hfc",              // Onset detection method
"threshold", 1.2,             // MAD multiplier for adaptive threshold
"refractory", 4,               // Frames between detections (cooldown)
"warmup", 8,                   // Frames before detection starts
"sensitivity", 1.5,            // Overall sensitivity multiplier
"smooth_window", 2,            // ODF smoothing window size
"odf_window", 64);             // Adaptive threshold history window

// Snare
"method", "flux",
"threshold", 1.4,
"refractory", 4,
"warmup", 8,
"sensitivity", 1.8,
"smooth_window", 2,
"odf_window", 64);

// Similar for Clap, Chat, OHc...
```

### Parameter Descriptions

1. **`method`** - Onset Detection Function (ODF) type
   - **`hfc`** (High-Frequency Content): Good for sharp transients, kicks, high-hats
   - **`flux`** (Spectral Flux): Good for broadband transients, snares, claps
   - **`complex`**: Phase-based, sensitive to phase changes
   - **`melflux`**: Mel-scaled flux, good for harmonic instruments
   - **`rms`**: Simple amplitude-based
   - **Recommendation:** 
     - Kick → `hfc` (sharp low-frequency attacks)
     - Snare/Clap → `flux` (broadband attacks)
     - Hats → `hfc` (sharp high-frequency attacks)

2. **`threshold`** - Adaptive threshold multiplier (MAD-based)
   - **Lower values (0.8-1.2):** More sensitive, more false positives
   - **Higher values (1.5-2.5):** Less sensitive, fewer false positives
   - **Default range:** 1.2-1.6
   - **Tuning:** If missing hits, decrease by 0.2. If too many false triggers, increase by 0.2

3. **`refractory`** - Cooldown period (frames)
   - Prevents multiple triggers from a single hit
   - At 44.1kHz, 256 hop size: 1 frame ≈ 5.8ms
   - **Kick/Snare:** 3-6 frames (17-35ms) - allows fast patterns
   - **Hats:** 2-4 frames (12-23ms) - allows very fast patterns
   - **Tuning:** If getting double-triggers, increase. If missing rapid hits, decrease

4. **`warmup`** - Initial frames before detection starts
   - Allows adaptive threshold to stabilize
   - **Default:** 8 frames (~47ms)
   - Usually doesn't need tuning unless startup detection issues

5. **`sensitivity`** - Overall multiplier on detection function
   - **Lower (0.8-1.2):** Less sensitive
   - **Higher (1.5-2.5):** More sensitive
   - Works in conjunction with `threshold`
   - **Tuning:** Fine-tune after setting `threshold`

6. **`smooth_window`** - ODF smoothing window (frames)
   - Reduces noise in detection function
   - **Lower (1-2):** More responsive, more noise
   - **Higher (4-8):** Smoother, more latency
   - **Default:** 2 (good balance)
   - **Tuning:** If too jittery, increase. If missing fast attacks, decrease

7. **`odf_window`** - Adaptive threshold history window (frames)
   - Size of rolling window for median/MAD calculation
   - **Lower (32-48):** Faster adaptation, more sensitive to recent changes
   - **Higher (64-128):** Slower adaptation, more stable
   - **Default:** 64 frames (~370ms at 256 hop)
   - **Tuning:** If threshold adapts too slowly, decrease. If too noisy, increase

### Tuning Strategy for HitGateOnset

1. **Start with `method`**: Choose based on instrument type (see above)
2. **Set `threshold`**: Start at 1.3, adjust in ±0.2 steps
3. **Set `refractory`**: Start at 4, adjust for your fastest expected pattern
4. **Fine-tune `sensitivity`**: Adjust by ±0.2 if needed
5. **Adjust `odf_window`**: Only if threshold seems too slow/fast to adapt

## 3. Mel Band Analysis (MelBands)

**Location:** `cpp/ess_stream/src/app/streaming_pipe.cpp` line 167

**Currently using defaults.** You can add parameters:

```cpp
Algorithm* melbands = F.create("MelBands",
  "numberBands", 64,                    // Number of mel bands (default: 24)
  "sampleRate", sampleRate,              // 44100 (auto-detected)
  "lowFrequencyBound", 0.0,              // Minimum frequency (Hz)
  "highFrequencyBound", 22050.0,         // Maximum frequency (Nyquist)
  "warpingFormula", "htkMel",           // Mel scale: "htkMel" or "slaneyMel"
  "weighting", "warping",                // "warping" or "linear"
  "normalize", "unit_sum",               // "unit_sum", "unit_tri", "unit_max"
  "type", "power",                       // "power" or "magnitude"
  "log", false);                         // Log-energies (log2(1+energy))
```

### Important Parameters

1. **`numberBands`** - Number of mel frequency bands
   - **Current (implicit):** Likely 24-40 (Essentia default)
   - **Your code expects:** 64 (see `expectedBands` in InstrumentSum)
   - **Must match** `expectedBands` in InstrumentSum!
   - **More bands:** Better frequency resolution, more computation
   - **Fewer bands:** Less resolution, faster computation

2. **`lowFrequencyBound` / `highFrequencyBound`** - Frequency range
   - **Default:** 0 Hz to 22050 Hz (Nyquist at 44.1kHz)
   - **Tuning:** Narrow range if focusing on specific frequency region
   - Example: For kick-heavy mix, maybe 20-8000 Hz

3. **`warpingFormula`** - Mel scale implementation
   - **`htkMel`** (default): HTK toolkit formula
   - **`slaneyMel`**: Auditory toolbox formula (different curve)
   - Usually `htkMel` is fine

4. **`type`** - Output units
   - **`power`** (default): Squared magnitudes (energy)
   - **`magnitude`**: Linear magnitudes
   - **Keep `power`** for onset detection (it responds better to energy changes)

## 4. ZeroMQ Publisher Thresholds

**Location:** `cpp/ess_stream/src/app/streaming_pipe.cpp` lines 173-202

```cpp
Algorithm *kick_gate_publisher = F.create("ZeroMQPublisher",
  "threshold", 0.5,              // Only publish if gate value >= 0.5
  "threshold_mode", "above",      // "always", "above", "below"
  "buffer_size", 1);              // Publish every frame that meets threshold
```

- **`threshold`** (0.0-1.0): Minimum gate output value to publish
  - Gate outputs 0.0 (no hit) or 1.0 (hit detected)
  - **Default:** 0.5 works since gates are binary
  - Usually doesn't need tuning

- **`buffer_size`**: Currently 1 (publish immediately)
  - Can increase for batching if network is bottleneck

## 5. Lighting Engine Parameters

**Location:** `cpp/ess_stream/src/app/streaming_pipe.cpp` lines 391-395

The LightingEngine filters predictions from InstrumentPredictor and maps them to RGB lighting commands.

```cpp
Algorithm *lighting_engine = F.create("LightingEngine",
  "confidence_threshold", 0.70,      // Minimum confidence to send
  "max_latency_sec", 2.0,             // Maximum prediction latency
  "min_latency_sec", 0.07,            // Minimum prediction latency
  "duplicate_window_sec", 0.4);       // Time window for duplicate detection
```

### Parameter Descriptions

1. **`confidence_threshold`** (0.0-1.0): Minimum confidence to send lighting command
   - **Lower values (0.1-0.5):** More sensitive, sends more events (including low-confidence)
   - **Higher values (0.6-0.9):** Less sensitive, only sends high-confidence predictions
   - **Default:** 0.3 (in header), but 0.70 used in pipeline (more conservative)
   - **Tuning:** 
     - If missing lighting events, decrease by 0.1-0.2
     - If too many false triggers, increase by 0.1-0.2
     - Consider your prediction confidence distribution

2. **`max_latency_sec`** (0.1-10.0): Maximum prediction latency (seconds)
   - Only sends predictions that are at most this far in the future
   - **Default:** 2.0 seconds
   - **Tuning:**
     - Increase if you want to schedule events further ahead
     - Decrease if predictions become less accurate at longer horizons
     - Should match your prediction horizon (currently 2.0 seconds)

3. **`min_latency_sec`** (0.01-1.0): Minimum prediction latency (seconds)
   - Only sends predictions that are at least this far in the future
   - Prevents sending events that are too close (not enough time for embedded device)
   - **Default:** 0.05 seconds (50ms)
   - **Tuning:**
     - Increase if embedded device needs more lead time
     - Decrease if you want to react to very near-term predictions
     - Should account for MQTT latency + embedded device processing time

4. **`duplicate_window_sec`** (0.01-1.0): Time window for duplicate detection (seconds)
   - Prevents sending the same event multiple times
   - Events with same instrument and time (rounded to 10ms) within this window are considered duplicates
   - **Default:** 0.1 seconds (100ms)
   - **Tuning:**
     - Increase if getting duplicate events
     - Decrease if legitimate rapid-fire events are being filtered
     - Should be larger than your frame processing time

### Color Mapping

The LightingEngine maps instruments to RGB colors:
- **Kick** → Red (1, 0, 0)
- **Snare** → Green (0, 1, 0)
- **Clap/Chat/OHat** → Blue (0, 0, 1)

To modify colors, edit `mapInstrumentToColor()` in `lighting_engine.cpp`.

## 6. MQTT Publisher Parameters

**Location:** `cpp/ess_stream/src/app/streaming_pipe.cpp` lines 396-402

The MQTTPublisher converts lighting commands to Unix timestamps and publishes to MQTT broker.

```cpp
Algorithm *mqtt_publisher = F.create("MQTTPublisher",
  "broker_host", "172.20.10.5",        // MQTT broker hostname/IP
  "broker_port", 1883,                 // MQTT broker port
  "topic", "beat/events/schedule",      // MQTT topic
  "client_id", "essentia_lighting",     // MQTT client ID
  "batch_size", 1,                     // Unused (immediate publish)
  "batch_interval_ms", 50);            // Unused (immediate publish)
```

### Parameter Descriptions

1. **`broker_host`** (string): MQTT broker hostname or IP address
   - **Default:** "localhost"
   - **Example:** "172.20.10.5", "192.168.1.100", "mqtt.example.com"
   - **Tuning:** Set to your MQTT broker's address

2. **`broker_port`** (1-65535): MQTT broker port
   - **Default:** 1883 (standard MQTT port)
   - **TLS/SSL:** Use 8883 for secure MQTT
   - **Tuning:** Match your broker configuration

3. **`topic`** (string): MQTT topic for publishing events
   - **Default:** "beat/events/schedule"
   - **Format:** Must match what embedded device subscribes to
   - **Tuning:** Change if using different topic structure

4. **`client_id`** (string): MQTT client identifier
   - **Default:** "essentia_lighting"
   - **Tuning:** Use unique ID if running multiple instances
   - Should be unique per client on broker

5. **`batch_size`** and **`batch_interval_ms`**: **Unused** (kept for compatibility)
   - All commands are published immediately upon arrival
   - No batching delay for lowest latency

### MQTT Message Format

Published messages are JSON with the following structure:
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

- **`unix_time`**: Unix timestamp (seconds since epoch)
- **`microseconds`**: Microsecond component (0-999999)
- **`confidence`**: Prediction confidence (0.0-1.0)
- **`r`, `g`, `b`**: RGB color values (0 or 1 in current implementation)
- **`event_id`**: Unique event identifier (instrument_time)

### Unix Timestamp Conversion

The MQTTPublisher automatically converts prediction times (relative to pipeline start) to Unix timestamps:
- Captures system time when pipeline starts
- Adds prediction time offset to get absolute Unix time
- Handles microsecond precision and overflow correctly
- No manual time synchronization needed

## Tuning Workflow

### Step 1: Verify Mel Band Count
1. Check what `MelBands` defaults to (likely 24)
2. Either:
   - Explicitly set `"numberBands", 64` in MelBands, OR
   - Change `"expectedBands", 24` in InstrumentSum to match

### Step 2: Test Individual Instruments
1. Play isolated samples of each instrument
2. Check if they trigger correctly
3. If instrument A triggers instrument B → adjust frequency masks

### Step 3: Adjust Frequency Masks
1. If instrument leaking: Narrow problematic frequency ranges or reduce weights
2. If instrument missing: Widen frequency ranges or increase weights
3. Focus on the characteristic frequency bands for each instrument

### Step 4: Tune Hit Detection
1. Start with `threshold` = 1.3 for all instruments
2. For each instrument:
   - If missing hits: Decrease `threshold` by 0.2
   - If false triggers: Increase `threshold` by 0.2
   - Try different `method` if threshold alone doesn't help
3. Adjust `refractory` if double-triggers occur

### Step 5: Fine-tune Sensitivity
1. Adjust `sensitivity` by ±0.2 increments
2. Adjust `smooth_window` if detection is too noisy
3. Adjust `odf_window` if adaptive threshold is too slow/fast

## Common Issues and Solutions

### Issue: Kick triggering Snare
- **Solution:** Narrow Snare's low-frequency lobes (180-280 Hz range) or reduce weight
- Also check Kick's high-frequency lobe (2500-5000 Hz) isn't too wide

### Issue: Hats triggering Kick/Snare
- **Solution:** Ensure Hats masks start at 3000+ Hz, no overlap with lower frequencies
- Increase `threshold` for Kick/Snare to reduce sensitivity to high-frequency leakage

### Issue: Missing fast hits
- **Solution:** Decrease `refractory` period (try 2-3 frames)
- Increase `sensitivity` or decrease `threshold`

### Issue: Too many false triggers
- **Solution:** Increase `threshold` by 0.2-0.4
- Increase `smooth_window` to reduce noise
- Check if frequency masks have too much overlap

### Issue: Slow response to volume changes
- **Solution:** Decrease `odf_window` (try 32-48 frames)
- The adaptive threshold will adapt faster

### Issue: Inconsistent detection
- **Solution:** Increase `odf_window` for more stable threshold
- Increase `smooth_window` to reduce ODF noise

## Quick Reference: Where to Edit

| Component | File | Line Range |
|-----------|------|------------|
| Frequency Masks | `instrument_sum.cpp` | ~124-137 |
| InstrumentSum Config | `streaming_pipe.cpp` | 168-171 |
| Hit Detection (Kick) | `streaming_pipe.cpp` | 235-242 |
| Hit Detection (Snare) | `streaming_pipe.cpp` | 243-250 |
| Hit Detection (Clap) | `streaming_pipe.cpp` | 251-258 |
| Hit Detection (Chat) | `streaming_pipe.cpp` | 259-266 |
| Hit Detection (OHc) | `streaming_pipe.cpp` | 267-274 |
| Mel Bands Config | `streaming_pipe.cpp` | 167 (add params) |
| Lighting Engine Config | `streaming_pipe.cpp` | 391-395 |
| MQTT Publisher Config | `streaming_pipe.cpp` | 396-402 |
| Color Mapping | `lighting_engine.cpp` | `mapInstrumentToColor()` function |

## Example: Tuning for Specific Music Style

**EDM/Electronic:**
- Kick: Lower `threshold` to 1.0-1.1 (more sensitive to synthetic kicks)
- Snare: Use `flux` method, `threshold` 1.3-1.5
- Hats: `hfc` method, lower `refractory` (2-3) for fast patterns

**Acoustic/Organic:**
- All instruments: Higher `threshold` (1.5-1.8) to reduce false positives
- Increase `odf_window` (80-100) for more stable adaptation
- May need to adjust frequency masks for natural instrument timbres

