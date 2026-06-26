#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "ESP32-VirtualMatrixPanel-I2S-DMA.h"
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <WiFi.h>
#include <ETH.h>
#include <ArduinoOTA.h>
#include <SPI.h>

WiFiClient ethernetClient;
WiFiServer server(3200);
#define DISPLAY_ENTRY "ENTRY_RD1"
int g_total = 0;
int g_occupied = 0;
String tenantName = "";
unsigned long lastPacketTime = 0;
bool enableScroll = false;
int textWidth = 0;
bool bottomDrawn = false;
int scrollX = 128;
unsigned long lastScroll = 0;
int scrollXTop = 128;
int scrollXBottom = 128;
bool idleScreenShown = false;
unsigned long lastScrollTop = 0;
unsigned long lastScrollBottom = 0;
WiFiClient primaryClient;
MatrixPanel_I2S_DMA *dma_display = nullptr;
#define ETH_CS   14
#define ETH_IRQ  10
#define ETH_RST  9

SPIClass spi(HSPI);

String incomingData = "";

// ===================== W5500 =====================

// =====================
// PANEL CONFIG (YOUR P5)
// =====================
#define PANEL_RES_X 64
#define PANEL_RES_Y 32

#define NUM_ROWS 2
#define NUM_COLS 2

#define SERPENT true
#define TOPDOWN false
#define VIRTUAL_MATRIX_CHAIN_TYPE CHAIN_BOTTOM_RIGHT_UP
// =====================
// YOUR GPIO MAPPING
// =====================
#define R1_PIN 33
#define G1_PIN 34
#define B1_PIN 35
#define R2_PIN 36
#define G2_PIN 37
#define B2_PIN 38

#define A_PIN 39
#define B_PIN 40
#define C_PIN 41
#define D_PIN 42
//#define E_PIN 18

#define LAT_PIN 17
#define OE_PIN  18
#define CLK_PIN 16


class CustomPanel : public VirtualMatrixPanel {
  public:
    using VirtualMatrixPanel::VirtualMatrixPanel;

  protected:
    VirtualCoords getCoords(int16_t x, int16_t y) {

      coords = VirtualMatrixPanel::getCoords(x, y);

      if (coords.x == -1 || coords.y == -1) return coords;

      uint8_t pxbase = panelResX;

      if (panelResY == 32) {
        if ((coords.y & 8) == 0) {
          coords.x += ((coords.x / pxbase) + 1) * pxbase;
        } else {
          coords.x += (coords.x / pxbase) * pxbase;
        }
        coords.y = (coords.y >> 4) * 8 + (coords.y & 0x07);
      }

      return coords;
    }
};
CustomPanel *matrix = nullptr;

void initETH() {

  spi.begin(13, 12, 11, ETH_CS);

  if (!ETH.begin(ETH_PHY_W5500, 1, ETH_CS, ETH_IRQ, ETH_RST, spi)) {

    return;
  }

  IPAddress local_IP(172, 24, 2, 82);
  IPAddress gateway(172, 24, 2, 1);
  IPAddress subnet(255, 255, 255, 0);

  ETH.config(local_IP, gateway, subnet);

  unsigned long start = millis();

  while (!ETH.linkUp()) {
    delay(50);
    yield();

    if (millis() - start > 5000) break;
  }

  server.begin();
}


void drawCenteredText(String text, int y, uint16_t color, const GFXfont* font) {

  int16_t x1, y1;
  uint16_t w, h;

  matrix->setFont(font);

  matrix->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);

  int x = (128 - w) / 2;

  matrix->setCursor(x, y);
  matrix->setTextColor(color);
  matrix->print(text);
  bottomDrawn = false;
}
// =====================
// DISPLAY OBJECTS
// =====================

void scrollTextTop(String text, uint16_t color){
    static int x = 128;

    static int prevX = 128;

    matrix->setFont(&FreeSansBold12pt7b);

    matrix->setTextWrap(false);

    int16_t x1, y1;
    uint16_t w, h;

    matrix->getTextBounds(
        text,
        0,
        0,
        &x1,
        &y1,
        &w,
        &h
    );

    // erase only previous text area
    matrix->fillRect(
        prevX - 2,
        0,
        w + 4,
        32,
        0
    );

    // draw new text
    matrix->setCursor(x, 24);

    matrix->setTextColor(color);

    matrix->print(text);

    prevX = x;

    x--;

    if (x < -((int)w))
    {
        x = 128;
    }
}

void handleEthernet()
{
    if (!ethernetClient || !ethernetClient.connected())
    {
        ethernetClient = server.available();

        return;
    }

    while (ethernetClient.available())
    {
        char c = ethernetClient.read();

        if (c == '$')
        {
            incomingData = "";
        }

        incomingData += c;

        if (incomingData.length() > 300)
        {
            incomingData = "";

            return;
        }

        if (c == '#')
        {
            processData(incomingData);

            incomingData = "";
        }
    }

    if (!ethernetClient.connected())
    {
        ethernetClient.stop();
    }
}
// =====================
// DISPLAY FUNCTIONS
// =====================

