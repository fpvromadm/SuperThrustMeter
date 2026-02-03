#include "Simulator.h"

static float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

bool simEnabled(const BoardConfig &cfg) { return cfg.sim_enabled; }

void updateSimTelemetry(AppState &state, const BoardConfig &cfg) {
  if (!simEnabled(cfg)) return;
  unsigned long now = millis();
  if (state.lastSimUpdateMs == 0) {
    state.lastSimUpdateMs = now;
    return;
  }
  unsigned long dt = now - state.lastSimUpdateMs;
  if (dt == 0) return;
  state.lastSimUpdateMs = now;

  float denom = (float)(cfg.max_pulse_width - cfg.min_pulse_width);
  float throttle = (denom > 0.0f) ? (float)(state.currentPwm - cfg.min_pulse_width) / denom : 0.0f;
  throttle = clampf(throttle, 0.0f, 1.0f);

  float noise = 0.0f;
  if (cfg.sim_noise_g > 0.0f) {
    long r = random(-1000, 1000);
    noise = ((float)r / 1000.0f) * cfg.sim_noise_g;
  }

  float target = (throttle * cfg.sim_thrust_max_g) + noise;
  float alpha = 1.0f;
  if (cfg.sim_response_ms > 0) {
    alpha = clampf((float)dt / (float)cfg.sim_response_ms, 0.0f, 1.0f);
  }
  state.simThrust = state.simThrust + (target - state.simThrust) * alpha;

  state.escVoltage = cfg.sim_voltage;
  state.escCurrent = cfg.sim_current_max * throttle;
}
