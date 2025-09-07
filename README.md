# Temp-Pressure System

![demo](https://github.com/tsogp/temp-pressure-system/blob/master/media/demo.gif)

This project is a **sensor hub** system composed of two parts:  
- **Client (STM32)**: Collects environmental and location data.  
- **Server (ESP32)**: Receives, validates, and forwards data to an MQTT broker.  

Together, they form an IoT pipeline for transmitting sensor readings from the STM32 to a remote MQTT subscriber.

---

## Project Structure
```
client/ # STM32 firmware
server/ # ESP32 firmware
```

### Client (STM32)
- Reads temperature and humidity from a **DHT22 sensor** (single-wire protocol).  
- Reads GPS coordinates from **NEO-6M GPS module** (UART with NMEA sentences).  
- Displays sensor values on an **I²C screen**.  
- Sends collected data to ESP32 over **UART** in JSON format.  

**Example message format:**
```json
{"id":1,"temp":24.56,"hum":61.23,"lat":48.856613,"lon":2.352222}
```

### Server (ESP32)

- Receives JSON messages from STM32 via UART.
- Parses and validates the JSON.
- Connects to Wi-Fi and an MQTT broker.
- Publishes the data to MQTT topics based on sensor ID:

```
sensors/bluepill/1
sensors/bluepill/unknown
```

### Protocols Used

The system makes use of several communication protocols:

- DHT22 Single-Wire Protocol: for reading temperature and humidity.
- UART: between STM32 and NEO-6M GPS module (NMEA sentences) and between STM32 client and ESP32 server (JSON messages).
- I²C: for STM32 to communicate with the display.
- Wi-Fi: ESP32 connects to a Wi-Fi network to access the broker.
- MQTT: ESP32 publishes sensor data to an MQTT broker.

### Getting Started

#### Requirements

- STM32 board (e.g., Blue Pill) with DHT22, NEO-6M GPS, I²C screen.
- ESP32 development board.
- MQTT broker (e.g., Mosquitto).

#### Steps

- Flash client/ code onto STM32.
- Flash server/ code onto ESP32.
- Configure Wi-Fi credentials and MQTT broker URL in server/main.c.
- Start your MQTT broker.
- Subscribe to the topic:
```
mosquitto_sub -h <broker_ip> -t "sensors/bluepill/#" -v
```
### Example Output

When STM32 reads a sample from sensors, you’ll see in MQTT:
```
sensors/bluepill/1 {"id":1,"temp":24.56,"hum":61.23,"lat":48.856613,"lon":2.352222}
```
###  Future Improvements

- Add support for multiple STM32 clients with unique IDs.
- Enable TLS/SSL for MQTT communication.
- Add persistent storage on ESP32 (e.g., SD card).
- Implement OTA (Over-The-Air) updates for ESP32.
