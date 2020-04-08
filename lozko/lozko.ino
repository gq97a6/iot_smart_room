//Variables
#define FRAMES_PER_SECOND 120

#define STRIP_LEN 101
#define STRIP_PIN 13

#define SUPPLY_PIN 27
#define VALVE_PIN 14

#define TEMP_RECEIVED_FREQ 20000
#define HEATUP_FREQ 180000
#define WIFI_RECON_FREQ 30000
#define MQTT_RECON_FREQ 30000

//After WIFI_REC times, give up reconnecting and restart esp
#define WIFI_REC 5
#define MQTT_REC 5

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

//Strip
#include <FastLED.h>
CRGB strip[STRIP_LEN];

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

//Status
char charArray[8];
uint8_t gHue;
int bedColor;
byte anim;
bool b5;
int heatMode; //Cold(0), Heat(1), Auto(2), Heat up(3)
float temperature;
float termostat;
bool valveS;
long tempReceivedAlarm;
long heatUpAlarm;
long wifiReconAlarm;
long mqttReconAlarm;

void setup()
{
  preferences.begin("memory", false);
  eepromGet();

  pinMode(VALVE_PIN, OUTPUT);
  pinMode(SUPPLY_PIN, OUTPUT);
  
  digitalWrite(VALVE_PIN, HIGH);
  digitalWrite(SUPPLY_PIN, HIGH);

  setHeatMode(heatMode, 0);
  
  FastLED.addLeds<WS2812B, STRIP_PIN, GRB>(strip, STRIP_LEN).setCorrection(CRGB(255,255,255));
  FastLED.setBrightness(255);

  //MQTT
  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(mqttCallback);

  //Wifi and OTA update
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

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
  });

  if(udp.listen(54091))
  {
    udp.onPacket([](AsyncUDPPacket packet)
    {
      String adr = "";
      adr += (char)packet.data()[0];
      adr += (char)packet.data()[1];
      adr += (char)packet.data()[2];

      if(adr == "loz" || adr == "glb")
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
}

void loop()
{
  conErrorHandle();
  
  if (WiFi.status() == WL_CONNECTED)
  {
    ArduinoOTA.handle();
  }

  //!!!
  if (heatMode == 2) //Auto
  {
    if (millis() >= tempReceivedAlarm || temperature > termostat - 0.3)
    {
      valve(0);
    }
    else if (temperature < termostat + 0.1)
    {
      valve(1);
    }
  }
  else if (heatMode == 3) //Heat up
  {
    if (millis() >= heatUpAlarm)//Turn off alarm
    {
      setHeatMode(0, 1);
    }
  }

  //Led strip
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
    client.subscribe("bedColor");
    client.subscribe("heatControl");
    client.subscribe("termostat");
    client.subscribe("temp");
    client.subscribe("b5");
    client.subscribe("bedAnim");
  }
}

//-------------------------------------------------------------------------------- Strip functions
void colorWipe(uint32_t color, int wait, int first, int last)
{
  for (int i = first; i < last; i++)
  {
    if(i<101)
    {
      strip[i] = color;
    }
    FastLED.show();
    
    delay(wait);
  }
}

#define COOLING  55
#define SPARKING 120
#define NUM_LEDS  101

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
    for ( int j = 0; j < NUM_LEDS; j++)
    {
      CRGB color = HeatColor(heat[j]);
      strip[j] = color;
    }
    
    FastLED.show();
  }
}

void rainbow()
{
  if (anim == 1)
  {
    fill_rainbow(strip, 101, gHue, 7);
    FastLED.show();    
  }
}

void confetti()
{
  if (anim == 2)
  {
    fadeToBlackBy( strip, 101, 10);
    int pos = random16(101);
    strip[pos] += CHSV( gHue + random8(64), 200, 255);
    
    FastLED.show();
  }
}

void sinelon()
{
  if (anim == 3)
  {
    fadeToBlackBy( strip, 101, 20);
    int pos = beatsin16( 13, 0, 101 - 1 );
    strip[pos] += CHSV( gHue, 255, 192);

    FastLED.show();
  }
}

