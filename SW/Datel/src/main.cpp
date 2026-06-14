#include <Arduino.h>
#include <LittleFS.h>

#include "EcosystemIdentity.h"
#include "EcosystemNode.h"
#include "OtaDistributor.h"
#include "SuspendManager.h"

#include "Knocker.h"
#include "Mutator.h"
#include "NodeConfig.h"

#ifndef WIFI_SSID
#include "secrets.h"
#endif

#ifndef SOL_PIN
#define SOL_PIN 0
#endif

#ifndef LED_PIN
#define LED_PIN 8
#endif

#define BAT_PIN 3

// Bump for every OTA release. Travels in the mesh "ping" and the /info/ping
// OSC telemetry, so the controller can watch each node flip to the new
// version as the swarm updates (old pre-versioning firmware reports 0).
#define FW_VERSION 2

const char *TAG = "Datel";

// === Ecosystem node (mesh + OSC) ===
// The real identity (obj id + root flag) is set at runtime in setup() via
// eco.setIdentity(): provisioning builds seed it from -DOBJ_ID into NVS,
// the generic OTA image loads it from NVS (see EcosystemIdentity.h).
static EcosystemConfig make_datel_cfg() {
    EcosystemConfig cfg{-1, false, "/datel", "/info"};
    cfg.ota_role = "datel";  // mesh OTA receive; role-matched vs. "birb"
    return cfg;
}
EcosystemConfig datel_cfg = make_datel_cfg();
EcosystemNode eco(datel_cfg);
int g_obj_id = -1;  // runtime object id (NVS); -1 = unprovisioned

// === State machine ===
static const uint32_t SUSPEND_LONG = 12000;
static const uint32_t SUSPEND_SHORT = 2000;
static const uint32_t IDLE_TIMEOUT = 35000;
SuspendManager suspendMgr(eco.scheduler(), IDLE_TIMEOUT);

// Defaults restored by OSC /reset (also used at setup()).
static const char    *DEFAULT_PATTERN  = "xx_p750AFF_xx_";
static const uint16_t DEFAULT_TEMPO    = 300;
static const uint32_t DENSITY_FLOOR_MS = 500;  // densest = 500 ms

// === Actuator & genetics ===
Knocker knocker(SOL_PIN);
Mutator mutator(0);  // DNA re-derived in setup() once the NVS id is known
bool knocking = false;
float battery_voltage = 0.0f;

// === OTA (any node can distribute firmware to the mesh) ===
OtaDistributor ota(eco.mesh(), eco.scheduler(), "datel");

// === Control mode ===
bool g_auto = true;    // global emergence master (informational/log)
bool g_listen = true;  // this node participates in emergence
bool g_blinking = true;     // LED blink while paused / low battery
bool g_tempo_rand = true;   // per-knock tempo random walk
float g_density = 0.0f;      // 0 = current (sparsest) .. 1 = floor (500 ms, densest)

// density 0 -> ms (as-is, sparsest), density 1 -> DENSITY_FLOOR_MS (densest).
// Never below the floor.
static uint32_t scaleByDensity(uint32_t ms) {
    if (ms <= DENSITY_FLOOR_MS) return ms;
    return DENSITY_FLOOR_MS +
           (uint32_t)lroundf((ms - DENSITY_FLOOR_MS) * (1.0f - g_density));
}

// === Function Prototypes ===
void send_knocking();
void send_knocked();
void send_battery();
void send_ping();
void send_velocity(float norm_value);

void osc_pause_received(OSCMessage &m);
void osc_velocity_received(OSCMessage &m);
void osc_auto_received(OSCMessage &m);
void osc_listen_received(OSCMessage &m);
void osc_pattern_received(OSCMessage &m);
void osc_peck_received(OSCMessage &m);
void osc_blinking_received(OSCMessage &m);
void osc_density_received(OSCMessage &m);
void osc_tempo_received(OSCMessage &m);
void osc_tempo_rand_received(OSCMessage &m);
void osc_reset_received(OSCMessage &m);

