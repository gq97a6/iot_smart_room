//Variables
#define FRAMES_PER_SECOND 120
#define BUTTON_SLEEP 100 //Ignore button after change
#define BUTTON_CHECK 10 //Check state every
#define MAX_DCE_TIMERS 20

#define STRIP_LEN_L 78
#define STRIP_LEN_P 78

#define STRIP_PIN_L 5
#define STRIP_PIN_P 18
#define STRIP_PIN_I 13

#define SUPPLY_5_PIN 15
#define SUPPLY_12_PIN 4
#define POTEN_PIN 34

//Alarms
#define UPDATE_FREQ 10000
#define WIFI_RECON_FREQ 30000
#define MQTT_RECON_FREQ 30000

#define MQTT_PORT 11045
#define MQTT_USER "derwsywl"
#define MQTT_PASSWORD "IItmHbbnu9mD"
const char* MQTT_SERVER = "tailor.cloudmqtt.com";

const char* ssid = "2.4G-Vectra-WiFi-8F493A";
const char* password = "brygida71";
IPAddress ipToPing (8, 8, 8, 8); // The remote ip to ping

//Delayed command execution
long DCETimers[MAX_DCE_TIMERS][2]; //When, distance
int DCELoop[MAX_DCE_TIMERS]; //0 == retain, >0 == loop
String DCECommand[MAX_DCE_TIMERS][6]; //Command OR address, cmd, A, B, C

//EEPROM
#include <Preferences.h>
Preferences preferences;

//LED strips
#include <FastLED.h>
CRGB stripL[STRIP_LEN_L];
CRGB stripP[STRIP_LEN_P];
CRGB stripI[3];

//BME280 sensor
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
Adafruit_BME280 bme;

//UDP
#include "WiFi.h"
#include "AsyncUDP.h"
AsyncUDP udp;

//OTA
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

//MQTT and WiFI
#include <SPI.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Ping.h>

WiFiClient wifiClient;
PubSubClient client(wifiClient);

//-------------------------------------------------------------------------------- Status

//History off buttons state up to 10 changes
long buttonsHistoryTimestamp[5][10] = 
  {{0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0}};
  
bool buttonsHistoryState[5][10] =
  {{0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0}};

int buttons[5][4] =
  {{27, 0, 0, 0},
  {26, 0, 0, 0},
  {25, 0, 0, 0},
  {33, 0, 0, 0},
  {32, 0, 0, 0}};
  //Pin, state, ascending, descending (now)
  
char charArray[8];
uint8_t gHue;
long updateAlarm;
long wifiReconAlarm;
long mqttReconAlarm;
long buttonCheckAlarm;
byte anim;
bool c5;
bool c12;
bool topBar;
byte potenPos;
bool valve;

void setup()
{
  Serial.begin(115200);
  
  for(int i=0; i<=4; i++)
  {
    pinMode(buttons[i][0], INPUT_PULLUP);
  }
  
  pinMode(POTEN_PIN, INPUT); //Poten
  pinMode(SUPPLY_5_PIN, OUTPUT); //5VDC supply
  pinMode(SUPPLY_12_PIN, OUTPUT); //12VDC supply

  //Power supply
  digitalWrite(SUPPLY_5_PIN, HIGH);
  digitalWrite(SUPPLY_12_PIN, HIGH);
  
  FastLED.addLeds<WS2812B, STRIP_PIN_L, GRB>(stripL, STRIP_LEN_L).setCorrection(CRGB(255,255,255));
  FastLED.addLeds<WS2812B, STRIP_PIN_P, GRB>(stripP, STRIP_LEN_P).setCorrection(CRGB(255,255,255));
  FastLED.addLeds<WS2812B, STRIP_PIN_I, GRB>(stripI, 3).setCorrection(CRGB(255,179,166));
  FastLED.setBrightness(255);

  //MQTT
  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(mqttCallback);

  //Wifi and OTA update
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  delay(1000);
  if(WiFi.status() != WL_CONNECTED)
  {
    Serial.println("notConnected!");
  }

  ArduinoOTA
  .onStart([]()
  {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";
  })
  .onEnd([]()
  {
    Serial.println("\nEnd");
  })
  .onProgress([](unsigned int progress, unsigned int total)
  {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  })
  .onError([](ota_error_t error)
  {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");

    digitalWrite(2, HIGH);
    delay(3000);
    digitalWrite(2, LOW);

    ESP.restart();
  });

  if(udp.listen(54091))
  {
    udp.onPacket([](AsyncUDPPacket packet)
    {
      String adr = "";
      adr += (char)packet.data()[0];
      adr += (char)packet.data()[1];
      adr += (char)packet.data()[2];

      if(adr == "cen" || adr == "glb")
      {
        String cmd = "";
        for (int i = 3; i < packet.length(); i++)
        {
          cmd += (char)packet.data()[i];
        }
        
        terminal(cmd);
      }
    });
  }
  
  ArduinoOTA.begin();
  bme.begin();
}

