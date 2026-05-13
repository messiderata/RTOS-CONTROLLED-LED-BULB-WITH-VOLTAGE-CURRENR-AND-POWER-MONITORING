#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <DS3231.h>

 
DS3231 clocks;
RTCDateTime dt;
 ;
 
/* Uncomment the initialize the I2C address, uncomment only one. If you get a totally blank screen, try the other */
#define i2c_Address 0x3c // Initialize with the I2C addr 0x3C, Typically eBay OLED's
//#define i2c_Address 0x3d // Initialize with the I2C addr 0x3D, Typically Adafruit OLED's

#define SCREEN_WIDTH 128   // OLED display width, in pixels
#define SCREEN_HEIGHT 64   // OLED display height, in pixels
#define OLED_RESET -1      // QT-PY / XIAO
Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

void setup() {
  Serial.begin(9600);

  // Initialize OLED display
  display.begin(i2c_Address, true); // Address 0x3C default

  // Initialize DS3231
  Serial.println("Initialize DS3231");;
  clocks.begin();

  // Display startup message
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0);
  display.println("ESP8266 + OLED");
  display.display();
  delay(2000); // Delay for 2 seconds
}

void loop() {
  timeDisplay();
  delay(1000); // Update time every second
}

void timeDisplay() {
  dt = rtc.now(); // Get current date and time
  int hour_12 = convertTo12HrFormat(dt.hour());

  // Clear the display before updating
  display.clearDisplay();

  // Print the current time to the Serial monitor
  Serial.print(dt.month());
  Serial.print("-");
  Serial.print(dt.day());
  Serial.print("-");
  Serial.print(dt.year());
  Serial.print(",");
  Serial.print(hour_12);
  Serial.print(":");
  Serial.print(dt.minute());
  Serial.print(":");
  Serial.print(dt.second());
  Serial.println(hour_12 < 12 ? " AM" : " PM");

  // Update the display with the current time
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0);
  display.print(dt.month());
  display.print("-");
  display.print(dt.day());
  display.print("-");
  display.print(dt.year());
  display.print(",");
  display.setCursor(62, 0);
  display.print(hour_12);
  display.print(":");
  display.print(dt.minute());
  display.print(":");
  display.print(dt.second());
  display.print(hour_12 < 12 ? " AM" : " PM");
  display.display();
}

int convertTo12HrFormat(int hr) {
  if (hr == 0) {
    return 12;
  } else if (hr < 12) {
    return hr;
  } else {
    return hr - 12;
  }
}
