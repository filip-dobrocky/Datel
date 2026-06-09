# Datel

ESP32-C3 firmware for a kinetic sound-art installation: a swarm of solenoid
"knocker" objects self-organize into a WiFi mesh (painlessMesh) and rhythmically
tap surfaces. Rhythm patterns mutate genetically as they propagate node-to-node,
and the swarm is steered externally over OSC/UDP. Built with PlatformIO + Arduino.

The shared mesh/OSC/state-machine code lives in the **Ecosystem** library
(`lib/Ecosystem`, a git submodule shared with the sibling `birb` project). Datel
keeps only its actuator (`Knocker`) and its rhythm `Mutator` locally.

## Build / flash / monitor

This repo uses a submodule — clone with `git clone --recursive`, or run
`git submodule update --init` in an existing clone. PlatformIO auto-builds
`lib/Ecosystem`.

One PlatformIO env per physical node; the env name's trailing number sets `OBJ_ID`
via a build flag (`olimex_c3_dev0` -> `OBJ_ID=0`, ... `olimex_c3_dev11` -> `OBJ_ID=11`).

- Build:   `pio run -e olimex_c3_dev0`
- Flash:   `pio run -e olimex_c3_dev0 -t upload`
- Monitor: `pio device monitor` (115200 baud; USB-CDC)

WiFi credentials live in `src/secrets.h` (`WIFI_SSID` / `WIFI_PASS`) and are meant
to be untracked. Network ports/channel now come from `EcosystemConfig.h` in the
library (mesh 5555, OSC recv 54345, OSC send 54355).

Test-only envs set build flags that bypass normal mesh behavior in `loop()`:
`-DTEST_PECK` exercises `Knocker::peck()`, `-DTEST_PATTERN` ramps velocity and knocks
continuously. Leave these off for production firmware.

## Architecture

Project-local files (`src/`):

- **`main.cpp`** — wiring only: builds an `EcosystemConfig`, constructs the global
  `EcosystemNode eco` + `SuspendManager suspendMgr`, registers mesh handlers
  (`eco.onMessage`) and OSC handlers (`eco.onOsc`), then `eco.begin()`. `loop()`
  pumps `eco.update()` (mesh + OSC) and `knocker.update()`.
- **`Knocker.h`** — drives the solenoid via PWM (`analogWrite`, 20 kHz). Plays a
  string pattern (`'x'` = hit, anything else = rest) at a tempo, with per-hit velocity
  decay. Uses its own `TaskScheduler` (`t_knock`/`t_off`/`t_peck`). `peck()` fires rapid
  envelope-shaped hits. Singleton via `Knocker::instance` because TaskScheduler callbacks
  must be static.
- **`Mutator.h`** — genetic mutation of rhythm patterns, subclassing the library's
  `MutatorBase`. Genes (`A`-`G`) map to operators (add/remove hit, stretch, compress,
  shuffle, …); `evolve()` applies sender DNA then own DNA. DNA generation and the
  `chaos` ramp live in `MutatorBase`.

Shared library (`lib/Ecosystem`): `EcosystemNode` (mesh + OSC), `SuspendManager`
(suspended/paused/idle state), `MutatorBase`, `EcosystemConfig.h`. See its README.

### Mesh protocol

Nodes broadcast JSON over painlessMesh; every message has a `"type"` field, routed by
`EcosystemNode` to the handler registered with `eco.onMessage(type, ...)`. Types:
`knocking`, `knocked`, `tweeting`, `tweeted`, `pause`, `ping`, `suspended`, `battery`,
`velocity`.

Behavioral flow: when a node knocks it broadcasts `knocking` (with `pattern`, `dna`,
`tempo`); receivers mutate the pattern, random-walk the tempo (clamped 60-500), and
briefly suspend. A finished knock broadcasts `knocked`, which prompts active (non-
suspended, non-paused) neighbors to knock — so activity ripples through the swarm.
`tweeting`/`tweeted` are the "birb" variant (random subdivision instead of pattern
mutation).

### OSC bridge (every node, no privileged root)

Any node receives OSC (`/pause`, `/velocity`) and rebroadcasts the change to the mesh,
so an external controller can attach to *any* node's softAP and drive the swarm.
Telemetry (`/ping`, `/suspended`, `/battery`) is emitted via `eco.forwardOsc()` to the
node's **own softAP-directed broadcast** (`10.x.y.255`) — whichever node the controller
is connected to delivers it. There is no special OSC "root": one node is still the
painlessMesh *topology* root (`OBJ_ID == 0`) for mesh stability/time-sync only. All
nodes run uniform `WIFI_AP_STA`.

> Earlier design forwarded telemetry only from the root to `255.255.255.255`, which an
> `AP_STA` node only sends out its STA interface — so a controller on a non-root node's
> AP never saw it. The softAP-directed broadcast from every node fixes that.

## Conventions / gotchas

- `OBJ_ID`, `LED_PIN`, `SOL_PIN`, `TEST_PECK`, etc. are compile-time `build_flags` in
  `platformio.ini`, not runtime config — changing a node's behavior usually means editing
  its env there.
- Logging uses ESP-IDF macros (`ESP_LOGI/D/E`) with per-file `TAG`s; verbosity is set by
  `-DCORE_DEBUG_LEVEL` in the env.
- `.pio/` is downloaded dependencies (painlessMesh, ArduinoJson, CNMAT OSC, osc_control,
  TaskScheduler) — do not edit or treat as project code. The CNMAT OSC + `osc_control`
  git deps are still listed in `platformio.ini` (not in the PlatformIO registry).
- Battery is read on `BAT_PIN` (GPIO3) and scaled in `measure_battery()`; low battery
  blinks the LED.
