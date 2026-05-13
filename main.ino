/**
 * =============================================================================
 * PROJECT: RTOS-Controlled LED Bulb with Voltage, Current, and Power Monitoring
 * FILE:    with app (main application)
 * PLATFORM: ESP32 (Arduino framework)
 * =============================================================================
 *
 * DESCRIPTION:
 *   Controls up to 4 AC lamp circuits via relay outputs. The user can switch
 *   between three screen modes using a dedicated button:
 *     Mode 0 – Clock display + Firebase remote lamp control
 *     Mode 1 – OLED menu system (INA219 sensor view / schedule monitor / members)
 *     Mode 2 – Schedule editor (set ON/OFF times per lamp, saved to EEPROM)
 *
 *   Sensor data (voltage, current, power) is read from an INA219 every second
 *   and uploaded to Firebase Realtime Database every 5 seconds. Lamps can also
 *   be toggled remotely through Firebase paths "Control/Lamp/Lamp N".
 *
 * HARDWARE:
 *   - ESP32 development board
 *   - DS3231 RTC  (I2C, address 0x68)
 *   - SH1106 128×64 OLED  (I2C, U8g2 library)
 *   - INA219 current/voltage sensor  (I2C)
 *   - 4× relay modules  → GPIO 26, 25, 33, 32
 *   - 4× physical lamp switches → GPIO 13, 12, 14, 27
 *   - Navigation buttons: UP=17, DOWN=4, ENTER=18, BACK=19, SCREEN=5
 *
 * COMMUNICATION:
 *   - I2C bus  (RTC, OLED, INA219)
 *   - WiFi 802.11  (Firebase RTDB)
 *   - EEPROM emulation (schedule persistence across power cycles)
 *
 * SYSTEM ARCHITECTURE:
 *   ┌─────────────────────────────────────────────────────────┐
 *   │  Input Layer : Buttons, Physical switches                │
 *   ├─────────────────────────────────────────────────────────┤
 *   │  Sensor Layer: INA219 (V/I/P), DS3231 (time/temp)       │
 *   ├─────────────────────────────────────────────────────────┤
 *   │  Processing  : Timer scheduler, mode state-machine       │
 *   ├─────────────────────────────────────────────────────────┤
 *   │  Comms Layer : Firebase RTDB (upload sensor, read ctrl)  │
 *   ├─────────────────────────────────────────────────────────┤
 *   │  Output Layer: 4× relays (lamps), SH1106 OLED display   │
 *   └─────────────────────────────────────────────────────────┘
 *
 * AUTHORS: Prudencio, Amista, Alvdendia, Manginsay, Rivera
 * =============================================================================
 */

/* ─────────────────────────────────────────────────────────────────────────────
 * INCLUDES
 * ───────────────────────────────────────────────────────────────────────────*/
#include <Arduino.h>
#include <Wire.h>
#include <Button.h>
#include "U8g2lib.h"
#include "RTClib.h"
#include <Adafruit_INA219.h>
#include <EEPROM.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"


/* ─────────────────────────────────────────────────────────────────────────────
 * PERIPHERAL OBJECTS
 * ───────────────────────────────────────────────────────────────────────────*/
RTC_DS3231 rtc;                                           // DS3231 real-time clock (I2C)
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0);       // 128×64 OLED, hardware I2C
Adafruit_INA219 ina219;                                   // INA219 power monitor (I2C)

/* ─────────────────────────────────────────────────────────────────────────────
 * OLED BITMAP ASSETS (stored in flash via PROGMEM)
 * Each bitmap is encoded as a packed bit array (MSB first, row-major).
 * ───────────────────────────────────────────────────────────────────────────*/

// 16×16 px — icon shown next to "INA 219" menu item
const unsigned char epd_bitmap__icon_ina[] PROGMEM = {
  0xff, 0xff, 0x80, 0x01, 0x83, 0xc1, 0x8d, 0xb1, 0x98, 0x19, 0x90, 0x09, 0xb0, 0x0d, 0xb0, 0x05,
  0x80, 0x11, 0x80, 0x21, 0x80, 0x41, 0x83, 0x81, 0x85, 0x41, 0x83, 0x81, 0x80, 0x01, 0xff, 0xff
};

// 16×16 px — icon shown next to "Monitoring" menu item
const unsigned char epd_bitmap__icon_monitoring[] PROGMEM = {
  0xff, 0xff, 0x80, 0x01, 0x80, 0x09, 0x80, 0x31, 0x80, 0xc1, 0x83, 0x01, 0x8c, 0x01, 0x90, 0x01,
  0xa0, 0x01, 0x80, 0x01, 0xff, 0xff, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x7f, 0xfe, 0xff, 0xff
};

// 16×16 px — icon shown next to "Timer" menu item
const unsigned char epd_bitmap__icon_timer[] PROGMEM = {
  0x0f, 0xf0, 0x10, 0x08, 0x20, 0x04, 0x41, 0x02, 0x81, 0x01, 0x81, 0x01, 0x83, 0x81, 0x82, 0xfd,
  0x81, 0x81, 0x80, 0x01, 0x80, 0x01, 0x40, 0x02, 0x20, 0x04, 0x10, 0x08, 0x0f, 0xf0, 0x00, 0x00
};

// 8×64 px — vertical scrollbar track drawn on the right edge of the display
const unsigned char epd_bitmap__icon_scrollbar[] PROGMEM = {
  0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02,
  0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02,
  0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02,
  0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02
};

// 128×21 px — rounded rectangle drawn behind the currently selected menu row
const unsigned char epd_bitmap__icon_selector[] PROGMEM = {
  0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x80,
  0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40,
  0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60,
  0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60,
  0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60,
  0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60,
  0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60,
  0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60,
  0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60,
  0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60,
  0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60,
  0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60,
  0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60,
  0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60,
  0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60,
  0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60,
  0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60,
  0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60,
  0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60,
  0x1f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc0,
  0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x80
};

// 128×20 px — alternative selection background (slightly different corner radius)
const unsigned char epd_bitmap__item_sel_bg[] PROGMEM = {
  0x1f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x80,
  0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0,
  0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60,
  0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60,
  0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60,
  0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60,
  0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60,
  0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60,
  0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60,
  0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60,
  0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60,
  0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60,
  0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60,
  0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60,
  0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60,
  0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60,
  0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xe0,
  0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xe0,
  0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xc0,
  0x3f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x80
};

// 128×64 px — decorative border frame used in the INA219 sensor screen
const unsigned char epd_bitmap_border[] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0xf7, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// 5-byte placeholders (currently identical); intended for lamp-off/on status glyphs
const unsigned char epd_bitmap_off[] PROGMEM = {
  0xf8, 0xf8, 0xf8, 0xf8, 0xf8
};

const unsigned char epd_bitmap_on[] PROGMEM = {
  0xf8, 0xf8, 0xf8, 0xf8, 0xf8
};

// 16×16 px — person silhouette reused as a placeholder icon for member entries
const unsigned char epd_bitmap__person[] PROGMEM = {
  0x3f, 0xfc, 0x40, 0x02, 0x82, 0x11, 0x82, 0x11, 0x80, 0x01, 0x82, 0x09, 0x81, 0xf1, 0x40, 0x02,
  0x3f, 0xfc, 0x10, 0x84, 0x10, 0x84, 0x10, 0x84, 0x10, 0x84, 0x10, 0x84, 0x10, 0xc4, 0x1f, 0xbc
};

