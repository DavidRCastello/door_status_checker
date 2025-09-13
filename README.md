# Door status checker project

Detection of a garage door open/closed status with IR sensor connected to ESP32 MCU. This information is sent to a MQTT server running in a RPi3, where it is stored into a database and shown using an Android APP/Website

## ESP-IDF installation instructions

<https://www.youtube.com/watch?v=dOVjb2wXI84>

## ESP32 Setup

The setup uses a IR proximity sensor similar to this [Waveshare sensor](https://www.waveshare.com/wiki/Infrared_Proximity_Sensor).
The system will only use the digital output of the sensor (as it is not available the analog one).

The sensor is read by the ESP32 and it is physically placed near the door to be controlled. The door spot where the sensor is sensing the position may be painted in white or have any white paper/plastic attached to improve the detection.

Sensor digital levels meaning:

* Logical '1': Object is FAR
* Logical '0': Object is NEAR

## MQTT Broker in RPi3

sudo apt-get install mosquitto mosquitto-clients
sudo systemctl enable mosquitto.service

In localhost
mosquitto_sub -d -h localhost -p 1883 -t "test"
mosquitto_pub -d -h localhost -p 1883 -t "test" -m "Hello world"

If you want to send data from another machine, the port shall be opened from incoming connections:

sudo nano /etc/mosquitto/mosquitto.conf
Add or modify:

listener 1883
allow_anonymous true


sudo systemctl restart mosquitto


## Python parser in RPi
sudo apt install python3-paho-mqtt


## Database
sudo apt-get install sqlite3

sqlite3 door_data.db

CREATE TABLE door_status (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp DATETIME DEFAULT (datetime('now','localtime')),
    status TEXT NOT NULL
);

