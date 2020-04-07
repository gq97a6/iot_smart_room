//Variables
#define FRAMES_PER_SECOND 120
#define BUTTON_SLEEP 50 //Ignore button state after change

#define STRIP_LEN_L 78
#define STRIP_LEN_P 78

#define STRIP_PIN_L 18
#define STRIP_PIN_P 5

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

//EEPROM
#include <Preferences.h>
Preferences preferences;

//LED strips
#include <FastLED.h>
CRGB stripL[STRIP_LEN_L];
CRGB stripP[STRIP_LEN_P];

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
byte anim;
bool c5;
bool c12;
bool topBar;

void setup()
{
  Serial.begin(115200);
  
  for(int i=0; i<=4; i++)
  {
    pinMode(buttons[i][0], INPUT_PULLUP);
  }
  
  pinMode(POTEN_PIN, INPUT); //Poten
  analogReadResolution(9);
  
  pinMode(SUPPLY_5_PIN, OUTPUT); //5VDC supply
  pinMode(SUPPLY_12_PIN, OUTPUT); //12VDC supply
  
  digitalWrite(15, HIGH);
  digitalWrite(4, HIGH);
  
  FastLED.addLeds<WS2812B, STRIP_PIN_L, GRB>(stripL, STRIP_LEN_L).setCorrection(TypicalLEDStrip);
  FastLED.addLeds<WS2812B, STRIP_PIN_P, GRB>(stripP, STRIP_LEN_P).setCorrection(TypicalLEDStrip);
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
        udpCallback(cmd);
      }
    });
  }
  
  ArduinoOTA.begin();
  bme.begin();
}

