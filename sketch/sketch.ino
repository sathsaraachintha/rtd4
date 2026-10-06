#include <Wire.h>
#include <SPI.h>
#include <PCA9536D.h> // Ensure you have this installed for the buttons!

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// --- CRUCIAL FIX: EXPANSION BUS RESET PIN ---
#define PCA_RESET 21 

// ---------------- LovyanGFX Setup ----------------
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel_instance;
  lgfx::Bus_SPI      _bus_instance;

public:
  LGFX(void) {
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read  = 16000000;
      cfg.spi_3wire  = false;
      cfg.use_lock   = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      
      cfg.pin_sclk = 12; 
      cfg.pin_mosi = 11; 
      cfg.pin_miso = 13; 
      cfg.pin_dc   = 46; 
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs           = 45; 
      cfg.pin_rst          = 47; 
      cfg.pin_busy         = -1;
      cfg.panel_width      = 240;
      cfg.panel_height     = 320;
      cfg.offset_x         = 0;
      cfg.offset_y         = 0;
      cfg.offset_rotation  = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits  = 1;
      cfg.readable         = true;
      cfg.invert           = true; 
      cfg.rgb_order        = false;
      cfg.dlen_16bit       = false;
      cfg.bus_shared       = true; 
      _panel_instance.config(cfg);
    }
    setPanel(&_panel_instance);
  }
};

LGFX tft; 
PCA9536 io; 

// ---------------- PINS & ADDRESSES ----------------
#define SDA_PIN 8
#define SCL_PIN 9
#define RTD_ADDR 0x3F

#define IO_PB1  0  // Button 1
#define IO_PB2  3  // Button 2

// ---------------- RTD CONFIG & DATA ----------------
uint8_t rtdType = 0;                 // 0 = PT100 | 1 = PT1000
uint8_t channelsToRead[4] = {1, 2, 3, 4}; 
uint8_t numChannels = 4;
#define CHUNK_SIZE 2

float rtdTemp[4] = {0};
float rtdRes[4]  = {0};
uint8_t rtdFault[4] = {0};

// ---------------- TIMERS & STATES ----------------
unsigned long lastRTDRead = 0;
unsigned long lastDisplayUpdate = 0;
bool lastPb1State = HIGH;
bool lastPb2State = HIGH;

// ---------------- CRC ----------------
uint8_t crc8(uint8_t *data, int len) {
  uint8_t crc = 0x00;
  while (len--) {
    uint8_t extract = *data++;
    for (uint8_t i = 8; i; i--) {
      uint8_t sum = (crc ^ extract) & 0x01;
      crc >>= 1;
      if (sum) crc ^= 0x8C;
      extract >>= 1;
    }
  }
  return crc;
}

// ---------------- FETCH RTD LOGIC ----------------
void fetchRTDChunked() {
  for (uint8_t start = 0; start < numChannels; start += CHUNK_SIZE) {
    uint8_t count = min((uint8_t)CHUNK_SIZE, (uint8_t)(numChannels - start));

    Wire.beginTransmission(RTD_ADDR);
    Wire.write(0x01);
    Wire.write(rtdType);

    for (uint8_t i = 0; i < count; i++) {
      Wire.write(channelsToRead[start + i]);
    }

    if (Wire.endTransmission() != 0) {
      Serial.println("[ERROR] I2C TX FAIL");
      continue;
    }

    delay(15);

    uint8_t totalBytes = count * 12;
    
    // Explicit casting fixes the compilation error
    Wire.requestFrom((uint8_t)RTD_ADDR, (uint8_t)totalBytes);

    if (Wire.available() != totalBytes) {
      Serial.println("[ERROR] I2C RX FAIL");
      continue;
    }

    for (uint8_t n = 0; n < count; n++) {
      uint8_t b[12];
      for (uint8_t i = 0; i < 12; i++) {
        b[i] = Wire.read();
      }

      if (crc8(b, 11) != b[11]) {
        Serial.printf("[WARNING] CRC ERROR Channel %d\n", b[1]);
        continue;
      }

      uint8_t ch = b[1]; // 1-indexed channel (1-4)
      
      memcpy(&rtdTemp[ch - 1], b + 2, 4);
      memcpy(&rtdRes[ch - 1], b + 6, 4);
      rtdFault[ch - 1] = b[10];
    }
  }
}