void showDenied(String status) {

  matrix->fillScreen(0);

  if (status == "ALREADY_IN") {
      drawCenteredText("ENTRY", 24, matrix->color565(255,0,0), &FreeSans12pt7b);
      // Bottom row
      drawCenteredText("EXISTS", 55, matrix->color565(255,0,0), &FreeSans12pt7b);
    } 
    else if (status == "FULL") {
      drawCenteredText("NO SLOTS", 24, matrix->color565(255,0,0), &FreeSans12pt7b);
      // Bottom row
      drawCenteredText("NO ENTRY", 55, matrix->color565(255,0,255), &FreeSans9pt7b);
    }
    else if (status == "INACTIVE_TAG") {
      drawCenteredText("VEHICLE", 24, matrix->color565(255,0,0), &FreeSans12pt7b);
      // Bottom row
      drawCenteredText("INACTIVE", 55, matrix->color565(255,0,255), &FreeSans9pt7b);
    }
    else if (status == "UNKNOWN") {
      drawCenteredText("VEHICLE", 24, matrix->color565(255,0,0), &FreeSans12pt7b);
      // Bottom row
      drawCenteredText("UNKNOWN", 55, matrix->color565(255,0,0), &FreeSans9pt7b);
    }
}



void showFull(int total, int occupied) {

  matrix->fillScreen(0);

  int vacant = total - occupied;

  // TOP
  drawCenteredText("NO SLOTS", 24, matrix->color565(255,0,0), &FreeSans12pt7b);

  String oPart = "O:" + String(occupied) + " ";
  String vPart = " V:" + String(vacant);

  // measure full width for centering
  String full = oPart + vPart;

  int16_t x1, y1;
  uint16_t w, h;

  matrix->getTextBounds(full, 0, 0, &x1, &y1, &w, &h);

  int x = (128 - w) / 2;

  // start position
  int cursorX = x;

  // ===== DRAW O PART =====
  matrix->setTextColor(matrix->color565(255,255,0)); // Yellow
  matrix->setCursor(cursorX, 55);
  matrix->print(oPart);

  // move cursor forward
  matrix->getTextBounds(oPart, 0, 0, &x1, &y1, &w, &h);
  cursorX += w;

  // ===== DRAW V PART =====
  matrix->setTextColor(matrix->color565(0,255,255)); // Cyan
  matrix->setCursor(cursorX, 55);
  matrix->print(vPart);
 // drawCenteredText(line, 55, matrix->color565(255,255,0));
 bottomDrawn = false;
}

void showTenant(String name, int total, int occupied) {

  matrix->fillScreen(0);

  int vacant = total - occupied;
 
  drawCenteredText(name, 24, matrix->color565(255,255,255), &FreeSans12pt7b);
  matrix->setFont(&FreeSans12pt7b);

  String oPart = "O:" + String(occupied) + " ";
  String vPart = " V:" + String(vacant);

  // measure full width for centering
  String full = oPart + vPart;

  int16_t x1, y1;
  uint16_t w, h;

  matrix->getTextBounds(full, 0, 0, &x1, &y1, &w, &h);

  int x = (128 - w) / 2;

  // start position
  int cursorX = x;

  // ===== DRAW O PART =====
  matrix->setTextColor(matrix->color565(255,255,0)); // Yellow
  matrix->setCursor(cursorX, 55);
  matrix->print(oPart);

  // move cursor forward
  matrix->getTextBounds(oPart, 0, 0, &x1, &y1, &w, &h);
  cursorX += w;

  // ===== DRAW V PART =====
  matrix->setTextColor(matrix->color565(0,255,255)); // Cyan
  matrix->setCursor(cursorX, 55);
  matrix->print(vPart);
 // drawCenteredText(line, 55, matrix->color565(0,255,255));
 bottomDrawn = false;
}

void setupOTA() {

  ArduinoOTA.setHostname("ParkingDisplay_RD2");

  ArduinoOTA.setPassword("twitza123");

  ArduinoOTA.onStart([]() {

    dma_display->clearScreen();
    dma_display->setCursor(10, 30);

    dma_display->setTextColor(
      dma_display->color565(255,0,0)
    );

    dma_display->println("OTA START");
  });

  ArduinoOTA.onEnd([]() {

    //Serial.println("\nOTA End");
  });

  ArduinoOTA.onProgress([](unsigned int progress,
                           unsigned int total) {

   // Serial.printf("OTA Progress: %u%%\r",(progress * 100) / total);
  });

  ArduinoOTA.onError([](ota_error_t error) {

    //Serial.printf("OTA Error[%u]\n", error);
  });

  ArduinoOTA.begin();

 // Serial.println("OTA Ready");
}