void loop()
{
  conErrorHandle(); //OTA and connection maintenance
  buttonsHandle();
  potenHandle();
  DCEHandle();
  
  float temp = bme.readTemperature();
  int humi = bme.readHumidity();
  int pres = bme.readPressure() / 100.0F;

  if(updateAlarm + UPDATE_FREQ < millis())
  {
    updateAlarm = millis();

    sendBrodcast("glb", "air", String(temp), String(humi), String(pres));
    
    String(temp).toCharArray(charArray, 8);
    client.publish("temp", charArray);
    
    String(humi).toCharArray(charArray, 8);
    client.publish("humi", charArray);
    
    String(pres).toCharArray(charArray, 8);
    client.publish("pres", charArray);
  }
  
  //Top bar control
  if(buttons[3][2] && 
  !buttons[0][1] && !buttons[1][1] && !buttons[2][1] && !buttons[4][1])
  {
    if(topBar)
    {
      topBar = 0;
      c12 = 0;
      digitalWrite(SUPPLY_12_PIN, HIGH);
    }
    else
    {
      topBar = 1;
      c12 = 1;
      digitalWrite(SUPPLY_12_PIN, LOW);
    }
  }

  //Fan control //0(gear 0) 1-160(gear 1) 161-175(X) 176-335(gear 2) 336-350(X) 351-511(gear 3)
  if(buttons[2][2] && 
  !buttons[0][1] && !buttons[1][1] && !buttons[3][1] && !buttons[4][1])
  {
    delay(500);
    switch(potenPos)
    {
      case 0:
        client.publish("wenfan;", "0");
        sendBrodcast("wen", "fan", String("0"), String(""), String(""));
        break;
        
      case 1:
        client.publish("wenfan;", "1");
        sendBrodcast("wen", "fan", String("1"), String(""), String(""));
        break;
        
      case 2:
        client.publish("wenfan;", "2");
        sendBrodcast("wen", "fan", String("2"), String(""), String(""));
        break;
        
      case 3:
        client.publish("wenfan;", "3");
        sendBrodcast("wen", "fan", String("3"), String(""), String(""));
        break;
    }
  }
  
  //Heating control
  if(buttons[0][2] && 
  !buttons[1][1] && !buttons[2][1] && !buttons[3][1] && !buttons[4][1])
  {
    delay(500);
    switch(potenPos)
    {
      case 0:
        sendBrodcast("loz", "heat", String("cold"), String(""), String(""));
        
        DCEAdd(100, "infd;#0000FF;0;;", 5, "", "", "", "", "");
        DCEAdd(200, "infd;#000000;0;;", 5, "", "", "", "", "");
        DCEAdd(100, "infd;#0000FF;1;;", 5, "", "", "", "", "");
        DCEAdd(200, "infd;#000000;1;;", 5, "", "", "", "", "");
        DCEAdd(100, "infd;#0000FF;2;;", 5, "", "", "", "", "");
        DCEAdd(200, "infd;#000000;2;;", 5, "", "", "", "", "");
        
        break;
        
      case 1:
        sendBrodcast("loz", "heat", String("auto"), String(""), String(""));
        DCEAdd(500, "infd;#FF0000;0;;", 3, "", "", "", "", "");
        DCEAdd(1000, "infd;#000000;0;;", 3, "", "", "", "", "");
        DCEAdd(1000, "infd;#FF0000;1;;", 3, "", "", "", "", "");
        DCEAdd(1500, "infd;#000000;1;;", 3, "", "", "", "", "");
        DCEAdd(500, "infd;#FF0000;2;;", 3, "", "", "", "", "");
        DCEAdd(1000, "infd;#000000;2;;", 3, "", "", "", "", "");
        
        break;
        
      case 2:
        sendBrodcast("loz", "heat", String("heatup"), String(""), String(""));
        DCEAdd(1000, "infd;#FF0000;0;;", 2, "", "", "", "", "");
        DCEAdd(1100, "infd;#000000;0;;", 2, "", "", "", "", "");
        DCEAdd(1000, "infd;#FF0000;1;;", 2, "", "", "", "", "");
        DCEAdd(1100, "infd;#000000;1;;", 2, "", "", "", "", "");
        DCEAdd(1000, "infd;#FF0000;2;;", 2, "", "", "", "", "");
        DCEAdd(1100, "infd;#000000;2;;", 2, "", "", "", "", "");
        
        break;
        
      case 3:
        sendBrodcast("loz", "heat", String("heat"), String(""), String(""));
        DCEAdd(100, "infd;#FF0000;0;;", 5, "", "", "", "", "");
        DCEAdd(200, "infd;#000000;0;;", 5, "", "", "", "", "");
        DCEAdd(100, "infd;#FF0000;1;;", 5, "", "", "", "", "");
        DCEAdd(200, "infd;#000000;1;;", 5, "", "", "", "", "");
        DCEAdd(100, "infd;#FF0000;2;;", 5, "", "", "", "", "");
        DCEAdd(200, "infd;#000000;2;;", 5, "", "", "", "", "");
        
        break;

      case 4:
        if(valve)
        {
          terminal("infd;#FF0000;0;;");
          terminal("infd;#FF0000;1;;");
          terminal("infd;#FF0000;2;;");
        }
        else
        {
          terminal("infd;#0000FF;0;;");
          terminal("infd;#0000FF;1;;");
          terminal("infd;#0000FF;2;;");
        }
        DCEAdd(1000, "infd;#000000;0;;", 1, "", "", "", "", "");
        DCEAdd(1000, "infd;#000000;1;;", 1, "", "", "", "", "");
        DCEAdd(1000, "infd;#000000;2;;", 1, "", "", "", "", "");
        
        break;
    }
  }

  //Blackout
  if(millis() - buttonsHistoryTimestamp[4][0] > 1000 && buttonsHistoryState[4][0] && 
  !buttons[0][1] && !buttons[1][1] && !buttons[2][1] && !buttons[3][1])
  {
    client.publish("loz5;", "0");
    client.publish("wenfan;", "0");
    
    sendBrodcast("loz", "5", String("off"), String(""), String(""));
    sendBrodcast("wen", "fan", String("0"), String(""), String(""));

    c5 = 0;
    digitalWrite(SUPPLY_12_PIN, HIGH);
      
    topBar = 0;
    c12 = 0;
    digitalWrite(SUPPLY_5_PIN, HIGH);
  }

  if(buttons[1][2] && 
  !buttons[0][1] && !buttons[2][1] && !buttons[3][1] && !buttons[4][1])
  {
    FastLED.show();
    sendBrodcast("loz", "shw", String(""), String(""), String(""));

    DCEAdd(500, "infd;#FF0000;0;;", 5, "", "", "", "", "");
    DCEAdd(1000, "infd;#000000;0;;", 5, "", "", "", "", "");

    DCEAdd(500, "infd;#00FF00;1;;", 5, "", "", "", "", "");
    DCEAdd(1000, "infd;#000000;1;;", 5, "", "", "", "", "");

    DCEAdd(500, "infd;#0000FF;2;;", 5, "", "", "", "", "");
    DCEAdd(1000, "infd;#000000;2;;", 5, "", "", "", "", "");
  }
  
  //Power on
  if(abs(millis() - buttonsHistoryTimestamp[4][1] - 750) <= 500 && buttonsHistoryState[4][1] && buttons[4][3] &&
  !buttons[0][1] && !buttons[1][1] && !buttons[2][1] && !buttons[3][1])
  {
    switch(potenPos)
    {
      case 0:
        c5 = 1;
        digitalWrite(SUPPLY_5_PIN, LOW);
        break;
        
      case 1:
        client.publish("loz5;", "1");
        sendBrodcast("loz", "5", String("on"), String(""), String(""));
        break;
        
      case 2:
        topBar = 1;
        c12 = 1;
        digitalWrite(SUPPLY_12_PIN, LOW);
        break;
        
      case 3:
        c5 = 1;
        digitalWrite(SUPPLY_5_PIN, LOW);
        
        client.publish("loz5;", "1");
        sendBrodcast("loz", "5", String("on"), String(""), String(""));
  
        delay(1000);
        FastLED.show();
        sendBrodcast("loz", "shw", String(""), String(""), String(""));
        break;
    }
  }

  //Power off
  if(abs(millis() - buttonsHistoryTimestamp[4][1] - 200) <= 200 && buttonsHistoryState[4][1] && buttons[4][3] &&
  !buttons[0][1] && !buttons[1][1] && !buttons[2][1] && !buttons[3][1])
  {
    switch(potenPos)
    {
      case 0:
        c5 = 0;
        digitalWrite(SUPPLY_5_PIN, HIGH);
        break;
        
      case 1:
        client.publish("loz5;", "0");
        sendBrodcast("loz", "5", String("off"), String(""), String(""));
        break;
        
      case 2:
        topBar = 0;
        c12 = 0;
        digitalWrite(SUPPLY_12_PIN, HIGH);
        break;
        
      case 3:
        c5 = 1;
        digitalWrite(SUPPLY_5_PIN, LOW);
        
        client.publish("loz5;", "1");
        sendBrodcast("loz", "5", String("on"), String(""), String(""));
  
        delay(1000);
        FastLED.show();
        sendBrodcast("loz", "shw", String(""), String(""), String(""));
        break;
        
      case 4:
        c5 = 0;
        digitalWrite(SUPPLY_5_PIN, HIGH);
        
        client.publish("loz5;", "0");
        sendBrodcast("loz", "5", String("off"), String(""), String(""));
        break;
    }
  }
  
  //Restart
  if(analogRead(POTEN_PIN) == 0 && buttons[0][1] && buttons[1][1] && buttons[2][1] && buttons[3][1] && buttons[4][1])
  {
    ESP.restart();
  }

  //Led strips
  Fire2012();
  rainbow();
  confetti();
  sinelon();
  bpm();

  delay(1000 / FRAMES_PER_SECOND);
  EVERY_N_MILLISECONDS(20)
  {
    gHue++;
  }
}

