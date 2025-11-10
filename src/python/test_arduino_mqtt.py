#!/usr/bin/env python3
"""
Test script for Arduino Nano ESP32 Event Scheduler via MQTT

This script sends test events to the Arduino device to verify:
- MQTT connectivity
- Event scheduling
- Time synchronization
- LED control

Usage:
    python test_arduino_mqtt.py [--broker BROKER] [--port PORT]
"""

import paho.mqtt.client as mqtt
import json
import time
import argparse
import sys
from typing import Dict, List, Optional, Tuple

try:
    import ntplib
    NTP_AVAILABLE = True
except ImportError:
    NTP_AVAILABLE = False
    print("WARNING: ntplib not installed. Install with: pip install ntplib")
    print("Time synchronization will use local system time.")


class TimeSyncClient:
    """Time synchronization client using NTP"""
    
    def __init__(self, ntp_server: str = "pool.ntp.org"):
        self.ntp_server = ntp_server
        self.ntp_client = None
        self.last_sync = None
        self.offset = 0
        
        if NTP_AVAILABLE:
            self.ntp_client = ntplib.NTPClient()
            self.sync_time()
    
    def sync_time(self) -> bool:
        """Sync with NTP server and calculate offset"""
        if not NTP_AVAILABLE or self.ntp_client is None:
            return False
        
        try:
            response = self.ntp_client.request(self.ntp_server, version=3)
            ntp_time = response.tx_time
            local_time = time.time()
            self.offset = ntp_time - local_time
            self.last_sync = time.time()
            print(f"Time synced with NTP: offset = {self.offset:.3f}s")
            return True
        except Exception as e:
            print(f"NTP sync failed: {e}")
            return False
    
    def get_synced_time(self) -> float:
        """Get current time synchronized with NTP"""
        if self.last_sync is None or (time.time() - self.last_sync) > 3600:
            # Re-sync if never synced or more than 1 hour old
            self.sync_time()
        
        return time.time() + self.offset
    
    def get_unix_timestamp_micros(self) -> Tuple[int, int]:
        """
        Get Unix timestamp with microsecond precision
        
        Returns:
            (seconds, microseconds) tuple
        """
        synced_time = self.get_synced_time()
        seconds = int(synced_time)
        microseconds = int((synced_time - seconds) * 1000000)
        return seconds, microseconds


