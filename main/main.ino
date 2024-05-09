#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <DS3231.h>
#include <EEPROM.h>

DS3231 clocks;
RTCDateTime dt;

#define i2c_Address 0x3c  // Initialize with the I2C addr 0x3C,
#define SCREEN_WIDTH 128  // OLED display width, in pixels
#define SCREEN_HEIGHT 64  // OLED display height, in pixels
#define OLED_RESET -1     // QT-PY / XIAO

#define upButton 17
#define downButton 4
#define enterButton 18
#define backButton 19

#define potentiometer 15

unsigned long lastButtonPressTime = 0;
const unsigned long buttonPressInterval = 150;
bool buttonPressed = false;

Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

int selected = 0;
int entered = -1;
int lastSelectedRelay = 0;  // Add a variable to store the last selected relay

int selectedRelay = 0;
int relayState[4] = { LOW, LOW, LOW, LOW };  // Assuming 4 relays connected
int relayPins[4] = { 12, 14, 27, 26 };       // Assuming 4 digital pins connected to the relays
int lastSavedState[4];                       // Store the last saved state of relays

void setup() {
  Serial.begin(9600);

  clocks.begin();
  clocks.setDateTime(__DATE__, __TIME__);
  // Initialize DS3231
  Serial.print("Initializing DS3231");

  display.begin(i2c_Address, true);    // Address 0x3C default
  display.clearDisplay();              // Clear the display
  display.setTextSize(1);              // Set text size
  display.setTextColor(SH110X_WHITE);  // Set text color
  ;
  pinMode(upButton, INPUT_PULLUP);
  pinMode(downButton, INPUT_PULLUP);
  pinMode(enterButton, INPUT_PULLUP);
  pinMode(backButton, INPUT_PULLUP);

  int selectButtonState;

  for (int i = 0; i < 4; i++) {
    pinMode(relayPins[i], OUTPUT);
  }

  for (int i = 0; i < 4; i++) {
    lastSavedState[i] = EEPROM.read(i);         // Read from EEPROM
    relayState[i] = lastSavedState[i];          // Set relay state to last saved state
    digitalWrite(relayPins[i], relayState[i]);  // Set relay state
  }
  while (true) {
    selectButtonState = digitalRead(enterButton);
    display.setCursor(20, 0);
    display.println("RTOS-CONTROLLED");
    display.setCursor(25, 8);
    display.println("LED BULB WITH");
    display.setCursor(0, 16);
    display.println("VOLTAGE, CURRENT, AND");
    display.setCursor(18, 24);
    display.println("POWER MONITORING");
    display.display();       // Show the display buffer on the hardware
    display.clearDisplay();  // Clear the display before moving to time display
    if (selectButtonState == LOW) {
      break;
    }
  }
  delay(350);
}

void loop() {
  dt = clocks.getDateTime();

  int down = digitalRead(upButton);
  int up = digitalRead(downButton);
  int enter = digitalRead(enterButton);
  int back = digitalRead(backButton);

  unsigned long currentMillis = millis();

  if (currentMillis - lastButtonPressTime >= buttonPressInterval) {
    if (up == LOW) {
      selected = (selected + 1) % 3;  // Wrap around to 0 after reaching the last option
      lastButtonPressTime = currentMillis;
    }

    if (down == LOW) {
      selected = (selected - 1 + 3) % 3;  // Ensure positive index, wrap around to 2 if negative
      lastButtonPressTime = currentMillis;
    }
  }

  if (enter == LOW) {
    entered = selected;
  }

  if (back == LOW) {
    entered = -1;
  };

  const char *options[3] = {
    " Monitor ",
    " Timer ",
    " Manual ",
  };

  if (entered == -1) {
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 12);
    display.println(F("Menu"));
    display.setTextSize(1);
    display.drawLine(0, 9, 128, 9, SH110X_WHITE);

    for (int i = 0; i < 3; i++) {
      if (i == selected) {
        display.setTextColor(SH110X_BLACK, SH110X_WHITE);
        display.println(options[i]);
      } else if (i != selected) {
        display.setTextColor(SH110X_WHITE);
        display.println(options[i]);
      }
    }
  } else if (entered == 0) {  // Update time every 10 seconds
    timeDisplay();
  } else if (entered == 1) {
    // Add code for Timer option here
  } else if (entered == 2) {
    relayControl();  // Continuous relay control even outside manual control section
  }
  display.display();
}

void timeDisplay() {

  Serial.println(clocks.dateFormat("d M y, h:ia", dt));
  display.setTextColor(SH110X_WHITE);
  // Clear the display before updating
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print(clocks.dateFormat("d M y, h:ia", dt));
  display.display();
}

void toggleRelay(int relay) {
  if (relay >= 0 && relay < 4) {
    relayState[relay] = !relayState[relay];

    // Write to EEPROM immediately after toggling the relay state
    lastSavedState[relay] = relayState[relay];  // Update last saved state
    EEPROM.write(relay, relayState[relay]);     // Write to EEPROM
    EEPROM.commit();                            // Commit changes to EEPROM

    digitalWrite(relayPins[relay], relayState[relay]);  // Set relay state
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
  int newSelectedRelay = map(potValue, 0, 1023, 0, 3);  // Map potentiometer value to selected relay index

  if (entered == 2) {  // Only update selectedRelay if in the manual control section
    selectedRelay = newSelectedRelay;
  }

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
