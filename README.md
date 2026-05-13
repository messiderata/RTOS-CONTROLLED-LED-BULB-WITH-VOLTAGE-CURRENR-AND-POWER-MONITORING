# RTOS-Controlled LED Bulb with Voltage, Current, and Power Monitoring

An ESP32-based smart lighting system that controls up to **4 AC lamp circuits** via relay outputs. It features real-time power monitoring (INA219), an OLED user interface with a scrollable menu, RTC-based scheduling (DS3231), EEPROM-persisted lamp schedules, and Firebase Realtime Database integration for remote lamp control and sensor data upload.

---

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [System Architecture](#2-system-architecture)
3. [Flowcharts](#3-flowcharts)
4. [Feature Documentation](#4-feature-documentation)
5. [Hardware Documentation](#5-hardware-documentation)
6. [EEPROM Layout](#6-eeprom-layout)
7. [Firebase Database Structure](#7-firebase-database-structure)
8. [Code Quality Notes](#8-code-quality-notes)
9. [Team Members](#9-team-members)

---

## 1. Project Overview

| Field | Detail |
|---|---|
| **Project Title** | RTOS-Controlled LED Bulb with Voltage, Current, and Power Monitoring |
| **Platform** | ESP32 (Arduino framework) |
| **Main Purpose** | Automate and remotely control 4 AC lamp circuits with real-time power monitoring |
| **Key Features** | Relay control, RTC scheduling, INA219 monitoring, OLED UI, Firebase remote control |
| **Communication** | I2C (RTC, OLED, INA219), WiFi 802.11 (Firebase RTDB), EEPROM (schedule persistence) |
| **Target Users** | Students, embedded systems hobbyists, home automation experimenters |

### Key Functionalities

- **3-mode OLED interface** — switch between Clock, Menu, and Schedule Editor with a dedicated button
- **4-lamp relay control** — active-LOW relays controlled by timer schedule or Firebase commands
- **INA219 power monitoring** — reads voltage (V), current (A), and power (W) every second
- **RTC-based scheduler** — per-lamp ON/OFF times stored in EEPROM, survive power cycles
- **Firebase remote control** — lamps toggled remotely via `Control/Lamp/Lamp N` RTDB paths
- **Firebase sensor upload** — live sensor data pushed to `Sensor/` every 5 seconds
- **OLED scrollable menu** — main menu + sub-menus with scrollbar, selector bitmap, and icon bitmaps

---

## 2. System Architecture

### Layer Diagram

```
┌─────────────────────────────────────────────────────────────┐
│  INPUT LAYER                                                │
│  Navigation Buttons: UP / DOWN / ENTER / BACK / SCREEN     │
│  Physical Lamp Switches: GPIO 13, 12, 14, 27                │
├─────────────────────────────────────────────────────────────┤
│  SENSOR LAYER                                               │
│  INA219  → Voltage (V), Current (A), Power (W)             │
│  DS3231  → Time, Date, Temperature (°C)                    │
├─────────────────────────────────────────────────────────────┤
│  PROCESSING LAYER                                           │
│  Mode state-machine (count 0 / 1 / 2)                      │
│  RTC-based lamp scheduler (isTimeInRange)                   │
│  Schedule editor (selectedComponent 0–6)                    │
│  Menu navigation (mainMenuSelection / submenuSelection)     │
├─────────────────────────────────────────────────────────────┤
│  COMMUNICATION LAYER                                        │
│  WiFi → Firebase RTDB                                       │
│    Upload: Sensor/current, Sensor/voltage, Sensor/power     │
│    Read:   Control/Lamp/Lamp 1–4                            │
│  I2C → DS3231, INA219, SH1106 OLED                         │
│  EEPROM → Persistent lamp schedules                         │
├─────────────────────────────────────────────────────────────┤
│  OUTPUT LAYER                                               │
│  4× Relay modules → AC lamp circuits                        │
│  SH1106 128×64 OLED → UI display                           │
└─────────────────────────────────────────────────────────────┘
```

### Main Control Flow

```
setup()
  │
  ├─ Init Serial, OLED, RTC, INA219
  ├─ Init navigation buttons + lamp switches + relay GPIOs
  ├─ Load lamp schedules from EEPROM
  └─ Connect WiFi → Initialize Firebase

loop()  (runs forever)
  │
  ├─ Every  1 000 ms → readSensors()
  ├─ Every  5 000 ms → updateFirebase()
  └─ Every     10 ms → mode state-machine
       │
       ├─ count == 0 → clockDisplay() + firebaseLampControl()
       ├─ count == 1 → timerFunction() + OLED menu tree
       └─ count == 2 → timerSection() + timerFunction() + ButtonPress()
```

### Screen Mode State Machine

```
         SCREEN button pressed
              │
    ┌─────────▼──────────┐
    │   count = 0        │◄────────────────┐
    │   Clock Display    │                 │
    │   Firebase Control │                 │
    └─────────┬──────────┘                 │
              │ SCREEN pressed             │
    ┌─────────▼──────────┐                 │
    │   count = 1        │                 │
    │   OLED Menu Tree   │                 │
    │   Timer Scheduler  │                 │
    └─────────┬──────────┘                 │
              │ SCREEN pressed             │ count > 2
    ┌─────────▼──────────┐                 │
    │   count = 2        │─────────────────┘
    │   Schedule Editor  │
    │   Timer Scheduler  │
    └────────────────────┘
```

### Menu Tree (Mode 1)

```
Main Menu
├── [0] INA 219       → monitoringDisplay() — scrollable schedule list
├── [1] Monitoring    → person_menu_items list
│       ├── Prudencio  → member detail card
│       ├── Amista     → member detail card
│       ├── Alvdendia  → member detail card
│       ├── Manginsay  → member detail card
│       └── Rivera     → member detail card
└── [2] Member        → sensorDisplay() — live V/I/P readout
```

---

## 3. Flowcharts

### System Startup

```
START
  │
  ▼
Init Serial (115200 baud)
  │
  ▼
Init OLED (SH1106 128×64, I2C)
  │
  ▼
Init DS3231 RTC ──── FAIL ──► Halt (infinite loop)
  │
  ▼
RTC lost power? ──── YES ───► Set time from compile timestamp
  │ NO
  ▼
Disable DS3231 square-wave output (register 0x0E)
  │
  ▼
Init navigation buttons (UP, DOWN, ENTER, BACK, SCREEN)
  │
  ▼
Init INA219 ──── FAIL ──► Halt (infinite loop)
  │
  ▼
Set INA219 calibration: 16V / 400mA
  │
  ▼
Init lamp switches + relay GPIO pins (OUTPUT)
  │
  ▼
Init EEPROM (512 bytes), load lamp 0 schedule
  │
  ▼
connectWiFi() ──── FAIL ──► Skip Firebase, continue without cloud
  │
  ▼
initializeFirebase()
  │
  ▼
Enter loop()
```

### Main Loop Execution

```
loop() ──────────────────────────────────────────────────────┐
  │                                                           │
  ▼                                                           │
currentMillis = millis()                                      │
  │                                                           │
  ├── (currentMillis - prevSensor  ≥ 1000ms) ? readSensors() │
  ├── (currentMillis - prevFirebase ≥ 5000ms) ? updateFirebase()
  │                                                           │
  └── (currentMillis - prevMillis  ≥ 10ms)  ?                │
        │                                                     │
        ├── SCREEN pressed? → count++                        │
        │                                                     │
        ├── count == 0 → clockDisplay() + firebaseLampControl()
        ├── count == 1 → timerFunction() + menu display      │
        ├── count == 2 → timerSection() + timerFunction() + ButtonPress()
        └── count >  2 → count = 0                           │
                                                              │
  loadLampOnTimeFromEEPROM(lampSelected) ◄───────────────────┘
  loadLampOffTimeFromEEPROM(lampSelected)
```

### Sensor Reading Cycle

```
readSensors()
  │
  ├─ current_mA  = getCurrent_mA()  / 1000   [mA → A]
  ├─ power_mW    = getPower_mW()    / 1000   [mW → W]
  ├─ shuntvoltage = getShuntVoltage_mV()     [mV]
  ├─ busvoltage  = getBusVoltage_V()         [V]
  └─ loadvoltage = busvoltage + shuntvoltage/1000  [V]
       │
       └── current_mA < 0 ? → clamp to 0.0
```

### Timer Scheduler Decision

```
timerFunction(now)
  │
  ├── manualControlActive == true? → RETURN (manual override)
  │
  └── FOR each lamp (0 to 3):
        │
        ├─ loadLampOnTimeFromEEPROM(lampIndex)
        ├─ loadLampOffTimeFromEEPROM(lampIndex)
        │
        └─ isTimeInRange(now, start..., end...)?
               │
               ├── YES → digitalWrite(relayPin, LOW)   [lamp ON]
               └── NO  → digitalWrite(relayPin, HIGH)  [lamp OFF]

isTimeInRange()
  │
  ├─ Convert start/end 12h → 24h → total seconds
  │
  ├─ startTotal ≤ endTotal?
  │     YES → same-day window:   start ≤ now ≤ end
  │     NO  → overnight window:  now ≥ start OR now ≤ end
  │
  └─ Return true/false
```

### Schedule Editor (Mode 2)

```
timerSection() renders UI
  │
  ▼
ENTER button → selectedComponent = (selectedComponent + 1) % 7
  │
  ▼
UP / DOWN buttons adjust selected field:
  component 0 → hour    (1–12, wraps)
  component 1 → minute  (0–59)
  component 2 → second  (0–59)
  component 3 → AM/PM   (toggle)
  component 4 → lamp    (0–3)
  component 5 → [ON  save field] BACK → saveLampOnTimeToEEPROM()
  component 6 → [OFF save field] BACK → saveLampOffTimeToEEPROM()
```

### Firebase Communication Flow

```
updateFirebase() (every 5 s)
  │
  ├─ Format: current (5 dp), voltage (2 dp), power (2 dp)
  ├─ setString → "Sensor/current"
  ├─ setString → "Sensor/voltage"
  └─ setString → "Sensor//power"   ← double-slash (known bug)

firebaseLampControl() (every 10 ms, Mode 0)
  │
  └─ FOR each lamp 1–4:
       getString("Control/Lamp/Lamp N")
         "0" → relay HIGH (lamp OFF)
         "1" → relay LOW  (lamp ON)
```

---

## 4. Feature Documentation

---

### FEATURE: 3-Mode OLED Interface

**DESCRIPTION:** A single SCREEN button cycles through three display modes. All rendering is non-blocking (millis()-based, 10 ms tick).

**INPUTS:** SCREEN button (GPIO 5) press event

**OUTPUTS:** OLED screen content; `count` global variable (0 / 1 / 2)

**LOGIC:** `count` increments on each press. Values > 2 reset to 0.

**ERROR CASES:** None — wraps safely.

**FUTURE IMPROVEMENTS:** Add mode labels or transition animations.

---

### FEATURE: Clock Display (Mode 0)

**DESCRIPTION:** Shows the current time (large 12-hour HH:MM), seconds, AM/PM, date, DS3231 chip temperature, and a 4-lamp status bar.

**INPUTS:** `DateTime now` from DS3231 RTC

**OUTPUTS:** OLED frame buffer → display

**LOGIC:**
- `formatDateTime()` converts 24h RTC → 12h "HH:MM"
- `periodDisplay()` derives "AM" / "PM"
- `dateDisplay()` formats "Weekday, DD-MM-YYYY"
- Lamp status: `lampStateN == HIGH` → "-" (off), else "°" glyph (on)

**ERROR CASES:** RTC not found → halted in `setup()`.

**FUTURE IMPROVEMENTS:** Add WiFi signal icon; replace "-"/"°" with proper lamp glyphs.

---

### FEATURE: OLED Menu Tree (Mode 1)

**DESCRIPTION:** A 3-item scrollable main menu with icons, scrollbar, and a selection highlight bitmap. Navigates into sub-screens for INA219, member list, and sensor readout.

**INPUTS:** UP / DOWN / ENTER / BACK buttons

**OUTPUTS:** OLED — menu list, sub-menu list, member detail card, or sensor/monitoring screen

**LOGIC:** Rotating-window render: selected item always at visual position index 1 (bold font). Items rendered as `(selection + i) % numItems`.

**ERROR CASES:** None — array indices are always modulo-bounded.

**FUTURE IMPROVEMENTS:** Add icons for all sub-menu entries; replace placeholder person bitmaps.

---

### FEATURE: Schedule Editor (Mode 2)

**DESCRIPTION:** Allows the user to set per-lamp ON and OFF times using UP/DOWN/ENTER buttons. Times are saved to EEPROM on BACK press.

**INPUTS:** UP, DOWN, ENTER, BACK buttons; `lampSelected` (0–3)

**OUTPUTS:** EEPROM write (ON or OFF time); Serial debug print; OLED display with indicator arrow

**LOGIC:**
- ENTER cycles through 7 fields (hour → minute → second → AM/PM → lamp → save-ON → save-OFF)
- UP/DOWN adjust the currently selected field
- BACK at field 5 → `saveLampOnTimeToEEPROM()`; at field 6 → `saveLampOffTimeToEEPROM()`

**ERROR CASES:** If `lampSelected` is out of range [0–3], EEPROM writes to incorrect addresses.

**FUTURE IMPROVEMENTS:** Add input validation; show confirmation feedback on save; add RTC sync.

---

### FEATURE: RTC-Based Lamp Scheduler

**DESCRIPTION:** Automatically turns each lamp ON or OFF based on its stored EEPROM schedule, checked against the current RTC time.

**INPUTS:** `DateTime now` (DS3231); EEPROM-stored ON/OFF times for each lamp

**OUTPUTS:** `digitalWrite()` on relay pins 26, 25, 33, 32

**LOGIC:** Converts 12h stored times to 24h seconds-since-midnight, then checks if `currentTime` is inside the [ON, OFF] window. Handles overnight windows (end < start).

**ERROR CASES:** Uninitialized EEPROM (0xFF bytes) → schedule with hour=255, which is never matched.

**FUTURE IMPROVEMENTS:** Add day-of-week scheduling; validate EEPROM on first boot; add manualControlActive toggle from UI.

---

### FEATURE: INA219 Power Monitoring

**DESCRIPTION:** Reads load voltage, current, and power from the INA219 sensor every second.

**INPUTS:** INA219 hardware (I2C); `sensorInterval` = 1000 ms

**OUTPUTS:** Globals: `loadvoltage` (V), `current_mA` (A), `power_mW` (W), `shuntvoltage` (mV), `busvoltage` (V)

**LOGIC:**
- `loadvoltage = busvoltage + shuntvoltage / 1000`
- Negative current clamped to 0 (prevents noise artefacts on open load)
- Calibrated for 16 V / 400 mA range

**ERROR CASES:** INA219 not detected → halted in `setup()`.

**FUTURE IMPROVEMENTS:** Add overload alert; log energy (Wh) over time.

---

### FEATURE: Firebase Sensor Upload

**DESCRIPTION:** Pushes the latest V/I/P readings to Firebase RTDB every 5 seconds.

**INPUTS:** `current_mA`, `loadvoltage`, `power_mW`; `firebaseInterval` = 5000 ms

**OUTPUTS:** Firebase paths: `Sensor/current`, `Sensor/voltage`, `Sensor//power`

**LOGIC:** All three `setString()` calls are ANDed into a `success` flag. Timeout tracking (5 s) is logged but does not abort the calls.

**ERROR CASES:**
- WiFi disconnected → Firebase calls silently fail; `fbdo.errorReason()` logged
- Double-slash bug: `"Sensor//power"` creates a nested node `/power` inside an empty node

**FUTURE IMPROVEMENTS:** Fix double-slash path; add WiFi reconnect logic; use `setFloat` instead of `setString`.

---

### FEATURE: Firebase Remote Lamp Control

**DESCRIPTION:** Reads lamp command states from Firebase RTDB and drives relays accordingly (Mode 0 only).

**INPUTS:** Firebase paths `Control/Lamp/Lamp 1` through `Control/Lamp/Lamp 4`; values "0" (off) or "1" (on)

**OUTPUTS:** `digitalWrite()` on relay pins; `lampState1–4` globals updated

**LOGIC:** Polled every 10 ms inside Mode 0. Firebase SDK rate-limits internally.

**ERROR CASES:** Timer scheduler (Mode 1/2) overrides Firebase commands since both write to the same relay pins.

**FUTURE IMPROVEMENTS:** Use `Firebase.RTDB.beginStream()` for real-time push instead of polling; add priority arbitration between scheduler and remote control.

---

## 5. Hardware Documentation

### Wiring Diagram

![Wiring Diagram](wire_diagram.png)

### Pin Mapping Table

| GPIO | Direction | Connected To | Notes |
|------|-----------|--------------|-------|
| 26 | OUTPUT | Relay 1 | Active-LOW relay (LOW = lamp ON) |
| 25 | OUTPUT | Relay 2 | Active-LOW relay |
| 33 | OUTPUT | Relay 3 | Active-LOW relay |
| 32 | OUTPUT | Relay 4 | Active-LOW relay |
| 13 | INPUT | Lamp Switch 1 | Physical toggle switch |
| 12 | INPUT | Lamp Switch 2 | Physical toggle switch |
| 14 | INPUT | Lamp Switch 3 | Physical toggle switch |
| 27 | INPUT | Lamp Switch 4 | Physical toggle switch |
| 17 | INPUT | UP button | Navigation |
| 4 | INPUT | DOWN button | Navigation |
| 18 | INPUT | ENTER button | Navigation |
| 19 | INPUT | BACK button | Navigation |
| 5 | INPUT | SCREEN button | Mode switch |
| SDA | I2C | DS3231, INA219, SH1106 | Shared I2C bus |
| SCL | I2C | DS3231, INA219, SH1106 | Shared I2C bus |

### I2C Device Addresses

| Device | Address | Library |
|--------|---------|---------|
| DS3231 RTC | 0x68 | RTClib |
| SH1106 OLED | 0x3C (default) | U8g2 |
| INA219 | 0x40 (default) | Adafruit_INA219 |

### INA219 Calibration

| Parameter | Value |
|-----------|-------|
| Mode | `setCalibration_16V_400mA()` |
| Max bus voltage | 16 V |
| Max shunt current | 400 mA |
| Shunt resistor | 0.1 Ω (Adafruit default) |

### Relay Wiring Summary

```
ESP32 GPIO (OUTPUT)
      │
      └──► Relay IN pin
                │
       ┌────────┘
       │
  [NC]─┤  Normally-Closed (lamp off when relay energised)
  [COM]─┤  Common
  [NO]─┘  Normally-Open   (lamp on when relay energised)
               │
          AC Lamp circuit
```

> **Note:** Relays are driven active-LOW — `digitalWrite(pin, LOW)` energises the relay and turns the lamp ON. `HIGH` = lamp OFF.

### Power Supply Considerations

- ESP32: 3.3 V logic, 5 V USB input (on-board regulator)
- Relay modules typically require 5 V coil voltage — use a separate 5 V rail if powering from USB
- INA219 measures the shunt on the low-side of the DC monitoring circuit
- AC relay switching — ensure proper isolation between the ESP32 low-voltage side and the mains AC side

---

## 6. EEPROM Layout

Total EEPROM size: **512 bytes**. Each lamp occupies **8 bytes**:

```
Byte offset   Field
───────────────────────────────────────────────
  +0          ON Hour    (1–12)
  +1          ON Minute  (0–59)
  +2          ON Second  (0–59)
  +3          ON Period  (0 = AM, 1 = PM)
  +4          OFF Hour   (1–12)
  +5          OFF Minute (0–59)
  +6          OFF Second (0–59)
  +7          OFF Period (0 = AM, 1 = PM)
```

| Lamp | ON base addr | OFF base addr |
|------|-------------|--------------|
| 1 | 8 | 12 |
| 2 | 16 | 20 |
| 3 | 24 | 28 |
| 4 | 32 | 36 |

Formula: `ON base = 8 + lampIndex * 8`, `OFF base = 12 + lampIndex * 8`

---

## 7. Firebase Database Structure

```
Firebase Realtime Database
│
├── Sensor/
│   ├── current   (String, e.g. "0.00123")   ← pushed every 5 s
│   ├── voltage   (String, e.g. "220.15")
│   └── /power    (String, e.g. "27.13")     ← note: double-slash bug in path
│
└── Control/
    └── Lamp/
        ├── Lamp 1  ("0" = off | "1" = on)   ← read every 10 ms (Mode 0)
        ├── Lamp 2
        ├── Lamp 3
        └── Lamp 4
```

---

## 8. Code Quality Notes

### Known Bugs

| Location | Issue | Impact |
|----------|-------|--------|
| `periodDisplay()` | `hour12 == 0;` is a comparison, not assignment (no-op) | None — `hour12` unused after the branch |
| `updateFirebase()` | Path `"Sensor//power"` has a double slash | Creates a nested node at wrong path in Firebase |
| `setup()` | `Serial.begin(115200)` called twice | Harmless but redundant |
| `ina219.begin()` | Called twice in `setup()` | Harmless but redundant |

### Performance Concerns

| Issue | Recommendation |
|-------|---------------|
| `loadLampOnTimeFromEEPROM()` called every `loop()` iteration | Call only when `lampSelected` changes or after a save — EEPROM reads are slow (~3 µs each) |
| `connectWiFi()` blocks entire MCU during connection (delay-based loop) | Use a non-blocking WiFi state machine or WiFiManager |
| `firebaseLampControl()` polls 4 RTDB paths every 10 ms | Use `Firebase.RTDB.beginStream()` for event-driven real-time control |
| Scroll input `previousMillis1` check compares wrong variable | Change to `currentMillis - previousMillis1 >= interval1` |

### Suggested Improvements

- Move WiFi credentials and Firebase API key to a `secrets.h` file excluded from version control
- Replace `String` type in Firebase calls with `const char*` to reduce heap fragmentation
- Add `manualControlActive` toggle in Mode 1 menu to allow manual relay override through the UI
- Validate EEPROM data on first boot (check for 0xFF sentinel) to avoid invalid schedule times
- Add watchdog timer reset to recover from Firebase or WiFi hangs

---

## 9. Team Members

| Name | Gender | Age | Location |
|------|--------|-----|----------|
| Philip Joshua F. Amista | Male | 21 | Marikina City |
| Kim Alvendia | Female | 22 | Marikina City |
| Mars Laurenz Manginsay | Male | 21 | Caloocan |
| James Rivera | Male | 21 | Laguna |
| Kimberly Prudencio | Female | 23 | Cavite |