void set_listen(bool v);
void set_blinking(bool v);
void apply_listen(int id, int v);
void apply_pattern(int id, const char *pat);
void apply_peck(int id, float freq, float dur_ms, float curve, float amp_norm);

void mutate_pattern(const char *pattern, const char *sender_dna);
void measure_battery();

// === Tasks (on the shared Ecosystem scheduler) ===
Task t_measure_battery(60000, TASK_FOREVER, &measure_battery, &eco.scheduler(),
                       false);

Task t_low_battery_indication(2000, TASK_FOREVER, []() {
    static bool led_on = false;
    digitalWrite(LED_PIN, led_on ? HIGH : LOW);
    led_on = !led_on;
}, &eco.scheduler(), false);

Task t_ping(5000, TASK_FOREVER, &send_ping, &eco.scheduler(), true);
Task t_send_battery(60000, TASK_FOREVER, &send_battery, &eco.scheduler(), false);

// Fast blink = node has no id in NVS (generic image on an unprovisioned
// board). It still joins the mesh and accepts OTA; flash a -DOBJ_ID env
// over USB once to seed the identity. LED is active-low.
Task t_unprovisioned_blink(150, TASK_FOREVER, []() {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
}, &eco.scheduler(), false);

// === Mesh message handlers ===
static void on_knocking(JsonDocument &doc, uint32_t /*from*/) {
    if (!g_listen) return;

    const char *pattern = doc["pattern"];
    const char *sender_dna = doc["dna"];
    int sender_tempo = doc["tempo"] | 400;

    if (pattern && sender_dna) {
        ESP_LOGD(TAG, "Pattern: %s, DNA: %s, Tempo: %d", pattern, sender_dna,
                 sender_tempo);
        mutate_pattern(pattern, sender_dna);

        if (g_tempo_rand) {
            // Random walk the tempo by +/-10..80, clamped 60..500
            int tempo_change = random(1, 9) * 10;
            if (random(0, 2) == 0) tempo_change = -tempo_change;
            int new_tempo = constrain(sender_tempo + tempo_change, 60, 500);
            knocker.setTempo(new_tempo);
            ESP_LOGI(TAG, "Tempo adjusted: %d -> %d (change: %+d)", sender_tempo,
                     new_tempo, tempo_change);
        }
    }

    suspendMgr.setSuspended(true, scaleByDensity(SUSPEND_SHORT));
}

static void on_knocked(JsonDocument & /*doc*/, uint32_t /*from*/) {
    if (g_listen && suspendMgr.active()) knocker.knock();
}

// TODO: generalize reaction to other species and move to Ecosystem base
static void on_tweeting(JsonDocument &doc, uint32_t /*from*/) {
    if (!g_listen) return;

    int sender_tempo = doc["tempo"] | 400;

    // birb variant: random subdivision (pattern ignored), random-walk tempo
    if (g_tempo_rand) {
        int tempo_change = random(1, 9) * 10;
        if (random(0, 2) == 0) tempo_change = -tempo_change;
        int new_tempo = constrain(sender_tempo + tempo_change, 60, 500);
        knocker.setTempo(new_tempo);
        ESP_LOGI(TAG, "Tempo adjusted (tweeting): %d -> %d", sender_tempo,
                 new_tempo);
    }

    suspendMgr.setSuspended(true, scaleByDensity(SUSPEND_SHORT));
}

static void on_tweeted(JsonDocument & /*doc*/, uint32_t /*from*/) {
    if (g_listen && suspendMgr.active()) knocker.knock();
}

static void on_pause(JsonDocument &doc, uint32_t /*from*/) {
    int pause_state = doc["state"] | 0;
    suspendMgr.setPaused(pause_state != 0);
    ESP_LOGI(TAG, "Mesh pause received: %d", pause_state);
}

