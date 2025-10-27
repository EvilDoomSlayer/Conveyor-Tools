/**
 * @file HX711_Calibration.ino
 * @author M. Alejandro Sáchez R.
 * @brief A simple script to find the calibration factor for an HX711 load cell amplifier.
 * * @details This sketch guides you through the process of taring the scale and then
 * measuring a known weight to calculate the `calibration_factor`. This factor
 * is essential for converting the raw ADC readings from the HX711 into
 * meaningful units like grams or pounds in your main project.
 * * WIRING INSTRUCTIONS:
 * * Load Cell -> HX711 Module
 * --------------------------
 * RED    (E+) -> E+
 * BLACK  (E-) -> E-
 * GREEN  (A+) -> A+
 * WHITE  (A-) -> A-
 * * HX711 Module -> Microcontroller (e.g., Arduino, ESP32)
 * -----------------------------------------------------
 * GND -> GND
 * VCC -> 5V or 3.3V (check your module's and microcontrollers's specifications)
 * DOUT -> Pin 23 (configurable below)
 * SCK  -> Pin 19 (configurable below)
 * * * CALIBRATION PROCESS:
 * * 1.  Follow the instructions that appear in the Serial Monitor.
 * 2.  First, the scale will be tared (set to zero) without any weight on it.
 * 3.  Next, place an object with a precisely known weight on the scale (e.g., a 250g coffee bag).
 * 4.  The Serial Monitor will display a raw reading value, for example: "Raw reading with object: -215340.0".
 * 5.  Use this value in the following formula to calculate your calibration factor:
 * * Calibration Factor = (Raw Reading) / (Known Weight in Grams)
 * * Example: -215340.0 / 250.0 = -861.36
 * * 6.  Your calibration factor is -861.36. Save this number! You will use it in your main
 * project sketch with `scale.set_scale(YOUR_FACTOR);` to get readings in grams.
 */

//======================================================================================
// LIBRARIES
//======================================================================================
#include "HX711.h"

//======================================================================================
// PIN DEFINITIONS
//======================================================================================
const int DOUT_PIN = 23; // Data Out from the HX711
const int SCK_PIN  = 19; // Serial Clock for the HX711

//======================================================================================
// GLOBAL OBJECTS
//======================================================================================
HX711 scale; // Create an instance of the HX711 class

//======================================================================================
// SETUP FUNCTION - Runs once at startup
//======================================================================================
void setup() {
  // Start serial communication for user feedback.
  Serial.begin(115200);
  Serial.println("--- HX711 Calibration Script ---");
  
  // Initialize communication with the HX711 module.
  scale.begin(DOUT_PIN, SCK_PIN);
  
  Serial.println("Place the scale on a flat, stable surface and do not put anything on it.");
  Serial.println("Performing tare operation...");
  
  // Before starting, reset the scale to its default settings.
  scale.set_scale();
  // Tare the scale. This sets the current reading as the baseline (zero).
  scale.tare();

  Serial.println("Tare complete.");
  Serial.print("Raw reading after tare (should be close to 0): ");
  // Take an average of 20 readings to get a stable baseline value.
  Serial.println(scale.read_average(20));

  Serial.println("Now, place an object of a known weight on the scale and wait...");
  // Wait 5 seconds to give the user time to place the object.
  delay(5000); 
}

//======================================================================================
// LOOP FUNCTION - Runs repeatedly after setup
//======================================================================================
void loop() {
  // Continuously get the raw reading from the scale.
  // scale.get_units(10) returns an average of 10 raw readings from the ADC.
  Serial.print("Raw reading with object: ");
  Serial.println(scale.get_units(10), 1); // Print the value with one decimal place.
  Serial.println("---------------------------------------");
  
  // Wait for a second before the next reading to avoid spamming the Serial Monitor.
  delay(1000);
}