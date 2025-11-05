#!/usr/bin/env python3
"""
ZeroMQ Audio Hit Visualizer

This script subscribes to audio gate hits from the C++ Essentia streaming pipeline
and visualizes them in real-time using the BeatVisualizer framework.

Usage:
    python zmq_hit_visualizer.py [endpoint]
    
Example:
    python zmq_hit_visualizer.py tcp://*:5555
"""

import zmq
import json
import time
import sys
import threading
import queue
from datetime import datetime
import numpy as np

# Set the backend explicitly to ensure compatibility
try:
    import vispy
    vispy.use('PyQt6')  # Try PyQt6 first
except ImportError:
    try:
        vispy.use('PySide2')  # Fallback to PySide2
    except ImportError:
        try:
            vispy.use('glfw')  # Fallback to glfw
        except ImportError:
            print("Warning: No suitable Vispy backend found. Window may not appear.")

# Import the existing visualizer
from beat_visualizer import BeatVisualizer
import vispy.scene as scene
from vispy import app


class ZeroMQHitVisualizer:
    """
    Real-time visualization of audio gate hits from ZeroMQ stream.
    
    This class combines ZeroMQ subscription with the BeatVisualizer
    to show gate hits from the C++ Essentia pipeline.
    """

    def __init__(self, endpoint="tcp://*:5556", config=None, predictions_endpoint=None):
        """
        Initialize the ZeroMQ Hit Visualizer.

        Args:
            endpoint (str): ZeroMQ endpoint for gates (default: port 5555 for gates)
            config (dict, optional): Visualization configuration
            predictions_endpoint (str): ZeroMQ endpoint for predictions (default: port 5556)
        """
        self.endpoint = endpoint or "tcp://*:5555"
        self.predictions_endpoint = predictions_endpoint or "tcp://*:5556"
        self.context = zmq.Context()
        self.socket = None
        self.predictions_socket = None
        self.running = False
        self.start_time = time.time()

        # Default visualization config for gate hits
        default_config = {
            "canvas_size": (1000, 800),
            # Legacy colors
            "bass_gate_color": (1.0, 0.0, 0.0, 1.0),
            "mid_gate_color": (0.0, 1.0, 0.0, 1.0),
            "high_gate_color": (0.0, 0.0, 1.0, 1.0),
            # Instrument colors (Kick, Snare, Clap, CHat, OHatCrash)
            "inst_colors": [
                (0.95, 0.30, 0.30, 1.0),  # Kick - soft red
                (0.95, 0.75, 0.20, 1.0),  # Snare - amber
                (0.40, 0.80, 1.00, 1.0),  # Clap - cyan
                (0.60, 0.95, 0.60, 1.0),  # Closed Hat - green
                (0.75, 0.60, 1.00, 1.0),  # Open Hat/Crash - violet
            ],
            "hit_duration": 0.12,  # seconds
            "hit_size": 56,
            "enabled": True,
            "show_grid": False,
            "background_color": (0.05, 0.05, 0.05, 1.0),
            "show_labels": False,
            "layout": "horizontal",  # 'horizontal' or 'vertical'
        }

        # Update with provided config
        if config:
            default_config.update(config)

        # Initialize the beat visualizer with our config
        self.visualizer = BeatVisualizer(default_config)

        # Track hit statistics for individual instrument gates
        self.hit_stats = {
            "gate.kick": {"total_hits": 0, "recent_hits": []},
            "gate.snare": {"total_hits": 0, "recent_hits": []},
            "gate.clap": {"total_hits": 0, "recent_hits": []},
            "gate.chat": {"total_hits": 0, "recent_hits": []},
            "gate.ohc": {"total_hits": 0, "recent_hits": []},
            "predictions": {"total_predictions": 0, "recent_predictions": []},
        }

        # Map feature names to instrument indices
        self.feature_to_index = {
            "gate.kick": 0,
            "gate.snare": 1,
            "gate.clap": 2,
            "gate.chat": 3,
            "gate.ohc": 4,
        }

        # Track current predictions for visualization
        self.current_predictions = {}  # instrument -> list of prediction dicts

        # Scheduled predictions storage (for time-based display)
        # Structure: list of {instrument, hit_index, t_pred_sec, inst_index, creation_time}
        self.scheduled_predictions = []  # Predictions waiting to be displayed
        self.predictions_lock = (
            threading.Lock()
        )  # Lock for scheduled_predictions access

        # Queue-based rendering architecture
        self.render_queue = queue.Queue()  # Thread-safe queue for visualization events
        self.render_timer = None  # Vispy timer for render loop
        self.pending_visuals = (
            []
        )  # List of (visual1, visual2, creation_time, expiry_time) tuples
        self.stats_lock = threading.Lock()  # Lock for stats updates

        # Layout constants
        self.hits_column_x_ratio = 0.15  # Hits at 15% of canvas width
        self.prediction_column_spacing_ratio = (
            0.10  # 10% spacing between prediction columns
        )
        
        # Prediction deduplication threshold (seconds)
        # Predictions with same (instrument, hit_index) within this threshold are considered duplicates
        self.prediction_time_threshold_sec = 0.003  # 3ms default

        # Threading
        self.zmq_thread = None
        self.predictions_thread = None
        self.stats_thread = None

    def start(self):
        """Start the ZeroMQ subscriber and visualization"""
        if not self.running:
            print("Starting ZeroMQ Hit Visualizer...")
            self.running = True

            # Start the visualizer
            self.visualizer.start()

            # Start ZeroMQ subscriber thread for gates (if endpoint provided)
            if self.endpoint:
                self.zmq_thread = threading.Thread(target=self._zmq_worker, daemon=True)
                self.zmq_thread.start()

            # Start predictions subscriber thread (only if endpoint provided)
            if self.predictions_endpoint:
                self.predictions_thread = threading.Thread(
                    target=self._predictions_worker, daemon=True
                )
                self.predictions_thread.start()

            # Start stats display thread
            self.stats_thread = threading.Thread(
                target=self._stats_worker,
                daemon=True
            )
            self.stats_thread.start()

            # Start render loop using Vispy timer (runs in main thread)
            # Process render queue at 60fps (~16ms intervals)
            self.render_timer = app.Timer(
                interval=1.0 / 60.0, connect=self._render_worker, start=True
            )

            print("ZeroMQ Hit Visualizer started successfully!")
            if self.endpoint:
                print(f"Listening for gate hits at: {self.endpoint}")
            if self.predictions_endpoint:
                print(f"Listening for predictions at: {self.predictions_endpoint}")
            elif not self.endpoint and not self.predictions_endpoint:
                print("Warning: No endpoints configured. No data will be received.")

    def _setup_zmq_socket(self):
        """Setup ZeroMQ PULL socket"""
        self.socket = self.context.socket(zmq.PULL)
        self.socket.bind(self.endpoint)
        self.socket.setsockopt(zmq.RCVTIMEO, 1000)  # 1 second timeout
        print(f"ZeroMQ socket bound to: {self.endpoint}")

    def _setup_predictions_socket(self):
        """Setup ZeroMQ PULL socket for predictions"""
        self.predictions_socket = self.context.socket(zmq.PULL)
        self.predictions_socket.bind(self.predictions_endpoint)
        self.predictions_socket.setsockopt(zmq.RCVTIMEO, 1000)  # 1 second timeout
        print(f"ZeroMQ predictions socket bound to: {self.predictions_endpoint}")

    def _zmq_worker(self):
        """Background thread that receives ZeroMQ gate messages"""
        if not self.endpoint:
            return
        self._setup_zmq_socket()

        while self.running:
            try:
                # Receive message
                data = self.socket.recv_string()

                # Debug: print first message received
                if not hasattr(self, "_first_gate_received"):
                    print(f"First gate message received: {data[:100]}...")
                    self._first_gate_received = True

                # Parse JSON data
                feature_data = json.loads(data)

                # Process the gate hit
                self._process_gate_hit(feature_data)

            except zmq.Again:
                # Timeout - continue loop
                continue
            except json.JSONDecodeError as e:
                print(f"JSON decode error: {e}")
                print(f"Raw data: {data[:200] if 'data' in locals() else 'N/A'}")
            except Exception as e:
                print(f"ZeroMQ error: {e}")
                import traceback

                traceback.print_exc()
                break

    def _predictions_worker(self):
        """Background thread that receives prediction messages"""
        if not self.predictions_endpoint:
            return
        self._setup_predictions_socket()

        while self.running:
            try:
                # Receive message (using recv_string like gate worker)
                data = self.predictions_socket.recv_string()

                # Debug: print first message received
                if not hasattr(self, "_first_prediction_received"):
                    print(f"First prediction message received: {data[:100]}...")
                    self._first_prediction_received = True

                # Parse JSON data
                prediction_data = json.loads(data)

                # Process predictions
                self._process_predictions(prediction_data)

            except zmq.Again:
                # Timeout - continue loop
                continue
            except json.JSONDecodeError as e:
                print(f"JSON decode error in predictions: {e}")
                print(f"Raw data: {data[:200] if 'data' in locals() else 'N/A'}")
            except Exception as e:
                print(f"Predictions error: {e}")
                import traceback

                traceback.print_exc()
                break

    def _process_gate_hit(self, feature_data):
        """Process a gate hit from the ZeroMQ stream"""
        # Check if this is a gate message (has 'feature_name') or prediction (has 'predictions')
        if "predictions" in feature_data:
            # This is actually a prediction message, route to prediction handler
            self._process_predictions(feature_data)
            return

        # Process gate message format
        feature_name = feature_data.get("feature_name", "")
        values = feature_data.get("values", [])
        timestamp = feature_data.get("timestamp", time.time())
        frame_count = feature_data.get("frame_count", 0)

        # Process individual instrument gate messages (gate.kick, gate.snare, etc.)
        if not feature_name.startswith("gate."):
            return

        # Map feature name to instrument index
        inst_index = self.feature_to_index.get(feature_name, -1)
        if inst_index < 0:
            return  # Unknown gate type

        # Check for hits (any value >= 0.5 indicates a hit)
        # For individual gates, values is a list of buffered values
        has_hit = any(v >= 0.5 for v in values) if values else False

        if has_hit:
            # Update statistics (thread-safe)
            with self.stats_lock:
                if feature_name in self.hit_stats:
                    self.hit_stats[feature_name]["total_hits"] += 1
                    current_time = time.time()
                    self.hit_stats[feature_name]["recent_hits"].append(current_time)

                    # Keep only recent hits (last 10 seconds)
                    cutoff_time = current_time - 10.0
                    self.hit_stats[feature_name]["recent_hits"] = [
                        t
                        for t in self.hit_stats[feature_name]["recent_hits"]
                        if t > cutoff_time
                    ]

            # Enqueue visualization event instead of creating directly
            event = {
                "type": "gate_hit",
                "inst_index": inst_index,
                "timestamp": time.time(),
            }
            try:
                self.render_queue.put_nowait(event)
            except queue.Full:
                pass  # Skip if queue is full (shouldn't happen with unbounded queue)

    def _process_predictions(self, prediction_data):
        """Process prediction messages from InstrumentPredictor"""
        try:
            timestamp_sec = prediction_data.get(
                "timestamp_sec", time.time() - self.start_time
            )
            predictions = prediction_data.get("predictions", [])
            current_time = time.time()

            # Map instrument names to indices
            inst_names = ["kick", "snare", "clap", "chat", "ohc"]

            # Store predictions with their scheduled times instead of displaying immediately
            new_scheduled = []
            for pred in predictions:
                instrument = pred.get("instrument", "unknown")
                hits = pred.get("hits", [])

                if hits:
                    # Get instrument index
                    try:
                        inst_index = inst_names.index(instrument.lower())
                    except ValueError:
                        inst_index = 0  # Default to kick

                    # Update statistics (thread-safe)
                    with self.stats_lock:
                        self.hit_stats["predictions"]["total_predictions"] += len(hits)
                        for hit in hits:
                            self.hit_stats["predictions"]["recent_predictions"].append(
                                current_time
                            )

                    # Store each prediction with its scheduled time
                    for hit in hits:
                        t_pred_frame = hit.get(
                            "t_pred_sec", 0
                        )  # Frame time when hit should occur
                        hit_index = hit.get("hit_index", 1)

                        # Convert frame time to wall clock time
                        # t_pred_frame is relative to timestamp_sec (when prediction was made)
                        # Calculate time offset from prediction timestamp
                        time_offset = t_pred_frame - timestamp_sec
                        # Convert to absolute wall clock time
                        t_pred_absolute = current_time + time_offset

                        # Only schedule predictions in the future
                        if t_pred_absolute > current_time:
                            new_scheduled.append(
                                {
                                    "instrument": instrument,
                                    "inst_index": inst_index,
                                    "hit_index": hit_index,
                                    "t_pred_sec": t_pred_absolute,  # Store as absolute wall clock time
                                    "creation_time": current_time,
                                }
                            )

            # Thread-safely update scheduled predictions
            # Smart deduplication: keep existing predictions, only add new ones if they're
            # sufficiently different (> threshold) or don't exist yet
            with self.predictions_lock:
                for new_pred in new_scheduled:
                    # Find all existing predictions with same (instrument, hit_index)
                    matching_indices = [
                        i
                        for i, p in enumerate(self.scheduled_predictions)
                        if p["instrument"] == new_pred["instrument"]
                        and p["hit_index"] == new_pred["hit_index"]
                    ]
                    
                    if matching_indices:
                        # Match found - check time difference with closest match
                        # (defensive: if multiple matches exist, use the closest one)
                        time_diffs = [
                            abs(
                                new_pred["t_pred_sec"]
                                - self.scheduled_predictions[i]["t_pred_sec"]
                            )
                            for i in matching_indices
                        ]
                        min_time_diff = min(time_diffs)
                        
                        if min_time_diff <= self.prediction_time_threshold_sec:
                            # Within threshold: keep old prediction(s), discard new one
                            # (skip adding this new prediction)
                            continue
                        else:
                            # Time difference > threshold: keep old AND add new
                            # (old predictions remain in list, we'll add new below)
                            pass
                    
                    # Add new prediction (either no match, or > threshold difference)
                    self.scheduled_predictions.append(new_pred)

            # Clean old predictions from recent list
            with self.stats_lock:
                cutoff_time = current_time - 10.0
                self.hit_stats["predictions"]["recent_predictions"] = [
                    t
                    for t in self.hit_stats["predictions"]["recent_predictions"]
                    if t > cutoff_time
                ]

        except KeyError as e:
            print(f"Missing key in prediction data: {e}")
        except Exception as e:
            print(f"Error processing predictions: {e}")

    # (legacy per-band gate visualization removed)

    def _render_worker(self, event=None):
        """
        Render loop that processes all pending visualization events.
        This runs in the main thread via Vispy timer, ensuring thread-safe scene access.
        """
        current_time = time.time()

        # Process all pending events in batch
        events_to_process = []
        while True:
            try:
                event = self.render_queue.get_nowait()
                events_to_process.append(event)
            except queue.Empty:
                break

        # Batch create all visuals at once
        for event in events_to_process:
            if event["type"] == "gate_hit":
                self._create_instrument_hit_visual(event["inst_index"], 5)

        # Check for predictions that have reached their scheduled time
        with self.predictions_lock:
            predictions_to_display = [
                p for p in self.scheduled_predictions if current_time >= p["t_pred_sec"]
            ]
            # Remove displayed predictions from scheduled list
            self.scheduled_predictions = [
                p for p in self.scheduled_predictions if current_time < p["t_pred_sec"]
            ]

        # Display predictions that are due
        for pred_info in predictions_to_display:
            self._create_prediction_visual(
                pred_info["inst_index"], pred_info["hit_index"], 5  # total_instruments
            )

        # Cleanup expired visuals
        self._cleanup_expired_visuals(current_time)

    def _create_instrument_hit_visual(self, inst_index, total_instruments):
        """Visualize a hit for packed instrument vector"""
        # Position instruments in fixed lanes
        if self.visualizer.config["layout"] == "horizontal":
            # rows = 5 lanes
            row = inst_index
            num_rows = total_instruments
            canvas_width = self.visualizer.config["canvas_size"][0]
            canvas_height = self.visualizer.config["canvas_size"][1]
            y_spacing = canvas_height // (num_rows + 1)
            y_pos = (row + 1) * y_spacing
            # Position hits on left side
            x_pos = int(canvas_width * self.hits_column_x_ratio)
        else:
            col = inst_index
            num_cols = total_instruments
            canvas_width = self.visualizer.config["canvas_size"][0]
            canvas_height = self.visualizer.config["canvas_size"][1]
            x_spacing = canvas_width // (num_cols + 1)
            x_pos = (col + 1) * x_spacing
            y_pos = canvas_height // 2

        color = self._get_instrument_color(inst_index)

        hit_circle = scene.Ellipse(
            center=(x_pos, y_pos),
            radius=(
                self.visualizer.config["hit_size"],
                self.visualizer.config["hit_size"],
            ),
            color=color,
            parent=self.visualizer.scene,
        )

        hit_label = None
        if self.visualizer.config["show_labels"]:
            names = ["KICK", "SNARE", "CLAP", "CHH", "OHH/CRASH"]
            hit_label = scene.Text(
                names[inst_index] if inst_index < len(names) else f"INST {inst_index}",
                pos=(x_pos, y_pos + self.visualizer.config["hit_size"] + 10),
                font_size=12,
                color="white",
                parent=self.visualizer.scene,
            )

        # Store visual with expiry time for cleanup
        creation_time = time.time()
        expiry_time = creation_time + self.visualizer.config["hit_duration"]
        self.pending_visuals.append((hit_circle, hit_label, creation_time, expiry_time))

    def _create_prediction_visual(self, inst_index, hit_index, total_instruments):
        """
        Visualize a predicted hit that has reached its scheduled time.

        Args:
            inst_index: Instrument index (0-4)
            hit_index: Prediction index (1, 2, 3, ...) determining column position
            total_instruments: Total number of instruments (5)
        """
        canvas_width = self.visualizer.config["canvas_size"][0]
        canvas_height = self.visualizer.config["canvas_size"][1]

        # Position: same Y position as instrument hits (same lane)
        if self.visualizer.config["layout"] == "horizontal":
            row = inst_index
            num_rows = total_instruments
            y_spacing = canvas_height // (num_rows + 1)
            y_pos = (row + 1) * y_spacing

            # Horizontal position: to the right of hits, spaced by hit_index
            # Column 0 = hits, Column 1 = first prediction, Column 2 = second prediction, etc.
            hits_x = int(canvas_width * self.hits_column_x_ratio)
            prediction_start_x = int(
                canvas_width * 0.35
            )  # Start predictions at 35% of canvas
            column_spacing = int(canvas_width * self.prediction_column_spacing_ratio)
            x_pos = prediction_start_x + (hit_index - 1) * column_spacing
        else:
            col = inst_index
            num_cols = total_instruments
            x_spacing = canvas_width // (num_cols + 1)
            x_pos = (col + 1) * x_spacing

            # Vertical position: below hits, spaced by hit_index
            hits_y = canvas_height // 2
            prediction_start_y = int(canvas_height * 0.35)
            row_spacing = int(canvas_height * self.prediction_column_spacing_ratio)
            y_pos = prediction_start_y + (hit_index - 1) * row_spacing

        # Use same color and size as hits (no confidence modifications)
        color = self._get_instrument_color(inst_index)
        hit_size = self.visualizer.config["hit_size"]

        # Draw predicted hit circle (same style as hits)
        pred_circle = scene.Ellipse(
            center=(x_pos, y_pos),
            radius=(hit_size, hit_size),
            color=color,
            parent=self.visualizer.scene,
        )

        # No label for predictions

        # Store visual with expiry time for cleanup (same duration as hits)
        creation_time = time.time()
        expiry_time = creation_time + self.visualizer.config["hit_duration"]
        self.pending_visuals.append((pred_circle, None, creation_time, expiry_time))

    # (legacy layout helpers removed)

    # (removed)

    # (removed)

    def _get_instrument_color(self, inst_index):
        colors = self.visualizer.config.get("inst_colors", [])
        if inst_index < len(colors):
            return colors[inst_index]
        return (1.0, 1.0, 1.0, 1.0)

    def _cleanup_expired_visuals(self, current_time):
        """Remove expired visuals from scene"""
        remaining_visuals = []
        for visual1, visual2, creation_time, expiry_time in self.pending_visuals:
            if current_time >= expiry_time:
                # Cleanup expired visual
                try:
                    if visual1:
                        visual1.parent = None
                    if visual2:
                        visual2.parent = None
                except:
                    pass
            else:
                # Keep this visual
                remaining_visuals.append((visual1, visual2, creation_time, expiry_time))

        self.pending_visuals = remaining_visuals

    def _stats_worker(self):
        """Background thread that displays statistics"""
        while self.running:
            try:
                time.sleep(5.0)  # Update every 5 seconds
                self._print_stats()
            except Exception as e:
                print(f"Stats error: {e}")

    def _print_stats(self):
        """Print current hit statistics"""
        print("\n" + "="*50)
        print("HIT & PREDICTION STATISTICS")
        print("="*50)

        # Thread-safe access to stats
        with self.stats_lock:
            for gate_name, stats in self.hit_stats.items():
                if "predictions" in gate_name:
                    total = stats["total_predictions"]
                    recent = len(stats["recent_predictions"])
                    rate = recent / 10.0  # Last 10 seconds
                    label = "PREDICTIONS"
                else:
                    total = stats["total_hits"]
                    recent = len(stats["recent_hits"])
                    rate = recent / 10.0  # Last 10 seconds
                    label = gate_name.upper()

                print(
                    f"{label:20} | Total: {total:4d} | Recent: {recent:2d} | Rate: {rate:.1f}/sec"
                )

        print("="*50)

    def stop(self):
        """Stop the visualizer and cleanup"""
        if self.running:
            print("Stopping ZeroMQ Hit Visualizer...")
            self.running = False

            # Stop render timer
            if self.render_timer:
                self.render_timer.stop()

            # Cleanup all visuals
            self._cleanup_expired_visuals(float("inf"))  # Expire all

            # Stop visualizer
            self.visualizer.stop()

            # Cleanup ZeroMQ
            if self.socket:
                self.socket.close()
            if self.predictions_socket:
                self.predictions_socket.close()
            self.context.term()

            print("ZeroMQ Hit Visualizer stopped.")

    def cleanup(self):
        """Full cleanup"""
        self.stop()
        self.visualizer.cleanup()


def main():
    """Main function"""
    # Default: listen for predictions on port 5556
    # Optionally also listen for gates on 5555
    predictions_endpoint = None  # "tcp://*:5556"
    gates_endpoint = None  # Set to "tcp://*:5555" if you want to visualize gates too

    # Allow command line override
    if len(sys.argv) > 1:
        predictions_endpoint = sys.argv[1]
    if len(sys.argv) > 2:
        gates_endpoint = sys.argv[2]

    print("ZeroMQ Audio Hit & Prediction Visualizer")
    print("=" * 50)
    if gates_endpoint:
        print(f"Gates endpoint: {gates_endpoint}")
    print(f"Predictions endpoint: {predictions_endpoint}")
    print("Visualizing predicted hits from C++ Essentia pipeline")
    print("Press Ctrl+C to stop")
    print("=" * 50)

    # Create and start visualizer
    visualizer = ZeroMQHitVisualizer(
        endpoint=gates_endpoint, predictions_endpoint=predictions_endpoint
    )

    try:
        visualizer.start()

        # Run Vispy app (this will block until window is closed)
        app.run()

    except KeyboardInterrupt:
        print("\nShutting down...")
        visualizer.cleanup()


if __name__ == "__main__":
    main()
