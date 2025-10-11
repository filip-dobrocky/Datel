#include <Arduino.h>
#include <painlessMesh.h>

#include "Knocker.h"

#ifndef SOL_PIN
#define SOL_PIN 0
#endif

const char* TAG = "Datel";

Knocker knocker(SOL_PIN);

bool knocking = false;

// === Function Prototypes ===

// === Setup & Loop ===
void setup() {
  knocker.setTempo(119);
  knocker.setPattern("x_xx_x_x____x___");

  knocker.setOnStarted([]() {
      ESP_LOGI(TAG, "Knocking started");
      knocking = true;
  });

  knocker.setOnFinished([]() {
      ESP_LOGI(TAG, "Knocking finished");
      knocking = false;
  });
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

  knocker.update();
}

// === Function Definitions ===
