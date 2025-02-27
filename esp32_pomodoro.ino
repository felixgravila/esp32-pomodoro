#include <Arduino.h>
#include <FastLED.h>

#define REFRESH_RATE_MS 10
#define SPEEDUP_FACTOR 100  // For debugging
#define PULSE_PERIOD_MS 5000

#define CLK 16
#define DT 17
#define SW 15
#define DEBOUNCE_MS 500

#define LED_PIN 13
#define NUM_LEDS 50
#define BRIGHTNESS 100
#define LED_TYPE WS2811
#define COLOR_ORDER GRB
CRGB abstracted_leds[NUM_LEDS];
CRGB leds[NUM_LEDS];

bool paused = false;                 // if time is currently stopped;
bool useLongFormBreak = false;       // If green should span 360 degrees
uint32_t current_set_time_ms = 0;    // 0-1499000 work, 1500000-1799000 pause
uint32_t pulse_time_counter_ms = 0;  // safer than millis() % PULSE_PERIOD_MS

volatile int16_t inputDelta = 0;          // Amount rotenc got turned
volatile bool clickFlag = false;          // Single click detected
volatile bool longPressFlag = false;      // Long press detected
volatile bool veryLongPressFlag = false;  // Very long press detected
volatile uint32_t buttonPressTime = 0;
volatile bool buttonPressed = false;

int16_t inputDelta_copy = 0;
bool clickFlag_copy = false;
bool longPressFlag_copy = false;
bool veryLongPressFlag_copy = false;

void abstract_leds() {
  // Simplify model assuming idx 0 = minute 1 on clock face
  for (int i = 0; i < NUM_LEDS; i++) {
    abstracted_leds[(i + 24) % NUM_LEDS] = leds[i];
  }
}


void IRAM_ATTR encoderISR() {
  static uint8_t lastState = 0;
  uint8_t state = (digitalRead(CLK) << 1) | digitalRead(DT);

  // Detect direction
  if ((lastState == 0b10 && state == 0b00) || (lastState == 0b00 && state == 0b01) || (lastState == 0b01 && state == 0b11) || (lastState == 0b11 && state == 0b10)) {
    inputDelta++;  // Clockwise
  } else if ((lastState == 0b10 && state == 0b11) || (lastState == 0b11 && state == 0b01) || (lastState == 0b01 && state == 0b00) || (lastState == 0b00 && state == 0b10)) {
    inputDelta--;  // Counterclockwise
  }

  lastState = state;
}

void IRAM_ATTR buttonISR() {
  static uint32_t lastInterruptTime = 0;
  uint32_t currentTime = millis();

  if (currentTime - lastInterruptTime > 50) {  // Debounce with 50ms delay
    if (!digitalRead(SW)) {                    // Button pressed
      Serial.println("Some kind of click.");
      buttonPressTime = currentTime;
      buttonPressed = true;
    } else {  // Button released
      Serial.println("Some kind of unclick.");
      uint32_t pressDuration = currentTime - buttonPressTime;
      buttonPressed = false;
      if (pressDuration > 2000) {  // Very long press thershold (2s)
        veryLongPressFlag = true;
      } else if (pressDuration > 700) {  // Long press threshold (700ms)
        longPressFlag = true;
      } else {
        clickFlag = true;
      }
    }
  }
  lastInterruptTime = currentTime;
}

void setLedValueForTime(int time_s, uint8_t red, uint8_t green, uint8_t blue, float brightness) {
  time_s = constrain(time_s, 0, 1499000);
  brightness = constrain(brightness, 0, 1);
  float threshold = (time_s / 1499000.0) * NUM_LEDS;
  int fullOffCount = floor(threshold);          // Completely off LEDs
  float fade = 1 - (threshold - fullOffCount);  // Fractional part for transition

  for (int i = 0; i < NUM_LEDS; i++) {
    if (i < fullOffCount) {
      leds[i].setRGB(0, 0, 0);
    } else if (i == fullOffCount) {
      leds[i].setRGB(red * brightness * fade, green * brightness * fade, blue * brightness * fade);
    } else {
      leds[i].setRGB(red * brightness, green * brightness, blue * brightness);
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(CLK, INPUT_PULLUP);
  pinMode(DT, INPUT_PULLUP);
  pinMode(SW, INPUT_PULLUP);

  FastLED.addLeds<WS2812B, LED_PIN, COLOR_ORDER>(abstracted_leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  Serial.println("Hello");

  attachInterrupt(digitalPinToInterrupt(CLK), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(DT), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(SW), buttonISR, CHANGE);
}

void loop() {
  // Read volatile flags
  noInterrupts();
  clickFlag_copy = clickFlag;
  longPressFlag_copy = longPressFlag;
  veryLongPressFlag_copy = veryLongPressFlag;
  clickFlag = false;
  longPressFlag = false;
  veryLongPressFlag = false;

  inputDelta_copy = inputDelta;
  inputDelta = 0;
  interrupts();

  if (clickFlag_copy) {
    Serial.println("Clicked!");
    paused = !paused;
  }

  if (longPressFlag_copy) {
    Serial.println("Long click!");
    useLongFormBreak = !useLongFormBreak;
  }

  // Handle pulsing
  float pulseModifier = 1.0;
  if (paused) {
    float pulseModifier0to1 = (1 - cos(pulse_time_counter_ms * PI * 2 / PULSE_PERIOD_MS)) / 2;
    pulseModifier = 0.1 + pulseModifier0to1 * 0.9;
  }

  // Handle manual time editing if paused
  if (paused && inputDelta_copy != 0) {
    int to_add_value = inputDelta_copy * 10000;
    if (current_set_time_ms < -to_add_value ) {
      // avoid underflow
      current_set_time_ms += 1800000;
    }
    current_set_time_ms = current_set_time_ms + to_add_value;
    current_set_time_ms = current_set_time_ms % 1800000;
  }

  if (current_set_time_ms < 1500000) {
    // working
    setLedValueForTime(current_set_time_ms, 255, 0, 0, pulseModifier);
  } else {
    int value = current_set_time_ms - 300000;  // 1200000-1499000
    if (useLongFormBreak) {
      value = (value - 1200000) * 5;
      setLedValueForTime(value, 0, 200, 70, pulseModifier);
    } else {
      setLedValueForTime(value, 0, 255, 0, pulseModifier);
    }
  }
  abstract_leds();
  FastLED.show();

  // Add time if not paused
  if (!paused) {
    uint16_t time_passed_to_add = SPEEDUP_FACTOR * REFRESH_RATE_MS;
    current_set_time_ms = (current_set_time_ms + time_passed_to_add);
    current_set_time_ms = current_set_time_ms % 1800000;
  }
  pulse_time_counter_ms = (pulse_time_counter_ms + REFRESH_RATE_MS) % PULSE_PERIOD_MS;
  delay(REFRESH_RATE_MS);
}
