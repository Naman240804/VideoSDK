# 📡 ESP32 Sound Detection System with MQTT & Web Server

## 📘 Overview

This project implements a sound-activated audio recording system using an ESP32. When sound above a certain amplitude threshold is detected, the ESP32 records audio data from an analog microphone, saves it to SPIFFS as a `.bin` file, and sends an MQTT alert. It also hosts a web server that lists and allows downloading of recorded audio files.

---

## 🎯 Features

- 📢 **Sound Detection** using onboard ADC.
- 🎙️ **3-second audio recording** on detection.
- 💾 **Storage to SPIFFS** with auto-clearing of old recordings.
- 🌐 **MQTT Alert Publishing** to `esp32/audio_alerts/{device_id}`.
- 🕸️ **Web Server** to view and download `.bin` audio files.
- 🕒 **NTP Time Syncing** to timestamp files accurately.
- 🛠️ **Debouncing & Memory Protection** using `isRecording` flag.
  
---

## 🔧 Hardware Requirements

- ESP32 Dev Board (with WiFi)
- Analog microphone module (connected to GPIO 34)
- Micro USB cable for power and flashing

---

## 🗂️ File Structure

| File/Component     | Description                                      |
|--------------------|--------------------------------------------------|
| `recording_*.bin`  | Audio recordings (3 seconds) saved on SPIFFS     |
| `/` (SPIFFS root)  | File system mount point                          |
| `Web Interface`    | Lists all `.bin` files with download links       |

---

## 📶 Network Requirements

- Connect to a Wi-Fi network (SSID and password set in `WIFI_SSID` and `WIFI_PASSWORD`)
- Requires internet for time synchronization via NTP servers
- MQTT broker set to `test.mosquitto.org:1883`

---

## 🚀 Getting Started

### 1. 📥 Clone the Code into Arduino IDE

Ensure you have installed these libraries:
- **ESP32 board support** (via Boards Manager)
- **PubSubClient**
- **ArduinoJson**

### 2. 🔧 Hardware Setup

- Microphone analog output to **GPIO 34**
- Make sure it's powered by 3.3V
- Use decoupling capacitor for stability (optional)

### 3. 💻 Upload and Monitor

- Connect the ESP32 via USB.
- Upload the sketch.
- Open Serial Monitor (`115200 baud`) to view status.

---

## 🌐 Accessing the Web Server

After connecting to WiFi, the ESP32 starts a web server on port `80`.

You will see output like:


Open that IP in a browser to:

- View device name and IP
- See list of `.bin` recordings
- Click to download them

---

## 📡 MQTT Alert Format

MQTT Topic:  


Payload JSON:
```json
{
  "event": "sound_detected",
  "device": "esp32-audio-B0FB",
  "timestamp": "2025-07-02T11:35:33Z",
  "amplitude": 0.65,
  "audio_filename": "/recording_20250702_113533.bin"
}