static void on_ping(JsonDocument &doc, uint32_t /*from*/) {
    int obj_id = doc["id"] | -1;
    int version = doc["version"] | 0;  // 0 = pre-versioning firmware
    // Forward to the controller; only the node hosting it actually delivers.
    eco.forwardOsc("/ping", [obj_id, version](OSCMessage &m) {
        m.add(obj_id);
        m.add(version);
    });
}

static void on_suspended(JsonDocument &doc, uint32_t /*from*/) {
    int obj_id = doc["id"] | -1;
    int state = doc["state"] | 0;
    eco.forwardOsc("/suspended", [obj_id, state](OSCMessage &m) {
        m.add(obj_id);
        m.add(state);
    });
}

static void on_battery(JsonDocument &doc, uint32_t /*from*/) {
    int obj_id = doc["id"] | -1;
    float voltage = doc["voltage"] | 0.0f;
    eco.forwardOsc("/battery", [obj_id, voltage](OSCMessage &m) {
        m.add(obj_id);
        m.add(voltage);
    });
}

static void on_velocity(JsonDocument &doc, uint32_t /*from*/) {
    float norm = doc["value"] | 0.0f;
    norm = constrain(norm, 0.0f, 1.0f);
    knocker.setVelocity((uint8_t)roundf(norm * 255.0f));
    ESP_LOGI(TAG, "Mesh velocity: %.3f", norm);
}

static void on_auto(JsonDocument &doc, uint32_t /*from*/) {
    int v = doc["state"] | 1;
    g_auto = (v != 0);
    set_listen(v != 0);
    ESP_LOGI(TAG, "Mesh auto: %d", v);
}

static void on_listen(JsonDocument &doc, uint32_t /*from*/) {
    apply_listen(doc["id"] | -1, doc["state"] | 0);
}

static void on_pattern(JsonDocument &doc, uint32_t /*from*/) {
    const char *p = doc["pattern"];
    if (p) apply_pattern(doc["id"] | -1, p);
}

static void on_peck(JsonDocument &doc, uint32_t /*from*/) {
    apply_peck(doc["id"] | -1, doc["freq"] | 12.0f, doc["dur"] | 2000.0f,
               doc["curve"] | 0.0f, doc["amp"] | 1.0f);
}

static void on_blinking(JsonDocument &doc, uint32_t /*from*/) {
    set_blinking((doc["state"] | 1) != 0);
    ESP_LOGI(TAG, "Mesh blinking: %d", (int)g_blinking);
}

static void on_density(JsonDocument &doc, uint32_t /*from*/) {
    g_density = constrain((float)(doc["value"] | 0.0f), 0.0f, 1.0f);
    ESP_LOGI(TAG, "Mesh density: %.3f", g_density);
}

static void on_tempo(JsonDocument &doc, uint32_t /*from*/) {
    int bpm = constrain((int)(doc["bpm"] | DEFAULT_TEMPO), 60, 500);
    knocker.setTempo(bpm);
    ESP_LOGI(TAG, "Mesh tempo: %d", bpm);
}

static void on_tempo_rand(JsonDocument &doc, uint32_t /*from*/) {
    g_tempo_rand = (doc["state"] | 1) != 0;
    ESP_LOGI(TAG, "Mesh tempoRand: %d", (int)g_tempo_rand);
}

static void on_reset(JsonDocument & /*doc*/, uint32_t /*from*/) {
    knocker.setPattern(DEFAULT_PATTERN);
    knocker.setTempo(DEFAULT_TEMPO);
    ESP_LOGI(TAG, "Mesh reset: pattern + tempo to defaults");
}

