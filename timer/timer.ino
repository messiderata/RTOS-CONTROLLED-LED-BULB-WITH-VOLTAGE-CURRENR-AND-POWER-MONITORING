#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

#define i2c_Address 0x3c
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

#define upButton D3
#define downButton D4
#define enterButton D5
#define backButton D6

const int potPin = A0;

unsigned long lastPotReadTime = 0;  // Last time the potentiometer was read
const unsigned long potReadInterval = 100;

unsigned long lastButtonPressTime = 0;
const unsigned long buttonPressInterval = 200;

Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

int selected = 0;

int loadSelected = 0;
int loadEntered = -1;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);
  display.begin(i2c_Address, true);
  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  pinMode(upButton, INPUT_PULLUP);
  pinMode(downButton, INPUT_PULLUP);
  pinMode(enterButton, INPUT_PULLUP);
  pinMode(backButton, INPUT_PULLUP);
}

void loop() {
  // put your main code here, to run repeatedly:
  unsigned long currentMillis = millis();

  if (currentMillis - lastPotReadTime >= potReadInterval) {
    int potValue = analogRead(potPin);
    selected = map(potValue, 0, 1023, 0, 2);

    lastPotReadTime = currentMillis;
  }

  loadSelection();
  // timeSet(selected);
}

void timeSet(int select) {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SH110X_WHITE);
  // Print the title
  display.setCursor(35, 10);
  display.print("Timer");

  const char *options[3] = {
    " hh ",
    " mm ",
    " ss ", 
  };

  int optionWidth = 30;                                 // Adjust the width of each option based on your preference
  int startX = (SCREEN_WIDTH - (optionWidth * 3)) / 2;  // Calculate the starting x-coordinate for printing options

  for (int i = 0; i < 3; i++) {
    if (i == select) {
      display.setTextColor(SH110X_BLACK, SH110X_WHITE);
    } else {
      display.setTextColor(SH110X_WHITE);
    }
    display.setTextSize(1);
    display.setCursor(startX + (optionWidth * i), 40);  // Adjust the y-coordinate as needed
    display.print(options[i]);

    
    if (i < 2) {
      display.print(":");
    }
  }

  display.display();
}

void loadSelection() {

  int down = digitalRead(upButton);
  int up = digitalRead(downButton);
  int enter = digitalRead(enterButton);
  int back = digitalRead(backButton);

  unsigned long currentMillis = millis();

  if (currentMillis - lastButtonPressTime >= buttonPressInterval) {
    if (up == LOW) {
      loadSelected = (loadSelected + 1) % 4;  // Wrap around to 0 after reaching the last option
      lastButtonPressTime = currentMillis;
    }

    if (down == LOW) {
      loadSelected = (loadSelected - 1 + 4) % 4;  // Ensure positive index, wrap around to 2 if negative
      lastButtonPressTime = currentMillis;
    }
  }

  if (enter == LOW) {
    loadEntered = loadSelected;
  }

  if (back == LOW) {
    loadEntered = -1;
  };

  const char *options[4] = {
    " LOAD 0 ",
    " LOAD 1 ",
    " LOAD 2 ",
    " LOAD 3 "
  };

  if (loadEntered == -1) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 0);
    display.println(F("Choose load:"));
    display.setTextSize(1);

    for (int i = 0; i < 4; i++) {
      if (i == loadSelected) {
        display.setTextColor(SH110X_BLACK, SH110X_WHITE);
        display.println(options[i]);
      } else if (i != loadSelected) {
        display.setTextColor(SH110X_WHITE);
        display.println(options[i]);
      }
    }
  } else if (loadEntered == 0 || currentMillis % 10000 == 0) {
    timeSet(selected);
  }
  display.display();
}

