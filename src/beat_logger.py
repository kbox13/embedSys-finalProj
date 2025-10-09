#!/usr/bin/env python3
"""
Beat Event Logger

A dedicated logger class for beat events that can be used by the streaming analyzer.
Provides asynchronous logging capabilities with configurable output formats.
"""

import os
import time
import logging
import threading
import queue
from datetime import datetime
from typing import Optional, List, Dict, Any


class BeatLogger:
    """
    Asynchronous beat event logger with configurable output formats
    """
    
    def __init__(self, log_dir: str = None, log_to_console: bool = True, 
                 log_to_file: bool = True, log_format: str = "detailed"):
        """
        Initialize the beat logger
        
        Args:
            log_dir: Directory for log files (None for default)
            log_to_console: Whether to log to console
            log_to_file: Whether to log to file
            log_format: Format type ("detailed", "simple", "csv")
        """
        self.log_dir = log_dir or os.path.join(os.path.dirname(__file__), '..', 'logs')
        self.log_to_console = log_to_console
        self.log_to_file = log_to_file
        self.log_format = log_format
        
        # Async action system
        self.action_queue: "queue.Queue[Dict[str, Any]]" = queue.Queue(maxsize=128)
        self.action_thread: Optional[threading.Thread] = None
        self.running = False
        
        # Logger setup
        self.logger = None
        self.log_file = None
        self.start_time = None
        
        # Statistics
        self.beat_count = 0
        
    def setup_logging(self):
        """Setup logging configuration"""
        if not self.log_to_file and not self.log_to_console:
            return
            
        # Create logs directory if it doesn't exist
        if self.log_to_file:
            os.makedirs(self.log_dir, exist_ok=True)
            
            # Create log filename with timestamp
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            self.log_file = os.path.join(self.log_dir, f"beat_events_{timestamp}.log")
        
        # Configure logging
        handlers = []
        if self.log_to_file:
            handlers.append(logging.FileHandler(self.log_file))
        if self.log_to_console:
            handlers.append(logging.StreamHandler())
            
        if handlers:
            logging.basicConfig(
                level=logging.INFO,
                format='%(asctime)s - %(levelname)s - %(message)s',
                handlers=handlers
            )
            
            self.logger = logging.getLogger(__name__)
            if self.log_to_file:
                self.logger.info(f"Beat event logging started. Log file: {self.log_file}")
            else:
                self.logger.info("Beat event logging started (console only)")
    
    def start(self):
        """Start the logger and action worker thread"""
        if self.running:
            return
            
        self.setup_logging()
        self.running = True
        self.start_time = time.time()
        
        # Start async action worker
        self.action_thread = threading.Thread(target=self._action_worker, daemon=True)
        self.action_thread.start()
        
        if self.logger:
            self.logger.info("Beat logger started")
    
    def stop(self):
        """Stop the logger and action worker thread"""
        self.running = False
        if self.action_thread and self.action_thread.is_alive():
            self.action_thread.join(timeout=2.0)
            
        if self.logger:
            self.logger.info(f"Beat logger stopped. Total beats logged: {self.beat_count}")
    
    def log_beat_event(self, beat_time: float, predicted_beats: List[float],  confidence_std: float = 0.0, **kwargs):
        """Log beat event with predictions (called asynchronously)"""
        try:
            self.beat_count += 1
            
            if self.log_format == "detailed":
                self._log_detailed_format(beat_time, predicted_beats, confidence_std)
            elif self.log_format == "simple":
                self._log_simple_format(beat_time, predicted_beats, confidence_std)
            elif self.log_format == "csv":
                self._log_csv_format(beat_time, predicted_beats, confidence_std, **kwargs)
            else:
                self._log_detailed_format(beat_time, predicted_beats, confidence_std)
                
        except Exception as e:
            if self.logger:
                self.logger.error(f"Error logging beat event: {e}")
    
    def _log_detailed_format(self, relative_beat_time: float, predicted_beats: List[float], 
                           confidence_std: float):
        """Log in detailed format matching console output"""
        if not self.logger:
            return
            
        # Format predicted beats to match the print output format
        ahead = ", ".join([f"{p:+.3f}s" for p in predicted_beats]) if predicted_beats else "n/a"
        
        self.logger.info(
            f"BEAT EVENT | Time: {relative_beat_time:.3f}s | "
            f"Next4: {ahead} (±{int(1e3*confidence_std)}ms)"
        )
    
    def _log_simple_format(self, relative_beat_time: float, predicted_beats: List[float], 
                         confidence_std: float):
        """Log in simple format"""
        if not self.logger:
            return
            
        self.logger.info(f"BEAT at {relative_beat_time:.3f}s")
    
    def _log_csv_format(self, relative_beat_time: float, predicted_beats: List[float], 
                       confidence_std: float, **kwargs):
        """Log in CSV format"""
        if not self.logger:
            return
            
        # CSV header (only log once)
        if self.beat_count == 1:
            self.logger.info("timestamp,confidence_ms,prediction_1,prediction_2,prediction_3,prediction_4")
        
        # CSV data row
        pred_str = ",".join([f"{p:.3f}" for p in predicted_beats[:4]]) if predicted_beats else ",,"
        self.logger.info(f"{relative_beat_time:.3f},{confidence_std*1000:.1f},{pred_str}")
    
    def queue_beat_action(self, action_type: str, **kwargs):
        """Queue an action to be processed asynchronously"""
        try:
            action_data = {
                'type': action_type,
                'timestamp': time.time(),
                **kwargs
            }
            self.action_queue.put_nowait(action_data)
        except queue.Full:
            if self.logger:
                self.logger.warning("Action queue full, dropping beat action")
    
    def _action_worker(self):
        """Background worker for processing async actions"""
        while self.running:
            try:
                action = self.action_queue.get(timeout=1.0)
                
                if action['type'] == 'beat_event':
                    self.log_beat_event(
                        beat_time=action['beat_time'],
                        predicted_beats=action['predicted_beats'],
                        confidence_std=action.get('confidence_std', 0.0),
                        **{k: v for k, v in action.items() 
                           if k not in ['type', 'timestamp', 'beat_time', 'predicted_beats', 'confidence_std']}
                    )
                elif action['type'] == 'downbeat_event':
                    self.log_beat_event(
                        beat_time=action['beat_time'],
                        predicted_beats=action['predicted_beats'],
                        confidence_std=action.get('confidence_std', 0.0),
                        **{k: v for k, v in action.items() 
                           if k not in ['type', 'timestamp', 'beat_time', 'predicted_beats', 'confidence_std']}
                    )
                elif action['type'] == 'custom_action':
                    # Placeholder for custom actions
                    if 'callback' in action:
                        action['callback'](action)
                
                self.action_queue.task_done()
                
            except queue.Empty:
                continue
            except Exception as e:
                if self.logger:
                    self.logger.error(f"Error in action worker: {e}")
    
    def get_stats(self) -> Dict[str, Any]:
        """Get logger statistics"""
        return {
            'beat_count': self.beat_count,
            'running': self.running,
            'queue_size': self.action_queue.qsize(),
            'log_file': self.log_file
        }
