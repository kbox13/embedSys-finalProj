# --- Beat predictor (phase/period Kalman filter) ------------------------------
import math
from collections import deque

class BeatPredictorKF:
    """
    Online estimator of (phase, period). 'phase' is the last-beat wall-clock time (s),
    'period' is the IBI (s). Feed it observed beat times in the *same clock* and
    call predict_next_beats(now, k) anytime.
    """
    def __init__(self, init_period=0.5, q_phase=1e-4, q_period=5e-5, r_meas=2.5e-3):
        self.phase = None
        self.period = init_period
        # Covariance
        self.P11 = 1e-2; self.P12 = 0.0
        self.P21 = 0.0;  self.P22 = 1e-2
        # Noises
        self.q_phase = q_phase
        self.q_period = q_period
        self.r_meas = r_meas
        # For guards
        self.last_obs = None
        self.recent_ibis = deque(maxlen=8)

    def _predict(self):
        # x=[phase; period], F=[[1,1],[0,1]]
        if self.phase is None:
            return
        self.phase = self.phase + self.period
        P11n = self.P11 + self.P12 + self.P21 + self.P22 + self.q_phase
        P12n = self.P12 + self.P22
        P21n = self.P21 + self.P22
        P22n = self.P22 + self.q_period
        self.P11, self.P12, self.P21, self.P22 = P11n, P12n, P21n, P22n

    def observe(self, t_obs):
        # Initialize
        if self.phase is None:
            self.phase = t_obs
            self.last_obs = t_obs
            return

        # One-step predict then update with z=t_obs, H=[1,0]
        self._predict()
        y = t_obs - self.phase
        S = self.P11 + self.r_meas
        K1 = self.P11 / S
        K2 = self.P21 / S

        self.phase += K1 * y
        self.period += K2 * y

        # P <- (I-KH)P
        P11n = (1 - K1) * self.P11
        P12n = (1 - K1) * self.P12
        P21n = self.P21 - K2 * self.P11
        P22n = self.P22 - K2 * self.P12
        self.P11, self.P12, self.P21, self.P22 = P11n, P12n, P21n, P22n

        # Robust clamps (40–300 BPM)
        self.period = max(0.2, min(self.period, 1.5))

        # Track IBI / outlier guard
        if self.last_obs is not None:
            ibi = t_obs - self.last_obs
            # Ignore obvious doubled/halved errors
            if 0.2 <= ibi <= 1.5:
                self.recent_ibis.append(ibi)
        self.last_obs = t_obs

    def predict_next_beats(self, now_s, k=4):
        if self.phase is None or self.period <= 0:
            return []
        m = max(1, math.ceil((now_s - self.phase) / self.period))
        return [self.phase + i*self.period for i in range(m, m + k)]

    def confidence_std(self):
        # Rough 1σ for next-beat timing
        return math.sqrt(max(self.P11 + self.P22 + self.r_meas, 1e-6))
