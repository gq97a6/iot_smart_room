//EEPROM
#include <Preferences.h>
Preferences preferences;

//Strip
#include <Adafruit_NeoPixel.h>
Adafruit_NeoPixel strip(101, 13, NEO_GRB + NEO_KHZ800);

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

  strip.begin();
  strip.show();
  strip.setBrightness(255);

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
    colorWipe(bedColor, 1, 0, 101);
    
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
  }
}

void colorWipe(uint32_t color, int wait, int first, int last)
{
  for (int i = first; i < last; i++)
  {
    strip.setPixelColor(i, color);
  }
  
  delay(wait);
  strip.show();
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
