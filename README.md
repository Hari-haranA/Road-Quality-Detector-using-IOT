# Road-Quality-Detector-using-IOT

# 🛣️ IoT-Based Automatic Road Quality Detection System

This project introduces an IoT-based automatic road quality detection system utilizing an **ESP8266** and an array of specialized sensors. It records road anomalies, differentiates between speed breakers and bumps, captures vehicle-to-environment motion spikes, and geotags every event using a **NEO 6M GPS** module. All collected telemetry is pushed in real time via Wi-Fi to a **Firebase** database for live mapping, tracking, and remote visualization.

## ⚙️ System Overview

* **Core Microcontroller:** ESP8266 Wi-Fi module for data processing and instant Firebase cloud sync.
* **Anomaly Detection:** MPU6050 accelerometer captures bumps, while an ultrasonic sensor classifies speed breakers.
* **Proximity Sensing:** TCRT5000 IR sensor detects sudden motion and acceleration spikes from passing traffic or pedestrians.
* **Geomapping:** NEO 6M GPS module logs exact spatial coordinates to build a comprehensive road quality map.
* **Development & Results:** Built on Arduino IDE, breadboard-tested in real-time, delivering high-accuracy hazard mapping for enhanced road safety.
