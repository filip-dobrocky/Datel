#include <Arduino.h>
#include <painlessMesh.h>
#include <ArduinoJson.h>
#include "osc_control.h"

#include "Knocker.h"
#include "network_config.h"

#include "Mutator.h"

#ifndef WIFI_SSID
#include "secrets.h"
#endif

#ifndef SOL_PIN
#define SOL_PIN 0
#endif

#ifndef OBJ_ID
#define OBJ_ID 0
#endif

#if OBJ_ID == 0
#define AP_MODE true
#endif

#ifndef LED_PIN
#define LED_PIN 8
#endif

#define BAT_PIN 3

const char* TAG = "Datel";


// === Globals ===
Knocker knocker(SOL_PIN);
bool knocking = false;

Scheduler userScheduler; // painlessMesh uses this

Mutator mutator(OBJ_ID);

painlessMesh mesh;
WiFiUDP udp;

const char *base_address = "/datel";
const char *info_address = "/info";

bool suspended = false;

bool paused = false;

float battery_voltage = 0.0f;

const uint32_t suspend_timeout_long = 15000;
const uint32_t suspend_timeout_short = 2000;

const uint32_t idle_timeout = 45000;

// Check if this node is the root/gateway (connected to external controller)
bool isRoot = false;

// === Function Prototypes ===
void suspend(bool state, uint32_t timeout = 0);
void ping();

void send_knocking();
void send_knocked();
void send_battery();
void send_suspended();
void send_ping();
void send_velocity(float norm_value);

void mesh_knocking_received(JsonDocument& doc);
void mesh_knocked_received(JsonDocument& doc);
void mesh_pause_received(JsonDocument& doc);
void mesh_ping_received(JsonDocument& doc, uint32_t from);
void mesh_suspended_received(JsonDocument& doc, uint32_t from);
void mesh_battery_received(JsonDocument& doc, uint32_t from);
void mesh_velocity_received(JsonDocument& doc);

void osc_pause_received(OSCMessage& m);
void osc_velocity_received(OSCMessage& m);

void mutate_pattern(const char* pattern, const char* sender_dna);

void measure_battery();

// Mesh callbacks
void receivedCallback(uint32_t from, String &msg);
void newConnectionCallback(uint32_t nodeId);
void changedConnectionCallback();
void nodeTimeAdjustedCallback(int32_t offset);

// === Tasks ===
Task t_unsuspend(suspend_timeout_short, TASK_ONCE, []() {
    suspend(false);
}, &userScheduler, false);

Task t_idle_timeout(idle_timeout, TASK_ONCE, []() {
    knocker.knock();
}, &userScheduler, false);

Task t_measure_battery(60000, TASK_FOREVER, &measure_battery, &userScheduler, false);

Task t_low_battery_indication(2000, TASK_FOREVER, []() {
    static bool led_on = false;
    digitalWrite(LED_PIN, led_on ? HIGH : LOW);
    led_on = !led_on;
}, &userScheduler, false);

Task t_ping(5000, TASK_FOREVER, &send_ping, &userScheduler, true);

Task t_send_battery(60000, TASK_FOREVER, &send_battery, &userScheduler, false);

// === OSC === (only for pause from external controller)
OSC_receive_msg rcv_pause("/pause");
OSC_receive_msg rcv_velocity("/velocity");

OSC_send_msg snd_ping("/ping");
OSC_send_msg snd_suspended("/suspended");
OSC_send_msg snd_battery("/battery");

