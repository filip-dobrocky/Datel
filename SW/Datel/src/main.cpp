#include <Arduino.h>
#include <painlessMesh.h>
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

#ifndef LED_PIN
#define LED_PIN 8
#endif

#define BAT_PIN 3

const char* TAG = "Datel";


// === Globals ===
Knocker knocker(SOL_PIN);
bool knocking = false;

Scheduler scheduler;

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

// === Function Prototypes ===
void suspend(bool state, uint32_t timeout = 0);
void ping();

void send_knocking();
void send_knocked();
void send_battery();

void knocking_received(OSCMessage& m);
void knocked_received(OSCMessage& m);
void pause_received(OSCMessage& m);

void mutate_pattern(const char* pattern, const char* sender_dna);

void measure_battery();

// === Tasks ===
Task t_unsuspend(suspend_timeout_short, TASK_ONCE, []() {
    suspend(false);
}, &scheduler, false);

Task t_idle_timeout(idle_timeout, TASK_ONCE, []() {
    knocker.knock();
}, &scheduler, false);

Task t_measure_battery(60000, TASK_FOREVER, &measure_battery, &scheduler, false);

Task t_low_battery_indication(2000, TASK_FOREVER, []() {
    static bool led_on = false;
    digitalWrite(LED_PIN, led_on ? HIGH : LOW);
    led_on = !led_on;
}, &scheduler, false);

Task t_ping(5000, TASK_FOREVER, &ping, &scheduler, true);

Task t_send_battery(60000, TASK_FOREVER, &send_battery, &scheduler, false);

// === OSC ===
OSC_receive_msg rcv_knocking("/knocking");
OSC_receive_msg rcv_knocked("/knocked");
OSC_receive_msg rcv_pause("/pause");

OSC_send_msg snd_knock("/knocking");
OSC_send_msg snd_knocked("/knocked");

OSC_send_msg snd_ping("/ping");
OSC_send_msg snd_suspended("/suspended");
OSC_send_msg snd_battery("/battery");

// === Setup & Loop ===
void setup() {
    pinMode(LED_PIN, OUTPUT);

    mesh.init(
        WIFI_SSID, WIFI_PASS,
        MESH_PORT,
        #if STATION_ONLY 
        WIFI_STA,
        #else
        WIFI_AP_STA,
        #endif
        MESH_CHANNEL,
        0, MAX_CONN
    );
    
    udp.begin(OSC_REC_PORT);
    
    scheduler.startNow();
    
    // === Knocker ===
    knocker.setTempo(400);
    // knocker.setPattern("x_xx_x_x____x___");
    knocker.setPattern("xxxxxxxx__xx___xxxx__xx__xxx_____xxxx______");
    
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
    
    // === OSC ===
    rcv_knocking.init(knocking_received);
    rcv_knocked.init(knocked_received);
    rcv_pause.init(pause_received);
    
    snd_knock.init(base_address);
    snd_knocked.init(base_address);
    
    snd_ping.init(info_address);
    snd_suspended.init(info_address);
    snd_battery.init(info_address);

    t_idle_timeout.restartDelayed(10000UL);
    t_measure_battery.restartDelayed(15000UL);
    t_send_battery.restartDelayed(20000UL);
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
    
    mesh.update();
    knocker.update();
    scheduler.execute();

    osc_control_loop(udp, base_address, info_address);
}

// === Function Definitions ===

void suspend(bool state, uint32_t timeout) {
    if (suspended != state) {
        suspended = state;
        
        if (suspended) {
            t_unsuspend.restartDelayed(timeout);
        }
        
        snd_suspended.m.add((int)OBJ_ID);
        snd_suspended.m.add((int)suspended);
        snd_suspended.send(udp, IPAddress(255, 255, 255, 255), OSC_SND_PORT);
        ESP_LOGI(TAG, "Suspended: %u", suspended);
    }
}

void ping() {
    ESP_LOGD(TAG, "Ping broadcast");
    
    snd_ping.m.add((int)OBJ_ID);
    snd_ping.send(udp, IPAddress(255, 255, 255, 255), OSC_SND_PORT);
}

void knocking_received(OSCMessage& m) {
    ESP_LOGD(TAG, "Knocking received");
    
    if (m.size() > 0) {
        int obj_id = m.getInt(0);
        ESP_LOGD(TAG, "Object ID: %d", obj_id);
    }
    
    if (m.size() > 2) {
        char pattern[MAX_PATTERN_LEN];
        m.getString(1, pattern, MAX_PATTERN_LEN);
        ESP_LOGD(TAG, "Pattern: %s", pattern);
        
        char sender_dna[MAX_DNA_LEN];
        m.getString(2, sender_dna, MAX_DNA_LEN);
        mutate_pattern(pattern, sender_dna);
    }
    
    suspend(true, suspend_timeout_short);
}

void knocked_received(OSCMessage& m) {
    ESP_LOGD(TAG, "Knocked received");
    
    if (m.size() > 0) {
        int obj_id = m.getInt(0);
        ESP_LOGD(TAG, "Object ID: %d", obj_id);
    }
    
    if (!suspended && !paused) {
        knocker.knock();
    }
}

void pause_received(OSCMessage& m) {
    ESP_LOGD(TAG, "Pause received");
    
    if (m.size() > 0) {
        int pause_state = m.getInt(0);
        paused = (pause_state != 0);
        ESP_LOGI(TAG, "Paused: %u", paused);

        if (paused) {
            t_idle_timeout.disable();
        }
        else {
            t_idle_timeout.restartDelayed(10000UL);
        }
    }
}

void send_knocking() {
    snd_knock.m.add((int)OBJ_ID);
    snd_knock.m.add(knocker.getPattern().c_str());
    snd_knock.m.add(mutator.get_dna());
    snd_knock.send(udp, IPAddress(255, 255, 255, 255), OSC_REC_PORT);
    ESP_LOGD(TAG, "Sent knocking");
}

void send_knocked() {
    snd_knocked.m.add((int)OBJ_ID);
    snd_knocked.send(udp, IPAddress(255, 255, 255, 255), OSC_REC_PORT);
    ESP_LOGD(TAG, "Sent knocked");
}

void send_battery() {
    snd_battery.m.add((int)OBJ_ID);
    snd_battery.m.add(battery_voltage);
    snd_battery.send(udp, IPAddress(255, 255, 255, 255), OSC_SND_PORT);
    ESP_LOGD(TAG, "Sent battery: %.2f V", battery_voltage);
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