/* ─────────────────────────────────────────────────────────────────────────────
 * ICON LOOKUP TABLES
 * These map menu-item indices to their corresponding 16×16 bitmaps.
 * ───────────────────────────────────────────────────────────────────────────*/

// Icons for the 3 top-level main-menu entries
const unsigned char *bitmap_icons[3] = {
  epd_bitmap__icon_ina,        // index 0 → INA219
  epd_bitmap__icon_monitoring, // index 1 → Monitoring
  epd_bitmap__person           // index 2 → Member
};

// Icons for the 5 member sub-menu entries (all use the person silhouette)
const unsigned char *bitmap_lamp_icons[5] = {
  epd_bitmap__person,
  epd_bitmap__person,
  epd_bitmap__person,
  epd_bitmap__person,
  epd_bitmap__person,
};

/* ─────────────────────────────────────────────────────────────────────────────
 * MENU CONTENT
 * ───────────────────────────────────────────────────────────────────────────*/

const int NUM_ITEMS        = 3;  // Number of main-menu entries
const int LAMP_NUM_ITEMS   = 4;  // Number of controllable lamps
const int PERSON_NUM_ITEMS = 5;  // Number of team members shown in sub-menu

// Main-menu labels (order matches bitmap_icons[])
const char *menu_items[NUM_ITEMS] = {
  "INA 219",
  "Monitoring",
  "Member"
};

// Member profile sub-menu labels
const char *person_menu_items[PERSON_NUM_ITEMS] = {
  "Prudencio",
  "Amista",
  "Alvdendia",
  "Manginsay",
  "Rivera",
};

// Schedule sub-menu lamp labels
const char *lamp_menu_items[LAMP_NUM_ITEMS] = {
  "LAMP 1",
  "LAMP 2",
  "LAMP 3",
  "LAMP 4",
};


/* ─────────────────────────────────────────────────────────────────────────────
 * FIREBASE / WIFI CREDENTIALS
 * WARNING: Hard-coded credentials — move to a secrets header or NVS for
 * production deployments to avoid leaking keys in version control.
 * ───────────────────────────────────────────────────────────────────────────*/
#define WIFI_SSID     "Meralco"
#define WIFI_PASSWORD "Amistacompany"
#define API_KEY       "AIzaSyA-p_FeLotO44gespq69cicLYm6SjCBJZo"
#define DATABASE_URL  "https://tryt-650d2-default-rtdb.firebaseio.com/"


/* ─────────────────────────────────────────────────────────────────────────────
 * HARDWARE PIN DEFINITIONS
 * ───────────────────────────────────────────────────────────────────────────*/

// Relay output pins — active-LOW relays: HIGH = off, LOW = on
const int relayPin[]  = { 26, 25, 33, 32 };

// Physical lamp switch input pins (one per lamp)
const int switchPin[] = { 13, 12, 14, 27 };

const int numberOfButtons = sizeof(switchPin) / sizeof(switchPin[0]);  // = 4

// Navigation button GPIO assignments
#define UP_BUTTON_PIN    17
#define DOWN_BUTTON_PIN   4
#define ENTER_BUTTON_PIN 18
#define BACK_BUTTON_PIN  19
#define SCREEN_BUTTON     5

/* ─────────────────────────────────────────────────────────────────────────────
 * EEPROM LAYOUT
 * Each lamp occupies 8 consecutive bytes starting from its base address.
 * Layout per lamp (offset from base):
 *   +0 hour  | +1 minute | +2 second | +3 period (0=AM, 1=PM)
 *
 * ON-time base addresses:  8, 16, 24, 32  (lamps 1-4)
 * OFF-time base addresses: 12, 20, 28, 36 (lamps 1-4)
 * ───────────────────────────────────────────────────────────────────────────*/
#define EEPROM_SIZE 512

#define EEPROM_ADDR_LAMP1_ON_HOUR    8
#define EEPROM_ADDR_LAMP1_ON_MINUTE  9
#define EEPROM_ADDR_LAMP1_ON_SECOND  10
#define EEPROM_ADDR_LAMP1_ON_PERIOD  11

#define EEPROM_ADDR_LAMP1_OFF_HOUR   12
#define EEPROM_ADDR_LAMP1_OFF_MINUTE 13
#define EEPROM_ADDR_LAMP1_OFF_SECOND 14
#define EEPROM_ADDR_LAMP1_OFF_PERIOD 15

#define EEPROM_ADDR_LAMP2_ON_HOUR    16
#define EEPROM_ADDR_LAMP2_ON_MINUTE  17
#define EEPROM_ADDR_LAMP2_ON_SECOND  18
#define EEPROM_ADDR_LAMP2_ON_PERIOD  19


/* ─────────────────────────────────────────────────────────────────────────────
 * GLOBAL STATE VARIABLES
 * ───────────────────────────────────────────────────────────────────────────*/

