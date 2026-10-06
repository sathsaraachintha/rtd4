// NPD-NORVI EXPE RTD – MASTER (CHUNKED READ) - NO DISPLAY

#include <Wire.h>

// ---------------- PINS ----------------
#define SDA   8
#define SCL   9

#define RTD_SLAVE_ADDR 0x3F

// ---------------- CONFIG ----------------
uint8_t rtdType = 0;                 // 0 = PT100 | 1 = PT1000
uint8_t channelsToRead[4] = {1};     // default channel 1
uint8_t numChannels = 1;

// ---------------- DATA ----------------
float rtdTemp[4] = {0};
float rtdRes[4]  = {0};
uint8_t rtdFault[4] = {0};
bool configMode = false;

// ---------------- TIMER ----------------
unsigned long lastRead = 0;
#define READ_INTERVAL 1000

// ---------------- CRC ----------------
uint8_t crc8(uint8_t *data, int len)
{
  uint8_t crc = 0x00;
  while (len--)
  {
    uint8_t extract = *data++;
    for (uint8_t i = 8; i; i--)
    {
      uint8_t sum = (crc ^ extract) & 0x01;
      crc >>= 1;
      if (sum) crc ^= 0x8C;
      extract >>= 1;
    }
  }
  return crc;
}

// ---------------- SERIAL ----------------
void handleSerial()
{
  if (Serial.available())
  {
    char c = Serial.read();
    if (c == 'C' || c == 'c') enterConfig();
  }
}

// ---------------- READ SLAVE (CHUNKED) ----------------
#define CHUNK_SIZE 2

void readRTD()
{
  for (int chunkStart = 0; chunkStart < numChannels; chunkStart += CHUNK_SIZE)
  {
      uint8_t chunkCount = min(CHUNK_SIZE, numChannels - chunkStart);

      Wire.beginTransmission(RTD_SLAVE_ADDR);
      Wire.write(0x01);        // command
      Wire.write(rtdType);     // RTD type

      for (int i = 0; i < chunkCount; i++)
      {
          Wire.write(channelsToRead[chunkStart + i]);
      }

      if (Wire.endTransmission() != 0)
      {
          Serial.println("I2C TX FAIL");
          continue;
      }

      delay(15);

      uint8_t totalBytes = chunkCount * 12;
      Wire.requestFrom((uint8_t)RTD_SLAVE_ADDR, (uint8_t)totalBytes);

      if (Wire.available() != totalBytes)
      {
          Serial.println("I2C RX FAIL");
          continue;
      }

      for (int idx = 0; idx < chunkCount; idx++)
      {
          uint8_t buf[12];
          for (int i = 0; i < 12; i++) buf[i] = Wire.read();

          if (crc8(buf, 11) != buf[11])
          {
              Serial.print("CRC ERROR Channel ");
              Serial.println(buf[1]);
              continue;
          }

          float temp, res;
          memcpy(&temp, &buf[2], 4);
          memcpy(&res, &buf[6], 4);
          uint8_t ch = buf[1];

          rtdTemp[ch-1] = temp;
          rtdRes[ch-1] = res;
          rtdFault[ch-1] = buf[10];

          Serial.print("Ch ");
          Serial.print(ch);
          Serial.print(" Temp: "); Serial.print(temp);
          Serial.print(" Res: "); Serial.print(res);
          Serial.print(" Fault: "); Serial.println(buf[10]);
      }
  }
}

// ---------------- SETUP ----------------
void setup()
{
  Serial.begin(115200);
  delay(10000);

  Wire.begin(SDA, SCL);
  delay(100);

  printCurrentConfig();

  Serial.println("NORVI RTD MASTER STARTED");
}

// ---------------- LOOP ----------------
unsigned long lastChunkRead = 0;
void loop()
{
  handleSerial();

  if (!configMode && millis() - lastChunkRead >= READ_INTERVAL)
  {
      lastChunkRead = millis();
      readRTD();
  }
}

// ---------------- CONFIG ----------------
void enterConfig() {
    configMode = true;
    Serial.println("\n===== CONFIG MODE =====");
    Serial.println("Enter RTD Type and Channels in one line:");
    Serial.println("Examples: 100,1 for PT100 Ch1, 1000,1-3 for PT1000 Ch1,2,3");

    while (Serial.available()) Serial.read(); // flush buffer
    String input = "";
    while (input.length() == 0)
    {
        if (Serial.available())
        {
            input = Serial.readStringUntil('\n');
            input.trim();
        }
        delay(10);
    }

    int commaIndex = input.indexOf(',');
    if (commaIndex != -1)
    {
        String typeStr = input.substring(0, commaIndex);
        String chStr   = input.substring(commaIndex + 1);

        int typeInput = typeStr.toInt();
        rtdType = (typeInput == 100) ? 0 : 1;

        numChannels = 0;
        int dashIndex = chStr.indexOf('-');
        if (dashIndex != -1)
        {
            int startCh = chStr.substring(0, dashIndex).toInt();
            int endCh   = chStr.substring(dashIndex + 1).toInt();
            for (int i = startCh; i <= endCh; i++)
                channelsToRead[numChannels++] = i;
        }
        else
        {
            int start = 0;
            while (start < chStr.length() && numChannels < 4)
            {
                int commaPos = chStr.indexOf(',', start);
                String chPart = (commaPos == -1) ? chStr.substring(start) : chStr.substring(start, commaPos);
                int ch = chPart.toInt();
                if (ch >= 1 && ch <= 4) channelsToRead[numChannels++] = ch;
                if (commaPos == -1) break;
                start = commaPos + 1;
            }
        }
    }
    Serial.println("\nConfiguration Saved:");
    printCurrentConfig();
    configMode = false;
}

// ---------------- PRINT CONFIG ----------------
void printCurrentConfig() {
    Serial.print("RTD Type : "); Serial.println(rtdType == 0 ? "PT100" : "PT1000");
    Serial.print("Channels : ");
    for (int i = 0; i < numChannels; i++)
    {
        Serial.print(channelsToRead[i]);
        if (i < numChannels - 1) Serial.print(", ");
    }
    Serial.println();
}