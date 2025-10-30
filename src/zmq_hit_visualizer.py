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

    def __init__(self, endpoint="tcp://*:5555", config=None):
        """
        Initialize the ZeroMQ Hit Visualizer.
        
        Args:
            endpoint (str): ZeroMQ endpoint to bind to
            config (dict, optional): Visualization configuration
        """
        self.endpoint = endpoint
        self.context = zmq.Context()
        self.socket = None
        self.running = False

        # Default visualization config for gate hits
        default_config = {
            "canvas_size": (1000, 800),
            "bass_gate_color": (1.0, 0.0, 0.0, 1.0),  # Red
            "mid_gate_color": (0.0, 1.0, 0.0, 1.0),  # Green
            "high_gate_color": (0.0, 0.0, 1.0, 1.0),  # Blue
            "hit_duration": 0.1,  # How long to show hit (seconds)
            "hit_size": 60,  # Size of hit circles
            "enabled": True,
            "show_grid": False,
            "background_color": (0.05, 0.05, 0.05, 1.0),  # Very dark background
            "show_labels": False,
            "layout": "horizontal",  # 'horizontal' or 'vertical'
        }

        # Update with provided config
        if config:
            default_config.update(config)

        # Initialize the beat visualizer with our config
        self.visualizer = BeatVisualizer(default_config)

        # Track hit statistics
        self.hit_stats = {
            'bass_gate': {'total_hits': 0, 'recent_hits': []},
            'mid_gate': {'total_hits': 0, 'recent_hits': []},
            'high_gate': {'total_hits': 0, 'recent_hits': []}
        }

        # Threading
        self.zmq_thread = None
        self.stats_thread = None

    def start(self):
        """Start the ZeroMQ subscriber and visualization"""
        if not self.running:
            print("Starting ZeroMQ Hit Visualizer...")
            self.running = True

            # Start the visualizer
            self.visualizer.start()

            # Start ZeroMQ subscriber thread
            self.zmq_thread = threading.Thread(
                target=self._zmq_worker, 
                daemon=True
            )
            self.zmq_thread.start()

            # Start stats display thread
            self.stats_thread = threading.Thread(
                target=self._stats_worker,
                daemon=True
            )
            self.stats_thread.start()

            print("ZeroMQ Hit Visualizer started successfully!")
            print("Listening for gate hits from C++ Essentia pipeline...")

    def _setup_zmq_socket(self):
        """Setup ZeroMQ PULL socket"""
        self.socket = self.context.socket(zmq.PULL)
        self.socket.bind(self.endpoint)
        self.socket.setsockopt(zmq.RCVTIMEO, 1000)  # 1 second timeout
        print(f"ZeroMQ socket bound to: {self.endpoint}")

    def _zmq_worker(self):
        """Background thread that receives ZeroMQ messages"""
        self._setup_zmq_socket()

        while self.running:
            try:
                # Receive message
                data = self.socket.recv_string()

                # Parse JSON data
                feature_data = json.loads(data)

                # Process the gate hit
                self._process_gate_hit(feature_data)

            except zmq.Again:
                # Timeout - continue loop
                continue
            except json.JSONDecodeError as e:
                print(f"JSON decode error: {e}")
            except Exception as e:
                print(f"ZeroMQ error: {e}")
                break

    def _process_gate_hit(self, feature_data):
        """Process a gate hit from the ZeroMQ stream"""
        feature_name = feature_data['feature_name']
        values = feature_data['values']
        timestamp = feature_data['timestamp']
        frame_count = feature_data['frame_count']

        # Only process gate features
        if 'gate' not in feature_name:
            return

        # Check for hits (values >= 0.5)
        hits = [i for i, v in enumerate(values) if v >= 0.5]

        if hits:
            # Update statistics
            self.hit_stats[feature_name]['total_hits'] += len(hits)
            current_time = time.time()
            self.hit_stats[feature_name]['recent_hits'].append(current_time)

            # Keep only recent hits (last 10 seconds)
            cutoff_time = current_time - 10.0
            self.hit_stats[feature_name]['recent_hits'] = [
                t for t in self.hit_stats[feature_name]['recent_hits'] 
                if t > cutoff_time
            ]

            # Visualize each hit
            for hit_index in hits:
                self._visualize_gate_hit(feature_name, hit_index, len(values))

    def _visualize_gate_hit(self, feature_name, hit_index, total_frames):
        """Create visualization for a gate hit"""
        # Calculate position based on feature type and layout
        if self.visualizer.config['layout'] == 'horizontal':
            x_pos, y_pos = self._get_horizontal_position(feature_name, hit_index, total_frames)
        else:
            x_pos, y_pos = self._get_vertical_position(feature_name, hit_index, total_frames)

        # Get color for this gate type
        color = self._get_gate_color(feature_name)

        # Create hit circle using proper Vispy scene API
        hit_circle = scene.Ellipse(
            center=(x_pos, y_pos),
            radius=(self.visualizer.config['hit_size'], self.visualizer.config['hit_size']),
            color=color,
            parent=self.visualizer.scene
        )

        # Add label if enabled
        if self.visualizer.config['show_labels']:
            label_text = f"{feature_name.replace('_gate', '').upper()}\n{hit_index}"
            hit_label = scene.Text(
                label_text,
                pos=(x_pos, y_pos + self.visualizer.config['hit_size'] + 10),
                font_size=12,
                color='white',
                parent=self.visualizer.scene
            )
        else:
            hit_label = None

        # Store for cleanup
        self.visualizer.beat_visuals.append((hit_circle, hit_label, time.time()))

        # Schedule cleanup
        self._schedule_cleanup(hit_circle, hit_label, self.visualizer.config['hit_duration'])

    def _get_horizontal_position(self, feature_name, hit_index, total_frames):
        """Calculate position for horizontal layout"""
        canvas_width = self.visualizer.config['canvas_size'][0]
        canvas_height = self.visualizer.config['canvas_size'][1]

        # Map feature names to rows
        feature_rows = {
            'bass_gate': 0,
            'mid_gate': 1, 
            'high_gate': 2
        }

        row = feature_rows.get(feature_name, 0)
        num_rows = 3

        # Calculate Y position (distribute across height)
        y_spacing = canvas_height // (num_rows + 1)
        y_pos = (row + 1) * y_spacing

        # Calculate X position based on hit index (distribute across width)
        x_spacing = canvas_width // (total_frames + 1)
        x_pos = (hit_index + 1) * x_spacing

        return x_pos, y_pos

    def _get_vertical_position(self, feature_name, hit_index, total_frames):
        """Calculate position for vertical layout"""
        canvas_width = self.visualizer.config['canvas_size'][0]
        canvas_height = self.visualizer.config['canvas_size'][1]

        # Map feature names to columns
        feature_cols = {
            'bass_gate': 0,
            'mid_gate': 1,
            'high_gate': 2
        }

        col = feature_cols.get(feature_name, 0)
        num_cols = 3

        # Calculate X position (distribute across width)
        x_spacing = canvas_width // (num_cols + 1)
        x_pos = (col + 1) * x_spacing

        # Calculate Y position based on hit index (distribute across height)
        y_spacing = canvas_height // (total_frames + 1)
        y_pos = (hit_index + 1) * y_spacing

        return x_pos, y_pos

    def _get_gate_color(self, feature_name):
        """Get color for gate type"""
        color_map = {
            'bass_gate': self.visualizer.config['bass_gate_color'],
            'mid_gate': self.visualizer.config['mid_gate_color'],
            'high_gate': self.visualizer.config['high_gate_color']
        }
        return color_map.get(feature_name, (1.0, 1.0, 1.0, 1.0))

    def _schedule_cleanup(self, visual1, visual2, duration):
        """Schedule cleanup of visuals after duration"""
        def cleanup():
            try:
                if visual1:
                    visual1.parent = None
                if visual2:
                    visual2.parent = None
            except:
                pass

        # Schedule cleanup
        threading.Timer(duration, cleanup).start()

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
        print("GATE HIT STATISTICS")
        print("="*50)

        for gate_name, stats in self.hit_stats.items():
            total_hits = stats['total_hits']
            recent_hits = len(stats['recent_hits'])
            hits_per_sec = recent_hits / 10.0  # Last 10 seconds

            print(f"{gate_name.upper():12} | Total: {total_hits:4d} | Recent: {recent_hits:2d} | Rate: {hits_per_sec:.1f}/sec")

        print("="*50)

    def stop(self):
        """Stop the visualizer and cleanup"""
        if self.running:
            print("Stopping ZeroMQ Hit Visualizer...")
            self.running = False

            # Stop visualizer
            self.visualizer.stop()

            # Cleanup ZeroMQ
            if self.socket:
                self.socket.close()
            self.context.term()

            print("ZeroMQ Hit Visualizer stopped.")

    def cleanup(self):
        """Full cleanup"""
        self.stop()
        self.visualizer.cleanup()


def main():
    """Main function"""
    # Default endpoint
    default_endpoint = "tcp://*:5555"
    
    # Use command line argument or default
    endpoint = sys.argv[1] if len(sys.argv) > 1 else default_endpoint
    
    print("ZeroMQ Audio Hit Visualizer")
    print("=" * 50)
    print(f"Endpoint: {endpoint}")
    print("Visualizing gate hits from C++ Essentia pipeline")
    print("Press Ctrl+C to stop")
    print("=" * 50)
    
    # Create and start visualizer
    visualizer = ZeroMQHitVisualizer(endpoint)
    
    try:
        visualizer.start()
        
        # Run Vispy app (this will block until window is closed)
        app.run()
            
    except KeyboardInterrupt:
        print("\nShutting down...")
        visualizer.cleanup()


if __name__ == "__main__":
    main()