void conErrorHandle()
{
  if(WiFi.status() == WL_CONNECTED) //No connection with wifi router
  {
    ArduinoOTA.handle();
    
    if(!client.loop()) //No connection with mqtt server
    {
      if (millis() >= mqttReconAlarm)
      {
        mqttReconAlarm = millis() + MQTT_RECON_FREQ;
        
        if(Ping.ping(ipToPing, 1)) //There is internet connection
        {
          mqttReconnect();
        }
      }
    }
  }
  else
  {
    if (millis() >= wifiReconAlarm)
    {
      wifiReconAlarm = millis() + WIFI_RECON_FREQ;

      WiFi.disconnect();
      WiFi.begin(ssid, password);
    }
  }
}

void mqttReconnect()
{
  //Create a random client ID
  String clientId = "ESP32Client-";
  clientId += String(random(0xffff), HEX);

  // Attempt to connect
  if (client.connect(clientId.c_str(), MQTT_USER, MQTT_PASSWORD))
  {
    //Subscribe list
    client.subscribe("cen5;");
    client.subscribe("cen12;");
    client.subscribe("cencwip;");
    client.subscribe("cenanim;");
    client.subscribe("cenbotim;");
    client.subscribe("valve");
  }
}

void sendBrodcast(char* adr, char* cmd, String a, String b, String c)
{
  char toSend[40];
  snprintf(toSend, 40, "%s%s;%s;%s;%s;", adr, cmd, a, b, c);
  udp.broadcastTo(toSend, 54091);
}