// === Setup & Loop ===
void setup() {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    // === Identity (NVS) ===
#ifdef OBJ_ID
    // Provisioning build (per-node env): seed the id into NVS over USB.
    g_obj_id = OBJ_ID;
    EcosystemIdentity::save(OBJ_ID);
    // A USB flash bypasses the OTA bookkeeping; drop the stale installed-md5
    // record so the next mesh OTA offer is not wrongly deduped.
    if (LittleFS.begin(true)) LittleFS.remove("/ota_fw.json");
#else
    // Generic OTA image: identity comes from NVS only.
    g_obj_id = EcosystemIdentity::load(-1);
    if (g_obj_id < 0) t_unprovisioned_blink.enable();
#endif
    eco.setIdentity(g_obj_id, /*is_root=*/g_obj_id == 0);
    mutator.regenerateDna((uint8_t)max(g_obj_id, 0));

    // Per-object hardware tuning (must run before the first setPattern/parse).
    NodeConfig node_cfg = nodeConfigFor(g_obj_id);
    knocker.setOffTimeScale(node_cfg.knocker_off_time_scale);
    g_hit_amp_min      = node_cfg.hit_amp_min;
    g_peck_amp_min     = node_cfg.peck_amp_min;
    g_hit_amp_default  = node_cfg.hit_amp_default;
    g_peck_amp_default = node_cfg.peck_amp_default;

    // Register mesh + OSC handlers, then bring the node up.
    eco.onMessage("knocking", on_knocking);
    eco.onMessage("knocked", on_knocked);
    eco.onMessage("tweeting", on_tweeting);
    eco.onMessage("tweeted", on_tweeted);
    eco.onMessage("pause", on_pause);
    eco.onMessage("ping", on_ping);
    eco.onMessage("suspended", on_suspended);
    eco.onMessage("battery", on_battery);
    eco.onMessage("velocity", on_velocity);
    eco.onMessage("auto", on_auto);
    eco.onMessage("listen", on_listen);
    eco.onMessage("pattern", on_pattern);
    eco.onMessage("peck", on_peck);
    eco.onMessage("blinking", on_blinking);
    eco.onMessage("density", on_density);
    eco.onMessage("tempo", on_tempo);
    eco.onMessage("tempoRand", on_tempo_rand);
    eco.onMessage("reset", on_reset);

    eco.onOsc("/pause", osc_pause_received);
    eco.onOsc("/velocity", osc_velocity_received);
    eco.onOsc("/auto", osc_auto_received);
    eco.onOsc("/listen", osc_listen_received);
    eco.onOsc("/pattern", osc_pattern_received);
    eco.onOsc("/peck", osc_peck_received);
    eco.onOsc("/blinking", osc_blinking_received);
    eco.onOsc("/density", osc_density_received);
    eco.onOsc("/tempo", osc_tempo_received);
    eco.onOsc("/tempoRand", osc_tempo_rand_received);
    eco.onOsc("/reset", osc_reset_received);

    eco.begin(WIFI_SSID, WIFI_PASS);

    // === State machine hooks ===
    suspendMgr.onSuspendChanged([](bool /*state*/) {
        // Announce suspend state to mesh + controller.
        eco.broadcast("suspended", [](JsonDocument &d) {
            d["state"] = (int)suspendMgr.isSuspended();
        });
        eco.forwardOsc("/suspended", [](OSCMessage &m) {
            m.add(g_obj_id);
            m.add((int)suspendMgr.isSuspended());
        });
        ESP_LOGI(TAG, "Suspended: %u", suspendMgr.isSuspended());
    });
    suspendMgr.onIdle([]() { if (g_listen) knocker.knock(); });

    // Blink the builtin LED while paused (low-duty heartbeat; off at rest).
    // The onboard GPIO8 LED is active-low. Managed on the shared scheduler.
    suspendMgr.setPauseLed(LED_PIN, /*active_high=*/false, /*on_ms=*/60,
                           /*period_ms=*/2000, /*rest_lit=*/false);

    // === Knocker ===
    knocker.setTempo(DEFAULT_TEMPO);
    // Steps: 2 hits, rest, peck(freq 12Hz, dur 6, curve 0, amp 255), rest, 2 hits, rest
    knocker.setPattern(DEFAULT_PATTERN);

    knocker.setOnStarted([]() {
        ESP_LOGI(TAG, "Knocking started");
        send_knocking();
        knocking = true;
    });

    knocker.setOnFinished([]() {
        ESP_LOGI(TAG, "Knocking finished");
        suspendMgr.pokeIdle(scaleByDensity(45000UL));
        suspendMgr.setSuspended(true, scaleByDensity(SUSPEND_LONG));
        send_knocked();
        knocking = false;
    });

    // Stagger the periodic tasks.
    suspendMgr.pokeIdle(10000UL);
    t_measure_battery.restartDelayed(15000UL);
    t_send_battery.restartDelayed(20000UL);

    // Firmware upload endpoint on this node's softAP (mesh OTA distributor).
    ota.begin();

    ESP_LOGI(TAG, "Node ID: %u, OBJ_ID: %d, isRoot: %d", eco.mesh().getNodeId(),
             g_obj_id, eco.isRoot());
    ESP_LOGI(TAG, "FW v%d, build %s %s, md5 %s, AP %s", FW_VERSION, __DATE__,
             __TIME__, ESP.getSketchMD5().c_str(),
             WiFi.softAPIP().toString().c_str());
}