char daysOfTheWeek[7][12] = {
  "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};

// ── Menu navigation ──────────────────────────────────────────────────────────
int mainMenuSelection = 0;   // Currently highlighted top-level item (0-2)
int submenuSelection  = 0;   // Currently highlighted sub-menu item

// ── Screen / mode state ──────────────────────────────────────────────────────
int lastState = LOW;
int count     = 0;           // Screen mode: 0=clock, 1=menu, 2=schedule editor
int scrollOffset    = 0;     // Current vertical scroll position (pixels)
const int scrollStep = 10;   // Pixels scrolled per button press

bool inSubMenu     = false;  // True when a sub-menu is open inside mode 1
bool inLampDisplay = false;  // True when member detail view is active
bool scheduleDisplay = false;

// ── Schedule editor field selector ───────────────────────────────────────────
// selectedComponent cycles 0-6; each value highlights a different field:
//   0=hour, 1=minute, 2=second, 3=AM/PM, 4=lamp select, 5=save ON, 6=save OFF
int selectedComponent = 0;

// ── RTC-sourced time components (used in schedule editor) ────────────────────
int time_s = 0;
int time_m = 0;
int time_h = 0;

// ── Per-lamp schedule (loaded from EEPROM) ───────────────────────────────────
int  startHour, startMinute, startSecond;
bool startPeriod;   // false = AM, true = PM
int  endHour,   endMinute,   endSecond;
bool endPeriod;

int  lampSelected = 0;  // Which lamp's schedule is being edited (0-3)

// ── Lamp relay states (HIGH = off for active-LOW relays) ─────────────────────
int lampState1 = HIGH;
int lampState2 = HIGH;
int lampState3 = HIGH;
int lampState4 = HIGH;

// ── Manual vs timer control flags ───────────────────────────────────────────
bool manualMode          = true;   // Unused placeholder; timer always active
bool manualControlActive = false;  // Prevents timer from overriding manual input
bool isPM                = false;  // AM/PM selection in schedule editor

/* ─────────────────────────────────────────────────────────────────────────────
 * BUTTON OBJECTS
 * Uses the Button library for edge-detection (pressed/released events).
 * ───────────────────────────────────────────────────────────────────────────*/
Button upButton(UP_BUTTON_PIN);
Button downButton(DOWN_BUTTON_PIN);
Button enterButton(ENTER_BUTTON_PIN);
Button backButton(BACK_BUTTON_PIN);
Button screenButton(SCREEN_BUTTON);

Button *buttons[numberOfButtons];   // Physical lamp toggle switches

/* ─────────────────────────────────────────────────────────────────────────────
 * SENSOR DATA (updated by readSensors() every sensorInterval ms)
 * ───────────────────────────────────────────────────────────────────────────*/
float current_mA    = 0;  // Load current  [A]  (INA219 raw mA ÷ 1000)
float loadvoltage   = 0;  // Load voltage  [V]  (bus + shunt)
float power_mW      = 0;  // Load power    [W]  (INA219 raw mW ÷ 1000)
float shuntvoltage  = 0;  // Shunt voltage [mV]
float busvoltage    = 0;  // Bus voltage   [V]

/* ─────────────────────────────────────────────────────────────────────────────
 * NON-BLOCKING TIMING  (millis()-based)
 * All periodic tasks are driven by elapsed-time checks; no blocking delays in
 * the main loop.
 * ───────────────────────────────────────────────────────────────────────────*/
unsigned long previousMillisSensor    = 0;
unsigned long previousMillisFirebase  = 0;
const unsigned long sensorInterval    = 1000;   // Sensor read: every 1 s
const unsigned long firebaseInterval  = 5000;   // Firebase push: every 5 s

unsigned long previousMillis  = 0;
const unsigned long interval  = 10;    // Main display refresh: every 10 ms

unsigned long previousMillis1  = 0;
const unsigned long interval1  = 100;  // Secondary task rate (scroll input)

unsigned long previousMillisLampControl    = 0;
const unsigned long lampControlInterval    = 200;  // (reserved, not yet used)

/* ─────────────────────────────────────────────────────────────────────────────
 * FIREBASE OBJECTS
 * ───────────────────────────────────────────────────────────────────────────*/
FirebaseData   fbdo;         // General data object (sensor uploads)
FirebaseAuth   auth;
FirebaseConfig config;
FirebaseData   lampControl;  // Separate stream object for lamp command reads

bool signupOK      = false;
bool wifiConnected = false;

/* ─────────────────────────────────────────────────────────────────────────────
 * FUNCTION FORWARD DECLARATIONS
 * ───────────────────────────────────────────────────────────────────────────*/
void connectWiFi();
void initializeFirebase();
void readSensors();
void updateFirebase();
void firebaseLampControl();
void clockDisplay(DateTime now);
void formatDateTime(DateTime now, char *buffer, size_t bufferSize);
void periodDisplay(DateTime now, char *buffer, size_t bufferSize);
void dateDisplay(DateTime now, char *buffer, size_t bufferSize);
void displayMainMenu(int numItems, const unsigned char *Icons[], const char *Items[]);
void displaySubMenu(DateTime now);
void displaySelectedLamp(int lampIndex);
void handleButtons();
void ButtonPress();
void timerFunction(DateTime now);
void timerSection();
void monitoringDisplay(DateTime now);
void sensorDisplay();
void handleScrollInput();
int  maxScrollOffset();
void saveLampOnTimeToEEPROM(int lampIndex);
void saveLampOffTimeToEEPROM(int lampIndex);
void loadLampOnTimeFromEEPROM(int lampIndex);
void loadLampOffTimeFromEEPROM(int lampIndex);
int  convertTo24Hour(int hour, bool isPM);
bool isTimeInRange(DateTime now,
                   int startHour, int startMinute, int startSecond, bool startPeriod,
                   int endHour,   int endMinute,   int endSecond,   bool endPeriod);
void manualRelayControl();


/* =============================================================================
 * SETUP — runs once on power-on / reset
 *
 * Initialisation sequence:
 *   1. Serial (debug)
 *   2. OLED display
 *   3. DS3231 RTC  — auto-sets time from compile timestamp if power was lost
 *   4. DS3231 register 0x0E  — disables square-wave output to save power
 *   5. Navigation buttons
 *   6. INA219  — calibrated for 16 V / 400 mA range
 *   7. Physical lamp switches + relay GPIOs
 *   8. EEPROM  — loads persisted schedule for lamp 0
 *   9. WiFi + Firebase (skipped if connection fails)
 * =============================================================================
 */
void setup() {
  Serial.begin(115200);
  u8g2.setColorIndex(1);   // White pixels on black background
  u8g2.begin();
  u8g2.setBitmapMode(1);

  /* RTC init ----------------------------------------------------------------*/
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    Serial.flush();
    while (1) delay(10);
  }

  if (rtc.lostPower()) {
    Serial.println("RTC lost power, let's set the time!");
    // Synchronise RTC to the moment this firmware was compiled
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // Disable DS3231 EOSC / square-wave output (register 0x0E, bit 7)
  Wire.beginTransmission(0x68);
  Wire.write(0x0E);
  Wire.write(0b00011100);
  Wire.endTransmission();

  while (!Serial) { delay(1); }   // Wait for USB serial on boards that need it

  /* Navigation button init --------------------------------------------------*/
  upButton.begin();
  downButton.begin();
  enterButton.begin();
  backButton.begin();
  screenButton.begin();

  /* INA219 init -------------------------------------------------------------*/
  if (!ina219.begin()) {
    Serial.println("Failed to find INA219 chip");
    while (1) { delay(10); }
  }
  ina219.setCalibration_16V_400mA();   // Max 16 V bus, 400 mA shunt range
  Serial.println("Measuring voltage, current, and power with INA219 ...");
  ina219.begin();   // Second begin() call is harmless but redundant

  /* Physical lamp switch + relay init ---------------------------------------*/
  for (int i = 0; i < numberOfButtons; i++) {
    buttons[i] = new Button(switchPin[i]);
    buttons[i]->begin();
    pinMode(relayPin[i], OUTPUT);
  }

  /* EEPROM — restore last saved schedule ------------------------------------*/
  EEPROM.begin(EEPROM_SIZE);
  loadLampOffTimeFromEEPROM(lampSelected);
  loadLampOnTimeFromEEPROM(lampSelected);

  /* WiFi + Firebase ---------------------------------------------------------*/
  Serial.begin(115200);   // Re-init serial (harmless duplicate)
  connectWiFi();
  if (wifiConnected) {
    initializeFirebase();
  }
}


/* =============================================================================
 * LOOP — runs continuously after setup()
 *
 * All tasks are gated by millis() comparisons to avoid blocking:
 *
 *   Every  1 000 ms : readSensors()      — poll INA219
 *   Every  5 000 ms : updateFirebase()   — push sensor data to RTDB
 *   Every     10 ms : mode state-machine — update display and handle buttons
 *
 * Screen mode state-machine (controlled by SCREEN_BUTTON / `count`):
 *   0 → clockDisplay()  + firebaseLampControl()
 *   1 → timerFunction() + OLED menu tree
 *   2 → timerSection()  + schedule editor + ButtonPress()
 *
 * NOTE: loadLampOffTimeFromEEPROM() / loadLampOnTimeFromEEPROM() are called
 * every iteration of loop() — this is unnecessary overhead; ideally they
 * should only be called when lampSelected changes or after a save.
 * =============================================================================
 */
void loop() {
  DateTime now = rtc.now();
  unsigned long currentMillis = millis();

  /* Sensor read (non-blocking) -----------------------------------------------*/
  if (currentMillis - previousMillisSensor >= sensorInterval) {
    previousMillisSensor = currentMillis;
    readSensors();
  }

  /* Firebase sensor upload (non-blocking) -------------------------------------*/
  if (currentMillis - previousMillisFirebase >= firebaseInterval) {
    previousMillisFirebase = currentMillis;
    updateFirebase();
  }

  /* 10 ms display/button tick -------------------------------------------------*/
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    // Advance screen mode on each SCREEN button press (cycles 0→1→2→0)
    if (screenButton.pressed()) {
      count++;
    }

    switch (count) {
      case 0:
        // Mode 0: Clock face + Firebase remote lamp control
        clockDisplay(now);
        firebaseLampControl();
        break;

      case 1:
        // Mode 1: RTC-based lamp scheduler + OLED menu tree
        timerFunction(now);

        if (inLampDisplay) {
          displaySelectedLamp(submenuSelection);   // Show member detail card
        } else if (inSubMenu) {
          displaySubMenu(now);                     // Show sub-menu (sensor/member)
        } else {
          displayMainMenu(NUM_ITEMS, bitmap_icons, menu_items);  // Top-level menu
        }
        handleButtons();
        break;

      case 2:
        // Mode 2: Schedule time editor
        timerSection();
        timerFunction(now);   // Keep relay control active while editing
        ButtonPress();
        break;

      default:
        count = 0;   // Wrap back to clock mode
        break;
    }

    // Secondary 100 ms task: scroll input (runs inside the 10 ms block;
    // the condition should compare previousMillis1, but currently compares
    // previousMillis — effective rate is ~10 ms)
    if (currentMillis - previousMillis >= interval1) {
      previousMillis1 = currentMillis;
      handleScrollInput();
    }
  }

  // Refresh active lamp schedule from EEPROM each loop iteration
  loadLampOffTimeFromEEPROM(lampSelected);
  loadLampOnTimeFromEEPROM(lampSelected);
}


