# Smart Plant Care Station: Project Plan

Embedded Systems Semester Project | Raspberry Pi Pico W, C++, ThingSpeak, React | Draft v1.0, 16 November 2026

## 1. Executive summary

This project builds a self-contained plant monitoring and care node. A Raspberry Pi Pico W reads outdoor air temperature (DHT22), light (Grove Sunlight Sensor) and soil moisture (Grove Soil Moisture Sensor), decides locally what to do (LED on/off), a MOSFET-switched grow light) and uploads telemetry to ThingSpeak. A React dashboard reads the ThingSpeak channel and shows live values, history and alert state.

Two design principles drive the plan:

1. **Firmware first.** Every sensor and actuator is proven in C++ on the bench, one at a time, before any networking or UI work starts.
2. **Local autonomy.** Alerts and the grow light must keep working with no Wi-Fi. The cloud is for visibility, not control.

The review of the project context found several issues that would cause failures if left unresolved. The most serious: the MOSFET link points to a P-channel SMD part, not the IRLZ44N; a 12 V supply, a 5 V strip and a 5 V charger are listed with no clear power plan; the grow light can feed back into its own light sensor and would switch on at night; and the Pico W and DHT22 are missing from the parts list. Section 7 lists every finding with a fix.

## 2. Scope and requirements

Assumed deployment (please confirm): one potted plant near a window, a grow light above it, the DHT22 on a short cable outside the window, and the electronics in a small enclosure.

### 2.1 Functional requirements

| ID | Requirement |
| --- | --- |
| FR1 | Measure air temperature at least every 2 s (DHT22 limit) and report a filtered value. |
| FR2 | Measure visible light, IR and UV index with the Sunlight sensor over I2C. |
| FR3 | Measure soil moisture and convert it to a calibrated percentage. |
| FR4 | Signal low temperature, low light or low soil moisture with a dedicated LED each |
| FR5 | Switch the grow light on when light is below a threshold, with hysteresis, minimum on/off times and a daily time window. |
| FR6 | Upload all readings and states to ThingSpeak every 30 s (configurable; free tier minimum is 15 s). |
| FR7 | Dashboard shows latest values, history charts, alert and grow-light state, and a device-online indicator. |
| FR8 | FR4 and FR5 keep working with no Wi-Fi. |

### 2.2 Non-functional requirements

- Reliability: hardware watchdog enabled; 72 h soak test with no unplanned reset.
- Safety: only low-voltage DC (12 V) near the plant; fused LED supply; mains adapter kept dry and enclosed.
- Security: Wi-Fi password and API keys kept out of Git (a git-ignored secrets header).
- Quality: control logic separated from hardware and unit-tested on a PC.
- Documentation: wiring diagram, calibration data and test evidence included in the report.

## 3. Concept design

### 3.1 System overview (abstraction)

```
 SENSE                    THINK (Pico W, C++)               ACT
 ┌──────────────┐      ┌─────────────────────────┐      ┌───────────────┐
 │ DHT22  temp  ├─────►│ Drivers → Validation →  ├─────►│ 3 LEDs │
 │ Sunlight I2C ├─────►│ Control rules (FSM)     ├─────►│ Grow light    │
 │ Soil ADC     ├─────►│ Telemetry queue         │      │ (MOSFET, 12V) │
 └──────────────┘      └───────────┬─────────────┘      └───────────────┘
                                   │ Wi-Fi, HTTP (REST)
                                   ▼
                        ┌─────────────────────┐   REST (JSON)   ┌───────────┐
                        │ ThingSpeak channel  ├────────────────►│ React     │
                        │ 8 fields, history   │                 │ dashboard │
                        └─────────────────────┘                 └───────────┘
```

### 3.2 Firmware control flow

```
 ┌────────┐   ┌──────────────┐   ┌───────────────────┐
 │ Reset  ├──►│ Init HAL:    ├──►│ Start Wi-Fi       │
 │ / boot │   │ GPIO,I2C,ADC │   │ (non-blocking)    │
 └────────┘   └──────────────┘   └─────────┬─────────┘
                                           ▼
        ┌──────────────────────────────────────────────┐
        │ MAIN LOOP (every 2 s)                        │
        │  1. read DHT22, Sunlight, Soil (avg samples) │
        │  2. validate (range, stale, error counters)  │
        │  3. run control rules (hysteresis, timers)   │
        │  4. drive LEDs / grow light         │
        │  5. every 30 s: send to ThingSpeak (if up)   │
        │  6. feed watchdog                            │
        └──────────────────────────────────────────────┘
```