void loop() {
#if TEST_PATTERN
    static uint16_t velocity = 230;
    if (!knocking) {
        knocker.setVelocity(velocity);
        velocity += 5;
        if (velocity > 255) velocity = 230;
        ESP_LOGI(TAG, "Velocity: %u", velocity);
        knocker.knock();
        ESP_LOGI(TAG, "Knock!");
    }
#elif TEST_PECK
    knocker.update();
    static uint32_t last_peck = 0;
    static uint8_t peck_phase = 0;
    if (millis() - last_peck > 3000) {
        last_peck = millis();
        switch (peck_phase % 4) {
            case 0: knocker.peck(20.0f, 2000, 0.0f, 255);   ESP_LOGI(TAG, "Peck: flat");             break;
            case 1: knocker.peck(14.0f, 2000, -10.0f, 255); ESP_LOGI(TAG, "Peck: fade out");         break;
            case 2: knocker.peck(8.0f,  2000, 6.0f, 255);   ESP_LOGI(TAG, "Peck: fade in");          break;
            case 3: knocker.peck(18.0f, 2000, -15.0f, 255); ESP_LOGI(TAG, "Peck: strong fade out");  break;
        }
        peck_phase++;
    }
#else
    eco.update();      // mesh.update() (runs scheduler) + osc_control_loop()
    knocker.update();
    ota.update();      // HTTP firmware-upload endpoint (softAP)
#endif
}

// === Send Functions ===
void send_knocking() {
    eco.broadcast("knocking", [](JsonDocument &d) {
        d["pattern"] = knocker.getPattern();
        d["dna"] = mutator.get_dna();
        d["tempo"] = knocker.getTempo();
    });
    ESP_LOGD(TAG, "Sent knocking (tempo: %d)", knocker.getTempo());
}

void send_knocked() { eco.broadcast("knocked"); }

void send_ping() {
    eco.broadcast("ping", [](JsonDocument &d) { d["version"] = FW_VERSION; });
    eco.forwardOsc("/ping", [](OSCMessage &m) {
        m.add(g_obj_id);
        m.add((int)FW_VERSION);
    });
}

void send_battery() {
    eco.broadcast("battery",
                  [](JsonDocument &d) { d["voltage"] = battery_voltage; });
    eco.forwardOsc("/battery", [](OSCMessage &m) {
        m.add(g_obj_id);
        m.add(battery_voltage);
    });
    ESP_LOGD(TAG, "Sent battery: %.2f V", battery_voltage);
}

void send_velocity(float norm_value) {
    norm_value = constrain(norm_value, 0.0f, 1.0f);
    eco.broadcast("velocity",
                  [norm_value](JsonDocument &d) { d["value"] = norm_value; });
}

// === OSC Receive (any node; rebroadcast to mesh) ===
void osc_pause_received(OSCMessage &m) {
    if (m.size() > 0) {
        int pause_state = m.getInt(0);
        suspendMgr.setPaused(pause_state != 0);
        eco.broadcast("pause",
                      [pause_state](JsonDocument &d) { d["state"] = pause_state; });
        ESP_LOGI(TAG, "OSC pause received: %d (broadcast to mesh)", pause_state);
    }
}

