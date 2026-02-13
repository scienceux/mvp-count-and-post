# Device Labeler Tool

A GUI tool for configuring ESP32 devices over serial connection.

## Setup

```bash
cd tools
pip install -r requirements.txt
```

## Usage

1. Connect your ESP32 device via USB
2. Run the tool:
   ```bash
   python device_labeler.py
   ```
3. Select the COM port from the dropdown and click **Connect**
4. Click **Read from Device** to load current settings
5. Edit fields as needed
6. Click **Write to Device** to save changes
7. Use **Identify (Blink)** to make the device LED blink for physical identification

## Configurable Fields

| Field | Description |
|-------|-------------|
| Device Name | Unique identifier for this device |
| Mode | Operating mode |
| Event Name | Name of the event being monitored |
| WiFi SSID | WiFi network name |
| WiFi Password | WiFi network password |

## Serial Protocol

The tool communicates using simple text commands:

| Command | Description | Response |
|---------|-------------|----------|
| `IDENTIFY` | Get device model and current config | `MODEL=...;NAME=...;MODE=...;EVENT=...;WIFI_SSID=...` |
| `GET_CONFIG` | Get current configuration | `NAME=...;MODE=...;EVENT=...;WIFI_SSID=...` |
| `SET_NAME <name>` | Set device name | `OK` |
| `SET_MODE <mode>` | Set device mode | `OK` |
| `SET_EVENT <event>` | Set event name | `OK` |
| `SET_WIFI <ssid>\|<pass>` | Set WiFi credentials | `OK` or `ERR` |
| `LED_ON` | Turn LED on | `OK` |
| `LED_OFF` | Turn LED off | `OK` |
| `BLINK<n>` | Blink LED n times | `OK` |
