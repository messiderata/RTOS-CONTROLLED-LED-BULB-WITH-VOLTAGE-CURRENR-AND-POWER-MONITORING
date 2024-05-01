#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <EEPROM.h> // Include the EEPROM library

#define i2c_Address 0x3c  // Initialize with the I2C addr 0x3C,
#define SCREEN_WIDTH 128  // OLED display width, in pixels
#define SCREEN_HEIGHT 64  // OLED display height, in pixels
#define OLED_RESET -1     // QT-PY / XIAO

#define potentiometer A0
#define enterButton D5

unsigned long lastButtonPressTime = 0;
const unsigned long buttonPressInterval = 200;

bool buttonPressed = false;

Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

int selectedRelay = 0;
int relayState[4] = {LOW, LOW, LOW, LOW}; // Assuming 4 relays connected
int relayPins[4] = {2, 3, 5 , 4}; // Assuming 4 digital pins connected to the relays
int lastSavedState[4]; // Store the last saved state of relays

void setup() {
  Serial.begin(9600);

  display.begin(i2c_Address, true);    // Address 0x3C default
  display.clearDisplay();              // Clear the display
  display.setTextSize(1);              // Set text size
  display.setTextColor(SH110X_WHITE);  // Set text color
  pinMode(enterButton, INPUT_PULLUP);

  // Set all relay pins as OUTPUT
  for (int i = 0; i < 4; i++) {
    pinMode(relayPins[i], OUTPUT);
  }

  // Display initial text at the top left corner
  display.setCursor(0, 0);
  display.println("Relay Control");
  display.display();

  // Read relay states from EEPROM during setup
  for (int i = 0; i < 4; i++) {
    lastSavedState[i] = EEPROM.read(i); // Read from EEPROM
    relayState[i] = lastSavedState[i]; // Set relay state to last saved state
    digitalWrite(relayPins[i], relayState[i]); // Set relay state
  }
}

// Function to toggle the state of a relay and save to EEPROM if changed
void toggleRelay(int relay) {
  if (relay >= 0 && relay < 4) {
    relayState[relay] = !relayState[relay];
    
    // Write to EEPROM if relay state has changed
    if (relayState[relay] != lastSavedState[relay]) {
      lastSavedState[relay] = relayState[relay]; // Update last saved state
      EEPROM.write(relay, relayState[relay]); // Write to EEPROM
      EEPROM.commit(); // Commit changes to EEPROM
    }
    
    digitalWrite(relayPins[relay], relayState[relay]); // Set relay state
  }
}

// Function to handle relay control
void relayControl() {
  int enter = digitalRead(enterButton);

  unsigned long currentMillis = millis();

  if (currentMillis - lastButtonPressTime >= buttonPressInterval) {
    if (enter == LOW && !buttonPressed) {
      // Toggle the state of the selected relay
      toggleRelay(selectedRelay);
      buttonPressed = true;
    }
    lastButtonPressTime = currentMillis;
  }

  if (enter == HIGH && buttonPressed) {
    // Reset buttonPressed flag after button released
    buttonPressed = false;
  }

  int potValue = analogRead(potentiometer);
  selectedRelay = map(potValue, 0, 1023, 0, 3); // Map potentiometer value to selected relay index

  const char *relayNames[4] = {
    "Relay 0",
    "Relay 1",
    "Relay 2",
    "Relay 3"
  };

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  for (int i = 0; i < 4; i++) {
    display.setCursor(0, 12 * i);
    if (i == selectedRelay) {
      display.setTextColor(SH110X_BLACK, SH110X_WHITE);
    } else {
      display.setTextColor(SH110X_WHITE);
    }
    display.print(relayNames[i]);
    display.print(": ");
    display.println(relayState[i] == HIGH ? "OFF" : "ON");

    // Set the state of the digital pin based on the relay state
    digitalWrite(relayPins[i], relayState[i]);
  }

  display.display();
}

void loop() {
  relayControl();
}