class ArduinoEventPublisher:
    """Publisher for Arduino event scheduler"""
    
    def __init__(self, broker_host: str = "172.20.10.5", broker_port: int = 1883):
        self.broker_host = broker_host
        self.broker_port = broker_port
        self.client = None
        self.connected = False
        self.time_sync = TimeSyncClient()
        
    def connect(self) -> bool:
        """Connect to MQTT broker"""
        try:
            self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
            self.client.on_connect = self._on_connect
            self.client.on_disconnect = self._on_disconnect
            
            print(f"Connecting to MQTT broker at {self.broker_host}:{self.broker_port}...")
            self.client.connect(self.broker_host, self.broker_port, 60)
            self.client.loop_start()
            
            # Wait for connection
            timeout = 5
            start = time.time()
            while not self.connected and (time.time() - start) < timeout:
                time.sleep(0.1)
            
            if self.connected:
                print("Connected to MQTT broker")
                return True
            else:
                print("Failed to connect to MQTT broker")
                return False
                
        except Exception as e:
            print(f"Error connecting to MQTT broker: {e}")
            return False
    
    def disconnect(self):
        """Disconnect from MQTT broker"""
        if self.client:
            self.client.loop_stop()
            self.client.disconnect()
            print("Disconnected from MQTT broker")
    
    def _on_connect(self, client, userdata, flags, reason_code, properties=None):
        """MQTT connection callback"""
        if reason_code == 0:
            self.connected = True
            print("MQTT connection successful")
        else:
            print(f"MQTT connection failed with code {reason_code}")
    
    def _on_disconnect(self, client, userdata, reason_code, properties=None, *args, **kwargs):
        """MQTT disconnection callback"""
        self.connected = False
        if reason_code != 0:
            print(f"Unexpected MQTT disconnection (rc={reason_code})")
    
    def publish_event(self, unix_time: int, microseconds: int, 
                     r: int, g: int, b: int, event_id: Optional[int] = None) -> bool:
        """
        Publish single LED event
        
        Args:
            unix_time: Unix timestamp (seconds)
            microseconds: Microseconds component
            r, g, b: RGB values (0 or 1)
            event_id: Optional event ID
        
        Returns:
            True if published successfully
        """
        if not self.connected:
            print("ERROR: Not connected to MQTT broker")
            return False
        
        if event_id is None:
            event_id = int(time.time() * 1000)  # Use timestamp as ID
        
        payload = {
            "unix_time": int(unix_time),
            "microseconds": int(microseconds),
            "r": int(1 if r else 0),
            "g": int(1 if g else 0),
            "b": int(1 if b else 0),
            "event_id": int(event_id)
        }
        
        topic = "beat/events/schedule"
        json_payload = json.dumps(payload)
        result = self.client.publish(topic, json_payload, qos=1)
        
        if result.rc == mqtt.MQTT_ERR_SUCCESS:
            print(f"Published event: ID={event_id}, RGB=({r},{g},{b}), "
                  f"time={unix_time}.{microseconds:06d}")
            print(f"  JSON payload: {json_payload}")
            return True
        else:
            print(f"Failed to publish event: {result.rc}")
            return False
    
    def publish_batch_events(self, events: List[Dict]) -> bool:
        """
        Publish multiple events in batch
        
        Args:
            events: List of event dicts with keys: unix_time, microseconds, r, g, b, event_id
        
        Returns:
            True if published successfully
        """
        if not self.connected:
            print("ERROR: Not connected to MQTT broker")
            return False
        
        payload = {
            "events": events,
            "batch_id": int(time.time() * 1000)
        }
        
        topic = "beat/events/schedule"
        result = self.client.publish(topic, json.dumps(payload), qos=1)
        
        if result.rc == mqtt.MQTT_ERR_SUCCESS:
            print(f"Published batch: {len(events)} events")
            return True
        else:
            print(f"Failed to publish batch: {result.rc}")
            return False
    
    def publish_time_sync(self) -> bool:
        """Publish time synchronization message"""
        if not self.connected:
            print("ERROR: Not connected to MQTT broker")
            return False
        
        seconds, micros = self.time_sync.get_unix_timestamp_micros()
        
        payload = {
            "unix_time": seconds,
            "microseconds": micros,
            "sync_source": "host"
        }
        
        topic = "beat/time/sync"
        result = self.client.publish(topic, json.dumps(payload), qos=1)
        
        if result.rc == mqtt.MQTT_ERR_SUCCESS:
            print(f"Published time sync: {seconds}.{micros:06d}")
            return True
        else:
            print(f"Failed to publish time sync: {result.rc}")
            return False
    
    def schedule_event_relative(self, delay_seconds: float, r: int, g: int, b: int,
                               event_id: Optional[int] = None) -> bool:
        """
        Schedule event relative to current time
        
        Args:
            delay_seconds: Delay in seconds from now
            r, g, b: RGB values (0 or 1)
            event_id: Optional event ID
        
        Returns:
            True if scheduled successfully
        """
        seconds, micros = self.time_sync.get_unix_timestamp_micros()
        
        # Calculate future time
        future_total = seconds + micros / 1000000.0 + delay_seconds
        future_seconds = int(future_total)
        future_micros = int((future_total - future_seconds) * 1000000)
        
        return self.publish_event(future_seconds, future_micros, r, g, b, event_id)


def test_basic_events(publisher: ArduinoEventPublisher):
    """Test basic single event scheduling"""
    print("\n=== Test 1: Basic Single Event ===")
    
    # Schedule event 2 seconds in future
    publisher.schedule_event_relative(2.0, r=1, g=0, b=0, event_id=1001)
    time.sleep(0.5)
    
    # Schedule another event 4 seconds in future
    publisher.schedule_event_relative(4.0, r=0, g=1, b=0, event_id=1002)
    time.sleep(0.5)
    
    # Schedule event 6 seconds in future
    publisher.schedule_event_relative(6.0, r=0, g=0, b=1, event_id=1003)


def test_batch_events(publisher: ArduinoEventPublisher):
    """Test batch event scheduling"""
    print("\n=== Test 2: Batch Events ===")
    
    seconds, micros = publisher.time_sync.get_unix_timestamp_micros()
    
    events = []
    for i in range(5):
        event_time = seconds + 8 + i * 0.5  # 8s, 8.5s, 9s, 9.5s, 10s
        event_seconds = int(event_time)
        event_micros = int((event_time - event_seconds) * 1000000)
        
        # Alternate colors
        r = 1 if i % 3 == 0 else 0
        g = 1 if i % 3 == 1 else 0
        b = 1 if i % 3 == 2 else 0
        
        events.append({
            "unix_time": event_seconds,
            "microseconds": event_micros,
            "r": r,
            "g": g,
            "b": b,
            "event_id": 2000 + i
        })
    
    publisher.publish_batch_events(events)


