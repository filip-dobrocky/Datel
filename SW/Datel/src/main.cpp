#include <Arduino.h>
#include <painlessMesh.h>
#include "osc_control.h"

#include "Knocker.h"
#include "network_config.h"

#ifndef WIFI_SSID
#include "secrets.h"
#endif

#ifndef SOL_PIN
#define SOL_PIN 0
#endif

#ifndef OBJ_ID
#define OBJ_ID 0
#endif

const char* TAG = "Datel";


// === Globals ===
Knocker knocker(SOL_PIN);
bool knocking = false;

Scheduler scheduler;

painlessMesh mesh;
WiFiUDP udp;

const char *base_address = "/datel";
const char *info_address = "/info";

bool suspended = false;
const uint32_t suspend_timeout_long = 15000;
const uint32_t suspend_timeout_short = 2000;

const uint32_t idle_timeout = 45000;

// === Function Prototypes ===
void suspend(bool state, uint32_t timeout = 0);
void ping();

void send_knocking();
void send_knocked();

void knocking_received(OSCMessage& m);
void knocked_received(OSCMessage& m);

void mutate_pattern(const char* pattern);

// === Tasks ===
Task t_unsuspend(suspend_timeout_short, TASK_ONCE, []() {
    suspend(false);
}, &scheduler, false);

Task t_idle_timeout(idle_timeout, TASK_ONCE, []() {
  knocker.knock();
}, &scheduler, false);

Task t_ping(5000, TASK_FOREVER, &ping, &scheduler, true);

// === OSC ===
OSC_receive_msg rcv_knocking("/knocking");
OSC_receive_msg rcv_knocked("/knocked");

OSC_send_msg snd_knock("/knocking");
OSC_send_msg snd_knocked("/knocked");

OSC_send_msg snd_ping("/ping");
OSC_send_msg snd_suspended("/suspended");

// === Setup & Loop ===
void setup() {
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
  knocker.setPattern("xxxxxxxx__xx____xxxx__xx__xxx_____xxxxxxx_____________");

  knocker.setOnStarted([]() {
      ESP_LOGI(TAG, "Knocking started");

      send_knocking();

      knocking = true;
  });

  knocker.setOnFinished([]() {
      ESP_LOGI(TAG, "Knocking finished");
      
      t_idle_timeout.restartDelayed();
      suspend(true, suspend_timeout_long);

      send_knocked();

      knocking = false;
  });

  // === OSC ===
  rcv_knocking.init(knocking_received);
  rcv_knocked.init(knocked_received);

  snd_knock.init(base_address);
  snd_knocked.init(base_address);

  snd_ping.init(info_address);
  snd_suspended.init(info_address);
}

void loop() {
  static unsigned long lastMillis = 0;
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

  mesh.update();
  knocker.update();
}

// === Function Definitions ===

void suspend(bool state, uint32_t timeout) {
    if (suspended != state) {
        suspended = state;

        if (suspended) {
          t_unsuspend.restartDelayed(timeout);
        }

        snd_suspended.m.empty();
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

  if (m.size() > 1) {
      const int max_len = 64;
      char pattern[max_len];
      int len = m.getString(1, pattern, max_len);
      ESP_LOGD(TAG, "Pattern: %s", pattern);

      mutate_pattern(pattern);
  }

  suspend(true, suspend_timeout_short);
}

void knocked_received(OSCMessage& m) {
  ESP_LOGD(TAG, "Knocked received");

  if (m.size() > 0) {
      int obj_id = m.getInt(0);
      ESP_LOGD(TAG, "Object ID: %d", obj_id);
  }

  if (!suspended) {
      knocker.knock();
  }
}

void send_knocking() {
    snd_knock.m.empty();
    snd_knock.m.add((int)OBJ_ID);
    snd_knock.m.add(knocker.getPattern().c_str());
    snd_knock.send(udp, IPAddress(255, 255, 255, 255), OSC_SND_PORT);
    ESP_LOGD(TAG, "Sent knocking");
}

void send_knocked() {
    snd_knocked.m.empty();
    snd_knocked.m.add((int)OBJ_ID);
    snd_knocked.send(udp, IPAddress(255, 255, 255, 255), OSC_SND_PORT);
    ESP_LOGD(TAG, "Sent knocked");
}

void mutate_pattern(const char* pattern) {
    String new_pattern = String(pattern);
    if (new_pattern.length() < 1) {
        return;
    }

    // TODO: implement mutation logic

    knocker.setPattern(new_pattern);
    ESP_LOGI(TAG, "New pattern: %s", knocker.getPattern().c_str());
}
