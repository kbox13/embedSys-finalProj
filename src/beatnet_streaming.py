#!/usr/bin/env python3
"""
BeatNet Streaming Script for System Audio Analysis

This script runs BeatNet in streaming mode and connects to music playing on your device.
It captures system audio and performs real-time beat/downbeat tracking.

Requirements:
- BeatNet installed
- PyAudio for audio streaming
- macOS with proper audio permissions
- System audio routed to capture device (e.g., BlackHole, SoundFlower, or built-in microphone)

Usage:
    python beatnet_streaming.py [--device DEVICE_ID] [--model MODEL_NUM] [--plot]
"""

import sys
import os
import argparse
import time
import signal
import threading
import queue
import logging
from datetime import datetime
from typing import Optional, List, Dict, Any, Callable

# Add BeatNet to path
sys.path.append(os.path.join(os.path.dirname(__file__), '..', 'BeatNet', 'src'))

try:
    import pyaudio
    import numpy as np
    import torch
    from BeatNet.BeatNet import BeatNet
    from beat_predictor_kf import BeatPredictorKF
    from beat_logger import BeatLogger
    from beat_visualizer import BeatVisualizer
    from vispy import app
except ImportError as e:
    print(f"Error importing required modules: {e}")
    print("Please install required dependencies:")
    print("pip install pyaudio librosa torch")
    sys.exit(1)