/* =============================================================================
 * handleButtons()
 * Processes UP / DOWN / ENTER / BACK navigation within the OLED menu tree
 * (Mode 1 only).
 *
 * State transitions:
 *   ENTER (not in sub-menu) → open sub-menu
 *   ENTER (in sub-menu, mainMenuSelection==1) → open member detail
 *   BACK  (in sub-menu)     → return to main menu, reset submenuSelection
 *   BACK  (in lamp display) → return to sub-menu list
 * =============================================================================
 */
void handleButtons() {
  if (inLampDisplay) {
    if (backButton.pressed()) {
      inLampDisplay = false;
    }
    return;   // Block all other buttons while detail card is open
  }

  if (upButton.pressed()) {
    if (inSubMenu) {
      submenuSelection = (submenuSelection - 1 + PERSON_NUM_ITEMS) % PERSON_NUM_ITEMS;
    } else {
      mainMenuSelection = (mainMenuSelection - 1 + NUM_ITEMS) % NUM_ITEMS;
    }
  }
  if (downButton.pressed()) {
    if (inSubMenu) {
      submenuSelection = (submenuSelection + 1) % PERSON_NUM_ITEMS;
    } else {
      mainMenuSelection = (mainMenuSelection + 1) % NUM_ITEMS;
    }
  }
  if (enterButton.pressed()) {
    if (!inSubMenu) {
      inSubMenu = true;
    } else {
      if (mainMenuSelection == 1) {   // "Monitoring" sub-menu → member detail
        inLampDisplay = true;
      }
    }
  }
  if (backButton.pressed()) {
    if (inSubMenu) {
      inSubMenu = false;
      submenuSelection = 0;
    }
  }
}


/* =============================================================================
 * ButtonPress()
 * Handles UP / DOWN / ENTER button input in the schedule editor (Mode 2).
 *
 * selectedComponent field map:
 *   0 → hour (12-hour, wraps 1-12)
 *   1 → minute (0-59)
 *   2 → second (0-59)
 *   3 → AM/PM toggle
 *   4 → lamp selector (0-3)
 *   5 → (no action here; BACK saves ON time — see timerSection)
 *   6 → (no action here; BACK saves OFF time — see timerSection)
 *
 * ENTER cycles selectedComponent forward through all 7 fields.
 * UP increments the selected field; DOWN decrements it.
 * =============================================================================
 */
void ButtonPress() {
  if (enterButton.pressed()) {
    selectedComponent = (selectedComponent + 1) % 7;
  }

  if (upButton.pressed()) {
    if (selectedComponent == 0) {
      time_h = (time_h - 1 + 12) % 12;
      if (time_h == 0) time_h = 12;
    } else if (selectedComponent == 1) {
      time_m = (time_m + 1) % 60;
    } else if (selectedComponent == 2) {
      time_s = (time_s + 1) % 60;
    } else if (selectedComponent == 3) {
      isPM = !isPM;
    } else if (selectedComponent == 4) {
      lampSelected = (lampSelected + 1) % 4;
    }
  }

  if (downButton.pressed()) {
    if (selectedComponent == 0) {
      time_h = (time_h + 1) % 12;
      if (time_h == 0) time_h = 12;
    } else if (selectedComponent == 1) {
      time_m = (time_m - 1 + 60) % 60;
    } else if (selectedComponent == 2) {
      time_s = (time_s - 1 + 60) % 60;
    } else if (selectedComponent == 3) {
      isPM = !isPM;
    } else if (selectedComponent == 4) {
      lampSelected = (lampSelected - 1 + 4) % 4;
    }
  }
}


/* =============================================================================
 * displaySelectedLamp(lampIndex)
 * Renders a member profile card on the OLED for the given sub-menu index.
 * Fields: name, gender, age, location.
 * =============================================================================
 */
void displaySelectedLamp(int lampIndex) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g_font_5x8r);

  switch (lampIndex) {
    case 0:
      u8g2.drawStr(8, 8,  "Philip Joshua F. Amista");
      u8g2.drawStr(8, 16, "Male");
      u8g2.drawStr(8, 24, "21");
      u8g2.drawStr(8, 32, "Marikina City");
      break;
    case 1:
      u8g2.drawStr(8, 8,  "Kim Alvendia");
      u8g2.drawStr(8, 16, "Female");
      u8g2.drawStr(8, 24, "22");
      u8g2.drawStr(8, 32, "Marikina City");
      break;
    case 2:
      u8g2.drawStr(8, 8,  "Mars Laurenz Manginsay");
      u8g2.drawStr(8, 16, "Male");
      u8g2.drawStr(8, 24, "21");
      u8g2.drawStr(8, 32, "Caloocan");
      break;
    case 3:
      u8g2.drawStr(8, 8,  "James Rivera");
      u8g2.drawStr(8, 16, "Male");
      u8g2.drawStr(8, 24, "21");
      u8g2.drawStr(8, 32, "Laguna");
      break;
    case 4:
      u8g2.drawStr(8, 8,  "Kimberly Prudencio");
      u8g2.drawStr(8, 16, "Female");
      u8g2.drawStr(8, 24, "23");
      u8g2.drawStr(8, 32, "Cavite");
      break;
  }

  u8g2.sendBuffer();
}


/* =============================================================================
 * displaySubMenu(now)
 * Renders one of three sub-screens depending on mainMenuSelection:
 *   0 (INA 219)    → monitoringDisplay() — scrollable sensor schedule list
 *   1 (Monitoring) → person_menu_items list with scrollbar + selector bitmap
 *   2 (Member)     → sensorDisplay()     — live V/I/P readout
 *
 * The scrollable person list renders PERSON_NUM_ITEMS rows at once, cycling
 * items with (submenuSelection + i) % PERSON_NUM_ITEMS so the selected item
 * always appears at position index 1 (bold font).
 * =============================================================================
 */
