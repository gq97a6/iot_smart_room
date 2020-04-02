//EEPROM
#include <Preferences.h>
Preferences preferences;

//Strip
#include <FastLED.h>
CRGB strip[101];
#define FRAMES_PER_SECOND 120
uint8_t gHue = 0;

//MQTT and WiFI
#include <SPI.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <WiFiUdp.h>
String topicStr;
String payloadStr;

//MQTT server details
const char* MQTT_SERVER = "tailor.cloudmqtt.com";
#define MQTT_PORT 11045
#define MQTT_USER "derwsywl"
#define MQTT_PASSWORD "IItmHbbnu9mD"

//WiFi details
const char* ssid = "2.4G-Vectra-WiFi-8F493A";
const char* password = "brygida71";
WiFiClient wifiClient;
PubSubClient client(wifiClient);

//Pins
int heatPin = 14;
int supplyPin = 27;

//State
int bedColor;
int heatMode; //Cold(0), Heat(1), Auto(2), Heat up(3)
float temperature;
float termostat;
bool valveS;
byte anim;

//Alarms
int heatUpFreq = 3 * 60 * 1000;
double heatUpAlarm;

int tempReceivedFreq = 20000;
double tempReceivedAlarm;

//Reconnecting
int wifiRecFreq = 10000;
long wifiRecAlarm;
int mqttRecFreq = 10000;
long mqttRecAlarm;
int wifiRecAtm = 0;
int mqttRecAtm = 0;
int wifiRec = 5; //After wifiRec times, give up reconnecting and restart esp
int mqttRec = 5;

int i; char charArray[8];
void setup()
{
  preferences.begin("memory", false);
  eepromGet();

  FastLED.addLeds<WS2812B,13,GRB>(strip, 101).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(255);

  pinMode(heatPin, OUTPUT);
  pinMode(supplyPin, OUTPUT);
  pinMode(2, OUTPUT);

  valve(0);
  digitalWrite(supplyPin, HIGH);

  Serial.begin(115200);
  Serial.setTimeout(30);

  //MQTT
  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(callback);

  //OTA update and WiFI
  Serial.println("Booting");
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

  ArduinoOTA.begin();
}

void loop()
{
  conErrorHandle();
  
  if (WiFi.status() == WL_CONNECTED)
  {
    ArduinoOTA.handle();
  }

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

  FastLED.delay(1000 / FRAMES_PER_SECOND);
  EVERY_N_MILLISECONDS(20)
  {
    gHue++;
  }
}

void callback(char* topic, byte* payload, unsigned int length)
{
  topicStr = String(topic);
  payloadStr = "";
  for (int i = 0; i < length; i++)
  {
    payloadStr += (char)payload[i];
  }

  if (topicStr == "bedColor")
  {
    payloadStr.toCharArray(charArray, 8);
    bedColor = toIntColor(charArray);
    colorWipe(bedColor, 20, 0, 101);
    
    eepromPut();
  }
  //--------------------------------------------------------------------------------
  else if (topicStr == "temp")
  {
    if (payloadStr.toFloat() > 0 && payloadStr.toFloat() < 40) //Validate
    {
      tempReceivedAlarm = millis() + tempReceivedFreq; //Check if we are getting current temperature
      temperature = payloadStr.toFloat();
    }
    else
    {
      temperature = 100;
    }
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
    eepromPut();
  }
  //--------------------------------------------------------------------------------
  else if (topicStr == "b5")
  {
    if (payloadStr == "1")
    {
      digitalWrite(supplyPin, LOW);
    }
    else if (payloadStr == "0")
    {
      digitalWrite(supplyPin, HIGH);
    }
    eepromPut();
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
    eepromPut();
  }
  else if (topicStr == "bedAnim")
  {
    anim = payloadStr.toInt();
  }
}

void reconnect()
{
  //Create a random client ID
  String clientId = "ESP32Client-";
  clientId += String(random(0xffff), HEX);

  // Attempt to connect
  if (client.connect(clientId.c_str(), MQTT_USER, MQTT_PASSWORD))
  {
    //Subscribe list
    client.subscribe("bedStrip");
    client.subscribe("bedColor");
    client.subscribe("heatControl");
    client.subscribe("termostat");
    client.subscribe("temp");
    client.subscribe("b5");
    client.subscribe("terminal");
    client.subscribe("update");
    client.subscribe("bedAnim");
  }
}

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

