# Door status checker project

Detection of a garage door open/closed status with IR sensor connected to ESP32 MCU. This information is sent to a MQTT server running in a RPi3, where it is stored into a database and shown using an Android APP/Website

## ESP-IDF installation instructions

<https://www.youtube.com/watch?v=dOVjb2wXI84>

## ESP32 Setup

The setup uses a IR proximity sensor similar to this [Waveshare sensor](https://www.waveshare.com/wiki/Infrared_Proximity_Sensor).
The system will only use the digital output of the sensor (as it is not available the analog one).

The sensor is read by the ESP32 and it is physically placed near the door to be controlled. The door spot where the sensor is sensing the position may be painted in white or have any white paper/plastic attached to improve the detection.

Sensor levels:
    - Logical '1': Object is FAR
    - Logical '0': Object is NEAR
