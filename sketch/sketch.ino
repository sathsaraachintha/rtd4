// ============================================================
// NPD-NORVI EXPE-RTD – MASTER
// LovyanGFX Version
// Based directly on the known-working TFT_eSPI code
// ============================================================

#include <Wire.h>
#include <SPI.h>

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include <CST816S.h>


// ============================================================
// PINS
// ============================================================

#define SDA   8
#define SCL   9

#define MISO  13
#define MOSI  11
#define SCLK  12

#define DSP_CS 45

#define RTD_SLAVE_ADDR 0x3F


// ============================================================
// LOVYANGFX DISPLAY CONFIGURATION
// ============================================================

class LGFX : public lgfx::LGFX_Device
{
    lgfx::Panel_ST7789 _panel_instance;
    lgfx::Bus_SPI      _bus_instance;

public:

    LGFX(void)
    {
        // ----------------------------------------------------
        // SPI BUS
        // ----------------------------------------------------

        {
            auto cfg = _bus_instance.config();

            cfg.spi_host = SPI2_HOST;
            cfg.spi_mode = 0;

            cfg.freq_write = 40000000;
            cfg.freq_read  = 16000000;

            cfg.spi_3wire = false;
            cfg.use_lock = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;

            cfg.pin_sclk = SCLK;
            cfg.pin_mosi = MOSI;
            cfg.pin_miso = MISO;

            cfg.pin_dc = 46;

            _bus_instance.config(cfg);

            _panel_instance.setBus(&_bus_instance);
        }


        // ----------------------------------------------------
        // ST7789 PANEL
        // ----------------------------------------------------

        {
            auto cfg = _panel_instance.config();

            cfg.pin_cs = DSP_CS;

            cfg.pin_rst = 47;

            cfg.pin_busy = -1;

            cfg.panel_width = 240;
            cfg.panel_height = 320;

            cfg.offset_x = 0;
            cfg.offset_y = 0;

            cfg.offset_rotation = 0;

            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits = 1;

            cfg.readable = true;

            cfg.invert = true;

            cfg.rgb_order = false;

            cfg.dlen_16bit = false;

            // Same SPI bus can be shared
            cfg.bus_shared = true;

            _panel_instance.config(cfg);
        }


        setPanel(&_panel_instance);
    }
};


// Create display object
LGFX tft;


// ============================================================
// TOUCH
// ============================================================
//
// Kept from your original working code.
// This uses the same I2C pins 8/9.
//

CST816S touch(8, 9, 47, 48);


// ============================================================
// RTD CONFIGURATION
// ============================================================

// 0 = PT100
// 1 = PT1000

uint8_t rtdType = 0;


// Channels
uint8_t channelsToRead[4] = {1};

uint8_t numChannels = 1;


// ============================================================
// RTD DATA
// ============================================================

float rtdTemp[4] = {0};

float rtdRes[4] = {0};

uint8_t rtdFault[4] = {0};


// Configuration mode
bool configMode = false;


// ============================================================
// TIMER
// ============================================================

#define READ_INTERVAL 1000

unsigned long lastChunkRead = 0;


// ============================================================
// CHUNK SIZE
// ============================================================

#define CHUNK_SIZE 2


// ============================================================
// CRC8
// ============================================================

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

            if (sum)
            {
                crc ^= 0x8C;
            }

            extract >>= 1;
        }
    }

    return crc;
}


// ============================================================
// DISPLAY HEADER
// ============================================================

void drawHeader()
{
    tft.fillScreen(TFT_BLACK);

    tft.setTextColor(TFT_YELLOW);

    tft.setFont(&fonts::FreeSansBold12pt7b);

    tft.setCursor(60, 30);

    tft.print("NORVI");


    tft.setCursor(20, 60);

    tft.print("EXPE-RTD TEST");
}


// ============================================================
// DISPLAY VALUES
// ============================================================

void drawValues()
{
    tft.fillScreen(TFT_BLACK);

    tft.setTextColor(TFT_YELLOW);

    tft.setFont(&fonts::FreeSansBold9pt7b);


    int y = 25;


    // --------------------------------------------------------
    // TITLE
    // --------------------------------------------------------

    tft.setCursor(0, y);

    tft.println("NORVI EXPE-RTD MASTER");


    y += 25;


    // --------------------------------------------------------
    // RTD TYPE
    // --------------------------------------------------------

    tft.setCursor(0, y);

    tft.setTextColor(TFT_CYAN);

    tft.print("RTD Type: ");

    tft.println(
        rtdType == 0 ? "PT100" : "PT1000"
    );


    y += 25;


    // --------------------------------------------------------
    // CHANNEL DATA
    // --------------------------------------------------------

    for (int i = 0; i < numChannels; i++)
    {
        uint8_t ch = channelsToRead[i];


        // Channel + temperature

        tft.setCursor(0, y);

        tft.setTextColor(TFT_WHITE);

        tft.print("Ch ");
        tft.print(ch);
        tft.print(" : ");


        tft.setTextColor(TFT_GREEN);

        tft.print("Temp: ");

        tft.print(
            rtdTemp[ch - 1],
            2
        );

        tft.println(" C");


        y += 22;


        // Resistance

        tft.setCursor(0, y);

        tft.setTextColor(TFT_BLUE);

        tft.print("Res: ");

        tft.print(
            rtdRes[ch - 1],
            2
        );

        tft.print(" Ohm");


        y += 22;


        // Fault

        tft.setCursor(0, y);

        tft.setTextColor(TFT_RED);

        tft.print("Fault: ");

        tft.println(
            rtdFault[ch - 1]
        );


        y += 25;
    }
}


