# theOPENhandy — WIP

theOPENhandy is an opensource Firmware for theHandy (Gen1).  

The goal:  
A completely offline, locally controlled Handy that integrates easily with automation tools, local apps, and real-time motion software.
Finetune the Motor/Motion to fit different needs.

It removes all cloud dependencies and replaces them with a fast, fully local control stack built around:

- **Local HTTP API** for direct control  
- **Real-time UDP (TCode)** for high-speed motion input 
- **On-device configuration portal** (no accounts, no cloud)  

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
Install Instructions for Flashing can be found in ./Documentation/install.md

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

| LED State                    | Meaning                                   |
|------------------------------|-------------------------------------------|
| **Violet (blinking)**        | Referencing / bootup                      |
| **Green**                    | Manual mode, ready                        |
| **Red (blinking)**           | Thermal error                             |
| **Red (solid)**              | Collision error                           |
| **Blue (solid)**             | UDP stream active                         |
| **Violet (solid)**           | Wireless AP mode / captive portal         |
| **Blue ↔ Violet alternating**| Establishing Voxta interaction            |

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
  - Buttons will be changed further down the road to reseble how the original worked.

- **POWER**
  - Toggle motion on/off

---

## Access via Browser:

Device reachable at:

http://openhandy.local or: http://<DEVICE_IP>