// ---------------- DISPLAY LOGIC ----------------
void displayRTD() {
  tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
  tft.println("  EXPE-RTD Module   ");
  tft.println("--------------------");
  
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.printf(" Type: %-12s\n\n", rtdType == 0 ? "PT100" : "PT1000");

  for (int i = 0; i < 4; i++) {
    if (rtdFault[i] != 0) {
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.printf(" CH%d: FAULT (E%d)      \n", i + 1, rtdFault[i]);
    } else {
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      // Padded to overwrite old text artifacts perfectly
      tft.printf(" CH%d: %5.1fC %5.1fR \n", i + 1, rtdTemp[i], rtdRes[i]);
    }
  }
  
  tft.println("                    "); 
  tft.println("                    "); 
  
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(0, 260);
  tft.println("--------------------");
  tft.println("[B2:PT-TYPE] [B1:REF]");
}

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);
  delay(1000); 

  Serial.println("\n--- BOOTING NORVI EXPE-RTD4 MASTER ---");

  // 1. Wake up the Expansion Bus
  pinMode(PCA_RESET, OUTPUT);
  digitalWrite(PCA_RESET, LOW);   
  delay(100);
  digitalWrite(PCA_RESET, HIGH);  
  
  Serial.println("Waiting 1s for RTD Module to initialize...");
  delay(1000); 

  // 2. Start I2C at standard 100kHz
  Wire.begin(SDA_PIN, SCL_PIN, 100000); 
  
  // 3. Initialize Buttons
  if (io.begin()) {
    io.pinMode(IO_PB1, INPUT);
    io.pinMode(IO_PB2, INPUT);
    Serial.println("Front Panel Buttons Initialized.");
  } else {
    Serial.println("WARNING: PCA9536 Not Found (Buttons disabled).");
  }

  // 4. Initialize TFT
  tft.init();
  tft.setRotation(0); 
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2); 

  Serial.println("System Ready. Reading Channels...");
}

// ---------------- MAIN LOOP ----------------
void loop() {
  // --- Button Input Handling ---
  bool currentPb1 = io.digitalRead(IO_PB1); 
  bool currentPb2 = io.digitalRead(IO_PB2); 

  // Button 1: Force Screen Clear / Refresh
  if (currentPb1 == LOW && lastPb1State == HIGH) {
    tft.fillScreen(TFT_BLACK); 
    delay(50); // debounce
  }
  lastPb1State = currentPb1;

  // Button 2: Toggle Sensor Type (PT100 <-> PT1000)
  if (currentPb2 == LOW && lastPb2State == HIGH) {
    rtdType = (rtdType == 0) ? 1 : 0;
    Serial.printf("Switched mode to: %s\n", rtdType == 0 ? "PT100" : "PT1000");
    tft.fillScreen(TFT_BLACK); 
    delay(50); // debounce
  }
  lastPb2State = currentPb2;

  // --- Background Tasks ---
  
  // Fetch Sensor Data (Every 1 second)
  if (millis() - lastRTDRead >= 1000) {
      lastRTDRead = millis();
      fetchRTDChunked();
      
      // Print to Serial for debugging
      Serial.printf("[DATA] CH1: %.1fC | CH2: %.1fC | CH3: %.1fC | CH4: %.1fC\n", 
                    rtdTemp[0], rtdTemp[1], rtdTemp[2], rtdTemp[3]);
  }

  // Update TFT Display (Every 100 milliseconds)
  if (millis() - lastDisplayUpdate >= 100) {
      lastDisplayUpdate = millis();
      tft.setCursor(0, 5);
      displayRTD();
  }
}