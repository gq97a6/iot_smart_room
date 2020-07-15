#define FIRST_PIN 19
#define SECOND_PIN 18
#define THIRD_PIN 21

#define WIFI_RECON_FREQ 30000
#define MQTT_RECON_FREQ 30000

//MQTT
#define MQTT_PORT 54090
#define MQTT_USER "mqtt"
#define MQTT_PASSWORD "r5Vk!@z&uZBY&W%h"
const char* MQTT_SERVER = "192.168.0.125";
#define IS_LOCAL true

//Wifi
const char* ssid = "Wi-Fi 2.4GHz";
const char* password = "ceF78*Tay90!hiQ13@";
IPAddress ipToPing (8, 8, 8, 8); // The remote ip to ping

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
long wifiReconAlarm;
long mqttReconAlarm;

void setup()
{
  pinMode(FIRST_PIN, OUTPUT);
  pinMode(SECOND_PIN, OUTPUT);
  pinMode(THIRD_PIN, OUTPUT);

  setGear(0);
  
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

      if(adr == "wen" || adr == "glb")
      {
        String cmd = "";
        for (int i = 3; i < packet.length(); i++)
        {
          cmd += (char)packet.data()[i];
        }
        terminal(cmd);
      }
      
      //packet.printf("Got %u bytes of data", packet.length());
    });
  }
  
  ArduinoOTA.begin();
}

void loop()
{
  conErrorHandle();
  delay(10);
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
        
        if(Ping.ping(ipToPing, 1) || IS_LOCAL) //There is internet connection or server is localy hosted
        {
          mqttReconnect();
        }
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
  String clientId = "ESP32Client-";
  clientId += String(random(0xffff), HEX);

  // Attempt to connect
  if (client.connect(clientId.c_str(), MQTT_USER, MQTT_PASSWORD))
  {
    //Subscribe list
    client.subscribe("wenfan;");
  }
}

void setGear(int t)
{
  switch(t)
  {
    case 0:
      digitalWrite(FIRST_PIN, HIGH);
      digitalWrite(SECOND_PIN, HIGH);
      digitalWrite(THIRD_PIN, HIGH);
      break;
      
    case 1:
      setGear(0);
      digitalWrite(FIRST_PIN, LOW);
      break;
      
    case 2:
      setGear(0);
      digitalWrite(SECOND_PIN, LOW);
      break;
      
    case 3:
      setGear(0);
      digitalWrite(THIRD_PIN, LOW);
      break;
  }
}

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

  if(parmA == "fan")
  {
    setGear(parmB.toInt());
  }
  else if(parmA == "rst")
  {
    ESP.restart();
  }
}
