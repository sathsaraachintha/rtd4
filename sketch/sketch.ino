#include <Wire.h> 
 
#define SDA 8 
#define SCL 9 
#define ADDR 0x3F 
#define CHUNK 2 
 
uint8_t rtdType = 0;   //rtdType = 0 → PT100  ,   rtdType = 1 → PT1000 
uint8_t channels[] = {1,2,3,4}; 
uint8_t numChannels = 4; 
 
float temp[4], res[4]; 
uint8_t fault[4]; 
 
uint8_t crc8(uint8_t *d, uint8_t n) 
{ 
  uint8_t c = 0; 
 
  while (n--) 
  { 
    uint8_t x = *d++; 
 
    for (uint8_t i = 8; i; i--) 
    { 
      uint8_t s = (c ^ x) & 1; 
      c >>= 1; 
      if (s) c ^= 0x8C; 
      x >>= 1; 
    } 
  } 
 
  return c; 
} 
 
void readRTD() 
{ 
  for (uint8_t start = 0; start < numChannels; start += CHUNK) 
  { 
    uint8_t count = min((uint8_t)CHUNK, 
                        (uint8_t)(numChannels - start)); 
 
    Wire.beginTransmission(ADDR); 
    Wire.write(0x01); 
    Wire.write(rtdType); 
 
    for (uint8_t i = 0; i < count; i++) 
      Wire.write(channels[start + i]); 
 
    if (Wire.endTransmission()) 
    { 
      Serial.println("I2C TX FAIL"); 
      continue; 
    } 
 
    delay(15); 
 
    uint8_t bytes = count * 12; 
    Wire.requestFrom(ADDR, bytes); 
 
    if (Wire.available() != bytes) 
    { 
      Serial.println("I2C RX FAIL"); 
      continue; 
    } 
 
    for (uint8_t n = 0; n < count; n++) 
    { 
      uint8_t b[12]; 
 
      for (uint8_t i = 0; i < 12; i++) 
        b[i] = Wire.read(); 
 
      if (crc8(b, 11) != b[11]) 
      { 
        Serial.print("CRC ERROR Channel "); 
        Serial.println(b[1]); 
        continue; 
      } 
 
      uint8_t ch = b[1]; 
 
      memcpy(&temp[ch - 1], b + 2, 4); 
      memcpy(&res[ch - 1], b + 6, 4); 
      fault[ch - 1] = b[10]; 
 
      Serial.print("Ch "); 
      Serial.print(ch); 
      Serial.print(" Temp: "); 
      Serial.print(temp[ch - 1]); 
      Serial.print(" Res: "); 
      Serial.print(res[ch - 1]); 
      Serial.print(" Fault: ");  
      Serial.println(fault[ch - 1]); 
    } 
  } 
} 
 
void setup() 
{ 
  Serial.begin(115200); 
  delay(1000); 
 
  Wire.begin(SDA, SCL); 
 
  Serial.println("NORVI RTD4 STARTED"); 
} 
 
void loop() 
{ 
  readRTD(); 
  delay(1000); 
}