void buttonsHandle()
{
  //Clear states
  for (int i = 0; i < 5; i++)
  {
    buttons[i][2] = 0;
    buttons[i][3] = 0;
  }
  
  if(buttonCheckAlarm + BUTTON_CHECK > millis())
  {
    return;
  }
  buttonCheckAlarm = millis();
  
  for (int i = 0; i < 5; i++)
  {
    if(buttonsHistoryTimestamp[i][0] + BUTTON_SLEEP > millis())
    {
      continue;
    }
    
    bool state = !digitalRead(buttons[i][0]);
    if (buttons[i][1] != state)
    {
      if (state)
      {
        buttons[i][2] = 1; //Ascending
      }
      else
      {
        buttons[i][3] = 1; //Descending
      }

      //Shift array
      for(int ii = 9; ii>0; ii--)
      {
        buttonsHistoryTimestamp[i][ii] = buttonsHistoryTimestamp[i][ii-1];
        buttonsHistoryState[i][ii] = buttonsHistoryState[i][ii-1];
      }
  
      buttonsHistoryTimestamp[i][0] = millis();
      buttonsHistoryState[i][0] = state;
    }

    buttons[i][1] = state;
  }
}

void DCEHandle()
{
  for(int i=0; i<MAX_DCE_TIMERS; i++)
  {
    if(millis() >= DCETimers[i][0] && DCETimers[i][0] > 0)
    {
      //Execute command and optional brodcast
      terminal(DCECommand[i][0]);
      if(DCECommand[i][1] != "")
      {
        char adrArray[10];
        char cmdArray[10];
        DCECommand[i][1].toCharArray(adrArray, 10);
        DCECommand[i][2].toCharArray(cmdArray, 10);
        
        sendBrodcast(adrArray, cmdArray, DCECommand[i][3], DCECommand[i][4], DCECommand[i][5]);
      }
      
      if(DCELoop[i] > 1)
      {
        //extend timer
        DCETimers[i][0] = millis() + DCETimers[1][1];
        DCELoop[i] -= 1;
      }
      else if(DCELoop[i] == 1)
      {
        //Close DCE, its last iteration
        DCETimers[i][0] = 0;
        DCECommand[i][0] = DCECommand[i][1] = DCECommand[i][2] = DCECommand[i][3] = DCECommand[i][4] = DCECommand[i][5] = "";
      }
      //Do nothing its infinite
    }
  }
}

