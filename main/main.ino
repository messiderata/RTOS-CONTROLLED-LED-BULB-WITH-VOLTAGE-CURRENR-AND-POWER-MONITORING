#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <DS3231.h>
#include "Countimer.h"
#include <EEPROM.h>

Countimer tdown;

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

unsigned long lastButtonPressTime = 0;
const unsigned long buttonPressInterval = 200;


Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);


int selected = 0;
int entered = -1;

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


  } else if (entered == 2) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 0);
    display.println("2");
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