void displaySubMenu(DateTime now) {
  u8g2.clearBuffer();
  const char **menu = nullptr;

  switch (mainMenuSelection) {
    case 0:
      monitoringDisplay(now);
      handleScrollInput();
      return;
    case 1:
      menu = person_menu_items;
      break;
    case 2:
      sensorDisplay();
      return;
  }

  int itemHeight = 20;
  int y = 17;
  int x = 6;

  if (menu != nullptr) {
    u8g2.drawBitmap(0,   22,       128 / 8, 21, epd_bitmap__icon_selector);
    u8g2.drawBitmap(120, 1,          8 / 8, 63, epd_bitmap__icon_scrollbar);
    // Scrollbar thumb: position proportional to submenuSelection
    u8g2.drawBox(125, 64 / PERSON_NUM_ITEMS * submenuSelection, PERSON_NUM_ITEMS, 64 / PERSON_NUM_ITEMS);

    for (int i = 0; i < PERSON_NUM_ITEMS; i++) {
      u8g2.setFont(i == 1 ? u8g_font_7x14B : u8g_font_7x14);  // Bold for selected row
      u8g2.drawStr(25, y, person_menu_items[(submenuSelection + i) % PERSON_NUM_ITEMS]);
      u8g2.drawBitmap(x, y - 13, 16 / 8, 16, bitmap_lamp_icons[(submenuSelection + i) % PERSON_NUM_ITEMS]);
      y += itemHeight;
    }
  }

  u8g2.sendBuffer();
}


/* =============================================================================
 * displayMainMenu(numItems, Icons[], Items[])
 * Renders the top-level scrollable icon+text menu.
 *
 * Layout:
 *   - Selector bitmap drawn at row 1 (y=22) — highlights the middle item
 *   - Scrollbar track on right edge; thumb box shows current position
 *   - Items rendered in a rotating window: the selected item is always index 1
 *     (displayed in bold), items above/below scroll into view using modulo
 * =============================================================================
 */
void displayMainMenu(int numItems, const unsigned char *Icons[], const char *Items[]) {
  u8g2.clearBuffer();
  int itemHeight = 20;
  int y = 17;
  int x = 6;

  u8g2.setFont(u8g_font_7x14);
  u8g2.drawBitmap(0,   22,       128 / 8, 21, epd_bitmap__icon_selector);
  u8g2.drawBitmap(120, 1,          8 / 8, 63, epd_bitmap__icon_scrollbar);
  u8g2.drawBox(125, 64 / numItems * mainMenuSelection, numItems, 64 / numItems);

  for (int i = 0; i < numItems; i++) {
    u8g2.setFont(i == 1 ? u8g_font_7x14B : u8g_font_7x14);
    u8g2.drawStr(25, y, Items[(mainMenuSelection + i) % numItems]);
    u8g2.drawBitmap(x, y - 13, 16 / 8, 16, Icons[(mainMenuSelection + i) % numItems]);
    y += itemHeight;
  }
  u8g2.sendBuffer();
}


/* =============================================================================
 * clockDisplay(now)
 * Renders the Mode-0 home screen:
 *   - Large 12-hour time (HH:MM) — profont29 (29 px tall)
 *   - Seconds + AM/PM — profont15 (top-right)
 *   - Day-of-week + date — profont12 (top-left)
 *   - DS3231 temperature (°C) — 7×14 font (centre)
 *   - Lamp status row — '-' = off, '°' glyph = on (bottom row, 4 lamps)
 *
 * Active-LOW relay convention: lampStateN==HIGH means relay off (lamp off).
 * =============================================================================
 */
void clockDisplay(DateTime now) {
  u8g2.clearBuffer();

  char timeString[20];
  char secondString[5];
  char dateString[50];
  char tempString[10];
  char periodString[6];

  float temperature = rtc.getTemperature();

  dateDisplay(now, dateString, sizeof(dateString));
  periodDisplay(now, periodString, sizeof(periodString));
  formatDateTime(now, timeString, sizeof(timeString));
  sprintf(secondString, "%02d", now.second());
  dtostrf(temperature, 4, 1, tempString);

  u8g2.setFont(u8g_font_profont29r);
  u8g2.drawStr(20, 33, timeString);
  u8g2.setFont(u8g_font_profont15r);
  u8g2.drawStr(100, 23, secondString);
  u8g2.drawStr(100, 33, periodString);
  u8g2.setFont(u8g_font_profont12r);
  u8g2.drawStr(8, 8, dateString);

  u8g2.setFont(u8g_font_7x14);
  u8g2.drawStr(45, 48, tempString);
  u8g2.drawGlyph(75, 48, 0xB0);   // Degree symbol (°)
  u8g2.drawStr(83, 48, "C");

  // Lamp status indicators: '-' = off, '°' = on
  u8g2.drawStr(40, 63,  lampState1 == HIGH ? "-" : ""); u8g2.drawGlyph(40, 63, lampState1 == HIGH ? '-' : 0xB0);
  u8g2.drawStr(55, 63,  lampState2 == HIGH ? "-" : ""); u8g2.drawGlyph(55, 63, lampState2 == HIGH ? '-' : 0xB0);
  u8g2.drawStr(70, 63,  lampState3 == HIGH ? "-" : ""); u8g2.drawGlyph(70, 63, lampState3 == HIGH ? '-' : 0xB0);
  u8g2.drawStr(85, 63,  lampState4 == HIGH ? "-" : ""); u8g2.drawGlyph(85, 63, lampState4 == HIGH ? '-' : 0xB0);

  u8g2.sendBuffer();
}


/* =============================================================================
 * formatDateTime(now, buffer, bufferSize)
 * Writes "HH:MM" in 12-hour format (no AM/PM) into buffer.
 * Hour 0 is remapped to 12 (midnight/noon display convention).
 * =============================================================================
 */
void formatDateTime(DateTime now, char *buffer, size_t bufferSize) {
  int hour12 = now.hour() % 12;
  if (hour12 == 0) hour12 = 12;
  snprintf(buffer, bufferSize, "%02d:%02d", hour12, now.minute());
}


/* =============================================================================
 * periodDisplay(now, buffer, bufferSize)
 * Writes "AM" or "PM" into buffer based on the 24-hour RTC value.
 * NOTE: The `hour12 == 0` branch has a no-op comparison bug (== instead of =);
 * it is harmless because hour12 is never used again in this function.
 * =============================================================================
 */
void periodDisplay(DateTime now, char *buffer, size_t bufferSize) {
  int hour12 = now.hour() % 12;
  if (hour12 == 0) {
    hour12 == 0;   // BUG: should be assignment; currently a no-op
  }
  const char *period = (now.hour() < 12) ? "AM" : "PM";
  snprintf(buffer, bufferSize, "%s", period);
}


/* =============================================================================
 * dateDisplay(now, buffer, bufferSize)
 * Writes "Weekday, DD-MM-YYYY" into buffer.
 * =============================================================================
 */
void dateDisplay(DateTime now, char *buffer, size_t bufferSize) {
  snprintf(buffer, bufferSize, "%s, %02d-%02d-%04d",
           daysOfTheWeek[now.dayOfTheWeek()],
           now.day(), now.month(), now.year());
}


