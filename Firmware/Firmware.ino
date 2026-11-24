#include <Arduino.h>
#include <Joystick.h>

// Pin mapping transcribed from the supplied Game controller schematic.
// Rows are the diode/cathode side of the key matrix. Columns are the switch side.
constexpr uint8_t ROW_PINS[3] = {0, 1, 2};       // ROW1, ROW2, ROW3 -> GPIO0, GPIO1, GPIO2
constexpr uint8_t COLUMN_PINS[3] = {3, 4, 5};    // COLUMN1..3 -> GPIO3, GPIO4, GPIO5

// Index into the USB button list (0 means HID button 1).
// Matrix order is [column][row], based on SW1..SW9 positions in the schematic.
// COLUMN1: SW1, SW6, SW7; COLUMN2: SW2, SW5, SW8; COLUMN3: SW3, SW4, SW9.
constexpr uint8_t SWITCH_INDEX[3][3] = {
  {0, 5, 6},  // COLUMN1: ROW1=SW1, ROW2=SW6, ROW3=SW7
  {1, 4, 7},  // COLUMN2: ROW1=SW2, ROW2=SW5, ROW3=SW8
  {2, 3, 8}   // COLUMN3: ROW1=SW3, ROW2=SW4, ROW3=SW9
};

// Press indicator LEDs: SW1..SW9 map to LED1, LED2, LED3, LED5..LED10.
// LED4 is connected to USB +5 V in hardware and needs no GPIO control.
constexpr uint8_t BUTTON_LED_PINS[9] = {6, 7, 8, 10, 9, 12, 13, 11, 14};
constexpr uint8_t ACTIVITY_LED_PIN = 15; // LED11: on while any button is pressed

constexpr uint32_t DEBOUNCE_MS = 8;

uint16_t scanSwitchMatrix() {
  uint16_t pressed = 0;

  // Idle all columns low. The schematic's 1N4148 diodes point from columns
  // toward rows, so a pressed key passes a selected column's HIGH to a row.
  for (uint8_t col = 0; col < 3; ++col) {
    digitalWrite(COLUMN_PINS[col], LOW);
  }

  for (uint8_t col = 0; col < 3; ++col) {
    digitalWrite(COLUMN_PINS[col], HIGH);
    delayMicroseconds(5); // allow the row input to settle

    for (uint8_t row = 0; row < 3; ++row) {
      if (digitalRead(ROW_PINS[row]) == HIGH) {
        const uint8_t switchIndex = SWITCH_INDEX[col][row];
        pressed |= static_cast<uint16_t>(1u << switchIndex);
      }
    }

    digitalWrite(COLUMN_PINS[col], LOW);
  }

  return pressed;
}

void applyControllerState(uint16_t buttons) {
  for (uint8_t i = 0; i < 9; ++i) {
    const bool down = (buttons & (1u << i)) != 0;
    Joystick.button(i + 1, down); // USB HID buttons 1..9 correspond to SW1..SW9
    digitalWrite(BUTTON_LED_PINS[i], down ? HIGH : LOW);
  }

  digitalWrite(ACTIVITY_LED_PIN, buttons ? HIGH : LOW);
  Joystick.send_now();
}

void setup() {
  // Rows have no external pull resistors in the schematic, so use RP2350
  // internal pull-downs. A pressed key drives its row high through its diode.
  for (uint8_t row = 0; row < 3; ++row) {
    pinMode(ROW_PINS[row], INPUT_PULLDOWN);
  }

  for (uint8_t col = 0; col < 3; ++col) {
    pinMode(COLUMN_PINS[col], OUTPUT);
    digitalWrite(COLUMN_PINS[col], LOW);
  }

  for (uint8_t i = 0; i < 9; ++i) {
    pinMode(BUTTON_LED_PINS[i], OUTPUT);
    digitalWrite(BUTTON_LED_PINS[i], LOW);
  }
  pinMode(ACTIVITY_LED_PIN, OUTPUT);
  digitalWrite(ACTIVITY_LED_PIN, LOW);

  // Arduino-Pico's built-in USB stack exposes the Joystick library as a USB
  // HID controller. Keep axes centered because this schematic has no sticks.
  Joystick.begin();
  Joystick.useManualSend(true);
  Joystick.use10bit();
  Joystick.X(512);
  Joystick.Y(512);
  Joystick.Z(512);
  Joystick.Zrotate(512);
  Joystick.sliderLeft(512);
  Joystick.sliderRight(512);
  Joystick.hat(-1); // centered / no D-pad direction
  for (uint8_t button = 1; button <= 9; ++button) {
    Joystick.button(button, false);
  }
  Joystick.send_now();
}

void loop() {
  static uint16_t candidateState = 0;
  static uint16_t stableState = 0;
  static uint32_t candidateSince = 0;

  const uint32_t now = millis();
  const uint16_t rawState = scanSwitchMatrix();

  if (rawState != candidateState) {
    candidateState = rawState;
    candidateSince = now;
  }

  if ((candidateState != stableState) && (now - candidateSince >= DEBOUNCE_MS)) {
    stableState = candidateState;
    applyControllerState(stableState);
  }
}
