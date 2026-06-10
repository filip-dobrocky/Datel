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

The node identity (`OBJ_ID`) lives in **NVS**, not in the firmware image. Envs:

- `olimex_c3_dev0` … `dev11` — **provisioning** builds: `-DOBJ_ID=n` seeds the id
  into NVS at boot. Flash once over USB per physical node; the id then survives
  every reflash (OTA or USB).
- `olimex_c3_ota` — generic image (no `OBJ_ID`, id from NVS), info-level logs;
  the artifact for bench-testing OTA.
- `olimex_c3_deploy` — generic image, `CORE_DEBUG_LEVEL=0`; the production OTA
  artifact (also the smallest — flash-headroom margin).
- `olimex_c3_bench` — `TEST_PECK` bench loop; never distribute via OTA (test
  loops skip `eco.update()`, so such a node drops off the mesh).

- Build:   `pio run -e olimex_c3_deploy`
- Flash:   `pio run -e olimex_c3_dev0 -t upload` (USB provisioning)
- Monitor: `pio device monitor` (115200 baud; USB-CDC)

### OTA update (whole swarm, no USB)

Scripted (recommended): join any node's softAP, bump `FW_VERSION` in
`src/main.cpp`, then `python tools/ota_update.py [--watch]` — builds the env
(default `olimex_c3_deploy`), auto-detects the node (gateway `10.x.y.1`),
uploads, and verifies the distributor's md5 echo. Manually:

1. `pio run -e olimex_c3_deploy` -> `.pio/build/olimex_c3_deploy/firmware.bin`.
2. Optionally OSC `/pause 1` to quiet the swarm.
3. Join any node's softAP and upload: `curl -F "fw=@firmware.bin" http://10.x.y.1/fw`
   (or the browser form at `/`; the AP IP is the gateway and is printed at boot).
   `GET /status` shows progress, `GET /abort` cancels.
4. The node stores the image to LittleFS and offers it to the mesh (role
   `"datel"`, announce repeats for up to 1 h). Each node pulls ~1100 × 1 KiB
   chunks, verifies MD5, reboots — minutes per node, tens of minutes fleet-wide.
   The distributor flashes itself last (all nodes done, or 25 min timeout).
5. There is **no rollback**: a crash-looping image means USB recovery. Bench-test
   the generic image on one node first, and never ship an image without the OTA
   receiver compiled in.

An unprovisioned board (generic image, empty NVS) fast-blinks the LED, joins the
mesh (OTA still works) and ignores id-targeted commands until a `devN` env is
flashed once.

WiFi credentials live in `src/secrets.h` (`WIFI_SSID` / `WIFI_PASS`) and are meant
to be untracked. Network ports/channel now come from `EcosystemConfig.h` in the
library (mesh 5555, OSC recv 54345, OSC send 54355).

Test-only envs set build flags that bypass normal mesh behavior in `loop()`:
`-DTEST_PECK` exercises `Knocker::peck()`, `-DTEST_PATTERN` ramps velocity and knocks
continuously. Leave these off for production firmware.

## Architecture

Project-local files (`src/`):

- **`main.cpp`** — wiring only: builds an `EcosystemConfig` (with `ota_role =
  "datel"`), constructs the global `EcosystemNode eco` + `SuspendManager
  suspendMgr` + `OtaDistributor ota`, resolves the runtime identity (`g_obj_id`:
  seeded from `-DOBJ_ID` into NVS on provisioning builds, loaded from NVS
  otherwise) and applies it via `eco.setIdentity()` / `mutator.regenerateDna()` /
  `knocker.setOffTimeScale()`, registers mesh handlers (`eco.onMessage`) and OSC
  handlers (`eco.onOsc`), then `eco.begin()` + `ota.begin()`. `loop()` pumps
  `eco.update()` (mesh + OSC), `knocker.update()` and `ota.update()`.
- **`NodeConfig.h`** — per-object **hardware** constants keyed by the runtime id:
  `knocker_off_time_scale` (250 for objects 7 and 10, 500 default) plus the
  amplitude tuning `hit_amp_min` / `peck_amp_min` / `hit_amp_default` /
  `peck_amp_default` (applied to Pattern.h's `g_*` runtime variables). Only
  per-hardware tuning belongs here; structural constants every node must agree
  on (`MAX_STEPS`, the notation) stay compile-time.
- **`Pattern.h`** — the step notation, shared by `Knocker` (playback) and `Mutator`
  (genetics) so there is one parser. A pattern is a sequence of `Step`s, each a **rest**,
  a **hit** (with per-step velocity), or a **peck** (freq/dur/curve/amp). `parsePattern`/
  `serializePattern` convert between the `Step[]` and the serialized string sent over the
  mesh; see the *Pattern notation* section. Amplitude floors/defaults (`g_hit_amp_min`,
  `g_peck_amp_min`, `g_hit_amp_default`, `g_peck_amp_default`) are **runtime** variables
  here, initialized from `-D`-overridable defaults and overridden per object from
  `NodeConfig.h` in `setup()`. Structural limits (`MAX_STEPS`, the notation) stay
  compile-time.
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

Shared library (`lib/Ecosystem`): `EcosystemNode` (mesh + OSC + OTA receive),
`SuspendManager` (suspended/paused/idle state), `MutatorBase`, `EcosystemConfig.h`,
`EcosystemIdentity.h` (NVS id), `OtaDistributor` (HTTP upload + mesh OTA send).
See its README.

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

- `OBJ_ID` is **runtime** (NVS via `EcosystemIdentity`, seeded by the `devN`
  provisioning envs); per-object hardware tuning lives in `src/NodeConfig.h`.
  `LED_PIN`, `SOL_PIN`, `TEST_PECK`, `USE_FS_LITTLEFS`, etc. remain compile-time
  `build_flags` in `platformio.ini`. Don't reintroduce per-node `-D` constants —
  they'd break the single-image OTA model.
- Logging uses ESP-IDF macros (`ESP_LOGI/D/E`) with per-file `TAG`s; verbosity is set by
  `-DCORE_DEBUG_LEVEL` in the env.
- `.pio/` is downloaded dependencies (painlessMesh, ArduinoJson, CNMAT OSC, osc_control,
  TaskScheduler) — do not edit or treat as project code. The CNMAT OSC + `osc_control`
  git deps are still listed in `platformio.ini` (not in the PlatformIO registry).
- Battery is read on `BAT_PIN` (GPIO3) and scaled in `measure_battery()`; low battery
  blinks the LED.
