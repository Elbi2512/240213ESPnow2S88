// 240505: Ontvangen van 1 client (S88:0). Nog geen versturen van wisselcommando's.
//

#include <Arduino.h>
#include <spi.h>
#if defined(ARDUINO_ARCH_ESP8266)
#include <ESP8266WiFi.h>
#elif defined(ARDUINO_ARCH_ESP32)
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#endif

#include <esp_now.h>
const char *ssid = "YOUR_WIFI_SSID";
const char *password = "YOUR_WIFI_PASSWORD";
bool bOTA = false; // update over the air alleen bij ingedrukte toets bij opstarten van de spanning

#include "SSD1306Wire.h" // legacy: #include "SSD1306.h"

// mac adress: 24:0A:C4:82:9D:BC

#define BOARDID1 1 // eerste ontvangreeks
#define BOARDID2 2

#define BOOTBUTTON 0
#define S88_CLOCK 23           // CLOCK line = HIGH? -> Do something NOW!
#define S88_LOAD 18            // LOAD line = HIGH? -> Master request the data NOW!
#define S88_DATAOUT 19         // DATA line = This line will be used to write out the requested data in sync with the CLOCK pulses when the LOAD line is triggered.
#define NUMBER_OF_S88MODULES 6 // The number of S88 units we want to simulate with the Arduino.
#define RXD2 16
#define TXD2 17
#define SDA 22
#define SCL 21
#define SCREEN_I2C_ADDRESS 0x3C
#define SCREEN_HEIGHT 64
#define SCREEN_WIDTH 128
#define SCREEN_VERTICAL_FLIP true
// Parameters for the scrolling text
#define scroll_delay 40         // set the scroll speed
#define scroll_char_width 9.666 // approximate average character width
#define scroll_y 46
// 51             // row of text line
#define scroll_height 24 // character height
int scroll_pos = SCREEN_WIDTH;
unsigned int next_scroll = 0;
String scroll_string;
const char *scroll_div = "  -  ";
// REPLACE WITH YOUR RECEIVER MAC Address
// uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
// uint8_t broadcastAddress[] = {0x0C, 0xDC, 0x7E, 0x5E, 0x99, 0xD4}; // esp32 met gele pin pockets
uint8_t broadcastAddress[] = {0xA0, 0x20, 0xA6, 0x00, 0x75, 0xA3}; // 8266 SAP F&R plank A0:20:A6:00:75:A3

// uint8_t broadcastAddress1[2][] = {{0xA0, 0x20, 0xA6, 0x00, 0xD3, 0xDA}, {0xA0, 0x20, 0xA6, 0x00, 0x75, 0xA3}};
uint8_t broadcastAddress2[] = {0x0C, 0xDC, 0x7E, 0x5E, 0x89, 0xDC}; // S88 host in Neustad
uint8_t broadcastAddress3[] = {0xA0, 0x20, 0xA6, 0x00, 0xD3, 0xDA};
// Structure example to send data
// Must match the receiver structure
typedef struct struct_message
{
  u8_t ID;
  u8_t OFFSET;
  u8_t AANTAL;
  uint16_t s88Data[NUMBER_OF_S88MODULES];
} struct_message;

bool bRec = false;

// Create a struct_message called myData
struct_message myData;

esp_now_peer_info_t peerInfo;

SSD1306Wire display(0x3c, SDA, SCL);

// Define the Arduino input Pin number for the DCC Signal

volatile uint16_t s88Data[NUMBER_OF_S88MODULES];

volatile boolean LOADTrigger = false;
volatile byte bitIndex = 0;
volatile byte s88DataIndex;

int nIndex = 0;

portMUX_TYPE muxs = portMUX_INITIALIZER_UNLOCKED;