//-------------------------------------------------------------------------------- Animacje
// Fire2012 by Mark Kriegsman, July 2012
// as part of "Five Elements" shown here: http://youtu.be/knWiGsmgycY

// This basic one-dimensional 'fire' simulation works roughly as follows:
// There's a underlying array of 'heat' cells, that model the temperature
// at each point along the line.  Every cycle through the simulation,
// four steps are performed:
//  1) All cells cool down a little bit, losing heat to the air
//  2) The heat from each cell drifts 'up' and diffuses a little
//  3) Sometimes randomly new 'sparks' of heat are added at the bottom
//  4) The heat from each cell is rendered as a color into the leds array
//     The heat-to-color mapping uses a black-body radiation approximation.

// Temperature is in arbitrary units from 0 (cold black) to 255 (white hot).
// This simulation scales it self a bit depending on NUM_LEDS; it should look
// "OK" on anywhere from 20 to 100 LEDs without too much tweaking.

// I recommend running this simulation at anywhere from 30-100 frames per second,
// meaning an interframe delay of about 10-35 milliseconds.

// Looks best on a high-density LED setup (60+ pixels/meter).

// There are two main parameters you can play with to control the look and
// feel of your fire: COOLING (used in step 1 above), and SPARKING (used
// in step 3 above).

// COOLING: How much does the air cool as it rises?
// Less cooling = taller flames.  More cooling = shorter flames.
// Default 50, suggested range 20-100
#define COOLING  55

// SPARKING: What chance (out of 255) is there that a new spark will be lit?
// Higher chance = more roaring fire.  Lower chance = more flickery fire.
// Default 120, suggested range 50-200.
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
//--------------------------------------------------------------------------------

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
    //Cold --------------------------------------------------------------------------------
  case 0:
  {
    heatMode = 0;
    valve(0);
    if (pub)
    {
      client.publish("heatControl", "cold");
    }
    break;
    //Heat --------------------------------------------------------------------------------
  case 1:
    heatMode = 1;
    valve(1);
    if (pub)
    {
      client.publish("heatControl", "heat");
    }
    break;
    //Auto --------------------------------------------------------------------------------
  case 2:
    heatMode = 2;
    valve(0);
    if (pub)
    {
      client.publish("heatControl", "auto");
    }
    break;
    //Heat up --------------------------------------------------------------------------------
  case 3:
    heatUpAlarm = millis() + heatUpFreq; //Set alarm to turn off valve
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
    digitalWrite(heatPin , LOW);
    client.publish("valve", "1");
  }
  else if (!pos && valveS)
  {
    valveS = pos;
    digitalWrite(heatPin , HIGH);
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

void conErrorHandle()
{
//Check mqtt connection --------------------------------------------------------------------------------
  if (client.loop())
  {
    mqttRecAtm = 0; //If fine reset reconnection atempts counter
  }
  else if (millis() >= mqttRecAlarm)
  {
    mqttRecAlarm = millis() + mqttRecFreq;
    if (mqttRecAtm >= mqttRec) //After mqttRec tries restart esp.
    {
      eepromPut();
      ESP.restart();
    }
    else //Reconnect
    {
      mqttRecAtm++;
      reconnect();
    }
  }
//Check wifi connection --------------------------------------------------------------------------------
  if (WiFi.status() == WL_CONNECTED)
  {
    wifiRecAtm = 0; //If fine reset reconnection atempts counter
  }
  else if (millis() >= wifiRecAlarm) //Wait for wifiRecAlarm
  {
    wifiRecAlarm = millis() + wifiRecFreq;
    if (wifiRecAtm >= wifiRec) //After wifiRec tries restart esp.
    {
      eepromPut();
      ESP.restart();
    }
    else //Reconnect
    {
      wifiRecAtm++;
      WiFi.begin(ssid, password);
    }
  }
}

void flash()
{
  for (int i = 0; i < 6; i++)
  {
    digitalWrite(2, HIGH);
    delay(200);
    digitalWrite(2, LOW);
    delay(200);
  }
}