/* =============================================================================
 * manualRelayControl()
 * Allows the physical lamp switches to toggle relays when the timer scheduler
 * is not running (count != 1 and count != 2).
 *
 * pressed()  → relay HIGH (off)
 * released() → relay LOW  (on)
 *
 * NOTE: The relay polarity here (HIGH=off on press) is counter-intuitive for
 * a push-to-make switch; review against actual hardware wiring.
 * =============================================================================
 */
void manualRelayControl() {
  bool timerActive = (count == 1 || count == 2);

  if (!timerActive) {
    for (int i = 0; i < numberOfButtons; i++) {
      if (buttons[i]->pressed()) {
        digitalWrite(relayPin[i], HIGH);
        switch (i) {
          case 0: lampState1 = HIGH; break;
          case 1: lampState2 = HIGH; break;
          case 2: lampState3 = HIGH; break;
          case 3: lampState4 = HIGH; break;
        }
      }
      if (buttons[i]->released()) {
        digitalWrite(relayPin[i], LOW);
        switch (i) {
          case 0: lampState1 = LOW; break;
          case 1: lampState2 = LOW; break;
          case 2: lampState3 = LOW; break;
          case 3: lampState4 = LOW; break;
        }
      }
    }
  }
}


/* =============================================================================
 * EEPROM SCHEDULE PERSISTENCE
 *
 * Each lamp's ON and OFF time occupies 4 bytes (hour, minute, second, period)
 * at a base address derived from:
 *   ON  base: EEPROM_ADDR_LAMP1_ON_HOUR  + lampIndex * 8
 *   OFF base: EEPROM_ADDR_LAMP1_OFF_HOUR + lampIndex * 8
 *
 * EEPROM.commit() flushes the write-buffer to flash (required on ESP32).
 * =============================================================================
 */

/** Save OFF time for lampIndex from global {time_h, time_m, time_s, isPM}. */
void saveLampOffTimeToEEPROM(int lampIndex) {
  int addr_hour   = EEPROM_ADDR_LAMP1_OFF_HOUR   + lampIndex * 8;
  int addr_minute = EEPROM_ADDR_LAMP1_OFF_MINUTE + lampIndex * 8;
  int addr_second = EEPROM_ADDR_LAMP1_OFF_SECOND + lampIndex * 8;
  int addr_period = EEPROM_ADDR_LAMP1_OFF_PERIOD + lampIndex * 8;

  EEPROM.write(addr_hour,   time_h);
  EEPROM.write(addr_minute, time_m);
  EEPROM.write(addr_second, time_s);
  EEPROM.write(addr_period, isPM);
  EEPROM.commit();
}

/** Save ON time for lampIndex from global {time_h, time_m, time_s, isPM}. */
void saveLampOnTimeToEEPROM(int lampIndex) {
  int addr_hour   = EEPROM_ADDR_LAMP1_ON_HOUR   + lampIndex * 8;
  int addr_minute = EEPROM_ADDR_LAMP1_ON_MINUTE + lampIndex * 8;
  int addr_second = EEPROM_ADDR_LAMP1_ON_SECOND + lampIndex * 8;
  int addr_period = EEPROM_ADDR_LAMP1_ON_PERIOD + lampIndex * 8;

  EEPROM.write(addr_hour,   time_h);
  EEPROM.write(addr_minute, time_m);
  EEPROM.write(addr_second, time_s);
  EEPROM.write(addr_period, isPM);
  EEPROM.commit();
}

/** Load OFF time for lampIndex into globals {endHour, endMinute, endSecond, endPeriod}. */
void loadLampOffTimeFromEEPROM(int lampIndex) {
  int addr_hour   = EEPROM_ADDR_LAMP1_OFF_HOUR   + lampIndex * 8;
  int addr_minute = EEPROM_ADDR_LAMP1_OFF_MINUTE + lampIndex * 8;
  int addr_second = EEPROM_ADDR_LAMP1_OFF_SECOND + lampIndex * 8;
  int addr_period = EEPROM_ADDR_LAMP1_OFF_PERIOD + lampIndex * 8;

  endHour   = EEPROM.read(addr_hour);
  endMinute = EEPROM.read(addr_minute);
  endSecond = EEPROM.read(addr_second);
  endPeriod = EEPROM.read(addr_period);
}

/** Load ON time for lampIndex into globals {startHour, startMinute, startSecond, startPeriod}. */
void loadLampOnTimeFromEEPROM(int lampIndex) {
  int addr_hour   = EEPROM_ADDR_LAMP1_ON_HOUR   + lampIndex * 8;
  int addr_minute = EEPROM_ADDR_LAMP1_ON_MINUTE + lampIndex * 8;
  int addr_second = EEPROM_ADDR_LAMP1_ON_SECOND + lampIndex * 8;
  int addr_period = EEPROM_ADDR_LAMP1_ON_PERIOD + lampIndex * 8;

  startHour   = EEPROM.read(addr_hour);
  startMinute = EEPROM.read(addr_minute);
  startSecond = EEPROM.read(addr_second);
  startPeriod = EEPROM.read(addr_period);
}


/* =============================================================================
 * convertTo24Hour(hour, isPM)
 * Converts a 12-hour clock value to 24-hour format.
 *   12 AM → 0    |  1-11 AM → 1-11
 *   12 PM → 12   |  1-11 PM → 13-23
 * =============================================================================
 */
int convertTo24Hour(int hour, bool isPM) {
  if (isPM && hour != 12) {
    return hour + 12;
  } else if (!isPM && hour == 12) {
    return 0;
  } else {
    return hour;
  }
}


/* =============================================================================
 * timerFunction(now)
 * Compares the current RTC time against each lamp's EEPROM-stored schedule and
 * drives the relay accordingly.
 *
 * For each lamp 0-3:
 *   - Loads ON/OFF schedule from EEPROM
 *   - If now is inside [ON time, OFF time] → relay LOW  (lamp on)
 *   - Otherwise                            → relay HIGH (lamp off)
 *
 * Skipped entirely when manualControlActive is true.
 * =============================================================================
 */
void timerFunction(DateTime now) {
  if (manualControlActive) return;

  for (int lampIndex = 0; lampIndex < 4; lampIndex++) {
    loadLampOnTimeFromEEPROM(lampIndex);
    loadLampOffTimeFromEEPROM(lampIndex);

    if (isTimeInRange(now, startHour, startMinute, startSecond, startPeriod,
                           endHour,   endMinute,   endSecond,   endPeriod)) {
      digitalWrite(relayPin[lampIndex], LOW);   // Lamp on
    } else {
      digitalWrite(relayPin[lampIndex], HIGH);  // Lamp off
    }
  }
}


/* =============================================================================
 * isTimeInRange(now, start..., end...)
 * Returns true if the current RTC time falls within the window
 * [startHour:startMinute:startSecond, endHour:endMinute:endSecond].
 *
 * Both 12-hour inputs are converted to 24-hour seconds-since-midnight for
 * comparison.  Handles overnight windows (end < start) correctly via Case 2.
 * =============================================================================
 */
