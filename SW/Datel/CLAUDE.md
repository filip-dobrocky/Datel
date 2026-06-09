# Datel

ESP32-C3 firmware for a kinetic sound-art installation: a swarm of solenoid
"knocker" objects self-organize into a WiFi mesh (painlessMesh) and rhythmically
tap surfaces. Rhythm patterns mutate genetically as they propagate node-to-node,
and the swarm is steered externally over OSC/UDP. Built with PlatformIO + Arduino.

## Build / flash / monitor

One PlatformIO env per physical node; the env name's trailing number sets `OBJ_ID`
via a build flag (`olimex_c3_dev0` -> `OBJ_ID=0`, ... `olimex_c3_dev11` -> `OBJ_ID=11`).

- Build:   `pio run -e olimex_c3_dev0`
- Flash:   `pio run -e olimex_c3_dev0 -t upload`
- Monitor: `pio device monitor` (115200 baud; USB-CDC)

`OBJ_ID == 0` is the **root/gateway** node: it runs in WiFi AP mode and bridges the
mesh to the external OSC controller. All other IDs are plain mesh nodes.

WiFi credentials live in `src/secrets.h` (`WIFI_SSID` / `WIFI_PASS`) and are meant to
be untracked. Network ports are in `src/network_config.h` (mesh 5555, OSC recv 54345,
OSC send 54355).

Test-only envs set build flags that bypass normal mesh behavior in `loop()`:
`-DTEST_PECK` exercises `Knocker::peck()`, `-DTEST_PATTERN` ramps velocity and knocks
continuously. Leave these off for production firmware.

## Architecture

- **`src/main.cpp`** — setup/loop, mesh + OSC plumbing, and message routing. Holds all
  globals (the single `Knocker`, `Mutator`, `painlessMesh`, scheduler tasks). `loop()`
  pumps `mesh.update()`, `knocker.update()`, and `osc_control_loop()`.
- **`src/Knocker.h`** — drives the solenoid via PWM (`analogWrite`, 20 kHz). Plays a
  string pattern (`'x'` = hit, anything else = rest) at a tempo, with per-hit velocity
  decay. Uses its own `TaskScheduler` (`t_knock`/`t_off`/`t_peck`). `peck()` fires rapid
  envelope-shaped hits. Singleton via `Knocker::instance` because TaskScheduler callbacks
  must be static.
- **`src/Mutator.h`** — genetic mutation of rhythm patterns. Each node derives a fixed
  "DNA" string from its `OBJ_ID`; genes (`A`-`G`) map to mutation operators (add/remove
  hit, stretch, compress, shuffle, etc.). `evolve()` applies sender DNA then own DNA, and
  a per-node `chaos` value rises over time to increase mutation probability.

### Mesh protocol

Nodes communicate by broadcasting JSON over painlessMesh; every message has a `"type"`
field routed in `receivedCallback`. Types: `knocking`, `knocked`, `tweeting`, `tweeted`,
`pause`, `ping`, `suspended`, `battery`, `velocity`.

Behavioral flow: when a node knocks it broadcasts `knocking` (with `pattern`, `dna`,
`tempo`); receivers mutate the pattern, random-walk the tempo (clamped 60-500), and
briefly `suspend`. A finished knock broadcasts `knocked`, which prompts un-suspended
neighbors to knock — so activity ripples through the swarm. `tweeting`/`tweeted` are the
"birb" variant (random subdivision instead of pattern mutation).

### OSC bridge

Any node can receive OSC (`/pause`, `/velocity`) and rebroadcasts the change to the mesh.
Only the root node forwards telemetry (`/ping`, `/suspended`, `/battery`) out to the OSC
controller at `/info`; non-root nodes' telemetry reaches the controller via the mesh ->
root relay. OSC support is the `osc_control` library (CNMAT OSC over UDP).

## Conventions / gotchas

- `OBJ_ID`, `LED_PIN`, `SOL_PIN`, `TEST_PECK`, etc. are compile-time `build_flags` in
  `platformio.ini`, not runtime config — changing a node's behavior usually means editing
  its env there.
- Logging uses ESP-IDF macros (`ESP_LOGI/D/E`) with per-file `TAG`s; verbosity is set by
  `-DCORE_DEBUG_LEVEL` in the env.
- `.pio/` is downloaded dependencies (painlessMesh, ArduinoJson, CNMAT OSC, osc_control,
  TaskScheduler) — do not edit or treat as project code.
- Battery is read on `BAT_PIN` (GPIO3) and scaled in `measure_battery()`; low battery
  blinks the LED.