void DCEAdd(long timer, String command, int loops, char* adr, char* cmd, char* a, char* b, char* c)
{
  for(int i=0; i<MAX_DCE_TIMERS; i++)
  {
    if(DCETimers[i][0] == 0) //Look for first empty
    {
      //Command
      DCETimers[i][0] = timer + millis();
      DCETimers[i][1] = timer;
      DCECommand[i][0] = command;
      DCELoop[i] = loops;

      if(adr != "")
      {
        //Brodcast
        DCECommand[i][1] = String(adr);
        DCECommand[i][2] = String(cmd);
        DCECommand[i][3] = String(a);
        DCECommand[i][4] = String(b);
        DCECommand[i][5] = String(c);
      }

      return;
    }
  }
}

bool DCEEdit(long timer, String command, int loops, char* adr, char* cmd, char* a, char* b, char* c)
{
  bool edited = 0;
  
  for(int i=0; i<MAX_DCE_TIMERS; i++)
  {
    if(DCECommand[i][0] == command) //Look for desired command and edit
    {
      edited = 1;
      
      //Command
      if(timer > 0)
      {
        DCETimers[i][0] = timer + millis();
        DCETimers[i][1] = timer;
      }
      else if(timer == 0)
      {  
        DCETimers[i][0] = 0;
      }
      
      DCECommand[i][0] = command;
      DCELoop[i] = loops;

      if(adr != "")
      {
        //Brodcast
        DCECommand[i][1] = String(adr);
        DCECommand[i][2] = String(cmd);
        DCECommand[i][3] = String(a);
        DCECommand[i][4] = String(b);
        DCECommand[i][5] = String(c);
      }
    }
  }

  return edited;
}

byte potenHandle()
{
  int poten = analogRead(POTEN_PIN);
  potenPos = -1;
    
  if(poten == 0)
  {
    potenPos = 0;
  }
  else if(poten <= 930)
  {
    potenPos = 1;
  }
  else if(poten <= 2350)
  {
    potenPos = 2;
  }
  else if(poten < 4095)
  {
    potenPos = 3;
  }
  else if(poten == 4095)
  {
    potenPos = 4;
  }
  return potenPos;
}

//-------------------------------------------------------------------------------- Strip functions
void colorWipe(uint32_t color, int wait, int first, int last)
{
  for (int i = first; i <= last; i++)
  {
    stripL[i] = color;
    stripP[i] = color;
    FastLED.show();
    
    delay(wait);
  }
}

#define COOLING  55
#define SPARKING 120
#define NUM_LEDS  156