bool isTimeInRange(DateTime now,
                   int startHour, int startMinute, int startSecond, bool startPeriod,
                   int endHour,   int endMinute,   int endSecond,   bool endPeriod) {
  int currentHour   = now.hour();
  int currentMinute = now.minute();
  int currentSecond = now.second();

  int startHour24 = convertTo24Hour(startHour, startPeriod);
  int endHour24   = convertTo24Hour(endHour,   endPeriod);

  int startTotal   = startHour24 * 3600 + startMinute * 60 + startSecond;
  int endTotal     = endHour24   * 3600 + endMinute   * 60 + endSecond;
  int currentTotal = currentHour * 3600 + currentMinute * 60 + currentSecond;

  if (startTotal <= endTotal) {
    // Same-day window
    return (currentTotal >= startTotal && currentTotal <= endTotal);
  } else {
    // Overnight window (e.g. 22:00 → 06:00)
    return (currentTotal >= startTotal || currentTotal <= endTotal);
  }
}


/* =============================================================================
 * timerSection()
 * Renders the schedule editor screen (Mode 2).
 *
 * Layout:
 *   Line 1 (y=15) : "Schedule" header
 *   Line 2 (y=30) : "HH : MM : SS AM/PM"  — editable time fields
 *   Line 3 (y=45) : Selected lamp label
 *   Line 4 (y=60) : "ON / OFF"  — save target selectors
 *
 * A down-arrow glyph (0x0076 in tiny_simon font) appears under the currently
 * selected field box to show which value the UP/DOWN buttons will modify.
 *
 * BACK button in selectedComponent==5 → save ON time to EEPROM
 * BACK button in selectedComponent==6 → save OFF time to EEPROM
 * =============================================================================
 */
void timerSection() {
  u8g2.clearBuffer();

  char hhString[3], mmString[3], ssString[3];
  snprintf(hhString, sizeof(hhString), "%02d", time_h);
  snprintf(mmString, sizeof(mmString), "%02d", time_m);
  snprintf(ssString, sizeof(ssString), "%02d", time_s);

  u8g2.setFont(u8g_font_courB12);
  u8g2.drawStr(25, 15, "Schedule");
  u8g2.setFont(u8g_font_5x8r);

  char timeString[20];
  snprintf(timeString, sizeof(timeString), "%s : %s : %s %s",
           hhString, mmString, ssString, isPM ? "PM" : "AM");
  u8g2.drawStr(25, 30, timeString);
  u8g2.drawStr(50, 45, lamp_menu_items[lampSelected]);
  u8g2.drawStr(47, 60, "ON");
  u8g2.drawStr(60, 60, "/");
  u8g2.drawStr(67, 60, "OFF");

  // Indicator arrow X/Y position — moves with selectedComponent
  int boxX = 29;
  int boxY = 20;

  if (selectedComponent == 1) { boxX = 53; }
  if (selectedComponent == 2) { boxX = 78; }
  if (selectedComponent == 3) { boxX = 93; }
  if (selectedComponent == 4) { boxX = 60; boxY = 35; }

  if (selectedComponent == 5) {
    boxX = 49; boxY = 52;
    if (backButton.pressed()) {
      Serial.printf("Time on: %d:%d:%d\n", time_h, time_m, time_s);
      saveLampOnTimeToEEPROM(lampSelected);
    }
  }

  if (selectedComponent == 6) {
    boxX = 72; boxY = 52;
    if (backButton.pressed()) {
      Serial.printf("Time off: %d:%d:%d\n", time_h, time_m, time_s);
      saveLampOffTimeToEEPROM(lampSelected);
    }
  }

  u8g2.setFont(u8g2_font_tiny_simon_tr);
  u8g2.drawGlyph(boxX, boxY, 0x0076);   // Down-arrow indicator
  u8g2.sendBuffer();
}


/* =============================================================================
 * monitoringDisplay(now)
 * Renders a scrollable schedule summary for all 4 lamps (Mode 1, INA219 sub).
 *
 * Layout (scrolled by scrollOffset):
 *   Top    : Current time HH:MM:SS AM/PM
 *   Below  : For each lamp, two rows:
 *             "Lamp N On:  HH:MM:SS AM/PM"
 *             "Lamp N Off: HH:MM:SS AM/PM"
 *
 * Each lamp block is 20 px tall; total content height = 4 × 20 = 80 px.
 * The display is 64 px, so maxScrollOffset() = 16 px.
 * =============================================================================
 */
void monitoringDisplay(DateTime now) {
  u8g2.clearBuffer();
  int x = 8;
  int y = 12 - scrollOffset;   // Scroll shifts all content upward

  char timeString[20], secondString[5], periodString[6];
  formatDateTime(now, timeString, sizeof(timeString));
  periodDisplay(now, periodString, sizeof(periodString));
  sprintf(secondString, "%02d", now.second());

  u8g2.setFont(u8g_font_5x8r);
  u8g2.drawStr(8, y, timeString);
  u8g2.drawStr(33, y, ":");
  u8g2.drawStr(38, y, secondString);
  u8g2.drawStr(50, y, periodString);

  y += 20;

  for (int i = 0; i < 4; i++) {
    // Read each lamp's schedule directly from EEPROM (avoids stale globals)
    int onHour   = EEPROM.read(EEPROM_ADDR_LAMP1_ON_HOUR   + i * 8);
    int onMinute = EEPROM.read(EEPROM_ADDR_LAMP1_ON_MINUTE + i * 8);
    int onSecond = EEPROM.read(EEPROM_ADDR_LAMP1_ON_SECOND + i * 8);
    int onPeriod = EEPROM.read(EEPROM_ADDR_LAMP1_ON_PERIOD + i * 8);

    int offHour   = EEPROM.read(EEPROM_ADDR_LAMP1_OFF_HOUR   + i * 8);
    int offMinute = EEPROM.read(EEPROM_ADDR_LAMP1_OFF_MINUTE + i * 8);
    int offSecond = EEPROM.read(EEPROM_ADDR_LAMP1_OFF_SECOND + i * 8);
    int offPeriod = EEPROM.read(EEPROM_ADDR_LAMP1_OFF_PERIOD + i * 8);

    char onTimeString[30], offTimeString[30];
    sprintf(onTimeString,  "Lamp %d On: %02d:%02d:%02d %s", i + 1, onHour,  onMinute,  onSecond,  onPeriod  == 0 ? "AM" : "PM");
    sprintf(offTimeString, "Lamp %d Off: %02d:%02d:%02d %s", i + 1, offHour, offMinute, offSecond, offPeriod == 0 ? "AM" : "PM");

    u8g2.setFont(u8g_font_5x8r);
    u8g2.drawStr(x, y + (i * 20),      onTimeString);
    u8g2.drawStr(x, y + (i * 20) + 10, offTimeString);
  }

  u8g2.sendBuffer();
}


/* =============================================================================
 * maxScrollOffset()
 * Returns the maximum number of pixels the monitoring screen can scroll down
 * before the last lamp row leaves the display.
 * =============================================================================
 */
int maxScrollOffset() {
  const int totalItems   = 4;   // Number of lamps
  const int itemHeight   = 30;  // Pixels per lamp block (two 10 px text rows + gap)
  const int visibleHeight = 64; // OLED display height

  return max(0, (totalItems * itemHeight) - visibleHeight);
}


/* =============================================================================
 * handleScrollInput()
 * Adjusts scrollOffset when UP/DOWN buttons are pressed inside the monitoring
 * view. Clamped to [0, maxScrollOffset()].
 * =============================================================================
 */
void handleScrollInput() {
  if (upButton.pressed()) {
    scrollOffset = max(0, scrollOffset - scrollStep);
  }
  if (downButton.pressed()) {
    scrollOffset = min(scrollOffset + scrollStep, maxScrollOffset());
  }
}


