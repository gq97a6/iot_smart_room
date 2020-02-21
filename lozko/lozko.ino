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
int i;
bool flip;
long bedColor;
byte currentHeatMode = 0; //Cold(0), Heat(1), Auto(2), Heat up(3)
byte previousHeatMode = 0;
float temperature = 100;
float termostat = 0;

//Alarms
int heatUpFreq = 3 * 60 * 1000;
long heatUpAlarm;

int heatFreq = 45 * 60 * 1000; //How long to wait before turing on heating
long heatAlarm;

int coldFreq = 4 * 60 * 1000; //How long to wait before turing off heating
long coldAlarm;

int tempReceivedFreq = 20000;
long tempReceivedAlarm;

//Reconnecting
int wifiRecFreq = 10000;
long wifiRecAlarm;
int mqttRecFreq = 10000;
long mqttRecAlarm;
int wifiRecAtm = 0;
int mqttRecAtm = 0;
int wifiRec = 5; //After wifiRec times, give up reconnecting and restart esp
int mqttRec = 5;

void setup()
{
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
  while (WiFi.waitForConnectResult() != WL_CONNECTED)
  {
    Serial.println("Connection Failed! Rebooting...");
    digitalWrite(2, HIGH);
    delay(3000);
    digitalWrite(2, LOW);

    ESP.restart();
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

  ArduinoOTA.begin();

  flash();
}

void loop()
{
  conErrorHandle();

  if(WiFi.status() == WL_CONNECTED)
  {
    ArduinoOTA.handle();
  }
  
  if(currentHeatMode == 2) //Auto
  {
    if(millis() >= tempReceivedAlarm)
    {
      setHeatMode(0);
    }
    
    if(temperature > termostat)
    {
      valve(0);
    }
    else if(temperature < termostat - 0.3)
    {
      valve(1);
    }
//OLD --------------------------------------------------------------------------------
//    if(flip && millis() >= heatAlarm) //Wait for alarm to turn on heating
//    {
//      flip = 0;
//      valve(1);
//      coldAlarm = millis() + coldFreq; //Set alarm for turning off heating
//    }
//    else if(!flip && millis() >= coldAlarm) //Wait for alarm to turn off heating
//    {
//      flip = 1;
//      valve(0);
//      heatAlarm = millis() + heatFreq; //Set alarm for turning on heating
//    }
  }
  else if(currentHeatMode == 3) //Heat up
  {
    
    if (millis() >= heatUpAlarm)//Turn off alarm
    {
      valve(0);

      if(previousHeatMode == 2)
      {
        setHeatMode(2);
      }
      else
      {
        setHeatMode(0);
      }
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
    bedColor = payloadStr.toInt();
    colorWipe(bedColor, 1, 0, 101);
  }
//--------------------------------------------------------------------------------
  else if (topicStr == "temp")
  {
    if(payloadStr.toFloat() > 0 && payloadStr.toFloat() < 40) //Validate
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
    if(payloadStr.toFloat() > 0 && payloadStr.toFloat() < 40) //Validate
    {
      termostat = payloadStr.toFloat();
      setHeatMode(2);
    }
    else
    {
      termostat = 0;
      setHeatMode(0);
    }
  }
//--------------------------------------------------------------------------------
  else if (topicStr == "5b")
  {
    if (payloadStr == "on")
    {
      digitalWrite(supplyPin, LOW);
    }
    else if (payloadStr == "off")
    {
      digitalWrite(supplyPin, HIGH);
    }
  }
//--------------------------------------------------------------------------------
  else if (topicStr == "heatControl")
  {
    if (payloadStr == "cold")
    {
      setHeatMode(0);
    }
    else if (payloadStr == "heat")
    {
      setHeatMode(1);
    }
    else if (payloadStr == "auto")
    {
      setHeatMode(2);
    }
    else if (payloadStr == "heatup")
    {
      setHeatMode(3);
    }
    else
    {
      setHeatMode(0);
    }
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
    client.subscribe("bedSwitch");
    client.subscribe("bedColor");
    client.subscribe("heatButton");
    client.subscribe("heatControl");
    client.subscribe("termostat");
    client.subscribe("temp");
    client.subscribe("5b");
    client.subscribe("terminal");
  }
}

void colorWipe(uint32_t color, int wait, int first, int last)
{
  for (int i = first; i < last; i++)
  {    
    strip.setPixelColor(i, color);
    strip.show();
    delay(wait);
  }
}

void setHeatMode(int m)
{
  switch(m)
//Cold --------------------------------------------------------------------------------
    case 0:
    {
      previousHeatMode = currentHeatMode;
      currentHeatMode = 0;
      valve(0);
      break;
//Heat --------------------------------------------------------------------------------      
    case 1:
      previousHeatMode = currentHeatMode;
      currentHeatMode = 1;
      valve(1);
      break;
//Auto --------------------------------------------------------------------------------
    case 2:
      if(!previousHeatMode)//If changing back from cold mode
      {
        flip = 1; //Start by on cycle
        coldAlarm = millis() + coldFreq; //Set alarm for turning off heating
        previousHeatMode = currentHeatMode;
        currentHeatMode = 2;
        valve(1);
      }
      else if(previousHeatMode || previousHeatMode == 3) //If changing back from heat/heatup mode 
      {
        flip = 0; //Start by off cycle
        heatAlarm = millis() + heatFreq; //Set alarm for turning on heating
        previousHeatMode = currentHeatMode;
        currentHeatMode = 2;
        valve(0);
      }
      break;
//Heat up --------------------------------------------------------------------------------
    case 3:
      heatUpAlarm = millis() + heatUpFreq; //Set alarm to turn off valve
      previousHeatMode = currentHeatMode;
      currentHeatMode = 3;
      valve(1);
      break;
  }
}

void valve(bool pos)
{
  if(pos)
  {
    digitalWrite(heatPin , LOW);
    client.publish("valve", "1");
  }
  else
  {
    digitalWrite(heatPin , HIGH);
    client.publish("valve", "0");
  }
}

void conErrorHandle()
{
//Check mqtt connection --------------------------------------------------------------------------------
  if(client.loop())
  {
    mqttRecAtm = 0; //If fine reset reconnection atempts counter
  }
  else if(millis() >= mqttRecAlarm)
  {
    mqttRecAlarm = millis() + mqttRecFreq;
    if(mqttRecAtm >= mqttRec) //After mqttRec tries restart esp.
    {
      ESP.restart(); 
    }
    else //Reconnect
    {
      mqttRecAtm++;
      reconnect();
    }
  }
//Check wifi connection --------------------------------------------------------------------------------
  if(WiFi.status() == WL_CONNECTED)
  {
    wifiRecAtm = 0; //If fine reset reconnection atempts counter
  }
  else if(millis() >= wifiRecAlarm) //Wait for wifiRecAlarm
  {
    wifiRecAlarm = millis() + wifiRecFreq;
    if(wifiRecAtm >= wifiRec) //After wifiRec tries restart esp.
    {
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
