//EEPROM
#include <Preferences.h>
Preferences preferences;

//LED strips
#include <Adafruit_NeoPixel.h>
Adafruit_NeoPixel stripL(79, 18, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel stripP(74, 5, NEO_GRB + NEO_KHZ800);

//BME280 sensor
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
Adafruit_BME280 bme;

//MQTT and WiFI
#include <SPI.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <WiFiUdp.h>
#include <ESP32Ping.h>

//MQTT server details
const char* MQTT_SERVER = "tailor.cloudmqtt.com";
#define MQTT_PORT 11045
#define MQTT_USER "derwsywl"
#define MQTT_PASSWORD "IItmHbbnu9mD"

//HTML and HTML server details
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
WebServer server(80);
const char* www_username = "admin";
const char* www_password = "esp32";
const char* www_realm = "Custom Auth Realm";
String authFailResponse = "Authentication Failed";

//WiFi details
const char* ssid = "2.4G-Vectra-WiFi-8F493A";
const char* password = "brygida71";
WiFiClient wifiClient;
PubSubClient client(wifiClient);
IPAddress ip (8, 8, 8, 8); // The remote ip to ping

//Variables
bool state; //Button state
int buttons[5][5] = 
{{27, 0, 0, 0, 0}, 
{26, 0, 0, 0, 0}, 
{25, 0, 0, 0, 0}, 
{33, 0, 0, 0, 0}, 
{32, 0, 0, 0, 0}};
//Pin, state, ascending, descending, clicked

//Callback
String topicStr;
String payloadStr;

//Alarms
int updateFreq = 10000;
long updateAlarm;

//Lock buttons to prevent errors
int heatButtonFreq = 200;
long heatButtonAlarm;
int topBarButtonFreq = 200;
long topBarButtonAlarm;

//Reconnecting
int wifiRecFreq = 10000;
long wifiRecAlarm;
int mqttRecFreq = 10000;
long mqttRecAlarm;
int wifiRecAtm = 0;
int mqttRecAtm = 0;
int wifiRec = 5; //After wifiRec times, give up reconnecting and restart esp
int mqttRec = 5;

//Status
float temp;
int humi;
int pres;
//--------------------
float termostat = 10;
String heatMode = "cold";
bool valve;
//--------------------
bool c12;
bool c5;
bool b5;
//--------------------
bool topBar;
bool bedStrip;
bool deskStrip;
int bedColor = 255;
int deskColor = 255; //FF 80 2D do 167 444 93

int i; int ii; char charArray[8]; int poten;
void setup()
{
  analogReadResolution(2);

  pinMode(2, OUTPUT); //Build-in diode

  pinMode(15, OUTPUT); //5VDC supply
  pinMode(4, OUTPUT); //12VDC supply
  digitalWrite(15, HIGH);
  digitalWrite(4, HIGH);

  for(i = 0; i<=4; i++)
  {
    pinMode(buttons[i][0], INPUT_PULLUP);
  }
  pinMode(34, INPUT); //Potn

  stripP.begin();
  stripP.show();
  stripP.setBrightness(255);

  stripL.begin();
  stripL.show();
  stripL.setBrightness(255);

  bme.begin();

  //MQTT
  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(callback);

  //OTA programing and WiFI
  Serial.begin(115200);
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

    digitalWrite(2, HIGH);
    delay(3000);
    digitalWrite(2, LOW);

    ESP.restart();
  });

  httpServer();
  ArduinoOTA.begin();
}