/* =============================================================================
 * readSensors()
 * Samples all INA219 channels and stores results in global floats.
 * Raw mA and mW values are divided by 1000 to convert to A and W.
 * Negative current (possible when load is disconnected) is clamped to 0.
 * =============================================================================
 */
void readSensors() {
  current_mA   = ina219.getCurrent_mA() / 1000.0f;   // mA → A
  power_mW     = ina219.getPower_mW()   / 1000.0f;   // mW → W
  shuntvoltage = ina219.getShuntVoltage_mV();         // mV
  busvoltage   = ina219.getBusVoltage_V();            // V
  loadvoltage  = busvoltage + (shuntvoltage / 1000.0f); // V

  if (current_mA < 0) {
    current_mA = 0.0f;   // Clamp noise / reverse-polarity artifacts
  }
}


/* =============================================================================
 * updateFirebase()
 * Pushes the latest sensor readings to Firebase RTDB paths:
 *   Sensor/current  — current in A (5 decimal places)
 *   Sensor/voltage  — load voltage in V (2 decimal places)
 *   Sensor//power   — power in W (2 decimal places)  [note: double slash is a bug]
 *
 * A 5-second software timeout is tracked but only used to distinguish a true
 * failure from a timeout in the Serial output; it does not abort the Firebase
 * calls themselves.
 * =============================================================================
 */
void updateFirebase() {
  String formattedCurrent = String(current_mA,  5);
  String formattedVoltage = String(loadvoltage,  2);
  String formattedPower   = String(power_mW,     2);

  unsigned long startTime = millis();
  const unsigned long timeout = 5000;

  bool success = true;
  success &= Firebase.RTDB.setString(&fbdo, "Sensor/current",  formattedCurrent);
  success &= Firebase.RTDB.setString(&fbdo, "Sensor/voltage",  formattedVoltage);
  success &= Firebase.RTDB.setString(&fbdo, "Sensor//power",   formattedPower);  // double-slash bug

  if (success) {
    Serial.println("Firebase update successful.");
    Serial.print("Current: "); Serial.println(formattedCurrent);
    Serial.print("Voltage: "); Serial.println(formattedVoltage);
    Serial.print("Power: ");   Serial.println(formattedPower);
  } else {
    unsigned long elapsed = millis() - startTime;
    if (elapsed >= timeout) {
      Serial.println("Firebase update timed out.");
    } else {
      Serial.println("Firebase update failed: " + fbdo.errorReason());
    }
  }
}


/* =============================================================================
 * sensorDisplay()
 * Renders the live INA219 readout screen (Mode 1 → Member sub-menu).
 *
 * Layout (uses epd_bitmap_border decorative frame):
 *   Left column  : Voltage (V) top, Current (A) bottom
 *   Right column : Power (W)
 * =============================================================================
 */
void sensorDisplay() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g_font_5x8r);
  u8g2.drawBitmap(0, 0, 128 / 8, 64, epd_bitmap_border);

  u8g2.drawStr(13, 17, "Voltage");
  u8g2.drawStr(13, 40, "Current");
  u8g2.drawStr(85, 17, "Power");

  char voltageStr[10], currentStr[10], powerStr[10];
  dtostrf(loadvoltage, 5, 2, voltageStr);
  dtostrf(current_mA,  5, 4, currentStr);
  dtostrf(power_mW,    5, 2, powerStr);

  u8g2.drawStr(20, 27, voltageStr);
  u8g2.drawStr(20, 50, currentStr);
  u8g2.drawStr(85, 34, powerStr);

  u8g2.drawStr(59, 23, "V");
  u8g2.drawStr(59, 47, "A");
  u8g2.drawStr(95, 47, "W");

  u8g2.sendBuffer();
}


/* =============================================================================
 * connectWiFi()
 * Connects the ESP32 to the configured SSID using a blocking poll loop.
 * Sets wifiConnected = true on success.
 *
 * NOTE: The while(WiFi.status() != WL_CONNECTED) loop with delay(500) is
 * fully blocking — the display and all sensors freeze during connection.
 * Consider using a non-blocking WiFi manager for production use.
 * =============================================================================
 */
void connectWiFi() {
  Serial.println("Connecting to WiFi");

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Already connected to WiFi.");
    wifiConnected = true;
    return;
  }

  if (strlen(WIFI_SSID) > 0 && strlen(WIFI_PASSWORD) > 0) {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println();
    Serial.print("Connected with IP: ");
    Serial.println(WiFi.localIP());
    wifiConnected = true;
  } else {
    Serial.println("WiFi credentials not provided. Skipping WiFi connection.");
  }
}


/* =============================================================================
 * initializeFirebase()
 * Configures and starts the Firebase client with anonymous sign-up auth.
 * tokenStatusCallback (from TokenHelper.h) handles automatic token refresh.
 * =============================================================================
 */
void initializeFirebase() {
  config.api_key      = API_KEY;
  config.database_url = DATABASE_URL;

  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("signUp OK");
    signupOK = true;
  } else {
    Serial.printf("%s\n", config.signer.signupError.message.c_str());
  }

  config.token_status_callback = tokenStatusCallback;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}


/* =============================================================================
 * firebaseLampControl()
 * Polls Firebase RTDB for each lamp's remote control state:
 *   "Control/Lamp/Lamp N" == "0" → relay HIGH (lamp off)
 *   "Control/Lamp/Lamp N" == "1" → relay LOW  (lamp on)
 *
 * Called every 10 ms from loop() Mode 0 — Firebase internally rate-limits
 * RTDB GET requests, so this will not flood the server, but consider using
 * streaming (Firebase.RTDB.beginStream) for truly real-time control without
 * repeated polling.
 * =============================================================================
 */
void firebaseLampControl() {
  // Lamp 1
  if (Firebase.RTDB.getString(&lampControl, "Control/Lamp/Lamp 1")) {
    String state = lampControl.stringData();
    if (state == "0") {
      digitalWrite(relayPin[0], HIGH); lampState1 = HIGH;
    } else if (state == "1") {
      digitalWrite(relayPin[0], LOW);  lampState1 = LOW;
    }
  }

  // Lamp 2
  if (Firebase.RTDB.getString(&lampControl, "Control/Lamp/Lamp 2")) {
    String state = lampControl.stringData();
    if (state == "0") {
      digitalWrite(relayPin[1], HIGH); lampState2 = HIGH;
    } else if (state == "1") {
      digitalWrite(relayPin[1], LOW);  lampState2 = LOW;
    }
  }

  // Lamp 3
  if (Firebase.RTDB.getString(&lampControl, "Control/Lamp/Lamp 3")) {
    String state = lampControl.stringData();
    if (state == "0") {
      digitalWrite(relayPin[2], HIGH); lampState3 = HIGH;
    } else if (state == "1") {
      digitalWrite(relayPin[2], LOW);  lampState3 = LOW;
    }
  }

  // Lamp 4
  if (Firebase.RTDB.getString(&lampControl, "Control/Lamp/Lamp 4")) {
    String state = lampControl.stringData();
    if (state == "0") {
      digitalWrite(relayPin[3], HIGH); lampState4 = HIGH;
    } else if (state == "1") {
      digitalWrite(relayPin[3], LOW);  lampState4 = LOW;
    }
  }
}
