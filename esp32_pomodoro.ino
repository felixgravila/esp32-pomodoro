#include <Arduino.h>
#include <FastLED.h>
#include <Preferences.h>

Preferences prefs;

#define REFRESH_RATE_MS 10
#define SPEEDUP_FACTOR 100  // For debugging
#define PULSE_PERIOD_MS 5000
#define TIME_TO_SLEEP_MS 60 * 60 * 1000  // 1h

#define CLK 16
#define DT 17
#define SW 15
#define DEBOUNCE_MS 50

#define LED_PIN 13
#define NUM_LEDS 50
#define LED_TYPE WS2811
#define COLOR_ORDER GRB
CRGB abstracted_leds[NUM_LEDS];
CRGB leds[NUM_LEDS];

int global_brightness = 100;

bool paused = true;                  // if time is currently stopped;
bool sleeping = true;                // low light, inactive
uint32_t last_event = 0;             // last event, for sleeping
bool debug_mode = false;             // apply SPEEDUP_FACTOR
uint32_t current_set_time_ms = 0;    // 0-1499000 work, 1500000-1799000 pause
uint32_t pulse_time_counter_ms = 0;  // safer than millis() % PULSE_PERIOD_MS

volatile int16_t inputDelta = 0;          // Amount rotenc got turned
volatile bool clickFlag = false;          // Single click detected
volatile bool longPressFlag = false;      // Long press detected
volatile bool veryLongPressFlag = false;  // Very long press detected
volatile uint32_t buttonPressTime = 0;
volatile bool buttonPressed = false;
volatile bool somethingHappened = false;  // Catch all for all ISR
/*
config for parameters
bit 8: blinking top
bit 7: long form break
*/
volatile uint8_t config = 0;

int16_t inputDelta_copy = 0;
bool clickFlag_copy = false;
bool longPressFlag_copy = false;
bool veryLongPressFlag_copy = false;
bool somethingHappened_copy = false;
uint8_t config_copy = 0;

void abstract_leds() {
  // Simplify model assuming idx 0 = minute 1 on clock face
  for (int i = 0; i < NUM_LEDS; i++) {
    abstracted_leds[(i + 24) % NUM_LEDS] = leds[i];
  }
}


