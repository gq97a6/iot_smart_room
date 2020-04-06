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

//Reconnecting
int wifiRecFreq = 20000;
long wifiRecAlarm;
int mqttRecFreq = 20000;
long mqttRecAlarm;
int wifiRecAtm = 0;
int mqttRecAtm = 0;
int wifiRec = 6; //After wifiRec times, give up reconnecting and restart esp
int mqttRec = 6;

void setup()
{
  pinMode(5, OUTPUT);
  pinMode(15, OUTPUT);
  pinMode(18, OUTPUT);

  setGear(0);
  
  //MQTT
  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(callback);

  //OTA update and WiFI
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

      if(adr == "wen" || adr == "glb")
      {
        String cmd = "";
        for (int i = 3; i < packet.length(); i++)
        {
          cmd += (char)packet.data()[i];
        }
        udpCallback(cmd);
      }
      
      //packet.printf("Got %u bytes of data", packet.length());
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

  delay(150);
}

void callback(char* topic, byte* payload, unsigned int length)
{
  setGear(0); //Safety
  
  String topicStr = String(topic);
  String payloadStr = "";
  for (int i = 0; i < length; i++)
  {
    payloadStr += (char)payload[i];
  }
  
  if(topicStr == "fan")
  {
    switch(payloadStr.toInt())
    {
      case 1:
        setGear(1);
        break;
      case 2:
        setGear(2);
        break;
      case 3:
        setGear(3);
        break;
    }
  }
  else if(topicStr == "terminal" && payloadStr == "fan restart")
  {
    ESP.restart();
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
    client.subscribe("fan");
    client.subscribe("terminal");
  }
}

void setGear(int t)
{
  switch(t)
  {
    case 0:
      digitalWrite(5, LOW);
      digitalWrite(15, HIGH);
      digitalWrite(18, HIGH);
      break;
      
    case 1:
      setGear(0);
      digitalWrite(5, HIGH);
      break;
      
    case 2:
      setGear(0);
      digitalWrite(15, LOW);
      break;
      
    case 3:
      setGear(0);
      digitalWrite(18, LOW);
      break;
  }
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
      ESP.restart();
    }
    else //Reconnect
    {
      wifiRecAtm++;
      WiFi.begin(ssid, password);
    }
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

  if(parmA == "gear")
  {
    setGear(parmB.toInt());
  }
  else if(parmA == "rst")
  {
    ESP.restart();
  }
}