class BeatNetStreamingAnalyzer:
    """
    Real-time BeatNet analyzer for system audio streaming
    """

    def __init__(self, model: int = 1, device_id: Optional[int] = None, 
                 plot: bool = False, thread: bool = False, async_mode: bool = False):
        """
        Initialize the BeatNet streaming analyzer
        
        Args:
            model: BeatNet model number (1-3)
            device_id: Audio input device ID (None for default)
            plot: Enable plotting
            thread: Use threading for inference
            async_mode: Run in asynchronous mode
        """
        self.model_num = model
        self.device_id = device_id
        self.plot_enabled = plot
        self.thread_enabled = thread
        self.async_mode = async_mode
        self.running = False
        self.beatnet = None
        self.audio = None
        self.stream_thread: Optional[threading.Thread] = None
        self.output_queue: "queue.Queue[Any]" = queue.Queue(maxsize=32)

        # Beat processing system
        self.beat_queue: "queue.Queue[Dict[str, Any]]" = queue.Queue(maxsize=128)
        self.beat_thread: Optional[threading.Thread] = None
        self.processed_events = set()  # Track processed events to avoid duplicates

        # Beat tracking results
        self.current_beats = []
        self.current_downbeats = []
        self.last_beat_time = 0.0

        # Statistics
        self.frame_count = 0
        self.start_time = None
        self.stream_wallclock_offset = None

        # Beat predictor for timing estimation
        self.predictor = BeatPredictorKF()

        # Wallclock mapping parameters
        self.offset_ema_alpha = 0.1  # EMA smoothing factor for offset estimation

    def list_audio_devices(self) -> List[Dict[str, Any]]:
        """
        List available audio input devices
        
        Returns:
            List of device information dictionaries
        """
        audio = pyaudio.PyAudio()
        devices = []

        print("\nAvailable Audio Input Devices:")
        print("-" * 50)

        for i in range(audio.get_device_count()):
            device_info = audio.get_device_info_by_index(i)
            if device_info['maxInputChannels'] > 0:
                devices.append({
                    'index': i,
                    'name': device_info['name'],
                    'channels': device_info['maxInputChannels'],
                    'sample_rate': device_info['defaultSampleRate']
                })
                print(f"Device {i}: {device_info['name']}")
                print(f"  Channels: {device_info['maxInputChannels']}")
                print(f"  Sample Rate: {device_info['defaultSampleRate']}")
                print()

        audio.terminate()
        return devices

    def initialize_beatnet(self) -> bool:
        """
        Initialize BeatNet with streaming mode
        
        Returns:
            True if initialization successful, False otherwise
        """
        try:
            print(f"Initializing BeatNet model {self.model_num}...")

            # Configure plotting
            plot_list = []
            if self.plot_enabled:
                plot_list = ['beat_particles', 'downbeat_particles']
            # Guard: plotting and threading/async are incompatible in BeatNet
            if self.plot_enabled and (self.thread_enabled or self.async_mode):
                print("Plotting cannot be used with threading or async mode; disabling plotting.")
                self.plot_enabled = False
                plot_list = []

            # Initialize BeatNet
            self.beatnet = BeatNet(
                model=self.model_num,
                mode='stream',
                inference_model='PF',
                plot=plot_list,
                thread=self.thread_enabled,
                device='cpu',  # Use CPU for better compatibility
                input_device_index=self.device_id
            )

            print("BeatNet initialized successfully!")
            return True

        except Exception as e:
            print(f"Error initializing BeatNet: {e}")
            return False

    def setup_audio_stream(self) -> bool:
        """
        Setup PyAudio stream for system audio capture
        
        Returns:
            True if setup successful, False otherwise
        """
        # BeatNet internally opens and manages its PyAudio stream in stream mode.
        # Nothing to do here; keep method for CLI flow compatibility.
        print("Using BeatNet-managed audio stream (no separate stream created).")
        return True

    def process_individual_beat(self, event_data: Dict[str, Any]):
        """
        Process a single beat/downbeat event

        Args:
            event_data: Dictionary containing event information

        Returns:
            Dictionary containing processed beat data for logging
        """
        beat_time = event_data['beat_time']
        wall_beat_time = event_data.get('wall_beat_time', beat_time)
        event_type = event_data.get('event_type', 1)  # 1=beat, 2=downbeat

        # Calculate relative time from program start for logging
        if self.start_time is not None:
            relative_time = wall_beat_time - self.start_time
        else:
            relative_time = beat_time

        # Update predictor with stream time (predictor doesn't care about absolute vs relative)
        self.predictor.observe(beat_time)

        # Get predictions for next beats
        preds = self.predictor.predict_next_beats(beat_time, k=4)
        sigma = self.predictor.confidence_std()

        # Return processed data for external logging
        action_type = 'beat_event' if event_type == 1 else 'downbeat_event'
        return {
            "action_type": action_type,
            "beat_time": relative_time,  # Use relative time for logging
            "wall_beat_time": wall_beat_time,  # Return this so it is available
            "predicted_beats": preds,
            "confidence_std": sigma,
        }

    def process_beat_results(self, output):
        """
        Legacy method for synchronous mode - now just calls individual beat processing
        """
        if output is None or len(output) == 0:
            return

        # BeatNet columns: [:,0]=time(s), [:,1]=flag (1=beat, 2=downbeat)
        beats = output[output[:, 1] == 1]  # Only beats, not downbeats

        if len(beats) > 0:
            # Process each beat individually
            for beat_row in beats:
                beat_time_stream = beat_row[0]
                now_wall = time.time()
                wall_beat_time = self._to_wallclock(beat_time_stream, now_wall)

                beat_data = {
                    'beat_time': beat_time_stream,
                    'wall_beat_time': wall_beat_time,
                    'timestamp': now_wall,
                    'frame_count': self.frame_count
                }

                self.process_individual_beat(beat_data)

    def run_streaming_analysis(self):
        """
        Main streaming analysis loop
        """
        print("\n🎵 Starting BeatNet streaming analysis...")
        print("Press Ctrl+C to stop\n")

        self.running = True
        self.start_time = time.time()

        try:
            # Delegate streaming, inference, and plotting to BeatNet as per README
            # This call blocks while the internal stream is active until interrupted
            self.beatnet.process()

        except KeyboardInterrupt:
            print("\n\nKeyboard interrupt. Stopping analysis...")
        except Exception as e:
            print(f"\nError during analysis: {e}")
        finally:
            self.cleanup()

    def _to_wallclock(self, ts_stream: float, now_wall: float) -> float:
        """
        Map BeatNet's stream time (seconds since stream start) to wall-clock seconds.
        We estimate offset at runtime: offset ≈ now - ts_stream_of_latest_beat.
        """
        if self.stream_wallclock_offset is None:
            self.stream_wallclock_offset = now_wall - ts_stream
        else:
            est = now_wall - ts_stream
            # Smooth to handle device/driver scheduling jitter
            self.stream_wallclock_offset = (
                (1 - self.offset_ema_alpha) * self.stream_wallclock_offset
                + self.offset_ema_alpha * est
            )
        return ts_stream + self.stream_wallclock_offset

    def _beat_detection_worker(self):
        """
        Background worker: detect beats and queue them for processing
        """
        try:
            self.running = True
            self.start_time = time.time()

            while self.running and self.beatnet.stream.is_active():
                # Extract features for current frame
                self.beatnet.activation_extractor_stream()
                # Increment BeatNet frame counter
                self.beatnet.counter += 1

                # Once warm, run inference and detect beats
                if self.beatnet.counter >= 5:
                    output = self.beatnet.estimator.process(self.beatnet.pred)

                    if output is not None and len(output) > 0:
                        # Extract beat times from BeatNet output
                        # BeatNet columns: [:,0]=time(s), [:,1]=flag (1=beat, 2=downbeat)

                        if len(output) > 0:
                            # Process each event individually with deduplication
                            for event_row in output:
                                event_time_stream = event_row[0]
                                event_type = event_row[1]  # 1=beat, 2=downbeat

                                # Create unique key for deduplication (time + type)
                                # Use 10ms tolerance for time matching
                                time_key = round(event_time_stream, 2)  # Round to 10ms
                                dedup_key = (time_key, event_type)

                                # Only queue if we haven't seen this exact event before
                                if dedup_key not in self.processed_events:
                                    self.processed_events.add(dedup_key)

                                    # Clean up old processed events (keep only last 1000)
                                    if len(self.processed_events) > 1000:
                                        # Remove oldest 500 events
                                        old_events = list(self.processed_events)[:500]
                                        for old_event in old_events:
                                            self.processed_events.discard(old_event)

                                    now_wall = time.time()
                                    wall_event_time = self._to_wallclock(event_time_stream, now_wall)

                                    # Queue this unique event for main thread processing
                                    event_data = {
                                        'beat_time': event_time_stream,
                                        'wall_beat_time': wall_event_time,
                                        'event_type': event_type,
                                        'timestamp': now_wall,
                                        'frame_count': self.frame_count
                                    }

                                    try:
                                        self.beat_queue.put_nowait(event_data)
                                    except queue.Full:
                                        # Drop oldest event if queue is full
                                        try:
                                            self.beat_queue.get_nowait()
                                            self.beat_queue.put_nowait(event_data)
                                        except queue.Empty:
                                            pass

                self.frame_count += 1
                time.sleep(0.001)  # ~1000 FPS

        except Exception as e:
            print(f"\nError in beat detection worker: {e}")
        finally:
            self.cleanup()

    # ---------------------- Async API ----------------------
    def _streaming_worker(self):
        """Background worker: stream audio, run inference, and enqueue outputs."""
        try:
            self.running = True
            self.start_time = time.time()

            while self.running and self.beatnet.stream.is_active():
                # Extract features for current frame
                self.beatnet.activation_extractor_stream()
                # Increment BeatNet frame counter (mirrors BeatNet.process loop behavior)
                self.beatnet.counter += 1
                # Once warm, run inference and emit output
                if self.beatnet.counter >= 5:
                    output = self.beatnet.estimator.process(self.beatnet.pred)
                    # Non-blocking put: keep only most recent outputs
                    if output is not None:
                        while not self.output_queue.empty():
                            try:
                                self.output_queue.get_nowait()
                            except queue.Empty:
                                break
                        try:
                            self.output_queue.put_nowait(output)
                        except queue.Full:
                            pass
                self.frame_count += 1
                time.sleep(0.001)
        except Exception as e:
            print(f"\nError in async streaming worker: {e}")
        finally:
            self.cleanup()

    def start_async(self) -> None:
        """Start streaming and beat detection in background thread (non-blocking)."""
        if self.beat_thread and self.beat_thread.is_alive():
            return
        if not self.beatnet:
            if not self.initialize_beatnet():
                raise RuntimeError("Failed to initialize BeatNet")

        # Start background beat detection
        self.beat_thread = threading.Thread(target=self._beat_detection_worker, daemon=True)
        self.beat_thread.start()

        print("Async beat detection started. Processing beats in main thread...")

    def stop_async(self) -> None:
        """Stop background beat detection."""
        self.running = False
        if self.beat_thread and self.beat_thread.is_alive():
            self.beat_thread.join(timeout=2.0)

    def get_latest_output(self, timeout: Optional[float] = 0.0):
        """Fetch the most recent output, if any. Returns None if no data available."""
        try:
            if timeout and timeout > 0:
                return self.output_queue.get(timeout=timeout)
            return self.output_queue.get_nowait()
        except queue.Empty:
            return None

    def get_next_beat(self, timeout: Optional[float] = 0.1):
        """Fetch the next beat from the queue. Returns None if no beat available."""
        try:
            if timeout and timeout > 0:
                return self.beat_queue.get(timeout=timeout)
            return self.beat_queue.get_nowait()
        except queue.Empty:
            return None

    def cleanup(self):
        """
        Clean up resources
        """
        self.running = False

        if self.beatnet and hasattr(self.beatnet, 'stream'):
            self.beatnet.stream.stop_stream()
            self.beatnet.stream.close()

        if self.audio:
            self.audio.terminate()

        # Display final statistics
        if self.start_time:
            duration = time.time() - self.start_time
            print(f"\n\n📊 Analysis Complete!")
            print(f"Duration: {duration:.1f} seconds")
            print(f"Frames processed: {self.frame_count}")
            print(f"Average FPS: {self.frame_count / duration:.1f}")


