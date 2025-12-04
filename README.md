# Conveyor-Tools

This repository contains standalone utility scripts (Arduino sketches) used for calibrating and debugging the hardware of the **Luggage Classification Conveyor** system. 

These tools are designed to run independently on the ESP32 to prepare the sensors and simulate external components before running the main firmware.

## Repository Contents

| Sketch Name | Description |
| :--- | :--- |
| **`ESP32_BLE_SERVER`** | A BLE Server simulator using FreeRTOS. It mimics the behavior of the external camera module by allowing you to manually send classification commands via the Serial Monitor. |
| **`HX711_Calibration`** | A utility to read raw data from the load cell. Used to calculate the `CALIBRATION_FACTOR` required for the main firmware. |

***

## 1. BLE Server Simulator (`ESP32_BLE_SERVER`)

The main conveyor firmware acts as a **BLE Client** that waits for classification data. This script turns a secondary ESP32 into a **BLE Server** to simulate that data source, allowing you to test the conveyor's mechanical sorting logic without the actual camera hardware.

### Features
* **FreeRTOS Architecture**: Uses dual-core processing. Core 0 handles the BLE stack, while Core 1 handles Serial input.
* **Dynamic Updates**: You can type messages directly into the Serial Monitor to update the BLE characteristic in real-time.

### Usage
1.  Upload this sketch to a **secondary** ESP32 (do not overwrite your main conveyor ESP32).
2.  Open the Serial Monitor at **115200 baud**.
3.  The device will start advertising the Service UUID: `4fafc201-1fb5-459e-8fcc-c5c9c331914b`.
4.  Once the main conveyor connects, type a classification command into the input field and press **Enter**.
    * *Example*: Type `light` to simulate a light object.
    * *Example*: Type `heavy` to simulate a heavy object.
5.  The script captures this input and updates the BLE characteristic, triggering the response in the main conveyor.

***

## 2. Load Cell Calibration (`HX711_Calibration`)

The main firmware uses a specific `CALIBRATION_FACTOR` to convert raw electrical signals into grams. This script provides the raw data needed to calculate that factor.

### Hardware Setup
Ensure the HX711 is connected to the following pins (matching the main firmware configuration):
* **DOUT**: GPIO 23
* **SCK**: GPIO 19

### Usage
1.  Upload the sketch to the conveyor's ESP32.
2.  Open the Serial Monitor at **115200 baud**.
3.  **Tare**: The script will automatically tare (zero) the scale on startup. Ensure the belt is empty.
4.  **Place Weight**: Place an object of a known weight (e.g., a 100g weight or a smartphone with known weight) on the scale.
5.  **Record Raw Value**: The monitor will print the raw average reading (e.g., `-215340.0`).
6.  **Calculate Factor**: Use the formula provided in the script header:
    ```
    Calibration Factor = (Raw Reading) / (Known Weight in Grams)
    ```
    *Example*: `-215340.0 / 100.0 = -2153.4`
7.  **Update Firmware**: Copy this resulting number into the `CALIBRATION_FACTOR` constant in your main project configuration.

## Dependencies

* **ESP32 Board Support Package**
* **HX711 Library** (by Bogdan Necula or similar)
* **ESP32 BLE Arduino** (built-in)
 