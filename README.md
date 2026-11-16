# Smart Plant Monitoring 

Embedded systems semester project. A Raspberry Pi Pico W keeps an eye on a plant (temperature, light, soil moisture), shouts at us with LEDs and a buzzer when something's off, switches a grow light when it gets too dark, and sends everything to ThingSpeak so a React dashboard can show it off.
  
---
 
## What's in ze box
 
| Thing | What it does |
|---|---|
| Raspberry Pi Pico W | The brain |
| DHT22 | Temperature (and humidity, bonus) |
| Grove Sunlight Sensor (I2C) | Visible / IR / UV light |
| Grove Soil Moisture Sensor | Is the plant thirsty? |
| 3 LEDs + buzzer | Complaints, in visual and audible form |
| 12 V LED strip + IRLZ44N MOSFET | The grow light |
 
Full parts list, pin map and wiring sketches live in the project plan doc (see `ProjectPlan.md`).
 
## Directory Layout (Planned....)

```
plant-station/
  firmware/    C++ for the Pico W (Pico SDK + CMake)
    src/drivers/   talk to hardware only, no decisions
    src/logic/     the "brain" rules, pure C++, no hardware stuff
    src/net/       Wi-Fi + ThingSpeak
    src/app/       main.cpp
    include/       secrets.example.h lives here
    test/          unit tests that run on your laptop
  dashboard/   Vite + React app
    index.html
    app.tsx .......
  hardware/    schematic, pin map, BOM
  docs/        plan, calibration data, test reports
     ProjectPlan.md
     DevRecord.md
```
 
---
 
## Setup (do this once)
 
### 1. Firmware tools
 
Easiest route: install **VS Code** and the **Raspberry Pi Pico** extension. It grabs the SDK, the ARM compiler, CMake and Ninja for you. Honestly just do this, it saves a lot of pain.
 
Prefer the terminal? You'll need:
 
- `arm-none-eabi-gcc`
- `cmake` (3.13 or newer)
- the [Pico SDK](https://github.com/raspberrypi/pico-sdk) and `export PICO_SDK_PATH=/path/to/pico-sdk`
Useful reading: the [Getting started with Pico](https://datasheets.raspberrypi.com/pico/getting-started-with-pico.pdf) guide and the [pico-examples](https://github.com/raspberrypi/pico-examples) repo (the DHT22, I2C, ADC and Wi-Fi examples are gold).
 
### 2. Secrets (do NOT commit these!)
 
```bash
cp firmware/include/secrets.example.h firmware/include/secrets.h
```
 
Then edit `secrets.h` with your Wi-Fi name, Wi-Fi password and ThingSpeak write key. `secrets.h` is in `.gitignore`. If you ever see it in `git status`, stop and shout in the group chat.
 
### 3. Dashboard tools
 
Install Node.js (LTS is fine), then:
 
```bash
cd dashboard
npm install
```
 
Create `dashboard/.env`:
 
```
VITE_THINGSPEAK_CHANNEL_ID=your_channel_id
VITE_THINGSPEAK_READ_KEY=your_read_key_if_channel_is_private
```
 
Only the **read** key goes here. The write key never touches the browser. Ever.
 
---
 
## Build and flash the Pico W
 
```bash
cd firmware
mkdir build && cd build
cmake .. -DPICO_BOARD=pico_w
make -j4
```
 
That spits out a `.uf2` file. To flash it:
 
1. Hold the **BOOTSEL** button on the Pico.
2. Plug in the USB cable, then let go of the button.
3. A drive called `RPI-RP2` shows up.
4. Drag the `.uf2` onto it. The Pico reboots by itself.
(Using the VS Code extension? There's a Run button that does all this. Fancy.)
 
### Reading serial logs
 
We print debug info over USB. Open a terminal at 115200 baud:
 
```bash
# Linux / Mac
screen /dev/ttyACM0 115200
# or: minicom -b 115200 -o -D /dev/ttyACM0
```
 
On Windows use PuTTY or the VS Code serial monitor. If nothing shows up, check that `pico_enable_stdio_usb(<target> 1)` is set in `CMakeLists.txt` and give it a couple of seconds after boot.
 
---
 
## Run the tests (no hardware needed)
 
The control logic is deliberately kept free of hardware code so we can test it on a normal computer:
 
```bash
cmake -S firmware/test -B build-test
cmake --build build-test
ctest --test-dir build-test --output-on-failure
```
 
If you change anything in `src/logic/`, run these *before* you push. Future you will say thanks.
 
## Run the dashboard
 
```bash
cd dashboard
npm run dev
```
 
Open the URL it prints (usually http://localhost:5173). It polls ThingSpeak every 30 s, so be patient.
 
---
 
## How we work
 
We build **one small piece at a time** and prove it works before moving on. Resist the urge to wire everything up on day one.
 
1. **Bench-test each thing alone.** DHT22 first, then the sunlight sensor, then soil moisture. Print values to serial and compare against a real thermometer / phone lux app.
2. **Then the outputs.** LEDs, buzzer, then the MOSFET with a small dummy load before the real strip.
3. **Then the logic.** Rules go in `src/logic/`, tests go in `test/`.
4. **Then Wi-Fi + ThingSpeak.** Only now. Not before.
5. **Then the dashboard.**
6. **Then integration and a 72 hour soak test.** Leave it running and try not to touch it.
Git stuff:
 
- Branch per feature: `feature/dht22-driver`, `feature/soil-calibration`, etc.
- Small commits with sensible messages (not "fix", not "asdf", not "final_final2").
- Pull request, one teammate looks at it, then merge into `main`.
- `main` should always build. If you broke it, you buy the coffee.
---
 
## Gotchas we already know about
 
- **The Pico W on-board LED is special.** It hangs off the Wi-Fi chip, so you can't use a normal GPIO. Call `cyw43_arch_init()` first, then `cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1)`.
- **DHT22 is slow.** One reading every 2 seconds at most, and it sometimes just fails. Handle checksum errors, don't panic.
- **Power sensors from 3V3, not 5V.** The ADC only likes up to 3.3 V.
- **Soil moisture probes corrode.** Only power the probe while measuring. Expect to recalibrate.
- **Average your ADC readings.** Wi-Fi makes it noisy. 16 samples is a decent start.
- **Join the grounds.** The 12 V supply and the Pico need a shared GND or the MOSFET behaves weirdly.
- **Don't run the LED strip through the breadboard.** It can't take the current. Use screw terminals or perfboard.
- **The grow light shines on its own light sensor.** Use hysteresis and keep the sensor shielded, otherwise the light will flicker on and off all night.
- **ThingSpeak free tier:** 8 fields, and at least 15 s between updates. We use 30 s. An update response of `0` means it failed.
## Safety (short but serious)
 
- 12 V DC only near the plant. Mains stays dry, away from the watering can.
- Put a 2 A fuse on the 12 V line.
- Double-check polarity before plugging the power supply in. Smoke is not a feature.
---
 
## Useful links
 
- [Pico W datasheet](https://datasheets.raspberrypi.com/picow/pico-w-datasheet.pdf)
- [Pico C/C++ SDK docs](https://www.raspberrypi.com/documentation/microcontrollers/c_sdk.html)
- [Grove Sunlight Sensor wiki](https://wiki.seeedstudio.com/Grove-Sunlight_Sensor/)
- [Grove Moisture Sensor wiki](https://wiki.seeedstudio.com/Grove-Moisture_Sensor/)
- [ThingSpeak docs](https://www.mathworks.com/help/thingspeak/)
- [React](https://react.dev) / [Vite](https://vite.dev) / [Recharts](https://recharts.org)