// === Setup & Loop ===
void setup() {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    // Initialize painlessMesh
    mesh.setDebugMsgTypes(ERROR | STARTUP | CONNECTION);
    mesh.init(
        WIFI_SSID, WIFI_PASS,
        &userScheduler,
        MESH_PORT,
#if STATION_ONLY 
        WIFI_STA,
#else
#if AP_MODE
        WIFI_AP,
#else
        WIFI_AP_STA,
#endif
#endif
        MESH_CHANNEL,
    (OBJ_ID == 0 ? 0 : 1), MAX_CONN
    );
    
    // Set mesh callbacks
    mesh.onReceive(&receivedCallback);
    mesh.onNewConnection(&newConnectionCallback);
    mesh.onChangedConnections(&changedConnectionCallback);
    mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);
    
    // Determine if this is the root node based on OBJ_ID
    // Root node (ID 0) connects to external OSC controller
    isRoot = (OBJ_ID == 0);
    
    // All nodes start UDP for OSC (so external device can connect to any node)
    udp.begin(OSC_REC_PORT);
    rcv_pause.init(osc_pause_received);
    rcv_velocity.init(osc_velocity_received);
    
    // Initialize OSC send messages on all nodes (any node might need to send to controller)
    snd_ping.init(info_address);
    snd_suspended.init(info_address);
    snd_battery.init(info_address);
    
    if (isRoot) {
        mesh.setRoot(true);
        ESP_LOGI(TAG, "Running as ROOT/GATEWAY node (OBJ_ID=%d)", OBJ_ID);
    } else {
        mesh.setContainsRoot(true);  // Tell this node the mesh contains a root
        ESP_LOGI(TAG, "Running as MESH node (OBJ_ID=%d) - OSC enabled", OBJ_ID);
    }

    // === Knocker ===
    knocker.setTempo(300);
    knocker.setPattern("xxxx____xxx___x___");
    //knocker.setPattern("xxxxxxxx__xx___xxxx__xx__xxx_____xxxx______");
    
    knocker.setOnStarted([]() {
        ESP_LOGI(TAG, "Knocking started");
        
        send_knocking();
        
        knocking = true;
    });
    
    knocker.setOnFinished([]() {
        ESP_LOGI(TAG, "Knocking finished");
        
        t_idle_timeout.restartDelayed(45000UL);
        suspend(true, suspend_timeout_long);
        
        send_knocked();
        
        knocking = false;
    });
    
    // Add tasks to scheduler
    userScheduler.addTask(t_unsuspend);
    userScheduler.addTask(t_idle_timeout);
    userScheduler.addTask(t_measure_battery);
    userScheduler.addTask(t_low_battery_indication);
    userScheduler.addTask(t_ping);
    userScheduler.addTask(t_send_battery);
    
    t_idle_timeout.restartDelayed(10000UL);
    t_measure_battery.restartDelayed(15000UL);
    t_send_battery.restartDelayed(20000UL);
    
    ESP_LOGI(TAG, "Node ID: %u, OBJ_ID: %d, isRoot: %d", mesh.getNodeId(), OBJ_ID, isRoot);
}

void loop() {  
    #if TEST_PATTERN
    static uint16_t velocity = 230;
    if (!knocking) {
        
        knocker.setVelocity(velocity);
        velocity += 5;
        if (velocity > 255) {
            velocity = 230;
        }
        
        ESP_LOGI(TAG, "Velocity: %u", velocity);
        
        knocker.knock();
        ESP_LOGI(TAG, "Knock!");
    }
    #endif
    
    mesh.update();  // This calls userScheduler.execute() internally
    knocker.update();

    // All nodes handle OSC (so external device can connect to any node)
    osc_control_loop(udp, base_address, info_address);
}

// === Function Definitions ===

// === Mesh Message Handlers ===
void receivedCallback(uint32_t from, String &msg) {
    ESP_LOGD(TAG, "Received from %u: %s", from, msg.c_str());
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, msg);
    
    if (error) {
        ESP_LOGE(TAG, "JSON parse error: %s", error.c_str());
        return;
    }
    
    const char* type = doc["type"];
    if (!type) {
        ESP_LOGE(TAG, "No message type");
        return;
    }
    
    // Route messages based on type
    if (strcmp(type, "knocking") == 0) {
        mesh_knocking_received(doc);
    } else if (strcmp(type, "knocked") == 0) {
        mesh_knocked_received(doc);
    } else if (strcmp(type, "pause") == 0) {
        mesh_pause_received(doc);
    } else if (strcmp(type, "ping") == 0) {
        mesh_ping_received(doc, from);
    } else if (strcmp(type, "suspended") == 0) {
        mesh_suspended_received(doc, from);
    } else if (strcmp(type, "battery") == 0) {
        mesh_battery_received(doc, from);
    } else if (strcmp(type, "velocity") == 0) {
        mesh_velocity_received(doc);
    } else {
        ESP_LOGW(TAG, "Unknown message type: %s", type);
    }
}