void showAllowed(String VehNo) {

  matrix->fillScreen(0);
  //matrix->setFont(&FreeSans12pt7b);

   drawCenteredText("ENTRY", 24, matrix->color565(0,255,0), &FreeSans12pt7b);

  // BOTTOM
  drawCenteredText("ALLOWED", 55, matrix->color565(0,255,0), &FreeSans12pt7b);
  delay(1000);
  matrix->fillRect(0, 32, 128, 32, 0);
  drawCenteredText(VehNo, 55, matrix->color565(0,255,255), &FreeSans9pt7b);
}

void processData(String data){

      if (data.indexOf(',') < 0)
    {
        return;
    }
    lastPacketTime = millis();
    idleScreenShown = false;
    data.replace("$", "");
    data.replace("#", "");


    // Expected Format:
    // tenant,total,occupied,status,vehicle_no,entry_no

    int i1 = data.indexOf(',');
    int i2 = data.indexOf(',', i1 + 1);
    int i3 = data.indexOf(',', i2 + 1);
    int i4 = data.indexOf(',', i3 + 1);
    int i5 = data.indexOf(',', i4 + 1);

    if (i1 < 0 || i2 < 0 || i3 < 0 || i4 < 0 || i5 < 0)
    {
        return;
    }

    String name      = data.substring(0, i1);
    int total        = data.substring(i1 + 1, i2).toInt();
    int occupied     = data.substring(i2 + 1, i3).toInt();
    String status    = data.substring(i3 + 1, i4);
    String veh_no    = data.substring(i4 + 1, i5);
    String entry_no  = data.substring(i5 + 1);

    name.trim();
    status.trim();
    veh_no.trim();
    entry_no.trim();

    
    if (entry_no != DISPLAY_ENTRY)
    {
        return;
    }

        showTenant(name, total, occupied);
        delay(1000);

        if (occupied >= total)
        {
            showFull(total, occupied);
            delay(1000);
        }

        if (status == "ALLOWED")
        {
            showAllowed(veh_no);
            delay(1000);
        }
        else if (status == "ALREADY_IN")
        {
            showDenied(status);
            delay(1000);
        }
        else if (status == "FULL")
        {
            showDenied(status);
            delay(1000);
        }
        else if (status == "INACTIVE_TAG")
        {
            showDenied(status);
            delay(1000);
        }
        else if (status == "UNKNOWN")
        {
            showDenied(status);
            delay(1000);
        }
}

// =====================
// SETUP
// =====================
void setup() {
    btStop();

    WiFi.mode(WIFI_OFF);

    initETH();

  
  HUB75_I2S_CFG mxconfig(
    PANEL_RES_X * 2,   // IMPORTANT (virtual trick)
    PANEL_RES_Y / 2,   // IMPORTANT (virtual trick)
    NUM_ROWS * NUM_COLS
  );

  // Assign your pins
  mxconfig.gpio.r1 = R1_PIN;
  mxconfig.gpio.g1 = G1_PIN;
  mxconfig.gpio.b1 = B1_PIN;
  mxconfig.gpio.r2 = R2_PIN;
  mxconfig.gpio.g2 = G2_PIN;
  mxconfig.gpio.b2 = B2_PIN;

  mxconfig.gpio.a = A_PIN;
  mxconfig.gpio.b = B_PIN;
  mxconfig.gpio.c = C_PIN;
  mxconfig.gpio.d = D_PIN;
 // mxconfig.gpio.e = E_PIN;

  mxconfig.gpio.lat = LAT_PIN;
  mxconfig.gpio.oe  = OE_PIN;
  mxconfig.gpio.clk = CLK_PIN;

  mxconfig.clkphase = false;
 //mxconfig.driver = HUB75_I2S_CFG::SHIFTREG;
  mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_8M;
  //mxconfig.double_buff = true;
  dma_display = new MatrixPanel_I2S_DMA(mxconfig);

  if (!dma_display->begin()) {
    while (1);
  }

  dma_display->setBrightness8(90);
  dma_display->clearScreen();

  // 👉 THIS FIXES YOUR PANEL ISSUE

  matrix = new CustomPanel(
    *dma_display,
    NUM_ROWS,
    NUM_COLS,
    PANEL_RES_X,
    PANEL_RES_Y,
    VIRTUAL_MATRIX_CHAIN_TYPE
  );

   matrix->fillScreen(0);

    drawCenteredText(
        "BOOTING",
        24,
        matrix->color565(0,255,0),
        &FreeSansBold12pt7b
    );
  setupOTA();
}
// =====================
// LOOP TEST
// =====================

void loop()
{
  handleEthernet();
  ArduinoOTA.handle();
  unsigned long start = millis();

  if ((millis() - lastPacketTime > 5000) && !idleScreenShown)
  {
      matrix->fillScreen(0);

      drawCenteredText("ENTRY - 1", 24,
      matrix->color565(255,255,255),
      &FreeSansBold12pt7b);

      drawCenteredText("2W PARKING",55,
      matrix->color565(255,0,255),
      &FreeSansBold9pt7b);

      idleScreenShown = true;
  }
}