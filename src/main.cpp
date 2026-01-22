#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>

// --- OBJECTS ---
Adafruit_ADS1115 ads;

// --- DATA STRUCTURE ---
struct AnalogConfig
{
  float slope;      // m
  float intercept;  // c
  float scaledRaw;  // 'x' in the formula (Range: 13107 - 65535)
  float tempValue;  // 'y' in the formula (Temperature)
};

AnalogConfig analogInput;

// --- HARDWARE CONFIGURATION ---
// 4mA * 250 Ohm = 1.0V
// 20mA * 250 Ohm = 5.0V
const float shuntResistor = 250.0; 

// --- TIMING ---
unsigned long previousMillis = 0;
const long interval = 1000; 

// --- FORWARD DECLARATION ---
void readSensors();
void handleSerialCommands();

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  Serial.println("--- SYSTEM STARTING (Range: 13107-65535) ---");
  Serial.println("Commands: 'm=<value>' or 'c=<value>' to calibrate.");
  
  Wire.begin();
  if (!ads.begin()) {
    Serial.println("Error: ADS1115 not found.");
    while (1);
  }
  
  // Set Gain to +/- 6.144V (1 bit = 0.1875mV)
  // This covers the 0-5V range required by the 250 ohm resistor
  ads.setGain(GAIN_TWOTHIRDS);

  // --- INITIAL CALIBRATION ---
  // Formula: Temp = (m * ScaledRaw) + c
  // Example Calculation:
  // If Raw Low (4mA) = 13107, Temp Low = -20
  // If Raw High (20mA) = 65535, Temp High = 100
  // m = (100 - (-20)) / (65535 - 13107) = 120 / 52428 = 0.002288
  // c = -20 - (0.002288 * 13107) = -50 approx.
  
  analogInput.slope = 0.002288;   // Default 'm' (Example)
  analogInput.intercept = -50.0;  // Default 'c' (Example)
}

void loop() {
  // 1. Check Serial Commands
  handleSerialCommands();

  // 2. Read Sensors periodically
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    readSensors();
  }
}

void readSensors() {
  // 1. Read Physical ADC (Signed 16-bit)
  int16_t adc0 = ads.readADC_SingleEnded(0);
  
  // 2. Convert to Voltage (0 - 5V range expected)
  float voltage = ads.computeVolts(adc0);

  // 3. Calculate Scaled Raw (13107 - 65535)
  // This maps the 0-5V range to a 16-bit scale.
  // 1V (4mA) = (1.0/5.0) * 65535 = 13107
  // 5V (20mA) = (5.0/5.0) * 65535 = 65535
  float raw_x = (voltage / 5.0) * 65535.0;
  
  analogInput.scaledRaw = raw_x;

  // 4. Calculate Temperature (y = mx + c)
  float m = analogInput.slope;
  float c = analogInput.intercept;
  
  analogInput.tempValue = (m * raw_x) + c;

  // 5. Output to Serial (No Current/mA displayed)
  // We use standard Serial.print formatting
  Serial.print("Raw: "); 
  Serial.print(analogInput.scaledRaw, 0); // Display as integer-like
  Serial.print(" | m: "); 
  Serial.print(m, 6); // High precision for small slope
  Serial.print(" c: "); 
  Serial.print(c, 2);
  Serial.print(" | Temp: "); 
  Serial.print(analogInput.tempValue, 2); 
  Serial.println(" C");
}

void handleSerialCommands() {
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    
    // Update Slope (m)
    if (input.startsWith("m=") || input.startsWith("M=")) {
      analogInput.slope = input.substring(2).toFloat();
      Serial.print(">>> Updated Slope (m): ");
      Serial.println(analogInput.slope, 6);
    }
    // Update Intercept (c)
    else if (input.startsWith("c=") || input.startsWith("C=")) {
      analogInput.intercept = input.substring(2).toFloat();
      Serial.print(">>> Updated Intercept (c): ");
      Serial.println(analogInput.intercept, 2);
    }
  }
}