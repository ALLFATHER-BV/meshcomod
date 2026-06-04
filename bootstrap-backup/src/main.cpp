#include <Arduino.h>

// Minimal bootable firmware scaffold for Heltec V4 Touch repo bootstrap.
// This keeps build/flash flow working while real Meshcomod modules are integrated.
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("meshcomod-heltec-v4-touch bootstrap firmware");
}

void loop() {
  delay(1000);
}
