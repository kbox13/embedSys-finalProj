import numpy as np


class TrajectoryPlanner:
    def __init__(self):
        self.fps = 60
    
    def compute_trajectory(self, x0, x1, to, tf):
        # returns an array of shape (n, 2) where n is the number of points in the trajectory
        # one val is the position and the other is the absolute time of that position based on to
        T = tf - to
        trajectory_func = self.min_jerk_trajectory(x0, x1, T)
        t = np.linspace(0, T, self.fps)
        
        positions = np.array([trajectory_func(t_i)[0] for t_i in t[1:]])
        times = np.array([t_i for t_i in t[1:]]) + to
        return np.column_stack((positions, times))
    
    def min_jerk_trajectory(self,x0, x1, T):
        x0 = np.asarray(x0, float)
        x1 = np.asarray(x1, float)
        dx = x1 - x0
        def state(t):
            t = np.clip(t, 0.0, T)
            tau = t / T
            # basis polynomials
            s   = 10*tau**3 - 15*tau**4 + 6*tau**5
            ds  = (30*tau**2 - 60*tau**3 + 30*tau**4) / T
            d2s = (60*tau - 180*tau**2 + 120*tau**3) / T**2
            d3s = (60 - 360*tau + 360*tau**2) / T**3
            x = x0 + dx * s
            v = dx * ds
            a = dx * d2s
            j = dx * d3s
            return x, v, a, j
        return state