void Fire2012()
{
  if (anim == 5)
  {
    // Array of temperature readings at each simulation cell
    static byte heat[NUM_LEDS];

    // Step 1. Cool down every cell a little
    for ( int i = 0; i < NUM_LEDS; i++)
    {
      heat[i] = qsub8( heat[i],  random8(0, ((COOLING * 10) / NUM_LEDS) + 2));
    }

    // Step 2. Heat from each cell drifts 'up' and diffuses a little
    for ( int k = NUM_LEDS - 1; k >= 2; k--)
    {
      heat[k] = (heat[k - 1] + heat[k - 2] + heat[k - 2] ) / 3;
    }

    // Step 3. Randomly ignite new 'sparks' of heat near the bottom
    if ( random8() < SPARKING )
    {
      int y = random8(7);
      heat[y] = qadd8( heat[y], random8(160, 255) );
    }

    // Step 4. Map from heat cells to LED colors
    for (int j = 0; j < NUM_LEDS; j++)
    {
      CRGB color = HeatColor(heat[j]);

      if(j < 78)
      {
        stripP[77-j] = color;
      }
      else
      {
        stripL[j-78] = color;
      }
    }
    
    FastLED.show();
  }
}

void rainbow()
{
  if (anim == 1)
  {
    fill_rainbow(stripL, 78, gHue, 7);
    fill_rainbow(stripP, 78, gHue, 7);
    
    FastLED.show();
  }
}

void confetti()
{
  if (anim == 2)
  {
    fadeToBlackBy( stripL, 78, 10);
    fadeToBlackBy( stripP, 78, 10);
    int pos = random16(78);
    stripP[pos] += CHSV( gHue + random8(64), 200, 255);
    stripL[pos] = stripP[pos];
    
    FastLED.show();
  }
}

void sinelon()
{
  if (anim == 3)
  {
    fadeToBlackBy( stripP, 78, 20);
    fadeToBlackBy( stripL, 78, 20);
    
    int pos = beatsin16( 13, 0, 78 - 1 );
    stripP[pos] += CHSV( gHue, 255, 192);
    stripL[pos] = stripP[pos];
    
    FastLED.show();
  }
}

void bpm()
{
  if (anim == 4)
  {
    uint8_t BeatsPerMinute = 62;
    uint8_t beat = beatsin8( BeatsPerMinute, 64, 255);
    
    CRGBPalette16 palette = PartyColors_p;
    for ( int i = 0; i < 78; i++) //9948
    {
      stripP[i] = ColorFromPalette(palette, gHue + (i * 2), beat - gHue + (i * 10));
      stripL[i] = stripP[i];
    }
    
    FastLED.show();
  }
}

int toIntColor(char hexColor[])
{
  int base = 1;
  int color = 0;

  for (int i = 6; i >= 1; i--)
  {
    if (hexColor[i] >= '0' && hexColor[i] <= '9')
    {
      color += (hexColor[i] - 48) * base;
      base = base * 16;
    }
    else if (hexColor[i] >= 'A' && hexColor[i] <= 'F')
    {
      color += (hexColor[i] - 55) * base;
      base = base * 16;
    }
  }

  return color;
}

unsigned int hexToDec(String hexString)
{ 
  unsigned int decValue = 0;
  int nextInt;
  
  for (int i = 0; i < hexString.length(); i++) {
    
    nextInt = int(hexString.charAt(i));
    if (nextInt >= 48 && nextInt <= 57) nextInt = map(nextInt, 48, 57, 0, 9);
    if (nextInt >= 65 && nextInt <= 70) nextInt = map(nextInt, 65, 70, 10, 15);
    if (nextInt >= 97 && nextInt <= 102) nextInt = map(nextInt, 97, 102, 10, 15);
    nextInt = constrain(nextInt, 0, 15);
    
    decValue = (decValue * 16) + nextInt;
  }
  
  return decValue;
}

String decToHex(byte decValue, byte desiredStringLength) {
  
  String hexString = String(decValue, HEX);
  while (hexString.length() < desiredStringLength) hexString = "0" + hexString;
  
  return hexString;
}

//-------------------------------------------------------------------------------- Callbacks

void mqttCallback(char* topic, byte* payload, unsigned int length)
{
  String cmd = "";

  int i = 3;
  while(true)
  {
    cmd += topic[i];
    
    if(topic[i] == ';')
    {
      break;
    }
    
    i++;
  }
  
  for (int i = 0; i < length; i++)
  {
    cmd += (char)payload[i];
  }
  
  cmd += ";;;";
  
  terminal(cmd);
}