void bpm()
{
  uint8_t BeatsPerMinute = 62;
  uint8_t beat = beatsin8( BeatsPerMinute, 64, 255);

  if (anim == 4)
  {
    CRGBPalette16 palette = PartyColors_p;
    for ( int i = 0; i < 101; i++) //9948
    {
      strip[i] = ColorFromPalette(palette, gHue + (i * 2), beat - gHue + (i * 10));
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

void setHeatMode(int m, bool pub)
{
  switch (m)
//-------------------------------------------------------------------------------- Cold 
  case 0:
  {
    heatMode = 0;
    valve(0);
    if (pub)
    {
      client.publish("heatControl", "cold");
    }
    break;
//-------------------------------------------------------------------------------- Heat
  case 1:
    heatMode = 1;
    valve(1);
    if (pub)
    {
      client.publish("heatControl", "heat");
    }
    break;
//-------------------------------------------------------------------------------- Auto
  case 2:
    heatMode = 2;
    valve(0);
    if (pub)
    {
      client.publish("heatControl", "auto");
    }
    break;
//-------------------------------------------------------------------------------- Heat up
  case 3:
    heatUpAlarm = millis() + TEMP_RECEIVED_FREQ; //Set alarm to turn off valve
    heatMode = 3;
    valve(1);
    if (pub)
    {
      client.publish("heatControl", "heatup");
    }
    break;
  }
}

void valve(bool pos)
{
  if (pos && !valveS)
  {
    valveS = pos;
    digitalWrite(VALVE_PIN , LOW);
    client.publish("valve", "1");
  }
  else if (!pos && valveS)
  {
    valveS = pos;
    digitalWrite(VALVE_PIN , HIGH);
    client.publish("valve", "0");
  }
}

void eepromPut()
{
  preferences.putDouble("hUpAlr", heatUpAlarm);
  preferences.putUInt("bedClr", bedColor);
  preferences.putUInt("htMd", heatMode);
  preferences.putFloat("termst", termostat);
}

void eepromGet()
{
  heatUpAlarm = preferences.getDouble("hUpAlr", 0);
  bedColor = preferences.getUInt("bedClr", 255);
  heatMode = preferences.getUInt("htMd", 0);
  termostat = preferences.getFloat("termst", 0);
}

void mqttCallback(char* topic, byte* payload, unsigned int length)
{
  String topicStr = String(topic);
  String payloadStr = "";
  
  for (int i = 0; i < length; i++)
  {
    payloadStr += (char)payload[i];
  }

  if (topicStr == "bedColor")
  {
    payloadStr.toCharArray(charArray, 8);
    bedColor = toIntColor(charArray);
    colorWipe(bedColor, 20, 0, 101);
  }
  //--------------------------------------------------------------------------------
  else if (topicStr == "temp")
  {
    
  }
  //--------------------------------------------------------------------------------
  else if (topicStr == "termostat")
  {
    if (payloadStr.toFloat() > 0 && payloadStr.toFloat() < 40) //Validate
    {
      termostat = payloadStr.toFloat();
      setHeatMode(2, 1);
    }
    else
    {
      termostat = 0;
      setHeatMode(0, 1);
    }
  }
  //--------------------------------------------------------------------------------
  else if (topicStr == "b5")
  {
    if (payloadStr == "1")
    {
      digitalWrite(SUPPLY_PIN, LOW);
    }
    else if (payloadStr == "0")
    {
      digitalWrite(SUPPLY_PIN, HIGH);
    }
  }
  //--------------------------------------------------------------------------------
  else if (topicStr == "heatControl")
  {
    if (payloadStr == "cold")
    {
      setHeatMode(0, 0);
    }
    else if (payloadStr == "heat")
    {
      setHeatMode(1, 0);
    }
    else if (payloadStr == "auto")
    {
      setHeatMode(2, 0);
    }
    else if (payloadStr == "heatup")
    {
      setHeatMode(3, 0);
    }
    else
    {
      setHeatMode(0, 0);
    }
  }
  else if (topicStr == "bedAnim")
  {
    anim = payloadStr.toInt();
  }

  eepromPut();
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

  if(parmA == "air") //Temperature, humidity, pressure
  {
    if (parmB.toFloat() > 0 && parmB.toFloat() < 40) //Validate
    {
      tempReceivedAlarm = millis() + TEMP_RECEIVED_FREQ; //Update temperature reset alarm
      temperature = parmB.toFloat();
    }
    else
    {
      temperature = 100;
    }
  }
  else if(parmA == "cwip") //Set one color for whole strip
  {
    for (int i = 0; i < 101; i++)
    {
      strip[i] = parmB.toInt();
    }
    FastLED.show();
  }
  else if(parmA == "shw")
  {
    FastLED.show();
  }
  else if(parmA == "setd") //Set color of one diode
  {
    strip[parmC.toInt()] = parmB.toInt();
    FastLED.show();
  }
  else if(parmA == "term") //Set termostat
  {
    termostat = parmB.toFloat();
    eepromPut();
  }
  else if(parmA == "rst")
  {
    ESP.restart();
  }
  else if(parmA == "anim")
  {
    anim = parmB.toInt();
  }
  else if(parmA == "5") //5V power supply
  {
    if(parmB == "on")
    {
      digitalWrite(SUPPLY_PIN, LOW);
    }
    else if(parmB == "off")
    {
      digitalWrite(SUPPLY_PIN, HIGH);
    }
  }
  else if(parmA == "heat")
  {
    if (parmB == "cold")
    {
      setHeatMode(0, 0);
    }
    else if (parmB == "heat")
    {
      setHeatMode(1, 0);
    }
    else if (parmB == "auto")
    {
      setHeatMode(2, 0);
    }
    else if (parmB == "heatup")
    {
      setHeatMode(3, 0);
    }
    else
    {
      setHeatMode(0, 0);
    }
    eepromPut();
  }
}
