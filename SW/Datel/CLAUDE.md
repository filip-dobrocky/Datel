# Datel

ESP32-C3 firmware for a kinetic sound-art installation: a swarm of solenoid
"knocker" objects self-organize into a WiFi mesh (painlessMesh) and rhythmically
tap surfaces. Rhythm patterns mutate genetically as they propagate node-to-node,
and the swarm is steered externally over OSC/UDP. Built with PlatformIO + Arduino.

The shared mesh/OSC/state-machine code lives in the **Ecosystem** library
(`lib/Ecosystem`, a git submodule shared with the sibling `birb` project). Datel
keeps its actuator (`Knocker`), its rhythm `Mutator`, and the pattern notation
(`Pattern.h`) locally.

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
- **`Pattern.h`** — the step notation, shared by `Knocker` (playback) and `Mutator`
  (genetics) so there is one parser. A pattern is a sequence of `Step`s, each a **rest**,
  a **hit** (with per-step velocity), or a **peck** (freq/dur/curve/amp). `parsePattern`/
  `serializePattern` convert between the `Step[]` and the serialized string sent over the
  mesh; see the *Pattern notation* section. Amplitude floors and limits (`HIT_AMP_MIN`,
  `PECK_AMP_MIN`, `MAX_STEPS`, …) are `#define`s here, overridable per target via `-D`.
- **`Knocker.h`** — drives the solenoid via PWM (`analogWrite`, 20 kHz). Parses the
  pattern string into `Step[]` and plays it at a tempo (one rest/hit per 16th-note; a peck
  occupies `dur` steps). Each hit plays its own velocity — there is **no** running decay;
  dynamics live in the pattern. `setVelocity()` (driven by OSC/mesh `velocity`) is a
  **master gain** scaling every hit/peck. Uses its own `TaskScheduler`
  (`t_knock`/`t_off`/`t_peck`); `peck()` fires rapid envelope-shaped hits and, while
  ringing, owns the pin (the knock loop gates on `t_peck.isEnabled()`, ring-over allowed).
  Singleton via `Knocker::instance` because TaskScheduler callbacks must be static.
- **`Mutator.h`** — genetic mutation of patterns, subclassing the library's `MutatorBase`,
  operating on `Step[]`. Genes `A`-`G` are the rhythm operators (add/remove hit, burst
  rests, duplicate, shuffle, stretch, compress); `H`-`N` are peck/velocity operators
  (hit↔peck, mutate freq/curve/amp/dur, mutate hit velocity). `evolve()` parses the input,
  applies sender DNA then own DNA, sanitizes, and re-serializes. DNA generation and the
  `chaos` ramp live in `MutatorBase`.

Shared library (`lib/Ecosystem`): `EcosystemNode` (mesh + OSC), `SuspendManager`
(suspended/paused/idle state), `MutatorBase`, `EcosystemConfig.h`. See its README.

### Pattern notation

Patterns are serialized as a string of fixed-width, self-delimiting tokens (the markers
`_ x p` are non-hex, so hex fields parse unambiguously; hex is uppercase):

- `_` — **rest** (one 16th-note step).
- `x` — **hit** at the default velocity; `x` + `HH` — hit with explicit velocity byte
  (clamped `>= HIT_AMP_MIN`). One step.
- `p` + `F D CC AA` — **peck**, occupying `dur` steps: `F` = freq nibble (`freq-5`, 5-20 Hz),
  `D` = dur nibble (`dur-1`, 1-16 steps), `CC` = curve byte (`curve+10`, -10..10), `AA` =
  amp byte (clamped `>= PECK_AMP_MIN`).

Three lengths are distinct and must not be conflated: **step count** (number of `Step`
structs, used by the mutator), **musical length** (sum of step durations, used to schedule
playback), and **serialized char length** (used for the mesh/`MAX_PATTERN_LEN` bound). The
parser skips whitespace (so human-authored literals can be spaced) and is OOB-safe —
malformed/truncated tokens are dropped. The whole swarm must run matching firmware; old
firmware cannot parse `p…`/`xHH` tokens (plain `x_` strings still parse on both).

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