void IRAM_ATTR CLOCK() // If CLOCK rises, check the LOAD line to see if we have to do something!
{
  portENTER_CRITICAL_ISR(&muxs);
  if (digitalRead(S88_LOAD) || LOADTrigger != false) // The LOAD line has been triggered by the command station, he wants the data NOW! Let's give it to him.
  {
    LOADTrigger = true;
    // The LOAD line will not stay high until all the bits are written out,
    // only a pulse occurs every cycle but this function must go on until all the bits are written out.
    if (s88Data[s88DataIndex] & (1 << bitIndex)) // Write every bit out.
      digitalWrite(S88_DATAOUT, HIGH);
    else
      digitalWrite(S88_DATAOUT, LOW);
    bitIndex++; // Next bit
  }
  if (bitIndex > 15) // 16 bits passed? Reset the counter...
  {
    s88DataIndex++;
    bitIndex = 0;
    if (s88DataIndex > NUMBER_OF_S88MODULES - 1)
    // Human counting to array counting conversion.
    // We want to simulate 5 S88 modules, so we give the variable NUMBER_OF_S88MODULES a value of 5.
    // However, the array starts always with a 0.
    {
      s88DataIndex = 0;    // Reset the s88DataIndex counter, this counter tracks the number of 16bit shift register 4014.
      LOADTrigger = false; // Everything is written out so we can stop writing data out.
    }
  }
  portEXIT_CRITICAL_ISR(&muxs);
}

// callback when data is sent
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status)
{
  char macStr[18];
  Serial.print("Packet from: ");
  // Copies the sender mac address to a string
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  Serial.print(macStr);
  Serial.print(" send status:\t");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

bool getbit(uint16_t welke, int bit)
{
  // Serial.printf("Get Bit: from: %d, Bit: %d, Outcome:%d\r\n", welke, bit,welke & (1 << bit) );
  return welke & (1 << bit);
}
/*
int sentdata()
{
  int nResult = 0;

  esp_err_t result = esp_now_send(0, (uint8_t *)&myData, sizeof(myData));
  if (result == ESP_OK)
  {
    Serial.println("Sent with success");
  }
  else
  {
    Serial.println("Error sending the data");
    nResult = 1;
  }

  return nResult;
}

*/
// callback function that will be executed when data is received
void OnDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len)
{
  char macStr[18];
  Serial.print("Packet received from: ");
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  Serial.println(macStr);
  bRec = true;
  Serial.print("Bytes received: ");
  Serial.println(len);
  memcpy(&myData, incomingData, sizeof(myData));
  u8_t offset = myData.OFFSET;
  u8_t aantal = myData.AANTAL;

  if (myData.ID == BOARDID1)
  {
    Serial.printf("aantal: %d, myData.ID: %d, offset: %d\r\n", myData.AANTAL, myData.ID, myData.OFFSET);
    for (int i = 0; i < aantal; i++)
    {
      s88Data[offset + i] = myData.s88Data[i];
      Serial.printf("sData[%d]: %X, offset:%d\r\n", i, s88Data[i + offset], i + offset);
    }
  }

  // bij een volgend ID hier eventueel meer koppelen

  if (myData.ID == BOARDID2)
  {
    Serial.printf("nAantal%d, aantal:%d, myData.ID: %d, offset: %d\r\n", aantal, myData.AANTAL, myData.ID, myData.OFFSET);
    for (int i = 0; i < aantal; i++)
    {
      s88Data[offset + i] = myData.s88Data[i];
      Serial.printf("myData[%d]: %X\r\n", i, myData.s88Data[i]);
      Serial.printf("sData[%d]: %X, offset:%d\r\n", i, s88Data[i + offset], i + offset);
    }
  }
}

int sentdata()
{
  int nResult = 0;
  myData.ID = BOARDID1;
  myData.OFFSET = 0;
  myData.AANTAL = NUMBER_OF_S88MODULES;
  uint8_t result = esp_now_send(broadcastAddress, (uint8_t *)&myData, sizeof(myData));
  if (result != 0)
  {
    Serial.println("Error sending the data");
    nResult = 1;
  }
  return nResult;
}