void loop()
{
  server.handleClient(); //HTML maintenance
  conErrorHandle(); //OTA and connection maintenance

  if(WiFi.status() == WL_CONNECTED)
  {
    ArduinoOTA.handle();
  }

  temp = bme.readTemperature();
  humi = bme.readHumidity();
  pres = bme.readPressure() / 100.0F;

  if (millis() >= updateAlarm)
  {
    updateAlarm = millis() + updateFreq;
    
    String(bme.readTemperature()).toCharArray(charArray, 8);
    client.publish("temp", charArray);
    
    String(bme.readHumidity()).toCharArray(charArray, 8);
    client.publish("humi", charArray);
    
    String(bme.readPressure() / 100.0F).toCharArray(charArray, 8);
    client.publish("pres", charArray);
  }

  buttonsHandle();
  
  //Top bar control
  if(buttons[3][2] && !buttons[0][1] && !buttons[1][1] && !buttons[2][1] && !buttons[4][1] && millis() >= topBarButtonAlarm)
  {
    topBarButtonAlarm = millis() + topBarButtonFreq;
    
    if(topBar)
    {
      topBar = 0;
      digitalWrite(4, HIGH);
      client.publish("c12", "0");
    }
    else
    {
      topBar = 1;
      digitalWrite(4, LOW);
      client.publish("c12", "1");
    }
  }

  //Fan control //0(0) 1-160(1) 161-175(X) 176-335(2) 336-350(X) 351-511(3)
  if(buttons[2][2] && !buttons[0][1] && !buttons[1][1] && !buttons[3][1] && !buttons[4][1])
  {
    poten = analogRead(34);
    if(poten == 0)
    {
      client.publish("fan", "0");
    }
    else if(poten >= 1 && poten <= 160)
    {
      client.publish("fan", "1");
    }
    else if(poten >= 176 && poten <= 335)
    {
      client.publish("fan", "2");
    }
    else if(poten >= 351 && poten <= 511)
    {
      client.publish("fan", "3");
    }
  }
  
  //Heating control
  if(buttons[0][2] && !buttons[1][1] && !buttons[2][1] && !buttons[3][1] && !buttons[4][1] && millis() >= heatButtonAlarm)
  {
    poten = analogRead(34);
    if(poten == 0)
    {
      client.publish("heatControl", "cold");
    }
    else if(poten >= 1 && poten <= 160)
    {
      client.publish("heatControl", "auto");
    }
    else if(poten >= 176 && poten <= 335)
    {
      client.publish("heatControl", "heatup");
    }
    else if(poten >= 351 && poten <= 511)
    {
      client.publish("heatControl", "heat");
    }
  }

  //Restart
  if(analogRead(34) == 0 && buttons[0][1] && buttons[1][1] && buttons[2][1] && buttons[3][1] && buttons[4][1])
  {
    ESP.restart();
  }
  
  delay(18);
}

void callback(char* topic, byte* payload, unsigned int length)
{
  topicStr = String(topic);
  payloadStr = "";
  for (int i = 0; i < length; i++)
  {
    payloadStr += (char)payload[i];
  }
  
  if (topicStr == "termostat")
  {
    termostat = payloadStr.toInt();
  }
//--------------------------------------------------------------------------------
  else if (topicStr == "heatMode")
  {
    heatMode = payloadStr;
  }
//--------------------------------------------------------------------------------
  else if (topicStr == "valve")
  {
    valve = payloadStr.toInt();
  }
//--------------------------------------------------------------------------------
  else if (topicStr == "c12")
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
  else if (topicStr == "b5")
  {
    b5 = payloadStr.toInt();
  }
//--------------------------------------------------------------------------------
  else if (topicStr == "bedStrip")
  {
    bedStrip = payloadStr.toInt();
  }
//--------------------------------------------------------------------------------
  else if (topicStr == "deskStrip")
  {
    if (payloadStr == "1")
    {
      deskStrip = 1;
      client.publish("c5", "1");
      digitalWrite(15, LOW);
      colorWipe(deskColor, 2, 0, 80);
    }
    else if (payloadStr == "0")
    {
      deskStrip = 0;
      client.publish("c5", "0");
      digitalWrite(15, HIGH);
    }
  }
//--------------------------------------------------------------------------------
  else if (topicStr == "topBar")
  {
    if (payloadStr == "1")
    {
      topBar = 1;
      client.publish("c12", "1");
      digitalWrite(4, LOW);
    }
    else if (payloadStr == "0")
    {
      topBar = 0;
      client.publish("c12", "0");
      digitalWrite(4, HIGH);
    }
  }
//--------------------------------------------------------------------------------
  else if (topicStr == "bedColor")
  {
    payloadStr.toCharArray(charArray, 8);
    bedColor = toIntColor(charArray);
  }
//--------------------------------------------------------------------------------
  else if (topicStr == "deskColor")
  {
    payloadStr.toCharArray(charArray, 8);
    deskColor = toIntColor(charArray);
    colorWipe(deskColor, 2, 0, 80);
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
    client.subscribe("topBar");
    client.subscribe("fan");
    client.subscribe("deskColor");
    client.subscribe("bedColor");
    client.subscribe("bedStrip");
    client.subscribe("deskStrip");
    client.subscribe("c12");
    client.subscribe("c5");
    client.subscribe("b5");
    client.subscribe("heatControl");
    client.subscribe("termostat");
    client.subscribe("valve");
  }
}

