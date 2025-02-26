#include <Arduino.h>
#include <FastLED.h>

#define CLK 16
#define DT 17
#define SW 15
#define DEBOUNCE_MS 500

#define LED_PIN 13
#define NUM_LEDS 50
#define BRIGHTNESS 100
#define LED_TYPE WS2811
#define COLOR_ORDER GRB
CRGB leds[NUM_LEDS];
int using_red = 1;


volatile int16_t inputDelta = 0;  // Updated inside ISR
volatile bool eventFlag = false;  // Indicates if new data is available

volatile bool clickFlag = false;          // Single click detected
volatile bool longPressFlag = false;      // Long press detected
volatile bool veryLongPressFlag = false;  // Very long press detected
volatile uint32_t buttonPressTime = 0;
volatile bool buttonPressed = false;

int16_t inputDelta_copy = 0;
bool clickFlag_copy = false;
bool longPressFlag_copy = false;
bool veryLongPressFlag_copy = false;
uint8_t inputDelta_remainder = 0;

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
  eventFlag = true;
}

void IRAM_ATTR buttonISR() {
  static uint32_t lastInterruptTime = 0;
  uint32_t currentTime = millis();

  if (currentTime - lastInterruptTime > 50) {  // Debounce with 50ms delay
    if (!digitalRead(SW)) {                    // Button pressed
      buttonPressTime = currentTime;
      buttonPressed = true;
    } else {  // Button released
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


void setup() {
  Serial.begin(115200);
  pinMode(CLK, INPUT_PULLUP);
  pinMode(DT, INPUT_PULLUP);
  pinMode(SW, INPUT_PULLUP);

  FastLED.addLeds<WS2812B, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  Serial.println("Hello");

  attachInterrupt(digitalPinToInterrupt(CLK), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(DT), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(SW), buttonISR, CHANGE);
}

int i = 0;
void loop() {
  noInterrupts();
  clickFlag_copy = clickFlag;
  longPressFlag_copy = longPressFlag;
  veryLongPressFlag_copy = veryLongPressFlag;
  clickFlag = false;
  longPressFlag = false;
  veryLongPressFlag = false;
  interrupts();

  if (clickFlag_copy) {
    Serial.println("Click!");
    using_red = 1 - using_red;
  }
  if (longPressFlag_copy) {
    Serial.println("Long click!");
  }
  if (veryLongPressFlag_copy) {
    Serial.println("Very Long click!");
  }

  if (eventFlag || clickFlag_copy || longPressFlag_copy || veryLongPressFlag_copy) {
    eventFlag = false;

    noInterrupts();
    inputDelta = constrain(inputDelta, 0, NUM_LEDS * 4 - 1);
    inputDelta_copy = inputDelta;
    interrupts();

    inputDelta_remainder = inputDelta_copy % 4;
    inputDelta_copy = std::round(inputDelta_copy / 4);
    Serial.println(inputDelta_copy);

    // leds[i] = 0xFF44DD;
    // leds[i].setRGB( 255, 68, 221);

    for (int i = 0; i < inputDelta_copy; i++) {
      leds[(i + 25) % NUM_LEDS].setRGB(255 * using_red, 255 * (1 - using_red), 0);
    }

    // Set fade brightness if transitioning
    leds[(inputDelta_copy + 25) % NUM_LEDS].setRGB(255 * using_red * inputDelta_remainder / 4, 255 * (1 - using_red) * inputDelta_remainder / 4, 0);

    for (int i = inputDelta_copy + 1; i < NUM_LEDS; i++) {
      leds[(i + 25) % NUM_LEDS].setRGB(0, 0, 0);
    }
    FastLED.show();
  }
  delay(10);
}