### 3.3 Firmware architecture

| Layer | Modules | Responsibility |
| --- | --- | --- |
| Drivers (HAL) | Dht22, Si1151 (sunlight), SoilSensor, Led, GrowLight | Talk to hardware only. No decisions. |
| Services | SensorManager, Calibration, Config | Averaging, range checks, unit conversion, thresholds. |
| Logic (pure C++) | ControlRules, AlertManager | State machine with hysteresis and timers. No hardware headers, so it is testable on a PC. |
| Network | WifiManager, ThingSpeakClient, Sntp | Connect, retry with backoff, upload, time. |
| App | main.cpp | Cooperative scheduler (timers in a super-loop). Add FreeRTOS only if needed. |

### 3.4 Control rules (starting values, to be calibrated)

| Rule | Trigger | Clear | Extras |
| --- | --- | --- | --- |
| Low temperature | Below 10 C for 5 min | Above 12 C | LED on |
| Grow light on | Visible light below L_on for 2 min, inside allowed window (for example 07:00 to 21:00) | Above L_off (L_off > L_on) or window ends | Minimum 10 min on/off; maximum 14 h per day |
| Light fault | Grow light on for 10 min and light still below target | Light recovers | Distinguishes a failed light from normal darkness |
| Low soil moisture | Below 30 % | Above 35 % | LED on |
| Sensor fault | 3 consecutive invalid reads | First valid read | Flag in the alert bitmask |

The thresholds depend on the plant species and must be set after calibration.

### 3.5 Cloud data model (ThingSpeak, 8 fields maximum)

| Field | Content |
| --- | --- |
| 1 | Outdoor temperature (C) |
| 2 | Humidity (%) from the DHT22 (free extra) |
| 3 | Visible light (calibrated units) |
| 4 | UV index or IR (choose one) |
| 5 | Soil moisture (%) |
| 6 | Grow light state (0 or 1) |
| 7 | Alert bitmask (temp, light, moisture, sensor fault) |
| 8 | Wi-Fi RSSI or uptime (diagnostics) |

### 3.6 Networking decision

Use **HTTP REST** for uploads and for the dashboard. It is the simplest path on the Pico W (lwIP) and ThingSpeak supports it directly. MQTT adds a broker setup, credentials and another library, and gives no benefit for one device sending a reading every 30 s. Treat MQTT as an optional stretch goal, for example for remote override commands. Use plain HTTP first, then move to HTTPS (mbedTLS) once everything else works, because plain HTTP sends the write key unencrypted.

### 3.7 Dashboard (React)

- Vite + React, fetch `https://api.thingspeak.com/channels/<id>/feeds.json?results=100` every 30 s.
- Status cards, line charts (Recharts), alert badges, grow-light indicator, and an online/offline badge (offline if the newest entry is older than 3 upload periods).
- Use a public channel for reading, or accept that a read key in front-end code is visible. Never put the write key in the browser.
- Check that ThingSpeak allows browser (CORS) requests from your host early in Sprint 5. If not, add a tiny proxy.

## 4. Hardware design

### 4.1 Power architecture

```
 Mains ─► [12 V 2 A PSU] ─► +12 V ─► [2 A fuse] ─► Grow light strip (+)
                            GND  ─┐                        │ (−)
                                  │                    MOSFET drain
 Mains ─► [5 V USB charger] ─► Pico W (VSYS)                │
                                  │                    MOSFET source
 Pico GND ────────────────────────┴────────────────────────┘
          (all grounds joined; the MOSFET needs a common reference)
 Pico 3V3(OUT) ─► DHT22, Sunlight sensor, Soil sensor (3.3 V only)
```

### 4.2 Pin map (proposed)

