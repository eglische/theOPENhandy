# theOPENhandy — WIP

theOPENhandy is an opensource Firmware for theHandy (Gen1).  
It removes all cloud dependencies and replaces them with a fast, fully local control stack built around:

- **Local HTTP API** for direct control  
- **Real-time UDP (TCode)** for high-speed motion input 
- **On-device configuration portal** (no accounts, no cloud)  

The goal is simple:  
A completely offline, locally controlled Handy that integrates easily with automation tools, local apps, and real-time motion software.
Finetune the Motor/Motion to fit different needs.
---

## What the Firmware Does

- Runs the actuator (motor) with precise position and speed control
- Processes IR sensors, thermal monitoring, and collision detection (later not yet fully implemented)
- Exposes a clean HTTP API for motion, speed, cropping, patterns, and device status  
- Accepts real-time motion input via UDP using the TCode protocol  
- Provides a captive Wi-Fi setup portal on first boot  
- Supports Wi-Fi AP and Client Mode, mDNS discovery, and optional boot sounds    
- Boots without cloud checks or external services  
- Fully self-contained — no external dependencies once flashed



## Installation
Install instructions for Flashing can be found in the /Documentation folder
---

## Setup Mode

To enter Setup Mode:

1. Hold the **Power** button.
2. Connect the device to power.
3. Keep holding until the **LED flashes violet**.

When active, the device exposes a WiFi access point:

- **SSID:** `OPENhandy-setup`
- **Password:** `access123`

Use the portal to configure:

- WiFi credentials
- DHCP / static IP
- UDP port for **TCODE** (bottom section)

---

## LED Status Indicators

|------------------------------|-------------------------------------------|
| LED State                    | Meaning                                   |
|------------------------------|-------------------------------------------|
| **Violet (blinking)**        | Referencing / bootup                      |
| **Green**                    | Manual mode, ready                        |
| **Red (blinking)**           | Thermal error                             |
| **Red (solid)**              | Collision error                           |
| **Blue (solid)**             | UDP stream active                         |
| **Violet (solid)**           | Wireless AP mode / captive portal         |
| **Blue ↔ Violet alternating**| Establishing Voxta interaction            |
|------------------------------|-------------------------------------------|

---

## Manual Mode

You can operate the device via:

- Physical buttons  
- Web interface (Motion tab)  
- HTTP API  

### Button Functions

- **UP / DOWN**
  - Short press → crop stroke (reduce range)
  - Medium press → gradually uncrop
  - Long press → reset to full 100% stroke

- **LEFT / RIGHT**
  - Adjust speed

- **POWER**
  - Toggle motion on/off

---

## HTTP API

Device reachable at:

http://openhandy.local
or:
http://<DEVICE_IP>


For example:
http://192.168.1.42

All API endpoints use **HTTP GET** and return **JSON**.

---

### Examples for API

GET /api/motion?action=status
Response example:

```json
{
  "ok": true,
  "speed": 50,
  "cutLower": 0.10,
  "cutUpper": 0.20,
  "pattern": 1
}

Start / Stop Motion
GET /api/motion?action=start
GET /api/motion?action=stop

Speed Control
GET /api/motion?action=faster
GET /api/motion?action=slower

Set an absolute speed:
GET /api/motion?action=setspeed&sp=XX
Example:
GET /api/motion?action=setspeed&sp=70

Pattern Selection
Mode	Pattern
0	Sine
1	Bounce
2	Double Bounce

Set pattern:
GET /api/motion?action=setpattern&mode=0
GET /api/motion?action=setpattern&mode=1
GET /api/motion?action=setpattern&mode=2

Stroke Cropping
lower and upper are fractions from 0.0 to 1.0.


Example:
GET /api/motion?action=setcrop&lower=0.10&upper=0.20

This applies:
10% crop at the bottom
20% crop at the top