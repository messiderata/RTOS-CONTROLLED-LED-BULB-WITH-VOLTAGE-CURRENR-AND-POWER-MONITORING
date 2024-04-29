#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <DS3231.h>


DS3231 clocks;
RTCDateTime dt;

#define i2c_Address 0x3c // Initialize with the I2C addr 0x3C, Typically eBay OLED's
//#define i2c_Address 0x3d // Initialize with the I2C addr 0x3D, Typically Adafruit OLED's

#define SCREEN_WIDTH 128   // OLED display width, in pixels
#define SCREEN_HEIGHT 64   // OLED display height, in pixels
#define OLED_RESET -1      // QT-PY / XIAO
Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

void setup() {
  Serial.begin(9600);

  display.begin(i2c_Address, true); // Address 0x3C default
  display.clearDisplay();                    // Clear the display
  display.setTextSize(1);                    // Set text size
  display.setTextColor(SH110X_WHITE);               // Set text color

  // Display the title
  display.setCursor(20, 0);
  display.println("RTOS-CONTROLLED");
  display.setCursor(25, 8);
  display.println("LED BULB WITH");
  display.setCursor(0, 16);
  display.println("VOLTAGE, CURRENT, AND");
  display.setCursor(18, 24);
  display.println("POWER MONITORING");
  display.display();  // Show the display buffer on the hardware

  delay(2000);             // Delay for 2 seconds to show the title
  display.clearDisplay();  // Clear the display before moving to time display

  clocks.begin();
  clocks.setDateTime(__DATE__, __TIME__);
  // clock.setDateTime(2024, 4, 28, 19, 21, 00);
  // Initialize DS3231
  Serial.print("Initializing DS3231");
}

void loop() {
  timeDisplay();
}

void timeDisplay() {

  dt = clocks.getDateTime();
  Serial.println(clocks.dateFormat("d S Y H:i:sa", dt));

  display.setTextColor(SH110X_BLACK,SH110X_WHITE );  
    // Clear the display before updating
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print(clocks.dateFormat("d S Y H:i:sa", dt));

  display.display();
  delay(1000);
  
}
