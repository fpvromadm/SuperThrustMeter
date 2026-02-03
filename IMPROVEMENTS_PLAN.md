# Thrust Scale – Implementation Plan for Improvements

This document outlines a phased plan to improve the Thrust Scale firmware: structure, security, robustness, and optional features.

---

## Phase 1: Security & Configuration (High priority)

**Goal:** Remove credentials from source and make configuration maintainable.

| Step | Task | Details |
|------|------|---------|
| 1.1 | Move WiFi credentials out of source | Create `src/config.h` (or use `#include "secrets.h"` from a file not in git). Add `secrets.h.example` with placeholder values. Add `secrets.h` to `.gitignore`. |
| 1.2 | Centralize config constants | Move pin assignments, PWM/safety constants, and `MAX_TEST_SAMPLES` into `config.h` (or a single config module) so hardware changes are in one place. |

**Deliverables:** No credentials in repo; single place for hardware/config constants.

---

## Phase 2: Code Structure (Medium priority)

**Goal:** Split `main.cpp` into logical modules for easier maintenance and testing.

| Step | Task | Details |
|------|------|---------|
| 2.1 | Create `config.h` / `config.cpp` | Pins, PWM, safety limits, scale factor path. Optional: `config.cpp` for non-const config (e.g. loaded from LittleFS). |
| 2.2 | Create `scale.h` / `scale.cpp` | Load cell init, tare, `update()`/`getData()`, scale factor load/save. Expose a simple API (e.g. `Scale::read()`, `Scale::tare()`, `Scale::setCalFactor()`). |
| 2.3 | Create `esc.h` / `esc.cpp` | ESC PWM setup, `setEscThrottlePwm()`, telemetry (voltage/current), `handleTelemInterrupt` + `readEscTelemetry()`. Keep interrupt handler minimal; state in `esc.cpp`. |
| 2.4 | Create `state_machine.h` / `state_machine.cpp` | State enum, `currentState`, and state machine logic from `loop()`: ARMING, PRE_TEST_TARE, RUNNING_SEQUENCE, safety, finish. Takes callbacks or references to scale, ESC, and sequence/result buffers. |
| 2.5 | Create `sequence.h` / `sequence.cpp` | `TestStep`, `DataPoint`, `parseAndStoreSequence()`, `testSequence`, `testResults`, `finishTest()` (build JSON and notify). |
| 2.6 | Create `web_handlers.h` / `web_handlers.cpp` | WebSocket event handler, command parsing (start_test, stop_test, tare, set_scale_factor, etc.), and `notifyClients()`. Depends on scale, ESC, state machine, sequence. |
| 2.7 | Slim down `main.cpp` | `setup()` and `loop()`: init modules (LittleFS, WiFi, scale, ESC, state machine, web), then only `ws.cleanupClients()`, telemetry read, state machine tick, and small delay. |

**Deliverables:** Clear separation: config, scale, ESC, state machine, sequence, web. `main.cpp` stays small and readable.

---

## Phase 3: Robustness & Error Handling (Medium priority)

**Goal:** Graceful degradation and clearer failure modes.

| Step | Task | Details |
|------|------|---------|
| 3.1 | LittleFS init failure | If `LittleFS.begin()` fails, log clearly and either retry or use a default scale factor in RAM (no persistence until FS is fixed). |
| 3.2 | Load cell init / read errors | Check `LoadCell.begin()` return value if the library supports it. Where you use `LoadCell.update()`/`getData()`, consider timeouts or “stale” detection (e.g. no successful read for N ms). |
| 3.3 | WiFi timeout / AP fallback | You already have AP fallback; document it in the UI or Serial. Optionally notify WebSocket clients when in AP mode (e.g. “status” message with IP). |
| 3.4 | Sequence parsing validation | In `parseAndStoreSequence()`, validate PWM range (e.g. 1000–2000) and spinup/stable > 0 to avoid nonsensical sequences. Return clear error reason (e.g. “Invalid PWM in step 2”). |
| 3.5 | Safety shutdown recovery | Ensure after SAFETY_SHUTDOWN the ESC is at min throttle and state can transition to IDLE on “reset”; document behavior in code or README. |

**Deliverables:** Predictable behavior on FS/WiFi/scale/sequence errors; clear messages for debugging.

---

## Phase 4: Documentation & Maintainability (Lower priority)

**Goal:** Easier onboarding and hardware bring-up.

| Step | Task | Details |
|------|------|---------|
| 4.1 | Pin assignment table | In README or `config.h`, document each pin (HX711_DOUT, HX711_SCK, ESC_PWM, ESC_TELEM) and any jumper/hardware notes. |
| 4.2 | Wiring / hardware section | Brief description: load cell → HX711 → ESP32; ESC → PWM and telem pins; power considerations. |
| 4.3 | Build & run | In README: install PlatformIO, upload firmware, upload filesystem (LittleFS), open `http://<IP>/`, WebSocket for live data. |
| 4.4 | Sequence format | Document the test sequence string format (e.g. `PWM - spinup_sec - stable_sec; ...`) and any limits (e.g. max steps, max total time). |

**Deliverables:** README (and optionally comments) that allow someone else to wire, build, and run a test.

---

## Phase 5: Optional Enhancements (As needed)

**Goal:** Nice-to-have features without blocking core improvements.

| Step | Task | Details |
|------|------|---------|
| 5.1 | Display timeout / power saving | If you add a display later, dim or turn off after idle; or use ESP32 light sleep when idle (requires careful handling of WiFi/WS). |
| 5.2 | Remote logging over WiFi | Optional: send test summary or logs to a server or MQTT for record-keeping. |
| 5.3 | Unit tests for scale/sequence logic | Extract pure functions (e.g. sequence parsing, safety check math) into a module that can be compiled for host (native) or ESP32, and add PlatformIO unit tests. |

**Deliverables:** Documented options; tests only if you decide to invest in them.

---

## Suggested Order of Implementation

1. **Phase 1** (security + config) – small change, high impact.  
2. **Phase 2** (modularization) – do in small steps (e.g. config → scale → ESC → state machine → sequence → web → main).  
3. **Phase 3** (robustness) – can be done in parallel with or after Phase 2, per module.  
4. **Phase 4** (docs) – can be done incrementally as you touch each area.  
5. **Phase 5** – only if you need those features.

---

## Quick Reference: Files to Add (Phase 2)

| File | Purpose |
|------|---------|
| `include/config.h`, `src/config.cpp` | Pins, constants, optional secrets include |
| `include/scale.h`, `src/scale.cpp` | Load cell and scale factor persistence |
| `include/esc.h`, `src/esc.cpp` | PWM and ESC telemetry |
| `include/state_machine.h`, `src/state_machine.cpp` | State enum and loop logic |
| `include/sequence.h`, `src/sequence.cpp` | Test steps, parsing, results, finishTest |
| `include/web_handlers.h`, `src/web_handlers.cpp` | WebSocket and HTTP handlers |
| `src/main.cpp` | Init + loop only |

After each new file, build and run a quick test (tare, set scale factor, run a short sequence) to avoid regressions.