void loop()
{
  conErrorHandle(); //OTA and connection maintenance

  float temp = bme.readTemperature();
  int humi = bme.readHumidity();
  int pres = bme.readPressure() / 100.0F;

  if (millis() >= updateAlarm)
  {
    updateAlarm = millis() + UPDATE_FREQ;

    sendBrodcast("glb", "air", String(temp), String(humi), String(pres));
    
    String(temp).toCharArray(charArray, 8);
    client.publish("temp", charArray);
    
    String(humi).toCharArray(charArray, 8);
    client.publish("humi", charArray);
    
    String(pres).toCharArray(charArray, 8);
    client.publish("pres", charArray);
  }
  
  buttonsHandle();
  
  //Top bar control
  if(buttons[3][2] && !buttons[0][1] && !buttons[1][1] && !buttons[2][1] && !buttons[4][1])
  {
    if(topBar)
    {
      topBar = 0;
      bool c12 = 0;
      digitalWrite(4, HIGH);
      client.publish("c12", "0");
    }
    else
    {
      topBar = 1;
      bool c12 = 1;
      digitalWrite(4, LOW);
      client.publish("c12", "1");
    }

    delay(100);
  }

  //Fan control //0(0) 1-160(1) 161-175(X) 176-335(2) 336-350(X) 351-511(3)
  if(buttons[2][2] && !buttons[0][1] && !buttons[1][1] && !buttons[3][1] && !buttons[4][1])
  {
    int poten = analogRead(34);
    if(poten == 0)
    {
      client.publish("fan", "0");
      sendBrodcast("wen", "gear", String("0"), String(""), String(""));
    }
    else if(poten >= 1 && poten <= 160)
    {
      client.publish("fan", "1");
      sendBrodcast("wen", "gear", String("1"), String(""), String(""));
    }
    else if(poten >= 176 && poten <= 335)
    {
      client.publish("fan", "2");
      sendBrodcast("wen", "gear", String("2"), String(""), String(""));
    }
    else if(poten >= 351 && poten <= 511)
    {
      client.publish("fan", "3");
      sendBrodcast("wen", "gear", String("3"), String(""), String(""));
    }
  }
  
  //Heating control
  if(buttons[0][2] && !buttons[1][1] && !buttons[2][1] && !buttons[3][1] && !buttons[4][1])
  {
    int poten = analogRead(34);
    if(poten == 0)
    {
      client.publish("heatControl", "cold");
      sendBrodcast("wen", "heat", String("cold"), String(""), String(""));
    }
    else if(poten >= 1 && poten <= 160)
    {
      client.publish("heatControl", "auto");
      sendBrodcast("wen", "heat", String("auto"), String(""), String(""));
    }
    else if(poten >= 176 && poten <= 335)
    {
      client.publish("heatControl", "heatup");
      sendBrodcast("wen", "heat", String("heatup"), String(""), String(""));
    }
    else if(poten >= 351 && poten <= 511)
    {
      client.publish("heatControl", "heat");
      sendBrodcast("wen", "heat", String("heat"), String(""), String(""));
    }
  }

  //Restart
  if(analogRead(34) == 0 && buttons[0][1] && buttons[1][1] && buttons[2][1] && buttons[3][1] && buttons[4][1])
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

  Serial.println(buttons[4][2]);
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
    client.subscribe("topBar");
    client.subscribe("deskColor");
    client.subscribe("deskAnim");
    client.subscribe("c12");
    client.subscribe("c5");
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
  for (int i = 0; i < 5; i++)
  {
    //Clear states
    buttons[i][2] = 0;
    buttons[i][3] = 0;
    
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
//-------------------------------------------------------------------------------- Callbacks

void mqttCallback(char* topic, byte* payload, unsigned int length)
{
  String topicStr = String(topic);
  String payloadStr = "";
  
  for (int i = 0; i < length; i++)
  {
    payloadStr += (char)payload[i];
  }
  
//--------------------------------------------------------------------------------
  if (topicStr == "c12")
  {
    if (payloadStr == "1")
    {
      c12 = 1;
      digitalWrite(4, LOW);
    }
    else if (payloadStr == "0")
    {
      c12 = 0;
      digitalWrite(4, HIGH);
    }
  }
//--------------------------------------------------------------------------------
  else if (topicStr == "c5")
  {
    if (payloadStr == "1")
    {
      c5 = 1;
      digitalWrite(15, LOW);
    }
    else if (payloadStr == "0")
    {
      c5 = 0;
      digitalWrite(15, HIGH);
    }
  }
//--------------------------------------------------------------------------------
  else if (topicStr == "topBar")
  {
    if (payloadStr == "1")
    {
      topBar = 1;
      c12 = 1;
      client.publish("c12", "1");
      digitalWrite(4, LOW);
    }
    else if (payloadStr == "0")
    {
      topBar = 0;
      c12 = 0;
      client.publish("c12", "0");
      digitalWrite(4, HIGH);
    }
  }
//--------------------------------------------------------------------------------
  else if (topicStr == "deskColor")
  {
    payloadStr.toCharArray(charArray, 8);
    colorWipe(toIntColor(charArray), 20, 0, 77);
  }
  else if (topicStr == "deskAnim")
  {
    anim = payloadStr.toInt();
  }
}

void udpCallback(String command)
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
    for (int i = 0; i <= 77; i++)
    {
      stripL[i] = parmB.toInt();
      stripP[i] = parmB.toInt();
    }
    FastLED.show();
  }
  //-------------------------------------------------------------------------------- Set color of one diode
  else if(parmA == "setd")
  {
    stripL[parmC.toInt()] = parmB.toInt();
    stripP[parmC.toInt()] = parmB.toInt();
    
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
    byte anim = parmB.toInt();
  }
  //-------------------------------------------------------------------------------- 5V power supply
  else if(parmA == "5")
  {
    if(parmB == "on")
    {
      digitalWrite(15, LOW);
    }
    else if(parmB == "off")
    {
      digitalWrite(15, HIGH);
    }
  }
  //-------------------------------------------------------------------------------- 12V power supply
  else if(parmA == "12")
  {
    if(parmB == "on")
    {
      digitalWrite(4, LOW);
    }
    else if(parmB == "off")
    {
      digitalWrite(4, HIGH);
    }
  }
}
