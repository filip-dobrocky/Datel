#include <Arduino.h>

#include "EcosystemNode.h"
#include "SuspendManager.h"

#include "Knocker.h"
#include "Mutator.h"

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

const char *TAG = "Datel";

// === Ecosystem node (mesh + OSC) ===
EcosystemConfig datel_cfg{OBJ_ID, ECO_IS_ROOT, "/datel", "/info"};
EcosystemNode eco(datel_cfg);

// === State machine ===
static const uint32_t SUSPEND_LONG = 12000;
static const uint32_t SUSPEND_SHORT = 2000;
static const uint32_t IDLE_TIMEOUT = 35000;
SuspendManager suspendMgr(eco.scheduler(), IDLE_TIMEOUT);

// === Actuator & genetics ===
Knocker knocker(SOL_PIN);
Mutator mutator(OBJ_ID);
bool knocking = false;
float battery_voltage = 0.0f;

// === Function Prototypes ===
void send_knocking();
void send_knocked();
void send_battery();
void send_ping();
void send_velocity(float norm_value);

void osc_pause_received(OSCMessage &m);
void osc_velocity_received(OSCMessage &m);

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

// === Mesh message handlers ===
static void on_knocking(JsonDocument &doc, uint32_t /*from*/) {
    const char *pattern = doc["pattern"];
    const char *sender_dna = doc["dna"];
    int sender_tempo = doc["tempo"] | 400;

    if (pattern && sender_dna) {
        ESP_LOGD(TAG, "Pattern: %s, DNA: %s, Tempo: %d", pattern, sender_dna,
                 sender_tempo);
        mutate_pattern(pattern, sender_dna);

        // Random walk the tempo by +/-10..80, clamped 60..500
        int tempo_change = random(1, 9) * 10;
        if (random(0, 2) == 0) tempo_change = -tempo_change;
        int new_tempo = constrain(sender_tempo + tempo_change, 60, 500);
        knocker.setTempo(new_tempo);
        ESP_LOGI(TAG, "Tempo adjusted: %d -> %d (change: %+d)", sender_tempo,
                 new_tempo, tempo_change);
    }

    suspendMgr.setSuspended(true, SUSPEND_SHORT);
}

static void on_knocked(JsonDocument & /*doc*/, uint32_t /*from*/) {
    if (suspendMgr.active()) knocker.knock();
}

static void on_tweeting(JsonDocument &doc, uint32_t /*from*/) {
    int sender_tempo = doc["tempo"] | 400;

    // birb variant: random subdivision (pattern ignored), random-walk tempo
    int tempo_change = random(1, 9) * 10;
    if (random(0, 2) == 0) tempo_change = -tempo_change;
    int new_tempo = constrain(sender_tempo + tempo_change, 60, 500);
    knocker.setTempo(new_tempo);
    ESP_LOGI(TAG, "Tempo adjusted (tweeting): %d -> %d", sender_tempo, new_tempo);

    suspendMgr.setSuspended(true, SUSPEND_SHORT);
}

static void on_tweeted(JsonDocument & /*doc*/, uint32_t /*from*/) {
    if (suspendMgr.active()) knocker.knock();
}

static void on_pause(JsonDocument &doc, uint32_t /*from*/) {
    int pause_state = doc["state"] | 0;
    suspendMgr.setPaused(pause_state != 0);
    ESP_LOGI(TAG, "Mesh pause received: %d", pause_state);
}

static void on_ping(JsonDocument &doc, uint32_t /*from*/) {
    int obj_id = doc["id"] | -1;
    // Forward to the controller; only the node hosting it actually delivers.
    eco.forwardOsc("/ping", [obj_id](OSCMessage &m) { m.add(obj_id); });
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

// === Setup & Loop ===
void setup() {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

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

    eco.onOsc("/pause", osc_pause_received);
    eco.onOsc("/velocity", osc_velocity_received);

    eco.begin(WIFI_SSID, WIFI_PASS);

    // === State machine hooks ===
    suspendMgr.onSuspendChanged([](bool /*state*/) {
        // Announce suspend state to mesh + controller.
        eco.broadcast("suspended", [](JsonDocument &d) {
            d["state"] = (int)suspendMgr.isSuspended();
        });
        eco.forwardOsc("/suspended", [](OSCMessage &m) {
            m.add((int)OBJ_ID);
            m.add((int)suspendMgr.isSuspended());
        });
        ESP_LOGI(TAG, "Suspended: %u", suspendMgr.isSuspended());
    });
    suspendMgr.onIdle([]() { knocker.knock(); });

    // === Knocker ===
    knocker.setTempo(300);
    knocker.setPattern("xxxx____xxx___x___");

    knocker.setOnStarted([]() {
        ESP_LOGI(TAG, "Knocking started");
        send_knocking();
        knocking = true;
    });

    knocker.setOnFinished([]() {
        ESP_LOGI(TAG, "Knocking finished");
        suspendMgr.pokeIdle(45000UL);
        suspendMgr.setSuspended(true, SUSPEND_LONG);
        send_knocked();
        knocking = false;
    });

    // Stagger the periodic tasks.
    suspendMgr.pokeIdle(10000UL);
    t_measure_battery.restartDelayed(15000UL);
    t_send_battery.restartDelayed(20000UL);

    ESP_LOGI(TAG, "Node ID: %u, OBJ_ID: %d, isRoot: %d", eco.mesh().getNodeId(),
             OBJ_ID, eco.isRoot());
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
    eco.broadcast("ping");
    eco.forwardOsc("/ping", [](OSCMessage &m) { m.add((int)OBJ_ID); });
}

void send_battery() {
    eco.broadcast("battery",
                  [](JsonDocument &d) { d["voltage"] = battery_voltage; });
    eco.forwardOsc("/battery", [](OSCMessage &m) {
        m.add((int)OBJ_ID);
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

// === Helpers ===
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

    if (battery_voltage < 3.3f) {
        t_low_battery_indication.enable();
    } else {
        t_low_battery_indication.disable();
        digitalWrite(LED_PIN, HIGH);
    }
}
