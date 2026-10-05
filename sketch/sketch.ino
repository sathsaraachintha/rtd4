#include <Wire.h>

#define SDA_PIN 8
#define SCL_PIN 9
#define PCA_RESET 21 

void setup() {
  Serial.begin(115200);
  // Give you a few seconds to open the Serial Monitor
  delay(3000); 
  
  Serial.println("\n==================================");
  Serial.println("NORVI I2C HARDWARE SCANNER");
  Serial.println("==================================");

  // Wake up the Expansion Bus
  pinMode(PCA_RESET, OUTPUT);
  digitalWrite(PCA_RESET, LOW);   
  delay(100);
  digitalWrite(PCA_RESET, HIGH);  
  
  // Give the RTD module a massive 1-second delay to boot up
  Serial.println("[SYSTEM] Waking up expansion bus... waiting 1 second.");
  delay(1000);                     

  // Start I2C at standard 100kHz speed
  Wire.begin(SDA_PIN, SCL_PIN, 100000);

  Serial.println("\n--- Scanning I2C Bus ---");
  byte error, address;
  int devicesFound = 0;

  for (address = 1; address < 127; address++) {
      Wire.beginTransmission(address);
      error = Wire.endTransmission();
      
      if (error == 0) {
          Serial.print("[FOUND] Device responded at address: 0x");
          if (address < 16) Serial.print("0");
          Serial.println(address, HEX);
          devicesFound++;
      } else if (error == 4) {
          Serial.print("[ERROR 4] Bus error at address: 0x");
          if (address < 16) Serial.print("0");
          Serial.println(address, HEX);
      }
  }
  
  if (devicesFound == 0) {
      Serial.println("\n[FAIL] NO DEVICES FOUND ON BUS!");
      Serial.println("Check module seating, DIP switches, and power.");
  } else {
      Serial.println("\n--- Scan Complete ---");
  }
}

void loop() {
  // Do nothing
}