void terminal(String command)
{
  String parmA = "";
  String parmB = "";
  String parmC = "";
  String parmD = "";

  //Create array
  char cmd[40];
  command.toCharArray(cmd, 40);

  //Slice array into 4 parameters, sample: prm1;prm2;prm3;prm4;
  int parm = 0;
  for (int i = 0; i < 40; i++)
  {
    if(cmd[i] == ';')
    {
      parm++;
    }
    else
    {
      switch(parm)
      {
        case 0:
          parmA += cmd[i];
          break;

        case 1:
          parmB += cmd[i];
          break;

        case 2:
          parmC += cmd[i];
          break;

        case 3:
          parmD += cmd[i];
          break;
          
      }
    }
  }

  //-------------------------------------------------------------------------------- Set one color for whole strip
  if(parmA == "cwip")
  {
    parmB.toUpperCase();
    parmB.toCharArray(charArray, 8);
    int colorA = toIntColor(charArray);

    parmB = parmB.substring(0,3) + decToHex(hexToDec(parmB.substring(3,5)) * 70/100, 2) + decToHex(hexToDec(parmB.substring(5)) * 65/100, 2);
    parmB.toUpperCase();
    parmB.toCharArray(charArray, 8);
    int colorB = toIntColor(charArray);
    
    for (int i = 0; i < STRIP_LEN_L; i++)
    {
      if(i<2)
      {
        stripL[i] = colorA;
      }
      else
      {
        stripL[i] = colorB;
      }
    }

    for (int i = 0; i < STRIP_LEN_P; i++)
    {
      if(i<6)
      {
        stripP[i] = colorA;
      }
      else
      {
        stripP[i] = colorB;
      }
    }
    
    
    FastLED.show();
    FastLED.show();
  }
  //-------------------------------------------------------------------------------- Set color of one diode
  else if(parmA == "setd")
  {
    parmB.toUpperCase();
    parmB.toCharArray(charArray, 8);
    int colorA = toIntColor(charArray);

    parmB = parmB.substring(0,3) + decToHex(hexToDec(parmB.substring(3,5)) * 70/100, 2) + decToHex(hexToDec(parmB.substring(5)) * 65/100, 2);
    parmB.toUpperCase();
    parmB.toCharArray(charArray, 8);
    int colorB = toIntColor(charArray);

    if(parmC.toInt()<2)
    {
      stripL[parmC.toInt()] = colorA;
      stripP[parmC.toInt()] = colorA;
    }
    else if(parmC.toInt()<6)
    {
      stripL[parmC.toInt()] = colorA;
      stripP[parmC.toInt()] = colorB;
    }
    else
    {
      stripL[parmC.toInt()] = colorB;
      stripP[parmC.toInt()] = colorB;
    }
    
    FastLED.show();
  }
  else if(parmA == "infd")
  {
    parmB.toCharArray(charArray, 8);
    int color = toIntColor(charArray);
    stripI[parmC.toInt()] = color;

    FastLED.show();
  }
  //--------------------------------------------------------------------------------
  else if(parmA == "rst")
  {
    ESP.restart();
  }
  //--------------------------------------------------------------------------------
  else if(parmA == "anim")
  {
    anim = parmB.toInt();
  }
  //-------------------------------------------------------------------------------- 5V power supply
  else if(parmA == "5")
  {
    if(parmB == "1")
    {
      digitalWrite(SUPPLY_5_PIN, LOW);
    }
    else if(parmB == "0")
    {
      digitalWrite(SUPPLY_5_PIN, HIGH);
    }
  }
  //-------------------------------------------------------------------------------- 12V power supply
  else if(parmA == "12")
  {
    if(parmB == "1")
    {
      digitalWrite(SUPPLY_12_PIN, LOW);
    }
    else if(parmB == "0")
    {
      digitalWrite(SUPPLY_12_PIN, HIGH);
    }
  }
  else if(parmA == "bip")
  {
    ledcWrite(0, parmB.toInt());
  }
  else if(parmA == "botim")
  {
    if(parmB.toInt() == 0)
    {
      //Turn off
      DCEEdit(0, "black", 0, "", "", "", "", "");
    }
    else
    {
      long timer = parmB.toInt() * 60 * 1000; //Minutes to milliseconds
      DCEAdd(timer, "black", 1, "wen", "fan", "0", "", "");
      DCEAdd(timer, "black", 1, "loz", "5", "0", "", "");
      DCEAdd(timer, "black", 1, "loz", "anim", "0", "", "");
    }
  }
  else if(parmA == "black")
  {
    terminal("12;0;;;");
    terminal("5;0;;;");
    terminal("anim;0;;;");
  }
  else if(parmA == "vlv")
  {
    valve = parmB.toInt();
  }
}