void osc_velocity_received(OSCMessage &m) {
    if (m.size() == 0) return;

    float norm = m.getFloat(0);
    if (isnan(norm)) {
        int v = m.getInt(0);
        norm = (v > 1) ? constrain((float)v / 255.0f, 0.0f, 1.0f)
                       : constrain((float)v, 0.0f, 1.0f);
    }
    norm = constrain(norm, 0.0f, 1.0f);

    knocker.setVelocity((uint8_t)roundf(norm * 255.0f));
    ESP_LOGI(TAG, "OSC velocity: %.3f", norm);

    send_velocity(norm);  // distribute to the rest of the swarm
}

void osc_auto_received(OSCMessage &m) {
    if (m.size() == 0) return;
    int v = m.getInt(0);
    g_auto = (v != 0);
    set_listen(v != 0);
    eco.broadcast("auto", [v](JsonDocument &d) { d["state"] = v; });
    ESP_LOGI(TAG, "OSC auto: %d (broadcast to mesh)", v);
}

void osc_listen_received(OSCMessage &m) {
    if (m.size() < 2) return;
    int id = m.getInt(0);
    int v = m.getInt(1);
    apply_listen(id, v);
    eco.broadcast("listen", [id, v](JsonDocument &d) {
        d["id"] = id;
        d["state"] = v;
    });
    ESP_LOGI(TAG, "OSC listen: id=%d state=%d (broadcast to mesh)", id, v);
}

void osc_pattern_received(OSCMessage &m) {
    if (m.size() < 2) return;
    int id = m.getInt(0);
    char pat[MAX_PATTERN_LEN];
    pat[0] = '\0';
    m.getString(1, pat, MAX_PATTERN_LEN);
    apply_pattern(id, pat);
    String p(pat);
    eco.broadcast("pattern", [id, p](JsonDocument &d) {
        d["id"] = id;
        d["pattern"] = p;
    });
    ESP_LOGI(TAG, "OSC pattern: id=%d pattern=%s (broadcast to mesh)", id, pat);
}

void osc_peck_received(OSCMessage &m) {
    if (m.size() < 4) return;
    int id = m.getInt(0);
    float freq = m.getFloat(1);
    float dur = m.getFloat(2);
    float curve = m.getFloat(3);
    float amp = (m.size() > 4) ? m.getFloat(4) : 1.0f;
    if (isnan(amp)) amp = 1.0f;
    apply_peck(id, freq, dur, curve, amp);
    eco.broadcast("peck", [id, freq, dur, curve, amp](JsonDocument &d) {
        d["id"] = id;
        d["freq"] = freq;
        d["dur"] = dur;
        d["curve"] = curve;
        d["amp"] = amp;
    });
    ESP_LOGI(TAG, "OSC peck: id=%d freq=%.1f dur=%.1f curve=%.1f amp=%.2f", id,
             freq, dur, curve, amp);
}

void osc_blinking_received(OSCMessage &m) {
    if (m.size() == 0) return;
    int v = m.getInt(0);
    set_blinking(v != 0);
    eco.broadcast("blinking", [v](JsonDocument &d) { d["state"] = v; });
    ESP_LOGI(TAG, "OSC blinking: %d (broadcast to mesh)", v);
}

void osc_density_received(OSCMessage &m) {
    if (m.size() == 0) return;
    float v = m.getFloat(0);
    if (isnan(v)) v = (float)m.getInt(0);
    v = constrain(v, 0.0f, 1.0f);
    g_density = v;
    eco.broadcast("density", [v](JsonDocument &d) { d["value"] = v; });
    ESP_LOGI(TAG, "OSC density: %.3f (broadcast to mesh)", v);
}