| Function | Pico W pin | Notes |
| --- | --- | --- |
| Sunlight SDA | GP4 (pin 6) | I2C0. Grove white wire. |
| Sunlight SCL | GP5 (pin 7) | I2C0. Grove yellow wire. Confirm I2C address (0x53) in the wiki. |
| DHT22 data | GP15 (pin 20) | 4.7 to 10 kOhm pull-up to 3V3 for a bare sensor. |
| Soil moisture analog | GP26 / ADC0 (pin 31) | Grove yellow wire. |
| Soil sensor power | GP17 (pin 22) | Power the probe only while sampling. Check its current draw is 10 mA or less, else use a transistor. |
| Low-temperature LED | GP10 (pin 14) | 330 Ohm series resistor. |
| Low-light LED | GP11 (pin 15) | 330 Ohm series resistor. |
| Low-moisture LED | GP12 (pin 16) | 330 Ohm series resistor. |
| Grow light gate | GP14 (pin 19) | PWM7A. 220 Ohm series, 10 kOhm gate-to-GND. |
| Status LED | On-board (CYW43 GPIO) | On a Pico W use the cyw43_arch_gpio_put call, not a normal GPIO. |

### 4.3 Grow light switching stage (low-side N-MOSFET)

```
 +12 V ──[2 A fuse]── LED strip (+)
                      LED strip (−)
                           │
                         Drain
 Pico GP14 ─[220 Ω]─┬─ Gate      IRLZ44N (N-channel, TO-220)
                    │           Source
                 [10 kΩ]           │
                    │              │
 12 V PSU GND ──────┴──────────────┴──── Pico GND (shared)
```

No flyback diode is needed for an LED strip (not inductive). Route the strip current through screw terminals or perfboard, not breadboard rails.

### 4.4 Physical layout sketch

```
        ═══ grow light strip on bracket (25 to 40 cm above plant) ═══
                    ╱  ╱  ╱  light cone  ╲  ╲  ╲

  [Sunlight sensor]  baffled from grow light, facing window

                  ┌───────────┐
                  │   plant   │
                  ├───────────┤
   Enclosure      │ soil pot  │◄── moisture probe (about 2/3 depth)
  ┌──────────┐    └───────────┘
  │ Pico W   │
  │ MOSFET   │──── 12 V to strip      ═════ window ═════
  │ terminals│
  │ LEDs+buzz│                         DHT22 outside, in a radiation
  └──────────┘                         shield, cable through window frame
```

### 4.5 Wiring notes

- Power all sensors from 3V3(OUT), not 5 V. The soil sensor output then stays at or below 3.3 V, which is the ADC limit.
- Grove cable colours: red VCC, black GND, yellow signal 1, white signal 2. Verify against each module's wiki before powering.
- Add a 100 nF capacitor across each sensor supply close to the sensor.
- Keep the 12 V strip wiring physically away from the I2C and analog lines.

## 5. Bill of materials and where to buy

Prices and stock change; check each page and order early, because lead times can eat into Sprint 0.