void newConnectionCallback(uint32_t nodeId) {
    ESP_LOGI(TAG, "New Connection, nodeId = %u", nodeId);
}

void changedConnectionCallback() {
    ESP_LOGI(TAG, "Changed connections");
    auto nodes = mesh.getNodeList();
    ESP_LOGI(TAG, "Num nodes: %d", nodes.size());
}

void nodeTimeAdjustedCallback(int32_t offset) {
    ESP_LOGD(TAG, "Adjusted time %u. Offset = %d", mesh.getNodeTime(), offset);
}

// === Send Functions (Mesh) ===
void send_knocking() {
    JsonDocument doc;
    doc["type"] = "knocking";
    doc["id"] = OBJ_ID;
    doc["pattern"] = knocker.getPattern();
    doc["dna"] = mutator.get_dna();
    doc["tempo"] = knocker.getTempo();
    
    String msg;
    serializeJson(doc, msg);
    mesh.sendBroadcast(msg);
    ESP_LOGD(TAG, "Sent knocking via mesh (tempo: %d)", knocker.getTempo());
}

void send_knocked() {
    JsonDocument doc;
    doc["type"] = "knocked";
    doc["id"] = OBJ_ID;
    
    String msg;
    serializeJson(doc, msg);
    mesh.sendBroadcast(msg);
    ESP_LOGD(TAG, "Sent knocked via mesh");
}

void send_ping() {
    JsonDocument doc;
    doc["type"] = "ping";
    doc["id"] = OBJ_ID;
    
    String msg;
    serializeJson(doc, msg);

    // Always send to mesh
    mesh.sendBroadcast(msg);

    // Only root sends directly via OSC to the controller
    if (isRoot) {
        snd_ping.m.add((int)OBJ_ID);
        snd_ping.send(udp, IPAddress(255, 255, 255, 255), OSC_SND_PORT);
        ESP_LOGD(TAG, "Sent ping via OSC (root)");
    } else {
        ESP_LOGD(TAG, "Sent ping via mesh (non-root)");
    }
}

void send_battery() {
    JsonDocument doc;
    doc["type"] = "battery";
    doc["id"] = OBJ_ID;
    doc["voltage"] = battery_voltage;
    
    String msg;
    serializeJson(doc, msg);

    // Always send to mesh
    mesh.sendBroadcast(msg);

    // Only root sends directly via OSC to the controller
    if (isRoot) {
        snd_battery.m.add((int)OBJ_ID);
        snd_battery.m.add(battery_voltage);
        snd_battery.send(udp, IPAddress(255, 255, 255, 255), OSC_SND_PORT);
        ESP_LOGD(TAG, "Sent battery via OSC (root): %.2f V", battery_voltage);
    } else {
        ESP_LOGD(TAG, "Sent battery via mesh (non-root): %.2f V", battery_voltage);
    }
}

void send_suspended() {
    JsonDocument doc;
    doc["type"] = "suspended";
    doc["id"] = OBJ_ID;
    doc["state"] = (int)suspended;
    
    String msg;
    serializeJson(doc, msg);

    // Always send to mesh
    mesh.sendBroadcast(msg);

    // Only root sends directly via OSC to the controller
    if (isRoot) {
        snd_suspended.m.add((int)OBJ_ID);
        snd_suspended.m.add((int)suspended);
        snd_suspended.send(udp, IPAddress(255, 255, 255, 255), OSC_SND_PORT);
        ESP_LOGD(TAG, "Sent suspended via OSC (root)");
    } else {
        ESP_LOGD(TAG, "Sent suspended via mesh (non-root)");
    }
}

