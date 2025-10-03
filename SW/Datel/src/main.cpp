#include <Arduino.h>
#include <painlessMesh.h>

#include "Knocker.h"

#ifndef SOL_PIN
#define SOL_PIN 0
#endif

const char* TAG = "Datel";

Knocker knocker(SOL_PIN);

// === Function Prototypes ===

// === Setup & Loop ===
void setup() {
}

void loop() {
  static unsigned long lastMillis = 0;
  unsigned long currentMillis = millis();
  if (currentMillis - lastMillis >= 5000) {
      lastMillis = currentMillis;
      knocker.knock();
      ESP_LOGI(TAG, "Knock!");
  }

  knocker.update();
}

// === Function Definitions ===
