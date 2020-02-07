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
int heatPin = 27;
int supplyPin = 14;

//State
int i;
bool flip;
long bedColor;
byte heatControl = 0; //Cold(0), Auto(1), Heat up(2), Heat(3)
byte revertheatControl; //Revert to previous mode before heat up, Cold(0), Auto(1)

//Alarms
int heatUpFreq = 3 * 60 * 1000;
long heatUpAlarm;

int heatFreq = 45 * 60 * 1000; //How long to wait before turing on heating
long heatAlarm;

int coldFreq = 4 * 60 * 1000; //How long to wait before turing off heating
long coldAlarm;

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
  
  if(heatControl == 1) //Auto
  {
    if(flip && millis() >= heatAlarm) //Wait for alarm to turn on heating
    {
      flip = 0;
      valve(1);
      coldAlarm = millis() + coldFreq; //Set alarm for turning off heating
    }
    else if(!flip && millis() >= coldAlarm) //Wait for alarm to turn off heating
    {
      flip = 1;
      valve(0);
      heatAlarm = millis() + heatFreq; //Set alarm for turning on heating
    }
  }
  else if(heatControl == 2) //Heat up
  {
    //Turn off alarm
    if (millis() >= heatUpAlarm)
    {
      valve(0);

      if(revertheatControl) //Go back to auto
      {
        flip = 0; //Start by turn off cycle
        heatAlarm = millis() + heatFreq; //Set alarm for turning on heating
        heatControl = 1; //Set mode
        valve(0);
        client.publish("heatControl", "auto");
      }
      else
      {
        heatControl = 0;
        valve(0);
        client.publish("heatControl", "cold");
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
  else if (topicStr == "heatControl")
  {
    if (payloadStr == "cold")
    {        
      heatControl = 0; //Turn off modes
      revertheatControl = 0; //After next heat up, turn of valve
      valve(0);
    }
    else if (payloadStr == "heat")
    {
      heatControl = 3; //Turn off modes
      revertheatControl = 0; //After next heat up, turn of valve
      valve(1);
    }
    else if (payloadStr == "auto")
    {
      if(!heatControl)//If changing back from cold mode
      {
        flip = 1; //Start by turn on cycle
        coldAlarm = millis() + coldFreq; //Set alarm for turning off heating
        valve(1);
      }
      else if(heatControl == 3) //If changing back from heat mode
      {
        flip = 0; //Start by turn of cycle
        heatAlarm = millis() + heatFreq; //Set alarm for turning on heating
        valve(0);
      }
      heatControl = 1; //Set mode
      revertheatControl = 1; //After next heat up, go back to auto
    }
    else if (payloadStr == "heatup")
    {
      heatControl = 2; //Set mode
      valve(1);
      heatUpAlarm = millis() + heatUpFreq; //Set alarm to turn off valve
    }
    else
    {
      valve(0);
    }
  }
}

void reconnect()
{
  //Loop until we're reconnected
  while (!client.connected())
  {
    Serial.print("Attempting MQTT connection...");

    //Create a random client ID
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);

    // Attempt to connect
    if (client.connect(clientId.c_str(), MQTT_USER, MQTT_PASSWORD))
    {
      Serial.print(" Connected.");
      Serial.println();
      Serial.println();

      //Subscribe list
      client.subscribe("bedSwitch");
      client.subscribe("bedColor");
      client.subscribe("heatButton");
      client.subscribe("heatControl");
      client.subscribe("termostat");
      client.subscribe("5b");
      client.subscribe("terminal");
    }
    else
    {
      Serial.print("Failed , rc=");
      Serial.print(client.state());
      Serial.println(", try again in 5 seconds.");

      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void colorWipe(uint32_t color, int wait, int first, int last)
{
  for (int i = first; i < last; i++)
  {    
    strip.setPixelColor(i, color);         //  Set pixel's color (in RAM)
    strip.show();                          //  Update strip to match
    delay(wait);
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
  if(client.loop()) //Check mqtt connection
  {
    mqttRecAtm = 0; //If fine reset reconnection atempts counter
  }
  else if(millis() >= mqttRecAlarm)
  {
    mqttRecAlarm = millis() + mqttRecFreq;
    if(mqttRecAtm >= mqttRec) //After mqttRec tries restart esp.=
    {
      ESP.restart(); 
    }
    else //Reconnect
    {
      mqttRecAtm++;
      reconnect();
    }
  }

  if(WiFi.status() == WL_CONNECTED) //Check wifi connection
  {
    wifiRecAtm = 0; //If fine reset reconnection atempts counter
  }
  else if(millis() >= wifiRecAlarm) //Wait for wifiRecAlarm
  {
    wifiRecAlarm = millis() + wifiRecFreq;
    if(wifiRecAtm >= wifiRec) //After wifiRec tries restart esp.=
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
