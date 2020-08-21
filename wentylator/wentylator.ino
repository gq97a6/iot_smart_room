//-------------------------------------------------------------------------------- Variables
#define FIRST_PIN 18 //19
#define SECOND_PIN 18
#define THIRD_PIN 21

#define MAX_DCE_TIMERS 20
#define ADDRESS "wen"

#define WIFI_RECON_FREQ 30000
#define MQTT_RECON_FREQ 30000

//MQTT
#define MQTT_PORT 54090
#define MQTT_USER "mqtt"
#define MQTT_PASSWORD "r5Vk!@z&uZBY&W%h"
const char* MQTT_SERVER = "192.168.0.125";

//Wifi
const char* ssid = "Wi-Fi 2.4GHz";
const char* password = "ceF78*Tay90!hiQ13@";

//UDP
#define UDP_PORT 54091

//Delayed command execution
long DCETimers[MAX_DCE_TIMERS][2]; //When, distance
int DCELoop[MAX_DCE_TIMERS]; //-1 == retain, >0 == loop
String DCECommand[MAX_DCE_TIMERS]; //Command

//-------------------------------------------------------------------------------- Libraries
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

//-------------------------------------------------------------------------------- Clients
WiFiClient wifiClient;
PubSubClient client(wifiClient);

//-------------------------------------------------------------------------------- Status
long wifiReconAlarm;
long mqttReconAlarm;
byte gear;

void setup()
{
  Serial.begin(115200);

  pinMode(FIRST_PIN, OUTPUT);
  //pinMode(SECOND_PIN, OUTPUT);
  //pinMode(THIRD_PIN, OUTPUT);

  terminal("fan;0");

  //MQTT
  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(mqttCallback);

  //Wifi and OTA update
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  delay(1000);
  if (WiFi.status() != WL_CONNECTED)
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

  if (udp.listen(UDP_PORT))
  {
    udp.onPacket([](AsyncUDPPacket packet)
    {
      String adr = "";
      for (int i = 0; i < packet.length(); i++)
      {
        adr += (char)packet.data()[i];
        if ((char)packet.data()[i + 1] == ';')
        {
          break;
        }
      }

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
  delay(10);
}

void conErrorHandle()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    ArduinoOTA.handle();

    if (!client.loop()) //No connection with mqtt server
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

void DCEHandle()
{
  for (int i = 0; i < MAX_DCE_TIMERS; i++)
  {
    if (millis() >= DCETimers[i][0] && DCELoop[i] != 0)
    {
      //Execute command
      terminal(DCECommand[i]);

      if (DCELoop[i] == -1) // Infinite, extend
      {
        DCETimers[i][0] = millis() + DCETimers[i][1];
      }
      else if (DCELoop[i] > 1) //X times, extend, decrease
      {
        DCETimers[i][0] = millis() + DCETimers[i][1];
        DCELoop[i] -= 1;
      }
      else if (DCELoop[i] == 1) //Close DCE, its last iteration
      {
        DCELoop[i] = 0;
      }
    }
  }
}

void DCEAdd(long timer, String command, int loops)
{
  for (int i = 0; i < MAX_DCE_TIMERS; i++)
  {
    if (DCELoop[i] == 0) //Look for first empty
    {
      //Command
      DCETimers[i][0] = timer + millis();
      DCETimers[i][1] = timer;
      DCELoop[i] = loops;
      DCECommand[i] = command;

      return;
    }
  }
}

bool DCEEdit(long timer, String command, int loops)
{
  bool edited = 0;

  for (int i = 0; i < MAX_DCE_TIMERS; i++)
  {
    if (DCECommand[i] == command || DCELoop[i] != 0) //Look for desired command and edit
    {
      edited = 1;

      //Command
      if (timer > 0)
      {
        DCETimers[i][0] = timer + millis();
        DCETimers[i][1] = timer;
      }
      else if (timer == 0)
      {
        DCETimers[i][0] = 0;
      }

      DCECommand[i] = command;
      DCELoop[i] = loops;
    }
  }

  return edited;
}

//-------------------------------------------------------------------------------- Callbacks
void mqttCallback(char* topic, byte* payload, unsigned int length)
{
  if (String(topic) == "terminal") //Terminal input
  {
    //Extract adress
    String adr = "";
    int i;
    for (i = 0; i < length; i++)
    {
      adr += (char)payload[i];
      if ((char)payload[i+1] == ';')
      {
        break;
      }
    }
    
    if (adr == ADDRESS || adr == "glb")
    {
      String cmd;
      for (int j = i + 2; j < length; j++)
      {
        cmd += (char)payload[j];
      }
      
      terminal(cmd);
    }
  }
  else //Standard input
  {
    //Extract adress
    String adr = "";
    int i;
    for (i = 0; i < sizeof(topic); i++)
    {
      adr += topic[i];
      if (topic[i+1] == ';')
      {
        break;
      }
    }

    if(adr == ADDRESS)
    {
      String cmd = String(topic).substring(i+2) + ';';
  
      for (int i = 0; i < length; i++)
      {
        cmd += (char)payload[i];
      }
  
      terminal(cmd);
    }
  }
}

void terminal(String command)
{
  //Create arrays
  String cmd[20];
  char cmdChar[40];
  command.toCharArray(cmdChar, 40);

  //Slice array into parameters
  int parm = 0;
  for (int i = 0; i < 40; i++)
  {
    if (cmdChar[i] == ';')
    {
      parm++;
    }
    else
    {
      cmd[parm] += cmdChar[i];
    }
  }

  if (cmd[0] == "fan")
  {
    switch (cmd[1].toInt())
    {
      case 0:
        digitalWrite(FIRST_PIN, HIGH);
        gear = 0;
        break;

      case 1:
        terminal("fan;0");
        digitalWrite(FIRST_PIN, LOW);
        gear = 1;
        break;

      case -1:
        if (gear)
        {
          terminal("fan;0");
        }
        else
        {
          terminal("fan;1");
        }
        break;
    }
  }
  else if (cmd[0] == "reset")
  {
    ESP.restart();
  }
  else if (cmd[0] == "sendBrodcast") //address, command, A, B, C...
  {
    String toSend;
    toSend += cmd[1] + cmd[2]; //Address and command

    //Parameters
    for(int i = 3; i<40; i++)
    {
      if (cmd[i] != "")
      {
        toSend += ';';
        toSend += cmd[i];
      }
    }

    char toSendA[40];
    toSend.toCharArray(toSendA, 40);
    udp.broadcastTo(toSendA, UDP_PORT);
  }
}