def signal_handler(signum, frame):
    """Handle interrupt signals"""
    print("\nReceived interrupt signal. Stopping...")
    sys.exit(0)


def main():
    """
    Main function
    """
    parser = argparse.ArgumentParser(
        description="BeatNet Streaming Analysis for System Audio",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python beatnet_streaming.py                    # Use default settings
  python beatnet_streaming.py --device 1         # Use specific audio device
  python beatnet_streaming.py --model 2          # Use BeatNet model 2
  python beatnet_streaming.py --plot             # Enable visualization
  python beatnet_streaming.py --list-devices     # List available devices
        """
    )

    parser.add_argument('--device', type=int, default=None,
                       help='Audio input device ID (use --list-devices to see available)')
    parser.add_argument('--model', type=int, choices=[1, 2, 3], default=1,
                       help='BeatNet model number (1-3)')
    parser.add_argument('--plot', action='store_true',
                       help='Enable beat/downbeat visualization')
    parser.add_argument('--thread', action='store_true',
                       help='Use threading for inference')
    parser.add_argument('--async', dest='async_mode', action='store_true',
                       help='Run streaming asynchronously and print outputs non-blocking')
    parser.add_argument('--list-devices', action='store_true',
                       help='List available audio input devices and exit')

    args = parser.parse_args()

    # Set up signal handler
    signal.signal(signal.SIGINT, signal_handler)

    # Create analyzer
    analyzer = BeatNetStreamingAnalyzer(
        model=args.model,
        device_id=args.device,
        plot=args.plot,
        thread=args.thread,
        async_mode=args.async_mode
    )

    # List devices if requested
    if args.list_devices:
        analyzer.list_audio_devices()
        return

    # Initialize BeatNet
    if not analyzer.initialize_beatnet():
        print("Failed to initialize BeatNet. Exiting.")
        sys.exit(1)

    # Initialize beat logger
    beat_logger = BeatLogger(
        log_to_console=True, log_to_file=True, log_format="detailed"
    )

    # Initialize beat visualizer
    beat_visualizer = BeatVisualizer()
    beat_visualizer.start()  # Start the visualization window

    # Async or blocking
    if args.async_mode:
        analyzer.start_async()
        beat_logger.start()
        print("Streaming started asynchronously. Press Ctrl+C to stop.\n")

        # Start Vispy event loop in a timer-based approach
        def process_beats_and_update():
            try:
                # Process beats as they arrive
                beat_data = analyzer.get_next_beat(timeout=0.1)
                if beat_data is not None:
                    beat_action = analyzer.process_individual_beat(beat_data)
                    beat_logger.queue_beat_action(
                        action_type=beat_action["action_type"],
                        beat_time=beat_action["beat_time"],
                        predicted_beats=beat_action["predicted_beats"],
                        confidence_std=beat_action["confidence_std"],
                    )
                    beat_visualizer.queue_visualization_action(
                        action_type=beat_action["action_type"],
                        beat_time=beat_action["beat_time"],
                        wall_beat_time=beat_action["wall_beat_time"],
                        predicted_beats=beat_action["predicted_beats"],
                        confidence_std=beat_action["confidence_std"],
                    )

                # Process Vispy events
                app.process_events()

                # Schedule next update
                if analyzer.running:
                    timer = threading.Timer(0.1, process_beats_and_update)
                    timer.daemon = True
                    timer.start()

            except Exception as e:
                print(f"Error in beat processing: {e}")

        # Start the processing loop
        process_beats_and_update()

        try:
            # Run Vispy app (this will block until window is closed)
            app.run()
        except KeyboardInterrupt:
            pass
        finally:
            beat_logger.stop()
            beat_visualizer.stop()
            analyzer.stop_async()
    else:
        beat_logger.start()
        try:
            analyzer.run_streaming_analysis()
        finally:
            beat_logger.stop()
            beat_visualizer.stop()


if __name__ == "__main__":
    main()