// === Receive Functions (Mesh) ===
void mesh_knocking_received(JsonDocument& doc) {
    int obj_id = doc["id"] | -1;
    ESP_LOGD(TAG, "Mesh knocking received from obj_id: %d", obj_id);
    
    const char* pattern = doc["pattern"];
    const char* sender_dna = doc["dna"];
    int sender_tempo = doc["tempo"] | 400;  // Default to 400 if not present
    
    if (pattern && sender_dna) {
        ESP_LOGD(TAG, "Pattern: %s, DNA: %s, Tempo: %d", pattern, sender_dna, sender_tempo);
        mutate_pattern(pattern, sender_dna);

        // Randomly adjust tempo by ±10 to ±50
        int tempo_change = random(1, 6) * 10;  // 10, 20, 30, 40, 50
        if (random(0, 2) == 0) {
            tempo_change = -tempo_change;  // 50% chance to decrease
        }
        
        int new_tempo = sender_tempo + tempo_change;
        
        // Clamp tempo between 120 and 400
        if (new_tempo < 120) {
            new_tempo = 120;
        } else if (new_tempo > 400) {
            new_tempo = 400;
        }
        
        knocker.setTempo(new_tempo);
        ESP_LOGI(TAG, "Tempo adjusted: %d -> %d (change: %+d)", sender_tempo, new_tempo, tempo_change);
    }
    
    suspend(true, suspend_timeout_short);
}

void mesh_knocked_received(JsonDocument& doc) {
    int obj_id = doc["id"] | -1;
    ESP_LOGD(TAG, "Mesh knocked received from obj_id: %d", obj_id);
    
    if (!suspended && !paused) {
        knocker.knock();
    }
}

void mesh_pause_received(JsonDocument& doc) {
    int pause_state = doc["state"] | 0;
    paused = (pause_state != 0);
    ESP_LOGI(TAG, "Mesh pause received: %u", paused);

    if (paused) {
        t_idle_timeout.disable();
    } else {
        t_idle_timeout.restartDelayed(10000UL);
    }
}

void mesh_ping_received(JsonDocument& doc, uint32_t from) {
    int obj_id = doc["id"] | -1;
    if (isRoot) {
        // Forward to OSC controller
        snd_ping.m.add(obj_id);
        snd_ping.send(udp, IPAddress(255, 255, 255, 255), OSC_SND_PORT);
        ESP_LOGD(TAG, "Forwarded ping from node %u (obj_id %d) to OSC", from, obj_id);
    } else {
        ESP_LOGD(TAG, "Mesh ping received (non-root) from %u (obj_id %d)", from, obj_id);
    }
}

void mesh_suspended_received(JsonDocument& doc, uint32_t from) {
    int obj_id = doc["id"] | -1;
    int state = doc["state"] | 0;
    if (isRoot) {
        snd_suspended.m.add(obj_id);
        snd_suspended.m.add(state);
        snd_suspended.send(udp, IPAddress(255, 255, 255, 255), OSC_SND_PORT);
        ESP_LOGD(TAG, "Forwarded suspended from node %u (obj_id %d) to OSC", from, obj_id);
    } else {
        ESP_LOGD(TAG, "Mesh suspended received (non-root) from %u (obj_id %d, state %d)", from, obj_id, state);
    }
}

void mesh_battery_received(JsonDocument& doc, uint32_t from) {
    int obj_id = doc["id"] | -1;
    float voltage = doc["voltage"] | 0.0f;
    if (isRoot) {
        snd_battery.m.add(obj_id);
        snd_battery.m.add(voltage);
        snd_battery.send(udp, IPAddress(255, 255, 255, 255), OSC_SND_PORT);
        ESP_LOGD(TAG, "Forwarded battery from node %u (obj_id %d, %.2fV) to OSC", from, obj_id, voltage);
    } else {
        ESP_LOGD(TAG, "Mesh battery received (non-root) from node %u (obj_id %d, %.2fV)", from, obj_id, voltage);
    }
}

