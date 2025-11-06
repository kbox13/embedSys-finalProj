# BeatNet Streaming Analysis

This directory contains scripts for real-time beat tracking using BeatNet with system audio input.

## Files

- `beatnet_streaming.py` - Main streaming analysis script
- `setup_macos_audio.py` - macOS audio setup helper
- `README.md` - This file

## Quick Start

### 1. Setup Audio Routing (macOS)

First, run the setup helper to check your system:

```bash
python setup_macos_audio.py
```

This will:
- Check for audio routing software (BlackHole, SoundFlower)
- Verify audio permissions
- List available audio devices
- Provide setup instructions

### 2. Install Audio Routing Software

If you don't have audio routing software installed:

**Option A: BlackHole (Recommended)**
- Download from: https://github.com/ExistentialAudio/BlackHole
- Install the .pkg file
- Restart your Mac

**Option B: SoundFlower**
- Download from: https://github.com/mattingalls/Soundflower
- Install the .pkg file
- Restart your Mac

### 3. Configure Audio Routing

1. Open **System Preferences > Sound**
2. Set **Output** to BlackHole 2ch (or SoundFlower 2ch)
3. This routes all system audio to the virtual device

### 4. Run BeatNet Analysis

```bash
# List available audio devices
python beatnet_streaming.py --list-devices

# Run analysis with default settings
python beatnet_streaming.py

# Run with specific audio device
python beatnet_streaming.py --device 1

# Run with different BeatNet model
python beatnet_streaming.py --model 2

# Enable visualization
python beatnet_streaming.py --plot

# Use threading for better performance
python beatnet_streaming.py --thread
```

## Command Line Options

- `--device DEVICE_ID` - Specify audio input device ID
- `--model MODEL_NUM` - BeatNet model number (1-3)
- `--plot` - Enable beat/downbeat visualization
- `--thread` - Use threading for inference
- `--list-devices` - List available audio input devices

## Troubleshooting

### Audio Permission Issues
- Go to **System Preferences > Security & Privacy > Privacy**
- Select **Microphone** from the left sidebar
- Add Terminal or your Python app to the list
- Restart the application

### No Audio Input
- Check that the virtual audio device is set as output
- Verify music is playing through the virtual device
- Try increasing the volume
- Check that the correct input device is selected

### Poor Beat Detection
- Ensure audio is loud enough (BeatNet works best with mastered audio)
- Try different BeatNet models (`--model 1`, `2`, or `3`)
- Reduce background noise
- Use high-quality audio sources

### Performance Issues
- Use `--thread` flag for better performance
- Close other audio applications
- Ensure sufficient CPU resources

## Requirements

- Python 3.7+
- BeatNet
- PyAudio
- Librosa
- Madmom
- PyTorch
- macOS (for audio routing)

## Installation

```bash
# Install dependencies
pip install pyaudio librosa madmom torch

# Install BeatNet
pip install git+https://github.com/mjhydri/BeatNet
```

## How It Works

1. **Audio Capture**: The script captures audio from your system's output using PyAudio
2. **Real-time Processing**: BeatNet processes audio in streaming mode with particle filtering
3. **Beat Detection**: Extracts beats, downbeats, and tempo information
4. **Live Display**: Shows real-time tempo, beat count, and analysis statistics

The system works by:
- Capturing system audio through a virtual audio device
- Processing audio frames in real-time using BeatNet's neural network
- Using particle filtering for causal beat tracking
- Displaying results as they're computed

## Example Output

```
🎵 Tempo: 128.5 BPM | Beats: 45 | Downbeats: 12 | Frame: 1234
```

## Notes

- BeatNet works best with mastered music at reasonable volume levels
- The streaming mode has a small delay (~0.084s) due to windowing
- Different BeatNet models are trained on different datasets:
  - Model 1: GTZAN dataset
  - Model 2: Ballroom dataset  
  - Model 3: Rock corpus dataset