void setup()
{
  Serial.begin(115200);
  Serial.printf("\r\nReboot\r\n");
  for (int i = 0; i < 5; i++)
  {
    delay(1000);
    Serial.println("press OTA key");
  }
  delay(2000);
  display.init();
  display.flipScreenVertically();
  display.clear(); // Note the format for setting a serial port is as follows: Serial2.begin(baud-rate, protocol, RX pin, TX pin);
  pinMode(S88_CLOCK, INPUT);
  pinMode(S88_LOAD, INPUT);
  pinMode(S88_DATAOUT, OUTPUT);
  digitalWrite(S88_DATAOUT, HIGH);
  if (!digitalRead(BOOTBUTTON))
  {
    bOTA = true;
    Serial.print("*");
    while (!digitalRead(BOOTBUTTON))
    {
      Serial.print("-");
      digitalWrite(LED_BUILTIN, true); // Turn the LED off by making the voltage HIGH
      delay(250);
      digitalWrite(LED_BUILTIN, false); // Turn the LED off by making the voltage HIGH
      delay(250);
    }
    Serial.println();
  }
  if (bOTA == true)
  {
    WiFi.begin(ssid, password);
    while (WiFi.waitForConnectResult() != WL_CONNECTED)
    {
      Serial.println("Connection Failed! Rebooting...");
      delay(5000);
      ESP.restart();
    }
    ArduinoOTA.setPassword("CHANGE_THIS_OTA_PASSWORD");
    ArduinoOTA
        .onStart([]()
                 {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type); })
        .onEnd([]()
               { Serial.println("\nEnd"); })
        .onProgress([](unsigned int progress, unsigned int total)
                    { Serial.printf("Progress: %u%%\r", (progress / (total / 100))); })
        .onError([](ota_error_t error)
                 {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed"); });

    ArduinoOTA.begin();

    Serial.println("Ready");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    display.drawString(1, 0, "Booted");
    char tekst[30];
    printf(tekst, "%s", WiFi.localIP());
    display.drawString(2, 2, tekst);
    display.display();
  }
  else
  {
    // No authentication by default

    // Do the reroute of the IRQ address preferably at the end of the setup, to prevent spontanious reboots..

    attachInterrupt(digitalPinToInterrupt(S88_CLOCK), CLOCK, RISING); // Activate CLOCK interrupt on EVERY rising pulse of the CLOCK line.
    WiFi.mode(WIFI_MODE_STA);
    Serial.println(WiFi.macAddress());
    WiFi.disconnect();

    // Init ESP-NOW
    if (esp_now_init() != ESP_OK)
    {
      Serial.println("Error initializing ESP-NOW");
      return;
    }
    // Once ESPNow is successfully Init, we will register for Send CB to
    // get the status of Trasnmitted packet
    esp_now_register_send_cb(OnDataSent);

    // Register peer
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    // Add peer
    if (esp_now_add_peer(&peerInfo) != ESP_OK)
    {
      Serial.println("Failed to add peer");
      return;
    }

    // register second peer
    memcpy(peerInfo.peer_addr, broadcastAddress2, 6);
    if (esp_now_add_peer(&peerInfo) != ESP_OK)
    {
      Serial.println("Failed to add peer");
      return;
    }
    // Register for a callback function that will be called when data is received
    esp_now_register_recv_cb(OnDataRecv);
    display.drawString(1, 0, "Booted");
    display.display();
    Serial.println("Init Done");
  }
}

void loop()
{
  if (bOTA == true)
  {
    ArduinoOTA.handle();
  }
  else
  {
    delay(1);
    if (!digitalRead(BOOTBUTTON) || bRec == true)
    {
      display.clear();
      for (int j = 0; j < 60; j += 15)
      {
        // int j = 0;
        int offset = -1;
        for (int i = 1; i <= 112; i += 7)
        {
          if (getbit(s88Data[j / 15], i / 7))
            display.drawString(i + offset, j, "1");
          else
            display.drawString(i + offset, j, "0");

          if (i % 28 == 22)
            offset += 3;
          if (i == 50)
            offset += 3;
        }
      } // hier lusje van maken als er meerdere S88 modelues mogen zijn
      bRec = false;
      display.display();
      while (!digitalRead(BOOTBUTTON))
      {
        for (int i=0; i<NUMBER_OF_S88MODULES; i++) {
          Serial.printf("S88[%d]: %04X, ", i, s88Data[i]);
        }
        Serial.println();
        yield();
      }
    }
    else
    {
      delay(500);
      Serial.print("*");
      //  sentdata();
    }
  }
}