| Item | Reference | Where | Notes |
| --- | --- | --- | --- |
| Raspberry Pi Pico WH (headers soldered) | Not in your list | Farnell FI (https://fi.farnell.com), Elfa Distrelec, Kiwi Electronics, The Pi Hut, Pimoroni | Missing from the list. A Pico 2 W is a near drop-in alternative (different board name in CMake). |
| DHT22 / AM2302 sensor (or module with pull-up) | Not in your list | Farnell FI, Kiwi Electronics, Adafruit resellers | Missing from the list. |
| Grove Sunlight Sensor | Farnell 4007692, Seeed 101020089 | https://fi.farnell.com/en-FI/seeed-studio/101020089/sunlight-sensor-arduino-raspberry/dp/4007692 | I2C, Si1151 based. |
| Grove Soil Moisture Sensor | Seeed | https://www.seeedstudio.com (search 'Grove Moisture Sensor') | Resistive type; see flaw 10. |
| Grove 4-pin male jumper cables | Farnell 3410135 (Seeed 110990210) | https://fi.farnell.com/seeed-studio/110990210/cable-4-pin-male-jumper-grove/dp/3410135 | One per Grove sensor, plus spare. |
| 12 V 2 A AC/DC supply | Farnell 2643258 (XP Power VEL24US120-EU-JA) | https://fi.farnell.com/en-FI/xp-power/vel24us120-eu-ja/adaptor-ac-dc-12v-2a/dp/2643258 | Confirm output plug and polarity. |
| DC barrel jack adapter | Farnell 4308210 (DFRobot FIT0151) | https://fi.farnell.com/en-FI/dfrobot/fit0151/dc-barrel-adapter-jack-5-5x2-1mm/dp/4308210 | Screw terminals for the 12 V rail. |
| Seeed RGB LED strip | Farnell 4007788 (Seeed 104020109) | https://fi.farnell.com/en-FI/seeed-studio/104020109/rgb-led-strip-60-m-seeedunio-arduino/dp/4007788 | Check voltage and whether addressable; see flaw 4. |
| 12 V white or full-spectrum LED strip (grow light) | To be chosen | Electronics distributors or horticultural LED suppliers | Non-addressable, 12 V, about 1.5 A or less. |
| N-channel logic-level MOSFET IRLZ44N (TO-220) | Replaces the NTD25P03L link | Search 'IRLZ44N' at Farnell, Mouser, Elfa Distrelec | Your current link is the wrong part; see flaw 1. |
| 5 V USB wall charger, 1 A or more | Any | Existing | Powers the Pico W. |
| Breadboard, jumper wires, resistor set | Any | Existing | For bench prototyping only. |
| 2 A fuse and holder, screw terminal blocks, perfboard | Not in your list | Farnell, Elfa Distrelec | For the final build. |
| 3 LEDs | Not in your list | Any | Part of the alert output. |
| Enclosure, cable gland, DHT22 radiation shield | Not in your list | Any | Outdoor sensor protection. |

## 6. Technical documentation links

| Topic | Link |
| --- | --- |
| Pico W datasheet (pinout, electrical limits) | https://datasheets.raspberrypi.com/picow/pico-w-datasheet.pdf |
| RP2040 datasheet | https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf |
| Getting started with Pico (SDK, toolchain) | https://datasheets.raspberrypi.com/pico/getting-started-with-pico.pdf |
| Connecting to the Internet with Pico W | https://datasheets.raspberrypi.com/picow/connecting-to-the-internet-with-pico-w.pdf |
| Pico C/C++ SDK documentation | https://www.raspberrypi.com/documentation/microcontrollers/c_sdk.html |
| Pico examples (DHT22, I2C, ADC, Wi-Fi, TLS) | https://github.com/raspberrypi/pico-examples |
| DHT22 / AM2302 datasheet | https://cdn-shop.adafruit.com/datasheets/Digital+humidity+and+temperature+sensor+AM2302.pdf |
| Grove Sunlight Sensor wiki | https://wiki.seeedstudio.com/Grove-Sunlight_Sensor/ |
| Si1151 datasheet | Search 'Si1151 datasheet' on silabs.com |
| Grove Moisture Sensor wiki | https://wiki.seeedstudio.com/Grove-Moisture_Sensor/ |
| IRLZ44N datasheet | Search 'IRLZ44N datasheet' on infineon.com |
| ThingSpeak documentation | https://www.mathworks.com/help/thingspeak/ |
| ThingSpeak REST API | https://www.mathworks.com/help/thingspeak/rest-api.html |
| ThingSpeak MQTT basics | https://www.mathworks.com/help/thingspeak/mqtt-basics.html |
| ThingSpeak TalkBack (device commands) | https://www.mathworks.com/help/thingspeak/talkback-app.html |
| React / Vite / Recharts | https://react.dev, https://vite.dev, https://recharts.org |
| Schematics (KiCad) / simulator (Wokwi) | https://www.kicad.org, https://wokwi.com |
| Host unit testing | https://github.com/catchorg/Catch2 |

Some documentation pages move; if a link fails, search the page title. Verify the free-tier ThingSpeak limits on the MathWorks site before relying on them.

## 7. Flaws and risks in the current spec

Severity: **H** = will break the build, **M** = likely to cause problems, **L** = improvement.

| # | Sev | Issue | Why it matters | Fix |
| --- | --- | --- | --- | --- |
| 1 | H | The MOSFET link is an onsemi NTD25P03L (P-channel, 30 V, DPAK SMD), not the IRLZ44N N-channel you describe. | Wrong polarity for the low-side circuit and not breadboard-friendly. | Buy a genuine IRLZ44N (TO-220) and update the BOM. |
| 2 | H | Voltage domains are undefined: a 12 V supply, a 5 V LED strip and a 5 V charger. | A 5 V strip on 12 V is destroyed instantly. | Choose a 12 V strip for the 12 V supply (recommended), or add a 12 to 5 V buck converter. |
| 3 | H | Pico W and DHT22 are missing from the parts list. | Cannot start the project. | Add and order now. |
| 4 | H | The Seeed RGB strip is not a grow light, and may be addressable (needs a data protocol, not a MOSFET). | RGB spectrum is poor for plants; a full-strength strip can draw several amps. | Use a 12 V white or full-spectrum strip as grow light. Use the RGB strip only as an optional status indicator, or drop it. Verify its datasheet. |
| 5 | H | Grow light feedback loop: the sensor sees the grow light. | Light turns on, sensor reads bright, light turns off, repeat (oscillation). | Hysteresis with L_off well above L_on, minimum on/off times, and shield the sensor from the grow light. |
| 6 | H | 'On when sunlight is low' also triggers at night. | Plants need a dark period; wastes energy. | Add a time window (SNTP) and a daily maximum of light hours. |
| 7 | M | The breadboard is a weak point for the strip current. | Breadboard contacts are only suitable for around 1 A and heat up. | Prototype with a small dummy load; wire the strip through screw terminals and perfboard. |
| 8 | M | The Sunlight sensor is not a lux meter, and there is no ready Pico C++ driver (the Seeed library targets Arduino). | Output is raw visible, IR and UV counts. Writing or porting a driver takes time. | Budget a full week; port the I2C register logic, and calibrate against a phone lux app or a lux meter. |
| 9 | M | Two power sources need a shared ground. | A floating MOSFET source gives unreliable switching. | Join the grounds of the 12 V supply and the Pico. |
| 10 | M | The Grove Soil Moisture Sensor is resistive. | Exposed probes corrode and drift within weeks in wet soil. | Power it only while sampling and expect to recalibrate, or switch to a capacitive, corrosion-resistant sensor. |
| 11 | M | The DHT22 outdoors: not weatherproof, solar self-heating, slow (0.5 Hz), occasional checksum errors. | Wrong readings and failures outdoors, especially in frost and rain. | Radiation shield, sealed cable entry, retry logic. Consider an SHT31, BME280 or DS18B20 if it fails in testing. |
| 12 | M | ADC input range is 0 to 3.3 V. | A sensor powered from 5 V can overdrive the pin. | Power the soil sensor from 3V3(OUT). Average at least 16 ADC samples, as Wi-Fi activity adds noise. |
| 13 | M | IRLZ44N at a 3.3 V gate: a threshold of 2 V or less does not guarantee full turn-on. | Higher Rds(on) means heat and a dimmer light. | Check the Rds(on) versus Vgs graph; measure the Vds drop and temperature under load. Fallback: a driver transistor to a 5 V gate. |
| 14 | M | ThingSpeak limits: 8 fields, 15 s minimum interval. | Cannot upload more or faster than this. | Use the field map in 3.5 and upload every 30 s. |
| 15 | M | The React app would expose API keys. | Anyone can read the browser code. | Public read channel, no write key in the front end, and optionally a proxy. |
| 16 | L | 'LED Buzzers'. | GPIO current limit is about 12 mA per pin. | Define 3 LEDs, rate-limit it, decide whether to flash at night. |
| 17 | L | Cloud dependency. | A Wi-Fi outage must not stop plant care. | Local-first logic with a short offline buffer and a hardware watchdog. |
| 18 | L | Mains adapter near watering. | Electrical safety. | Keep the adapter dry and enclosed, fuse the 12 V line, and use only low-voltage wiring near the plant. |
| 19 | L | Plain HTTP uploads send the write key unencrypted. | Credential exposure. | Acceptable for the first prototype; move to HTTPS as a late sprint if time permits. |

## 8. Iterative development plan (12 weeks)

Adjust week numbers to your semester calendar. Each sprint ends with a working, demonstrable increment, and no sprint starts before the previous exit criteria are met.

| Sprint | Week | Goal | Key tasks | Exit criteria |
| --- | --- | --- | --- | --- |
| S0 Foundation | 1 | Tools, plan, parts | Confirm scope and open questions; order all parts now; install Pico SDK and the VS Code Raspberry Pi Pico extension (CMake, C++17); create Git repo; blink the on-board LED (CYW43 API); USB serial logging | Blink and serial output work; BOM ordered; README written |
| S1a DHT22 | 2 | Temperature | Wrap the pico-examples DHT code in a Dht22 class; enforce 2 s spacing; handle checksum errors | 1 h log, 95 % or more valid reads, within 1 C of a reference thermometer |
| S1b Sunlight | 3 | Light | I2C bus scan; read part ID; configure and read visible, IR and UV; wrapper class; cover and uncover tests | Part ID reads correctly; readings respond sensibly; documented approximate lux mapping |
| S1c Soil | 4 | Moisture | ADC averaging; dry-air and water calibration; percentage conversion; probe power gating | Repeatable within 3 % across three trials |
| S2 Actuators | 5 | Outputs | LEDs patterns; MOSFET with a dummy load first, then the strip; measure current and temperature | Clean switching at 3.3 V gate; MOSFET cool after 30 min; no resets when the strip switches |
| S3 Control logic | 6 | Decisions | Pure C++ rule library; host unit tests (Catch2) with simulated inputs; integrate on the device | All rule tests pass on PC; on-device alerts and grow-light behaviour match the table in 3.4 |
| S4a Cloud upload | 7 | Wi-Fi + ThingSpeak | CYW43 connect; DHCP; HTTP upload; treat entry id 0 in the response as a failed update | 24 h of data on the channel without gaps over 2 min |
| S4b Resilience | 8 | Robustness | Hardware watchdog; reconnect with backoff; SNTP time; small offline buffer; secrets header | Router off for 5 min: control continues, uploads resume, no reset loop |
| S5 Dashboard | 9 to 10 | UI | Vite + React; fetch feeds; cards, charts, online badge; deploy; check CORS first | Dashboard values match the serial log; works on a phone |
| S6 Integration | 11 | Final hardware | Enclosure, perfboard wiring, final calibration, 72 h soak test | 72 h with no unplanned reset; all alerts verified |
| S7 Delivery | 12 | Documentation | Report, wiring diagrams, test evidence, demo script, backup video | Submission-ready package |

Stretch goals if time allows: PWM dimming, TalkBack remote override, HTTPS, MQTT.

## 9. Test strategy

| Level | What | How |
| --- | --- | --- |
| Unit (PC) | Control rules, hysteresis, calibration maths, response parsing | Catch2 with simulated sensor values in CMake |
| Component bench | Each sensor and actuator alone | Serial logs, reference thermometer, multimeter, phone lux app |
| Integration | Sensors to logic to actuators | Scripted scenarios: cover the light sensor, dry the probe, warm the DHT22 |
| System | Wi-Fi loss, power cycle, 72 h soak | Log analysis; count resets and gaps |
| Acceptance | Requirements FR1 to FR8 | Checklist with evidence in the report |

## 10. Repository and deliverables

```
 plant-station/
   firmware/   CMakeLists.txt, src/drivers, src/logic, src/net, src/app,
               include/secrets.example.h (real secrets.h is git-ignored), test/
   dashboard/  Vite + React app
   hardware/   schematic (KiCad), pin map, BOM
   docs/       calibration data, test reports, report draft
```

Report outline: 1 Introduction and requirements; 2 Architecture and concept design; 3 Hardware design and BOM; 4 Firmware design; 5 Cloud and dashboard; 6 Test results; 7 Reflection and future work.

## 11. Open questions to confirm

- Where will the plant live (indoor window, greenhouse, outdoor)? This decides the DHT22 placement and enclosure needs.
- Which plant species? It sets the temperature, moisture and light thresholds.
- What size and wattage is the grow light, and what supply voltage do you want? (Recommended: 12 V.)
- What is the intended role of the RGB strip, if any?
- Does the course require MQTT, or is HTTP acceptable?
- Budget and delivery deadline for ordering parts?