void mesh_velocity_received(JsonDocument& doc) {
    // Normalize and map to 0..255
    float norm = doc["value"] | 0.0f;
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;
    uint8_t vel = (uint8_t)roundf(norm * 255.0f);
    knocker.setVelocity(vel);
    ESP_LOGI(TAG, "Mesh velocity received: norm=%.3f -> %u", norm, vel);
}

// === OSC Receive (Any node can receive, will broadcast to mesh) ===
void osc_pause_received(OSCMessage& m) {
    ESP_LOGI(TAG, "OSC pause received on node (OBJ_ID=%d, isRoot=%d)", OBJ_ID, isRoot);
    
    if (m.size() > 0) {
        int pause_state = m.getInt(0);
        paused = (pause_state != 0);
        ESP_LOGI(TAG, "Paused: %u - broadcasting to mesh", paused);

        // Broadcast pause to all mesh nodes (regardless of which node received OSC)
        JsonDocument doc;
        doc["type"] = "pause";
        doc["state"] = pause_state;
        
        String msg;
        serializeJson(doc, msg);
        mesh.sendBroadcast(msg);
        ESP_LOGD(TAG, "Broadcasted pause to mesh");

        if (paused) {
            t_idle_timeout.disable();
        } else {
            t_idle_timeout.restartDelayed(10000UL);
        }
    }
}

void osc_velocity_received(OSCMessage& m) {
    ESP_LOGI(TAG, "OSC velocity received on node (OBJ_ID=%d)", OBJ_ID);
    if (m.size() > 0) {
        float norm = 0.0f;
        // Accept float or int
        #ifdef OSCMessage_h
        #endif
        // CNMAT OSCMessage supports getFloat/getInt
        // Try to read as float first
        norm = m.getFloat(0);
        // If the sender used int, map accordingly (0..1 or 0..255)
        if (isnan(norm)) {
            int v = m.getInt(0);
            if (v > 1) {
                norm = constrain((float)v / 255.0f, 0.0f, 1.0f);
            } else {
                norm = constrain((float)v, 0.0f, 1.0f);
            }
        }

        if (norm < 0.0f) norm = 0.0f;
        if (norm > 1.0f) norm = 1.0f;
        uint8_t vel = (uint8_t)roundf(norm * 255.0f);
        knocker.setVelocity(vel);
        ESP_LOGI(TAG, "Set velocity from OSC: norm=%.3f -> %u", norm, vel);

        // Distribute to mesh so all nodes update
        send_velocity(norm);
    }
}

void send_velocity(float norm_value) {
    // Clamp
    if (norm_value < 0.0f) norm_value = 0.0f;
    if (norm_value > 1.0f) norm_value = 1.0f;

    JsonDocument doc;
    doc["type"] = "velocity";
    doc["id"] = OBJ_ID;
    doc["value"] = norm_value;

    String msg;
    serializeJson(doc, msg);
    mesh.sendBroadcast(msg);
    ESP_LOGD(TAG, "Sent velocity via mesh: norm=%.3f", norm_value);
}

void suspend(bool state, uint32_t timeout) {
    if (suspended != state) {
        suspended = state;
        
        if (suspended) {
            t_unsuspend.restartDelayed(timeout);
        }
        
        send_suspended();
        ESP_LOGI(TAG, "Suspended: %u", suspended);
    }
}

void mutate_pattern(const char* pattern, const char* sender_dna) {
    char mutated_pattern[MAX_PATTERN_LEN];
    memset(mutated_pattern, 0, MAX_PATTERN_LEN);
    
    mutator.evolve(pattern, sender_dna, mutated_pattern);
    
    knocker.setPattern(mutated_pattern);
    ESP_LOGI(TAG, "New pattern: %s", knocker.getPattern().c_str());
}

void measure_battery() {
    battery_voltage = analogReadMilliVolts(BAT_PIN) / 500.0 ;
    ESP_LOGI(TAG, "Battery: %.2f V", battery_voltage);

    if (battery_voltage < 3.3f) {
        t_low_battery_indication.enable();
    } else {
        t_low_battery_indication.disable();
        digitalWrite(LED_PIN, HIGH);
    }
}