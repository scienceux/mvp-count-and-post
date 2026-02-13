"""
Simple tool to save a device name to ESP32 NVM via serial.
Usage: python device_labeler.py COM_PORT DEVICE_NAME
"""

import sys
import serial
import time

def main():
    if len(sys.argv) != 3:
        print("Usage: python device_labeler.py <COM_PORT> <DEVICE_NAME>")
        print("Example: python device_labeler.py COM3 sensor-001")
        sys.exit(1)

    port = sys.argv[1]
    device_name = sys.argv[2]

    try:
        ser = serial.Serial(port, 115200, timeout=3)
        time.sleep(2)  # Wait for ESP32 to reset after connection
        
        # Send command to set device name (no colon - firmware uses substring(8))
        command = f"SET_NAME{device_name}\n"
        ser.write(command.encode())
        print(f"Sent: {command.strip()}")
        
        # Read response
        time.sleep(0.5)
        response = ser.read_all().decode(errors='ignore').strip()
        if response:
            print(f"Response: {response}")
        else:
            print("No response received")
        
        ser.close()
        print("Done.")
        
    except serial.SerialException as e:
        print(f"Serial error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()
