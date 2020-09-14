//-------------------------------------------------------------------------------- Variables
#include "constants.h"
#define ADDRESS "loz"

#define STRIP_LEN 101
#define STRIP_PIN 13

#define SUPPLY_PIN 27
#define VALVE_PIN 14

#define TEMP_RECEIVED_FREQ 20000
#define HEATUP_FREQ 180000

//-------------------------------------------------------------------------------- Libraries
//Wifi
#include <WiFi.h>

//OTA
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

//UDP
#include "AsyncUDP.h"

//MQTT
#include <PubSubClient.h>

//EEPROM
#include <Preferences.h>

//LED strip
#include <FastLED.h>

//-------------------------------------------------------------------------------- Clients
WiFiClient wifiClient;
PubSubClient client(wifiClient);
AsyncUDP udp;
CRGB strip[STRIP_LEN];
Preferences preferences;

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

  terminal("heat;" + String(heatMode));
  
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

  if (udp.listen(UDP_PORT))
  {
    udp.onPacket([](AsyncUDPPacket packet)
    {
      //Extract adress
      String adr = "";
      for (int i = 0; i < packet.length(); i++)
      {
        adr += (char)packet.data()[i];
        if((char)packet.data()[i+1] == ';')
        {
          break;
        }
      }

      //Execute command
      if (adr == ADDRESS || adr == "glb")
      {
        String cmd = "";
        for (int i = 4; i < packet.length(); i++)
        {
          cmd += (char)packet.data()[i];
        }
        terminal(cmd);
      }
    });
  }
  
  ArduinoOTA.begin();
}

void loop()
{
  conErrorHandle();

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
  if(WiFi.status() == WL_CONNECTED)
  {
    ArduinoOTA.handle();
    
    if(!client.loop()) //No connection with mqtt server
    {
      if (millis() >= mqttReconAlarm)
      {
        mqttReconAlarm = millis() + MQTT_RECON_FREQ;
        mqttReconnect();
      }
    }
  }
  else //No connection with wifi router
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
  String clientId = "ESP32#";
  clientId += ADDRESS;

  // Attempt to connect
  if (client.connect(clientId.c_str(), MQTT_USER, MQTT_PASSWORD))
  {
    //Subscribe list
    client.subscribe("#"); 
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

//-------------------------------------------------------------------------------- Callbacks
void mqttCallback(char* topic, byte* payload, unsigned int length)
{
  //Get payload
  String payloadS;
  for (int i = 0; i < length; i++)
  {
    payloadS += (char)payload[i];
  }
      
  if (String(topic) == "terminal") //Terminal input, payload is a command
  {
    String adr = String(payloadS).substring(0, String(payloadS).indexOf(';'));
    if(adr == ADDRESS || adr == "glb")
    {
      terminal(String(payloadS).substring(String(payloadS).indexOf(';') + 1));
    }
  }
  else //Standard input, topic is a command, payload is a variable
  {
    String adr = String(topic).substring(0, String(topic).indexOf(';'));
    if(adr == ADDRESS || adr == "glb")
    {
      terminal(String(topic).substring(String(topic).indexOf(';') + 1) + ";" + payloadS);
    }
  }
}

void terminal(String command)
{
  //Create arrays
  String cmd[MAXT_ELEMENTS];
  char cmdChar[MAXT_CMD];
  command.toCharArray(cmdChar, MAXT_CMD);

  //Slice array into parameters
  int parm = 0;
  for (int i = 0; i < MAXT_CMD; i++)
  {
    if (cmdChar[i] == ';')
    {
      parm++;
    }
    else if(cmdChar[i] != 0)
    {
      cmd[parm] += cmdChar[i];
    }
    else
    {
      break;
    }
  }

  if(cmd[0] == "air") //Temperature, humidity, pressure
  {
    if (cmd[1].toFloat() > 0 && cmd[1].toFloat() < 40) //Validate
    {
      tempReceivedAlarm = millis() + TEMP_RECEIVED_FREQ; //Update temperature reset alarm
      temperature = cmd[1].toFloat();
    }
    else
    {
      temperature = 100;
    }
  }
  else if(cmd[0] == "cwip") //Set one color for whole strip
  {
    cmd[1].toCharArray(charArray, 8);
    int color = toIntColor(charArray);
    
    for (int i = 0; i < STRIP_LEN; i++)
    {
      strip[i] = color;
    }
    
    FastLED.show();
  }
  else if(cmd[0] == "shw")
  {
    FastLED.show();
  }
  else if(cmd[0] == "setd") //Set color of one diode
  {
    cmd[1].toCharArray(charArray, 8);
    strip[cmd[2].toInt()] = toIntColor(charArray);
  }
  else if(cmd[0] == "term") //Set termostat
  {
    termostat = cmd[1].toFloat();
    eepromPut();
  }
  else if(cmd[0] == "reset")
  {
    ESP.restart();
  }
  else if(cmd[0] == "anim")
  {
    anim = cmd[1].toInt();
  }
  else if(cmd[0] == "5") //5V power supply
  {
    if(cmd[1] == "1")
    {
      digitalWrite(SUPPLY_PIN, LOW);
    }
    else if(cmd[1] == "0")
    {
      digitalWrite(SUPPLY_PIN, HIGH);
    }
  }
  else if(cmd[0] == "heat")
  {
    switch (cmd[1].toInt())
    {
      case 0: //Cold
        heatMode = 0; terminal("valve;0"); break;
        
      case 1: //Heat
        heatMode = 1; terminal("valve;1"); break;
        
      case 4: //Toggle
        if(heatMode == 0)
        {
          terminal("heat;1");
        }
        else
        {
          terminal("heat;0");
        }
        break;
    }
    
    eepromPut();
  }
  else if(cmd[0] == "valve")
  {
    bool pos = cmd[1].toInt();
    if (pos && !valveS)
    {
      valveS = pos;
      digitalWrite(VALVE_PIN , LOW);
      client.publish("valve", "1");
      terminal("sendBrodcast;glb;vlv;1");
    }
    else if (!pos && valveS)
    {
      valveS = pos;
      digitalWrite(VALVE_PIN , HIGH);
      client.publish("valve", "0");
      terminal("sendBrodcast;glb;vlv;0");
    }
  }
  else if (cmd[0] == "sendBrodcast") //address, command, A, B, C...
  {
    String toSend = cmd[1] + ';' + cmd[2];//Address and command

    //Parameters
    for(int i = 3; i<MAXT_CMD; i++)
    {
      if (cmd[i] != "")
      {
        toSend += ';';
        toSend += cmd[i];
      }
      else
      {
        break;
      }
    }

    char toSendA[MAXT_CMD];
    toSend.toCharArray(toSendA, MAXT_CMD);
    udp.broadcastTo(toSendA, UDP_PORT);
  }
}