void colorWipe(uint32_t color, int wait, int first, int last)
{
  for (int i = first; i < last; i++)
  {
    stripL.setPixelColor(i, color);
    stripP.setPixelColor(i, color);
  
    delay(wait);
  }
  
  stripL.show();
  stripP.show();
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

void buttonsHandle()
{
  for (i = 0; i < 5; i++)
  {
    state = !digitalRead(buttons[i][0]);

    for (ii = 2; ii <= 4; ii++)
    {
      buttons[i][ii] = 0;
    }
    
    if (buttons[i][1] != state)
    {
      buttons[i][1] = state;
      if (state)
      {
        buttons[i][2] = 1; //Ascending
      }
      else
      {
        buttons[i][3] = 1; //Descending
      }
    }
  }
}

//-------------------------------------------------------------------------------- EEPROM #FIX
//void eepromPut()
//{
//  preferences.putDouble("hUpAlr", heatUpAlarm);
//  preferences.putUInt("bedClr", bedColor);
//  preferences.putUInt("htMd", heatMode);
//  preferences.putFloat("termst", termostat);
//
//  float termostat = 10;
//  String heatMode = "cold";
//  bool valve;
//  bool c12;
//  bool c5;
//  bool topBar;
//  bool bedStrip;
//  bool deskStrip;
//  int bedColor = 255;
//  int deskColor = 255;
//}
//
//void eepromGet()
//{
//  heatUpAlarm = preferences.getDouble("hUpAlr", 0);
//  bedColor = preferences.getUInt("bedClr", 255);
//  heatMode = preferences.getUInt("htMd", 0);
//  termostat = preferences.getFloat("termst", 0);
//}

void conErrorHandle()
{
//-------------------------------------------------------------------------------- Check mqtt connection 
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
//-------------------------------------------------------------------------------- Check wifi connection 
  if(WiFi.status() == WL_CONNECTED)
  {
    wifiRecAtm = 0; //If fine reset reconnection atempts counter
  }
//  else if(Ping.ping(ip, 3); //Check internet connection
//  {
//    
//  }
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

//-------------------------------------------------------------------------------- httpServer
void httpServer()
{
  if (MDNS.begin("esp32"))
  {
    Serial.println("MDNS responder started");
  }

  server.on("/", []()
  {
    if (server.authenticate(www_username, www_password))
    {
      //Digest Auth Method with Custom realm and Failure Response
      return server.requestAuthentication(DIGEST_AUTH, www_realm, authFailResponse);
    }
   
    mainPage();
  });
  
  server.onNotFound(notFoundPage);
  
  server.begin();
}

//-------------------------------------------------------------------------------- http pages
void notFoundPage()
{
  if (!server.authenticate(www_username, www_password))
  {
    //Digest Auth Method with Custom realm and Failure Response
    return server.requestAuthentication(DIGEST_AUTH, www_realm, authFailResponse);
  }
  
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for (uint8_t i = 0; i < server.args(); i++)
  {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }

  server.send(404, "text/plain", message);
}

void mainPage()
{
  char html[1000];
  snprintf(html, 1000,
  
  "<html>\
    <head>\
      <meta http-equiv='refresh' content='99999'/>\
      <title>ESP32 Demo</title>\
      <style>\
        body { background-color: black; font-family: Arial, Helvetica, Sans-Serif; Color: white; }\
      </style>\
    </head>\
    <body>\
      <p>Temperature: %1.1f</p>\
      <p>Humidity: %d</p>\
      <p>Preassure: %d</p>\
      </br></br>\
      <p>Termostat: %1.1f</p>\
      <p>Heat mode: %s</p>\
      <p>Valve: %d</p>\
      </br></br>\
      <p>12V: <a href='/'>%d</a> </p>\
      <p>5V: <a href='/'>%d</a> </p>\
      <p>5V (bed): <a href='/'>%d</a> </p>\
      </br></br>\
      <p>Top bar: <a href='/'>%d</a> </p>\
      <p>Bed strip: <a href='/'>%d</a> </p>\
      <p>Desk strip: <a href='/'>%d</a> </p>\
      <p>Bed color: %d</p>\
      <p>Desk color: %d</p>\
    </body>\
  </html>",
  
  temp, humi, pres, termostat, heatMode, valve, c12, c5, b5, topBar, bedStrip, deskStrip, bedColor, deskColor);
  
            
  server.send(200, "text/html", html);
}
