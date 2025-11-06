#!/usr/bin/env python3
"""
ZeroMQ Audio Feature Subscriber

This script subscribes to audio features published by the C++ Essentia streaming pipeline.
It can connect to multiple publishers and display the received data in real-time.

Usage:
    python zmq_audio_subscriber.py [endpoints...]
    
Example:
    python zmq_audio_subscriber.py tcp://localhost:5555 tcp://localhost:5556
"""

import zmq
import json
import time
import sys
import threading
from datetime import datetime

class AudioFeatureSubscriber:
    def __init__(self, endpoint):
        self.endpoint = endpoint
        self.context = zmq.Context()
        self.socket = None
        self.running = True
        
    def setup_socket(self):
        """Setup ZeroMQ PULL socket to receive from all publishers"""
        self.socket = self.context.socket(zmq.PULL)
        self.socket.bind(self.endpoint)  # Bind to receive from all PUSH sockets
        self.socket.setsockopt(zmq.RCVTIMEO, 1000)  # 1 second timeout
        print(f"Bound to: {self.endpoint}")
    
    def receive_data(self):
        """Receive and process data from the PULL socket"""
        while self.running:
            try:
                # Receive single message (PUSH-PULL pattern)
                data = self.socket.recv_string()
                
                # Parse JSON data
                feature_data = json.loads(data)
                
                # Display the data
                timestamp = datetime.fromtimestamp(feature_data['timestamp'] / 1000)
                feature_name = feature_data['feature_name']
                values = feature_data['values']
                
                print(f"\n[{timestamp}] {feature_name.upper()}")
                print(f"Frame: {feature_data['frame_count']}")
                print(f"Values: {values[:5]}{'...' if len(values) > 5 else ''}")
                print(f"Count: {len(values)}")
                
                # Show some statistics for gate signals
                if 'gate' in feature_name:
                    hits = sum(1 for v in values if v > 0.5)
                    print(f"Hits: {hits}/{len(values)} ({hits/len(values)*100:.1f}%)")
                
            except zmq.Again:
                # Timeout - continue loop
                continue
            except json.JSONDecodeError as e:
                print(f"JSON decode error: {e}")
            except Exception as e:
                print(f"Error receiving data: {e}")
                break
    
    def start(self):
        """Start receiving data from the PULL socket"""
        self.setup_socket()
        
        print(f"\nListening for audio features from all publishers...")
        print("Press Ctrl+C to stop")
        
        try:
            self.receive_data()
        except KeyboardInterrupt:
            print("\nShutting down...")
            self.running = False
        
        # Cleanup
        if self.socket:
            self.socket.close()
        self.context.term()

def main():
    # Default endpoint - all features published to same port
    default_endpoint = "tcp://*:5555"
    
    # Use command line argument or default
    endpoint = sys.argv[1] if len(sys.argv) > 1 else default_endpoint
    
    print("ZeroMQ Audio Feature Subscriber")
    print("=" * 40)
    print(f"Endpoint: {endpoint}")
    print("All audio features will be received from this single port")
    
    subscriber = AudioFeatureSubscriber(endpoint)
    subscriber.start()

if __name__ == "__main__":
    main()
