# theOPENhandy — WIP

theOPENhandy is an open-source firmware project for theHandy (Gen1).  
It exists to preserve user **autonomy, privacy, and control over hardware.**

---


<details>
<summary>### On personal Note (click to expand)</summary>
Shoutout to the security-obsessed, dogma-driven, RFID-implanted pentester who runs the "other" forum:


Socrates condemned writing.
You condemned the use of AI.
The argument, however, still stands on its own.



My understanding of 2FA/MFA was questioned by this aforementioned person (I was called a moron, in other words).  
Yes, these mechanisms are standard for some and, in many cases, required on platforms like GitHub, which is, in this case, justified and a reasonable thing to have.  
The futility of the attempt to discredit me ad hominem in this regard is proven by this very statement.  
The disagreement was and is not about understanding; it is about proportionality and consequences.

### On Security vs. Traceability
Stronger authentication reduces some risks/vectors.  
It also increases the ability to tie usage to a specific person via a device.

- A password leak exposes access.
- A tightly bound identity can expose a person.

**I let you be the judge of what is worse in this context, but I draw the line there.**

Users should not be forced to link their identity to a device, directly or indirectly, in relation to such content. As much as this risk can be argued as an edge case, it is likewise an edge case to argue that 2FA for such a community is necessary because of bad actors: so far, we have had no qualified case of ID theft or the spread of malicious software. It also has to be said that this will not prevent a bad actor from doing so; it just makes it harder.


### Bottom Line:
This project exists for those who don't want an app or API that phones home every time the toy is used, no matter how "secure" you make it. It can be misused given enough ambition, just like a community that enforces 2FA can still be infiltrated by a bad actor.

**Thank you, rant over.**

</details>




## The goal:  
A completely offline, locally controlled Handy that does not Phone home.
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