import threading
import queue
import time
import numpy as np
from vispy import app, scene

# Set the backend explicitly to ensure compatibility
try:
    import vispy
    vispy.use('PyQt6')  # Try PyQt5 first
except ImportError:
    try:
        vispy.use('PySide2')  # Fallback to PySide2
    except ImportError:
        try:
            vispy.use('glfw')  # Fallback to glfw
        except ImportError:
            print("Warning: No suitable Vispy backend found. Window may not appear.")


class BeatVisualizer:
    """
    Real-time beat visualization using Vispy.
    
    This class provides windowed visualizations for beats and predicted beats
    using a background thread architecture similar to BeatLogger.
    """
    
    def __init__(self, config=None):
        """
        Initialize the BeatVisualizer.
        
        Args:
            config (dict, optional): Configuration dictionary. Defaults to None.
        """
        # Default configuration
        self.config = {
            'canvas_size': (800, 600),      # Width, Height
            'beat_color': (1.0, 0.0, 0.0, 1.0),      # Red RGBA
            'predicted_color': (0.0, 0.0, 1.0, 1.0), # Blue RGBA
            'downbeat_color': (0.0, 1.0, 0.0, 1.0),  # Green RGBA
            'beat_duration': 0.25,           # How long to show beat (seconds)
            'beat_size': 50,                # Size of beat circles
            'predicted_size': 40,           # Size of predicted beat circles
            'enabled': True,
            'show_grid': False,
            'background_color': (0.1, 0.1, 0.1, 1.0)  # Dark background
        }
        
        # Update config with provided values
        if config:
            self.config.update(config)
        
        # Threading components
        self.action_queue = queue.Queue()
        self.visualization_thread = None
        self.running = False
        
        # Vispy components
        self.canvas = None
        self.scene = None
        self.beat_visuals = []  # Track active beat visualizations
        self.predicted_visuals = []  # Track predicted beat visualizations
        self.scheduled_timers = []  # Track scheduled beat timers
        
        # Timing
        self.start_time = time.time()
    
    def start(self):
        """Start the visualization worker thread and create Vispy canvas"""
        if not self.running:
            print("Starting BeatVisualizer...")
            self.running = True
            
            # Create Vispy canvas in main thread (required for Qt on macOS)
            self._create_vispy_canvas()
            
            # Start visualization worker thread
            self.visualization_thread = threading.Thread(
                target=self._visualization_worker, 
                daemon=True
            )
            self.visualization_thread.start()
            
            print("BeatVisualizer started successfully!")
    
    def _create_vispy_canvas(self):
        """Create the main Vispy canvas"""
        print("Creating Vispy canvas...")
        
        # Create canvas
        self.canvas = scene.SceneCanvas(
            title='Beat Visualizer',
            size=self.config['canvas_size'],
            show=True,
            bgcolor=self.config['background_color']
        )
        
        print(f"Canvas created with size: {self.config['canvas_size']}")
        
        # Create scene
        self.scene = self.canvas.scene
        
        # Add grid if enabled
        if self.config['show_grid']:
            self._add_grid()
        
        # Add title
        title = scene.Text('Beat Detection Visualization', 
                          parent=self.scene, 
                          pos=(self.config['canvas_size'][0]//2, 50),
                          font_size=20,
                          color='white')
        
        print("Vispy canvas ready!")
        # Note: app.run() is not called here to avoid blocking the main thread
    
    def _add_grid(self):
        """Add a grid to the visualization"""
        # Create grid lines
        grid_size = 100
        for i in range(-5, 6):
            # Vertical lines
            line = scene.Line(
                pos=np.array([[i * grid_size, -500], [i * grid_size, 500]]),
                color=(0.3, 0.3, 0.3, 1.0),
                parent=self.scene
            )
            
            # Horizontal lines
            line = scene.Line(
                pos=np.array([[-500, i * grid_size], [500, i * grid_size]]),
                color=(0.3, 0.3, 0.3, 1.0),
                parent=self.scene
            )
    
    def queue_visualization_action(self, action_type, beat_time, wall_beat_time, predicted_beats=None, confidence_std=None):
        """
        Queue a visualization action (called from main thread)
        
        Args:
            action_type (str): Type of action ('beat_event' or 'downbeat_event')
            beat_time (float): Time of the beat
            wall_beat_time (float): Wall clock time of the beat
            predicted_beats (list, optional): List of predicted beat times
            confidence_std (float, optional): Confidence standard deviation
        """
        if not self.config['enabled'] or not self.canvas:
            return
            
        action = {
            'action_type': action_type,
            'beat_time': beat_time,
            'wall_beat_time': wall_beat_time,
            'predicted_beats': predicted_beats,
            'confidence_std': confidence_std,
            'timestamp': time.time()
        }
        
        try:
            self.action_queue.put_nowait(action)
        except queue.Full:
            pass  # Skip if queue is full
    
    def _visualization_worker(self):
        """Background thread that processes visualization actions"""
        while self.running:
            try:
                # Get action from queue (blocking with timeout)
                action = self.action_queue.get(timeout=0.1)
                
                if action is None:  # Stop signal
                    break
                    
                # Process the visualization action
                self._process_visualization_action(action)
                
                # Mark task as done
                self.action_queue.task_done()
                
            except queue.Empty:
                continue
            except Exception as e:
                print(f"Visualization error: {e}")
    
    def _process_visualization_action(self, action):
        """Process a single visualization action"""
        action_type = action['action_type']
        beat_time = action['beat_time']
        predicted_beats = action.get('predicted_beats', [])
        confidence_std = action.get('confidence_std', 0)
        
        if action_type == 'beat_event':
            self._visualize_beat(beat_time, predicted_beats, confidence_std)
        elif action_type == 'downbeat_event':
            self._visualize_downbeat(beat_time, predicted_beats, confidence_std)
    
    def _visualize_beat(self, beat_time, predicted_beats, confidence_std):
        """Create Vispy visualization for beat"""
        # Position for beats is upper half of screen beat on left predicted on right
        x_pos = self.config['canvas_size'][0] // 4
        y_pos = self.config['canvas_size'][1] // 4
        
        # Create beat circle
        beat_circle = scene.Ellipse(
            center=(x_pos, y_pos),
            radius=(self.config['beat_size'], self.config['beat_size']),
            color=self.config['beat_color'],
            parent=self.scene
        )
        
        # # Add beat label
        # beat_label = scene.Text(
        #     f'BEAT\n{beat_time:.2f}s',
        #     pos=(x_pos, y_pos + 80),
        #     font_size=12,
        #     color='white',
        #     parent=self.scene
        # )
        
        # Store for cleanup
        self.beat_visuals.append((beat_circle, None, time.time()))
        
        # Visualize predicted beats
        if predicted_beats:
            self._visualize_predicted_beats(predicted_beats, beat_time)
        
        # Add confidence indicator
        # self._add_confidence_indicator(confidence_std, x_pos, y_pos - 100)
        
        # Schedule cleanup
        self._schedule_cleanup(beat_circle, None, self.config['beat_duration'])
    
    def _visualize_downbeat(self, beat_time, predicted_beats, confidence_std):
        """Create Vispy visualization for downbeat"""
        # Calculate position
        x_pos = self.config['canvas_size'][0] // 4
        y_pos = 3*self.config['canvas_size'][1] // 4
        
        # Create downbeat circle (larger and different color)
        downbeat_circle = scene.Ellipse(
            center=(x_pos, y_pos),
            radius=(self.config['beat_size'] * 1.5, self.config['beat_size'] * 1.5),
            color=self.config['downbeat_color'],
            parent=self.scene
        )
        
        # Add downbeat label
        # downbeat_label = scene.Text(
        #     f'DOWNBEAT\n{beat_time:.2f}s',
        #     pos=(x_pos, y_pos + 100),
        #     font_size=14,
        #     color='white',
        #     parent=self.scene
        # )
        
        # Store for cleanup
        self.beat_visuals.append((downbeat_circle, None, time.time()))
        
        # Visualize predicted beats
        if predicted_beats:
            self._visualize_predicted_beats(predicted_beats, beat_time)
        
        # Add confidence indicator
        # self._add_confidence_indicator(confidence_std, x_pos, y_pos - 120)
        
        # Schedule cleanup
        self._schedule_cleanup(downbeat_circle, None,self.config['beat_duration'])
    
    def _visualize_predicted_beats(self, predicted_beats, beat_time):
        """Schedule predicted beats to appear at their predicted times"""
        current_time = time.time()
        
        for i, pred_time in enumerate(predicted_beats[:2]):  # Show first 2
            # Calculate delay until the predicted beat should appear
            delay = pred_time - beat_time
            
            # Only schedule if the predicted beat is in the future
            if delay > 0:
                # Schedule the predicted beat to appear at its predicted time
                timer = threading.Timer(delay, self._show_predicted_beat, 
                                      args=(pred_time, i, len(self.scheduled_timers)))
                timer.start()
                self.scheduled_timers.append(timer)
    
    def _show_predicted_beat(self, pred_time, index, timer_index):
        """Show a predicted beat at its scheduled time"""
        if not self.running or not self.canvas:
            return
            
        # Position predicted beats on the right side of the screen
        # Position predicted beats based on their index, distributed in the right half of the screen
        min_x = 5*self.config['canvas_size'][0] // 8
        max_x = self.config['canvas_size'][0] - self.config['predicted_size'] * 2
        num = 2  # maximum predicted beats shown
        if num > 1:
            x_step = (max_x - min_x) // (num - 1)
            x_pos = min_x + x_step * index
        else:
            x_pos = (min_x + max_x) // 2
        y_pos = self.config['canvas_size'][1] // 2
        
        # Create predicted beat circle
        pred_circle = scene.Ellipse(
            center=(x_pos, y_pos),
            radius=(self.config['predicted_size'], self.config['predicted_size']),
            color=self.config['predicted_color'],
            parent=self.scene
        )
        
        # Add predicted beat label
        # pred_label = scene.Text(
        #     f'Pred {pred_time:.2f}s',
        #     pos=(x_pos, y_pos - 20),
        #     font_size=10,
        #     color='lightblue',
        #     parent=self.scene
        # )
        
        # Store for cleanup
        self.predicted_visuals.append((pred_circle, None, time.time()))
        
        # Schedule cleanup
        self._schedule_cleanup(pred_circle, None, self.config['beat_duration'])
        
        # Remove completed timer from tracking list
        try:
            if timer_index < len(self.scheduled_timers):
                self.scheduled_timers.pop(timer_index)
        except (IndexError, ValueError):
            pass  # Timer already removed or index out of bounds
    
    def _add_confidence_indicator(self, confidence_std, x_pos, y_pos):
        """Add confidence indicator as a bar"""
        # Create confidence bar
        bar_width = confidence_std * 200  # Scale confidence to bar width
        bar_height = 20
        
        confidence_bar = scene.Rectangle(
            center=(x_pos, y_pos),
            size=(bar_width, bar_height),
            color=(0.8, 0.8, 0.0, 1.0),  # Yellow
            parent=self.scene
        )
        
        # Add confidence label
        conf_label = scene.Text(
            f'Conf: {confidence_std:.3f}',
            pos=(x_pos, y_pos - 30),
            font_size=10,
            color='yellow',
            parent=self.scene
        )
        
        # Store for cleanup
        self.predicted_visuals.append((confidence_bar, conf_label, time.time()))
        
        # Schedule cleanup
        self._schedule_cleanup(confidence_bar, conf_label, self.config['beat_duration'])
    
    def _schedule_cleanup(self, visual1, visual2, duration):
        """Schedule cleanup of visuals after duration"""
        def cleanup():
            try:
                visual1.parent = None
                visual2.parent = None
            except:
                pass
        
        # Schedule cleanup
        threading.Timer(duration, cleanup).start()
    
    def stop(self):
        """Stop the visualization worker thread"""
        if self.running:
            self.running = False
            self.action_queue.put(None)  # Signal to stop
            if self.visualization_thread:
                self.visualization_thread.join(timeout=1.0)
            # Note: Vispy thread will be cleaned up when the process exits
    
    def cleanup(self):
        """Clean up all visuals and stop thread"""
        self.stop()
        
        # Cancel all scheduled timers
        for timer in self.scheduled_timers:
            try:
                timer.cancel()
            except:
                pass
        self.scheduled_timers.clear()
        
        # Clear all visuals
        for visual_tuple in self.beat_visuals + self.predicted_visuals:
            try:
                visual_tuple[0].parent = None
                visual_tuple[1].parent = None
            except:
                pass
        
        self.beat_visuals.clear()
        self.predicted_visuals.clear()
    
    def update_config(self, new_config):
        """Update configuration"""
        self.config.update(new_config)
    
    def is_enabled(self):
        """Check if visualizer is enabled"""
        return self.config['enabled']
    
    def set_enabled(self, enabled):
        """Enable or disable visualizer"""
        self.config['enabled'] = enabled