// ============================================================
// SERIAL HANDLER
// ============================================================

void handleSerial()
{
    if (Serial.available())
    {
        char c = Serial.read();

        if (c == 'C' || c == 'c')
        {
            enterConfig();
        }
    }
}


// ============================================================
// READ RTD
// ============================================================

void readRTD()
{
    for (
        int chunkStart = 0;
        chunkStart < numChannels;
        chunkStart += CHUNK_SIZE
    )
    {
        uint8_t chunkCount =
            min(
                (uint8_t)CHUNK_SIZE,
                (uint8_t)(numChannels - chunkStart)
            );


        // ----------------------------------------------------
        // START I2C TRANSMISSION
        // ----------------------------------------------------

        Wire.beginTransmission(
            RTD_SLAVE_ADDR
        );


        // Command
        Wire.write(0x01);


        // RTD type
        Wire.write(rtdType);


        // Channels
        for (int i = 0; i < chunkCount; i++)
        {
            Wire.write(
                channelsToRead[
                    chunkStart + i
                ]
            );
        }


        // ----------------------------------------------------
        // SEND
        // ----------------------------------------------------

        uint8_t txResult =
            Wire.endTransmission();


        if (txResult != 0)
        {
            Serial.print("I2C TX FAIL - Error: ");

            Serial.println(txResult);

            continue;
        }


        // ----------------------------------------------------
        // WAIT FOR SLAVE
        // ----------------------------------------------------

        delay(15);


        // ----------------------------------------------------
        // REQUEST RESPONSE
        // ----------------------------------------------------

        uint8_t totalBytes =
            chunkCount * 12;


        Wire.requestFrom(
            (uint8_t)RTD_SLAVE_ADDR,
            totalBytes
        );


        // ----------------------------------------------------
        // CHECK RESPONSE SIZE
        // ----------------------------------------------------

        if (
            Wire.available() !=
            totalBytes
        )
        {
            Serial.println(
                "I2C RX FAIL"
            );

            continue;
        }


        // ----------------------------------------------------
        // READ EACH CHANNEL
        // ----------------------------------------------------

        for (
            int idx = 0;
            idx < chunkCount;
            idx++
        )
        {
            uint8_t buf[12];


            for (int i = 0; i < 12; i++)
            {
                buf[i] = Wire.read();
            }


            // ------------------------------------------------
            // CRC
            // ------------------------------------------------

            if (
                crc8(buf, 11)
                !=
                buf[11]
            )
            {
                Serial.print(
                    "CRC ERROR Channel "
                );

                Serial.println(
                    buf[1]
                );

                continue;
            }


            // ------------------------------------------------
            // EXTRACT TEMPERATURE
            // ------------------------------------------------

            float temp;

            memcpy(
                &temp,
                &buf[2],
                4
            );


            // ------------------------------------------------
            // EXTRACT RESISTANCE
            // ------------------------------------------------

            float res;

            memcpy(
                &res,
                &buf[6],
                4
            );


            // Channel
            uint8_t ch = buf[1];


            // Safety check
            if (
                ch < 1 ||
                ch > 4
            )
            {
                Serial.println(
                    "Invalid channel!"
                );

                continue;
            }


            // ------------------------------------------------
            // SAVE VALUES
            // ------------------------------------------------

            rtdTemp[ch - 1] =
                temp;

            rtdRes[ch - 1] =
                res;

            rtdFault[ch - 1] =
                buf[10];


            // ------------------------------------------------
            // SERIAL OUTPUT
            // ------------------------------------------------

            Serial.print("Ch ");

            Serial.print(ch);

            Serial.print(" Temp: ");

            Serial.print(
                temp
            );

            Serial.print(" Res: ");

            Serial.print(
                res
            );

            Serial.print(" Fault: ");

            Serial.println(
                buf[10]
            );
        }
    }


    // --------------------------------------------------------
    // UPDATE DISPLAY
    // --------------------------------------------------------

    drawValues();
}


// ============================================================
// SETUP
// ============================================================

