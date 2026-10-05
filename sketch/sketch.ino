#include <Wire.h>
#include <SPI.h>

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// --- CRUCIAL FIX: EXPANSION BUS RESET PIN ---
#define PCA_RESET 21 

// --- LovyanGFX Display Configuration for NORVI X ---
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

// --- PINS & ADDRESSES ---
#define SDA_PIN 8
#define SCL_PIN 9
#define RTD_SLAVE_ADDR 0x3F 

// --- RTD CONFIGURATION ---
const uint8_t rtdType = 0;           // 0 = PT100
const uint8_t numChannels = 4;       
const uint8_t channelsToRead[4] = {1, 2, 3, 4}; 

float rtdTemp[4] = {0, 0, 0, 0};
float rtdRes[4]  = {0, 0, 0, 0};
uint8_t rtdFault[4] = {0, 0, 0, 0};

unsigned long lastRTDRead = 0;
unsigned long lastDisplayUpdate = 0;

// System Status String for On-Screen Debugging
String sysStatus = "BOOTING...";

// ============================================================
// CRC CHECK FOR RTD
// ============================================================
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

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n==================================");
  Serial.println("NORVI EXPE-RTD4 (PT100) CHUNKED");
  Serial.println("==================================");

  // Wake up the EXPE-RTD module on the bus
  pinMode(PCA_RESET, OUTPUT);
  digitalWrite(PCA_RESET, LOW);   
  delay(100);
  digitalWrite(PCA_RESET, HIGH);  
  
  // THE FIX: 1 Full Second boot delay for the RTD Module!
  delay(1000);                     

  // THE FIX: Explicitly set I2C bus to 100kHz for stability!
  Wire.begin(SDA_PIN, SCL_PIN, 100000);

  tft.init();
  tft.setRotation(0); 
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2); 
  
  sysStatus = "WAITING...";
  Serial.println("[SYSTEM] Boot Sequence Complete.\n");
}

// ============================================================
// MAIN LOOP
// ============================================================
void loop() {
  // --- FETCH RTD DATA (Every 1 Second) ---
  if (millis() - lastRTDRead >= 1000) {
     lastRTDRead = millis();
     readRTDChunked();
     
     // Print to Serial Monitor
     Serial.printf("[DATA] CH1: %.1fC | CH2: %.1fC | STAT: %s\n", rtdTemp[0], rtdTemp[1], sysStatus.c_str());
  }

  // --- TFT REFRESH (Every 100ms) ---
  if (millis() - lastDisplayUpdate >= 100) {
    lastDisplayUpdate = millis();
    tft.setCursor(0, 5);
    displayRTD();
  }
}

// ============================================================
// RTD CHUNKED FETCH LOGIC
// ============================================================
#define CHUNK_SIZE 2

void readRTDChunked() {
  for (int chunkStart = 0; chunkStart < numChannels; chunkStart += CHUNK_SIZE) {
      uint8_t chunkCount = min((int)CHUNK_SIZE, (int)(numChannels - chunkStart));

      Wire.beginTransmission(RTD_SLAVE_ADDR);
      Wire.write(0x01);        // command
      Wire.write(rtdType);     // 0 = PT100

      // Request specific channels
      for (int i = 0; i < chunkCount; i++) {
          Wire.write(channelsToRead[chunkStart + i]);
      }

      int txError = Wire.endTransmission();
      if (txError != 0) {
          sysStatus = "I2C TX FAIL (" + String(txError) + ")";
          continue; // Skip reading if TX failed
      }

      delay(15); // Give the RTD MCU time to prepare the data

      uint8_t totalBytes = chunkCount * 12; 
      int rxBytes = Wire.requestFrom((int)RTD_SLAVE_ADDR, (int)totalBytes);

      if (rxBytes != totalBytes) {
          sysStatus = "I2C RX FAIL (" + String(rxBytes) + ")";
          continue; // Skip parsing if we didn't get enough bytes
      }

      sysStatus = "READ OK";

      // Parse the incoming chunk
      for (int idx = 0; idx < chunkCount; idx++) {
          uint8_t buf[12];
          for (int i = 0; i < 12; i++) buf[i] = Wire.read();

          if (crc8(buf, 11) != buf[11]) {
              sysStatus = "CRC ERROR CH" + String(buf[1]);
              continue;
          }

          float temp, res;
          memcpy(&temp, &buf[2], 4);
          memcpy(&res, &buf[6], 4);
          
          uint8_t ch = buf[1] - 1; // 0-indexed for array storage

          rtdTemp[ch] = temp;
          rtdRes[ch]  = res;
          rtdFault[ch] = buf[10];
      }
  }
}

// ============================================================
// DISPLAY FUNCTION
// ============================================================
void displayRTD() {
  tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
  tft.println("   PT100 MONITOR    ");
  tft.println("--------------------");
  tft.println("");

  for (int i = 0; i < 4; i++) {
    if (rtdFault[i] != 0) {
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.printf(" CH%d: FAULT (E%d)      \n", i + 1, rtdFault[i]);
    } else {
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.printf(" CH%d: %.1fC %.1fR   \n", i + 1, rtdTemp[i], rtdRes[i]);
    }
  }
  
  tft.println("                    "); 
  tft.println("                    "); 
  
  // --- ON-SCREEN DEBUGGER ---
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(0, 240);
  tft.println("--------------------");
  
  if (sysStatus == "READ OK") {
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
  } else {
    tft.setTextColor(TFT_RED, TFT_BLACK);
  }
  
  // Print the system status padded with spaces to overwrite old text
  tft.printf(" STAT: %-12s \n", sysStatus.c_str());
}