void osc_tempo_received(OSCMessage &m) {
    if (m.size() == 0) return;
    float f = m.getFloat(0);
    int bpm = isnan(f) ? m.getInt(0) : (int)roundf(f);
    bpm = constrain(bpm, 60, 500);
    knocker.setTempo(bpm);
    eco.broadcast("tempo", [bpm](JsonDocument &d) { d["bpm"] = bpm; });
    ESP_LOGI(TAG, "OSC tempo: %d (broadcast to mesh)", bpm);
}

void osc_tempo_rand_received(OSCMessage &m) {
    if (m.size() == 0) return;
    int v = m.getInt(0);
    g_tempo_rand = (v != 0);
    eco.broadcast("tempoRand", [v](JsonDocument &d) { d["state"] = v; });
    ESP_LOGI(TAG, "OSC tempoRand: %d (broadcast to mesh)", v);
}

void osc_reset_received(OSCMessage & /*m*/) {
    knocker.setPattern(DEFAULT_PATTERN);
    knocker.setTempo(DEFAULT_TEMPO);
    eco.broadcast("reset");
    ESP_LOGI(TAG, "OSC reset: pattern + tempo to defaults (broadcast to mesh)");
}

// === Helpers ===
void set_listen(bool v) {
    g_listen = v;
    if (v) suspendMgr.pokeIdle(scaleByDensity(IDLE_TIMEOUT));  // re-arm idle self-trigger
    else suspendMgr.cancelIdle();              // stop self-triggering when quiet
    ESP_LOGI(TAG, "Listen: %d", v);
}

void set_blinking(bool v) {
    g_blinking = v;
    suspendMgr.setPauseLedEnabled(v);  // pause-LED blink
    if (!v) {                          // turn the low-battery blink off now
        t_low_battery_indication.disable();
        if (!suspendMgr.isPaused()) digitalWrite(LED_PIN, HIGH);  // active-low: off
    } else {
        measure_battery();             // re-evaluate low-battery indicator now
    }
}

void apply_listen(int id, int v) {
    if (id == g_obj_id) set_listen(v != 0);
}

void apply_pattern(int id, const char *pat) {
    if (id != g_obj_id || suspendMgr.isPaused()) return;  // respect pause
    knocker.setPattern(pat);
    knocker.knock();  // play immediately
    ESP_LOGI(TAG, "Pattern set & played: %s", pat);
}

void apply_peck(int id, float freq, float dur_ms, float curve, float amp_norm) {
    if (id != g_obj_id || suspendMgr.isPaused()) return;  // respect pause
    if (dur_ms < 0.0f) dur_ms = 0.0f;
    uint8_t amp =
        clampPeckAmp((int)roundf(constrain(amp_norm, 0.0f, 1.0f) * 255.0f));
    knocker.peck(clampFreq((int)roundf(freq)), (uint32_t)roundf(dur_ms),
                 (float)clampCurve((int)roundf(curve)), amp);
    ESP_LOGI(TAG, "Peck: freq=%.1f dur_ms=%.0f curve=%.1f amp=%u", freq, dur_ms,
             curve, amp);
}

void mutate_pattern(const char *pattern, const char *sender_dna) {
    char mutated_pattern[MAX_PATTERN_LEN];
    memset(mutated_pattern, 0, MAX_PATTERN_LEN);
    mutator.evolve(pattern, sender_dna, mutated_pattern);
    knocker.setPattern(mutated_pattern);
    ESP_LOGI(TAG, "New pattern: %s", knocker.getPattern().c_str());
}

void measure_battery() {
    battery_voltage = analogReadMilliVolts(BAT_PIN) / 500.0;
    ESP_LOGI(TAG, "Battery: %.2f V", battery_voltage);

    // While paused, SuspendManager owns the LED (pause blink) -- don't fight it.
    if (battery_voltage < 3.3f && !suspendMgr.isPaused() && g_blinking) {
        t_low_battery_indication.enable();
    } else {
        t_low_battery_indication.disable();
        if (!suspendMgr.isPaused()) digitalWrite(LED_PIN, HIGH);
    }
}
