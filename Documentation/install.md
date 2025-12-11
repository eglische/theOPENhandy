# Installing theOPENhandy Firmware

This guide explains how to back up the original Handy firmware, connect a USB-TTL adapter, and flash the latest **md5-openhandy** release.  


## Important Information

- If you solder the header (pins) to the PCB, make sure they face in the **same direction as the physical buttons**.
- Only **3.3 V** USB-TTL adapters must be used. **Never connect 5 V**.
- Opening and flashing the device may **void your warranty** and can potentially **brick the device** if done incorrectly.
- This is a **WIP (Work in Progress)** project. By using it, you are actively participating in development;  
  Feedback is welcome and will be considered. Without neither positive nor negative feedback, this will not move forward. So consider it.
- If you don’t want to post publicly under GitHub Issues, you can reach me on Discord: **Yeti_ch**
- We do **not** share the official firmware, configuration data, or dumps, nor will we or i disclose who is involved in the project.


Youtube instruction for the disassembly can be found here:
https://youtu.be/LiFM278WkV0

---

## 1. Hardware Required

- Handy device with the accessible PCB header (see `Handy_PCB.jpg`)
- USB-TTL UART adapter (**3.3 V**, common chipset such as **CP2102**, **CH340**, or **FT232**)
- Dupont jumper wires
- 6-pin male header, either hold against the pad on the pcb or soldered to it.

If you search online for a USB-TTL (USB-to-Serial) adapter to purchase:

- “**USB to TTL 3.3V adapter**”
- “**CP2102 UART 3.3V**”
- “**CH340 3.3V serial adapter**”

---

## 2. Identify the PCB Pins

In the included picture (`Handy_PCB.jpg`), the 6 pads used for flashing are marked in red.

Pin order (1 → 6):

1. **3.3V**
2. **TX (ESP32 TX0)**
3. **RX (ESP32 RX0)**
4. **EN**
5. **IO0 / BOOT**
6. **GND**

### Voltage Rule

- Use **3.3 V only**  
- Applying **5 V** will permanently destroy the ESP32

---

## 3. Wiring the USB-TTL Adapter

### Required Connections

| Handy Pad | USB-TTL |
|-----------|---------|
| 3.3V      | 3.3V |
| TX        | RX |
| RX        | TX |
| EN        | GND |
| IO0       | not connected |
| GND       | GND |


Most USB-TTL adapters have at least two GND pins.  
Tie **GND ↔ IO0** to jump into Bootmode while uploading:

### Entering Bootloader Mode

1. Power the Handy PCB through the USB-TTL (3.3 V).
2. Start the flash command.
3. When esptool prints `Connecting........`, **briefly short BOOT (IO0) to GND**.
4. The ESP32 enters bootloader mode and accepts firmware.

Bootloader and partitions are untouched—you only flash the application.

---

## 4. Install esptool

### Windows
Either Install/Download yourself: https://github.com/espressif/esptool/releases

or: 

1. Install Python: https://www.python.org/downloads/  
2. Open *Command Prompt* and run: 
'pip install esptool'

### Linux (quick overview)

sudo apt install python3 python3-pip
pip3 install esptool

Contributions to improve Linux documentation are welcome. ;-)

---

## 5. Back Up the Original Handy Firmware

Dump the **entire flash** before installing anything.

### Windows
Replace `COM3` with your actual port.
esptool.exe --chip esp32 -p COM3 -b 460800 read_flash 0 ALL full_flash.bin
This creates `full_flash.bin` of the factory firmware.

### Linux
Replace `/dev/ttyUSB0` with your actual port.
esptool.py --chip esp32 -p /dev/ttyUSB0 -b 460800 read_flash 0 ALL full_flash.bin
This creates `full_flash.bin` of the factory firmware.

### What is included in a full-flash dump?
A full-flash read (read_flash 0 <size>) copies every byte of the SPI flash, including bootloader, partition table, OTA data, NVS, and the application firmware.
If you read the entire flash region, you get:
Bootloader (0x0000)
Partition table (0x8000)
OTA data (0xe000)
NVS
PHY/NVS calibration data
Application firmware (0x10000 or wherever your app is)
All other partitions

---

## 6. Flash the theOPENhandy Firmware

Download the latest firmware from the GitHub in the Main folder:  
`md5-openhandy_v0.1_21112025_build022.bin`

### Windows
esptool.exe --chip esp32 -p COM3 -b 460800 write_flash 0x10000 md5-openhandy_v0.1_21112025_build021.bin

### Linux
esptool.py --chip esp32 -p /dev/ttyUSB0 -b 460800 write_flash 0x10000 md5-openhandy_v0.1_21112025_build021.bin

You flash **only** the application partition at `0x10000`.  
Bootloader and storage layout remain unchanged.

---

## 7. First Boot: Captive Portal

After flashing and rebooting normally:

1. The Handy starts in Wi-Fi setup mode.
2. Join the temporary access point, for example you can use your Cellphone and then open Chrome, it will use captive to redirect you to http://openhandy.local/
3.  Enter your prefered Settings under the Network Button:
   - Home Wi-Fi SSID
   - Password
   - Fix IP and UDP are optional

   
After reboot, your Handy joins the network. If it does not find your SSID or can't log on, it will revert back to AP-Mode until it can. It needs to re-boot to try again.


---

## 8. Using theOPENhandy

Beside the UDP interface, which you can use for Multifunplayer, you can also use the new HTTP API Endpoints, if you keen to make your own apps / functions and Logic;

### 8.1 HTTP API Overview

Once on your network: http://openhandy.local/ or via the IP you find in your DHCP Lease Table or via Advanced IP Scanner


Common endpoints:

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/motion/on` | POST | Start motor motion |
| `/api/motion/off` | POST | Stop motor |
| `/api/speed?value=<0-100>` | POST | Set speed (0–100%) |
| `/api/crop?lower=<0-1>&upper=<0-1>` | POST | Set lower/upper crop |
| `/api/pattern?mode=<0-2>` | POST | Select pattern (0=sine, 1=bounce, 2=double bounce) |
| `/api/status` | GET | Read live state and sensor data |
| `/api/discovery` | POST | Send UDP discovery burst |

All endpoints return JSON.

---

## 8.2 Real-Time UDP Control (TCode)

The Handy supports TCode input over UDP for full-speed motion control.

Recommended tools:

- MultiFunPlayer  
  https://github.com/Yoooi0/MultiFunPlayer

- Voxta Companion App (Via the Folder on the Github, it's a Python Script)
  More info to Voxta here: https://portal.voxta.ai/

Configure UDP to the device’s IP and chosen UDP port.

---

## 9. Contributing

If you want to improve Linux flashing instructions or add macOS support, pull requests are appreciated.