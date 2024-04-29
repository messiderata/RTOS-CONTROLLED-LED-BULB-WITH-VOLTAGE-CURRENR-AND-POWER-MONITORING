#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>


#define i2c_Address 0x3c // Initialize with the I2C addr 0x3C, Typically eBay OLED's

#define SCREEN_WIDTH 128   // OLED display width, in pixels
#define SCREEN_HEIGHT 64   // OLED display height, in pixels
#define OLED_RESET -1      // QT-PY / XIAO
Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);


int selected = 0;
int entered = -1;

void setup() {
  Serial.begin(9600);

  display.begin(i2c_Address, true); // Address 0x3C default
  display.clearDisplay();                    // Clear the display
  display.setTextSize(1);                    // Set text size
  display.setTextColor(SH110X_WHITE);               // Set text color
  ;
  pinMode(D3, INPUT_PULLUP);
  pinMode(D4, INPUT_PULLUP);
  pinMode(D5, INPUT_PULLUP);
  pinMode(D6, INPUT_PULLUP);

  int selectButtonState;

  while (true) {
    selectButtonState = digitalRead(D5);
    display.setCursor(20, 0);
    display.println("RTOS-CONTROLLED");
    display.setCursor(25, 8);
    display.println("LED BULB WITH");
    display.setCursor(0, 16);
    display.println("VOLTAGE, CURRENT, AND");
    display.setCursor(18, 24);
    display.println("POWER MONITORING");
    display.display();  // Show the display buffer on the hardware
    display.clearDisplay();  // Clear the display before moving to time display
    if (selectButtonState == LOW) {
      break;
    }
  }
}

void loop() {
  displaymenu();
}

void displaymenu(void) {


  int down = digitalRead(D3);
  int up = digitalRead(D4);
  int enter = digitalRead(D5);
  int back = digitalRead(D6);


  if (up == LOW) {
    selected = (selected + 1) % 3; // Wrap around to 0 after reaching the last option
    delay(200);
  }

  if (down == LOW) {
    selected = (selected - 1 + 3) % 3; // Ensure positive index, wrap around to 2 if negative
    delay(200);
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
    " Schedule ",
  };
  if (entered == -1) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 0);
    display.println(F("Menu"));
    display.println("");
    for (int i = 0; i < 3; i++) {
      if (i == selected) {
        display.setTextColor(SH110X_BLACK, SH110X_WHITE);
        display.println(options[i]);
      } else if (i != selected) {
        display.setTextColor(SH110X_WHITE);
        display.println(options[i]);
      }
    }
  }

  else if (entered == 0) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 0);
    display.println("0");
  } else if (entered == 1) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 0);
    display.println("1");
  }
  else if (entered == 2) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 0);
    display.println("2");
  } 
  display.display();

}