void setup()
{
    Serial.begin(115200);

    delay(10000);


    // ========================================================
    // I2C
    // ========================================================
    //
    // EXACTLY LIKE YOUR WORKING CODE
    //

    Wire.begin(
        SDA,
        SCL
    );

    delay(100);


    // ========================================================
    // SPI
    // ========================================================
    //
    // EXACTLY LIKE YOUR WORKING CODE
    //

    SPI.begin(
        SCLK,
        MISO,
        MOSI
    );

    delay(1000);


    // ========================================================
    // LOVYANGFX
    // ========================================================

    tft.init();

    // Same rotation as TFT_eSPI version
    tft.setRotation(0);


    // ========================================================
    // DISPLAY
    // ========================================================

    drawHeader();


    // ========================================================
    // CONFIG
    // ========================================================

    printCurrentConfig();


    Serial.println(
        "NORVI RTD MASTER STARTED"
    );
}


// ============================================================
// LOOP
// ============================================================

void loop()
{
    handleSerial();


    if (
        !configMode &&
        millis() - lastChunkRead >=
        READ_INTERVAL
    )
    {
        lastChunkRead = millis();

        readRTD();
    }
}


// ============================================================
// CONFIGURATION
// ============================================================

void enterConfig()
{
    configMode = true;


    Serial.println();
    Serial.println(
        "===== CONFIG MODE ====="
    );


    Serial.println(
        "Enter RTD Type and Channels in one line:"
    );


    Serial.println(
        "Examples: 100,1 for PT100 Ch1, 1000,1-3 for PT1000 Ch1,2,3"
    );


    // Clear serial buffer

    while (Serial.available())
    {
        Serial.read();
    }


    String input = "";


    while (input.length() == 0)
    {
        if (Serial.available())
        {
            input =
                Serial.readStringUntil('\n');

            input.trim();
        }

        delay(10);
    }


    // ========================================================
    // SPLIT TYPE AND CHANNELS
    // ========================================================

    int commaIndex =
        input.indexOf(',');


    if (commaIndex != -1)
    {
        String typeStr =
            input.substring(
                0,
                commaIndex
            );


        String chStr =
            input.substring(
                commaIndex + 1
            );


        // ----------------------------------------------------
        // RTD TYPE
        // ----------------------------------------------------

        int typeInput =
            typeStr.toInt();


        if (typeInput == 100)
        {
            rtdType = 0;
        }
        else
        {
            rtdType = 1;
        }


        // ----------------------------------------------------
        // CHANNELS
        // ----------------------------------------------------

        numChannels = 0;


        int dashIndex =
            chStr.indexOf('-');


        // ----------------------------------------------------
        // RANGE
        // ----------------------------------------------------

        if (dashIndex != -1)
        {
            int startCh =
                chStr.substring(
                    0,
                    dashIndex
                ).toInt();


            int endCh =
                chStr.substring(
                    dashIndex + 1
                ).toInt();


            for (
                int i = startCh;
                i <= endCh &&
                numChannels < 4;
                i++
            )
            {
                if (
                    i >= 1 &&
                    i <= 4
                )
                {
                    channelsToRead[
                        numChannels++
                    ] = i;
                }
            }
        }


        // ----------------------------------------------------
        // INDIVIDUAL CHANNELS
        // ----------------------------------------------------

        else
        {
            int start = 0;


            while (
                start < chStr.length() &&
                numChannels < 4
            )
            {
                int commaPos =
                    chStr.indexOf(
                        ',',
                        start
                    );


                String chPart;


                if (commaPos == -1)
                {
                    chPart =
                        chStr.substring(start);
                }
                else
                {
                    chPart =
                        chStr.substring(
                            start,
                            commaPos
                        );
                }


                int ch =
                    chPart.toInt();


                if (
                    ch >= 1 &&
                    ch <= 4
                )
                {
                    channelsToRead[
                        numChannels++
                    ] = ch;
                }


                if (commaPos == -1)
                {
                    break;
                }


                start =
                    commaPos + 1;
            }
        }
    }


    // ========================================================
    // PRINT CONFIG
    // ========================================================

    Serial.println();
    Serial.println(
        "Configuration Saved:"
    );


    printCurrentConfig();


    configMode = false;
}


// ============================================================
// PRINT CONFIGURATION
// ============================================================

void printCurrentConfig()
{
    Serial.print(
        "RTD Type : "
    );


    Serial.println(
        rtdType == 0
        ? "PT100"
        : "PT1000"
    );


    Serial.print(
        "Channels : "
    );


    for (
        int i = 0;
        i < numChannels;
        i++
    )
    {
        Serial.print(
            channelsToRead[i]
        );


        if (
            i <
            numChannels - 1
        )
        {
            Serial.print(", ");
        }
    }


    Serial.println();
}