def test_time_sync(publisher: ArduinoEventPublisher):
    """Test time synchronization"""
    print("\n=== Test 3: Time Synchronization ===")
    publisher.publish_time_sync()


def test_rapid_events(publisher: ArduinoEventPublisher):
    """Test rapid event scheduling (beat-like pattern)"""
    print("\n=== Test 4: Rapid Events (Beat Pattern) ===")
    
    base_time = publisher.time_sync.get_synced_time() + 12.0  # Start 12 seconds from now
    bpm = 120  # Beats per minute
    beat_interval = 60.0 / bpm  # Seconds between beats
    
    for i in range(8):
        event_time = base_time + i * beat_interval
        event_seconds = int(event_time)
        event_micros = int((event_time - event_seconds) * 1000000)
        
        # Alternate colors for visual effect
        r = 1 if i % 2 == 0 else 0
        g = 0 if i % 2 == 0 else 1
        b = 0
        
        publisher.publish_event(
            event_seconds, event_micros,
            r, g, b,
            event_id=3000 + i
        )
        time.sleep(0.1)  # Small delay between publishes


def test_color_combinations(publisher: ArduinoEventPublisher):
    """Test different RGB color combinations"""
    print("\n=== Test 5: Color Combinations ===")
    
    colors = [
        (1, 0, 0, "Red"),
        (0, 1, 0, "Green"),
        (0, 0, 1, "Blue"),
        (1, 1, 0, "Yellow"),
        (1, 0, 1, "Magenta"),
        (0, 1, 1, "Cyan"),
        (1, 1, 1, "White"),
    ]
    
    base_time = publisher.time_sync.get_synced_time() + 20.0
    
    for i, (r, g, b, name) in enumerate(colors):
        event_time = base_time + i * 0.5
        event_seconds = int(event_time)
        event_micros = int((event_time - event_seconds) * 1000000)
        
        print(f"  Scheduling {name} at +{i * 0.5:.1f}s")
        publisher.publish_event(
            event_seconds, event_micros,
            r, g, b,
            event_id=4000 + i
        )
        time.sleep(0.1)


def main():
    parser = argparse.ArgumentParser(description="Test Arduino MQTT Event Scheduler")
    parser.add_argument("--broker", default="172.20.10.5",
                       help="MQTT broker hostname (default: localhost)")
    parser.add_argument("--port", type=int, default=1883,
                       help="MQTT broker port (default: 1883)")
    parser.add_argument("--test", choices=["all", "basic", "batch", "sync", "rapid", "colors"],
                       default="all", help="Which test to run (default: all)")
    
    args = parser.parse_args()
    
    print("=" * 60)
    print("Arduino Event Scheduler Test Script")
    print("=" * 60)
    
    # Create publisher
    publisher = ArduinoEventPublisher(args.broker, args.port)
    
    # Connect to broker
    if not publisher.connect():
        print("ERROR: Failed to connect to MQTT broker")
        print("Make sure Mosquitto is running:")
        print("  mosquitto -c /usr/local/etc/mosquitto/mosquitto.conf")
        sys.exit(1)
    
    try:
        # Send time sync first
        print("\nSending time synchronization...")
        publisher.publish_time_sync()
        time.sleep(1)
        
        # Run selected tests
        if args.test == "all":
            test_basic_events(publisher)
            time.sleep(1)
            test_batch_events(publisher)
            time.sleep(1)
            test_time_sync(publisher)
            time.sleep(1)
            test_rapid_events(publisher)
            time.sleep(1)
            test_color_combinations(publisher)
        elif args.test == "basic":
            test_basic_events(publisher)
        elif args.test == "batch":
            test_batch_events(publisher)
        elif args.test == "sync":
            test_time_sync(publisher)
        elif args.test == "rapid":
            test_rapid_events(publisher)
        elif args.test == "colors":
            test_color_combinations(publisher)
        
        print("\n" + "=" * 60)
        print("Tests completed!")
        print("Watch the Arduino Serial Monitor to see event execution")
        print("=" * 60)
        
        # Keep connection alive for a bit
        time.sleep(2)
        
    except KeyboardInterrupt:
        print("\nInterrupted by user")
    finally:
        publisher.disconnect()


if __name__ == "__main__":
    main()