void IRAM_ATTR encoderISR() {
  static uint8_t lastState = 0;
  uint8_t state = (digitalRead(CLK) << 1) | digitalRead(DT);
  somethingHappened = true;

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
  somethingHappened = true;

  if (currentTime - lastInterruptTime > DEBOUNCE_MS) {
    // hack with !buttonPressed
    // stupid stuff happening otherwise
    if (!digitalRead(SW) && !buttonPressed) {  // Button pressed
      buttonPressTime = currentTime;
      buttonPressed = true;
    } else {  // Button released
      uint32_t pressDuration = currentTime - buttonPressTime;
      buttonPressed = false;
      if (pressDuration > 2000) {  // Very long press thershold (2s)
        veryLongPressFlag = true;
      } else if (pressDuration > 700) {  // Long press threshold (700ms)
        config = (config + 1) % 4;
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
  prefs.begin("pomodoro", false);  // rw
  if (prefs.isKey("brightness")) {
    global_brightness = prefs.getInt("brightness");
    config = prefs.getInt("config");
    config_copy = prefs.getInt("config");
  } else {
    prefs.putInt("brightness", global_brightness);
    prefs.putInt("config", config);
  }
  prefs.end();

  pinMode(CLK, INPUT_PULLUP);
  pinMode(DT, INPUT_PULLUP);
  pinMode(SW, INPUT_PULLUP);

  FastLED.addLeds<WS2812B, LED_PIN, COLOR_ORDER>(abstracted_leds, NUM_LEDS);
  FastLED.setBrightness(global_brightness);
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

  somethingHappened_copy = somethingHappened;
  somethingHappened = false;

  if (config_copy != config) {
    config_copy = config;
    prefs.begin("pomodoro", false);
    prefs.putInt("config", config_copy);
    prefs.end();
  }
  interrupts();

  // Get config flags from config bits
  bool blinkWhileRunning = config_copy & 1;
  bool useLongFormBreak = (config_copy >> 1) & 1;

  if (clickFlag_copy) {
    Serial.println("Clicked!");
    paused = !paused;
  }

  // Handle wake up from sleep
  // and remember last event, also for sleep
  if (somethingHappened_copy) {
    last_event = millis();
    sleeping = false;
  }
  if (millis() - last_event >= TIME_TO_SLEEP_MS) {
    sleeping = true;
  }

  if (veryLongPressFlag_copy) {
    Serial.println("Very long click! Resetting and sleeping.");
    sleeping = true;
    paused = true;
    current_set_time_ms = 0;
  }

  // Handle pulsing and blinking
  uint8_t blink_addition = 0;
  float pulseModifier = 1.0;
  if (paused) {
    float pulseModifier0to1 = (1 - cos(pulse_time_counter_ms * PI * 2 / PULSE_PERIOD_MS)) / 2;
    pulseModifier = 0.1 + pulseModifier0to1 * 0.9;
  } else if (blinkWhileRunning) {
    float blinkModifier0to1 = (1 - cos((millis() % 1000) * PI * 2 / 1000)) / 2;
    blink_addition = round(blinkModifier0to1 * 40);
  }

  // Handle sleeping
  float sleepModifier = 1.0;
  if (sleeping) {
    sleepModifier = 0.1;
  }

  // Handle rotenc
  if (inputDelta_copy != 0) {
    if (paused) {
      // Moving time
      int to_add_value = inputDelta_copy * 10000;
      if (current_set_time_ms < -to_add_value) {
        // avoid underflow
        current_set_time_ms += 1800000;
      }
      current_set_time_ms = current_set_time_ms + to_add_value;
      current_set_time_ms = current_set_time_ms % 1800000;
    } else {
      // Adjusting brightness
      global_brightness += inputDelta_copy;
      global_brightness = constrain(global_brightness, 0, 255);
      prefs.begin("pomodoro", false);
      prefs.putInt("brightness", global_brightness);
      prefs.end();
    }
  }

  if (current_set_time_ms < 1500000) {
    // working
    setLedValueForTime(current_set_time_ms, 255, 0, 0, pulseModifier * sleepModifier);
    leds[0].setRGB(255 * pulseModifier * sleepModifier, blink_addition, blink_addition);             // blink
    leds[NUM_LEDS - 1].setRGB(255 * pulseModifier * sleepModifier, blink_addition, blink_addition);  // blink
  } else {
    int value = current_set_time_ms - 300000;  // 1200000-1499000
    if (useLongFormBreak) {
      value = (value - 1200000) * 5;
      setLedValueForTime(value, 0, 180, 0, pulseModifier * sleepModifier);
      leds[0].setRGB(blink_addition, 180 * pulseModifier * sleepModifier, blink_addition);             // blink
      leds[NUM_LEDS - 1].setRGB(blink_addition, 180 * pulseModifier * sleepModifier, blink_addition);  // blink
    } else {
      setLedValueForTime(value, 0, 180, 20, pulseModifier * sleepModifier);
      leds[0].setRGB(blink_addition, 180 * pulseModifier * sleepModifier, blink_addition);             // blink
      leds[NUM_LEDS - 1].setRGB(blink_addition, 180 * pulseModifier * sleepModifier, blink_addition);  // blink
    }
  }

  // Add time if not paused
  if (!paused) {
    uint16_t time_passed_to_add = REFRESH_RATE_MS;
    if (debug_mode) {
      time_passed_to_add *= SPEEDUP_FACTOR;
    }
    current_set_time_ms = current_set_time_ms + time_passed_to_add;
    if (current_set_time_ms >= 1800000) {
      // Done with the break. Reset and pause.
      current_set_time_ms = 0;
      paused = true;
    } else if (current_set_time_ms >= 1500000 && current_set_time_ms < 1500000 + time_passed_to_add) {
      // Done with work. Set max break and pause.
      current_set_time_ms = 1500000;
      paused = true;
    }

    current_set_time_ms = current_set_time_ms % 1800000;
  }
  pulse_time_counter_ms = (pulse_time_counter_ms + REFRESH_RATE_MS) % PULSE_PERIOD_MS;

  FastLED.setBrightness(global_brightness);
  abstract_leds();
  FastLED.show();
  delay(REFRESH_RATE_MS);
}
