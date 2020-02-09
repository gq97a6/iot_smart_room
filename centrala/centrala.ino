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
long deskColor;
int topBarStatus;

int i; int ii; char charArray[8]; int poten;
void setup()
{
  analogReadResolution(2);

  pinMode(2, OUTPUT); //Build-in diode

  pinMode(15, OUTPUT); //5VDC supply
  pinMode(4, OUTPUT); //12VDC supply
  digitalWrite(15, LOW);
  digitalWrite(4, HIGH);

  pinMode(buttons[0][0], INPUT_PULLUP); //Button 1
  pinMode(buttons[1][0], INPUT_PULLUP); //Button 2
  pinMode(buttons[2][0], INPUT_PULLUP); //Button 3
  pinMode(buttons[3][0], INPUT_PULLUP); //Button 4
  pinMode(buttons[4][0], INPUT_PULLUP); //Button 5
  pinMode(34, INPUT); //Potn

  stripP.begin();
  stripP.show();
  stripP.setBrightness(255);

  stripL.begin();
  stripL.show();
  stripL.setBrightness(255);

  colorWipe(stripL.Color(15, 0, 255), 10, 0, 100);

  bme.begin();

  //MQTT
  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(callback);

  //OTA programing and WiFI
  Serial.begin(115200);
  Serial.println("Booting");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED)
  {
    Serial.println("Connection Failed! Rebooting...");
    digitalWrite(2, HIGH);
    delay(5000);
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
  
  if (millis() >= updateAlarm)
  {
    updateAlarm = millis() + updateFreq;
    
    String(bme.readTemperature()).toCharArray(charArray, 8);
    client.publish("temp", charArray);
    
    String(bme.readHumidity()).toCharArray(charArray, 8);
    client.publish("humi", charArray);
    
    String(bme.readPressure() / 100.0F).toCharArray(charArray, 8);
    client.publish("prea", charArray);
  }

  buttonsHandle();
  
  //Top bar control
  if(buttons[3][2] && !buttons[0][1] && !buttons[1][1] && !buttons[2][1] && !buttons[4][1] && millis() >= topBarButtonAlarm)
  {
    topBarButtonAlarm = millis() + topBarButtonFreq;
    
    if(topBarStatus)
    {
      topBarStatus = 0;
      digitalWrite(4, HIGH);
      client.publish("12", "off");
    }
    else
    {
      topBarStatus = 1;
      digitalWrite(4, LOW);
      client.publish("12", "on");
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

  if (topicStr == "deskSwitch")
  {
    if(payloadStr == "on")
    {
      digitalWrite(15, LOW);
      colorWipe(deskColor, 1, 0, 80);
      client.publish("5", "on");
    }
    else if(payloadStr == "off")
    {
      digitalWrite(15, HIGH);
      client.publish("5", "off");
    }
  }
  else if (topicStr == "deskColor")
  {
    deskColor = payloadStr.toInt();
    colorWipe(deskColor, 1, 0, 80);
  }
  else if (topicStr == "12")
  {
    if(payloadStr == "on")
    {
      digitalWrite(4, LOW);
    }
    else if(payloadStr == "off")
    {
      digitalWrite(4, HIGH);
    }
  }
  else if (topicStr == "5")
  {
    if(payloadStr == "on")
    {
      digitalWrite(15, LOW);
    }
    else if(payloadStr == "off")
    {
      digitalWrite(15, HIGH);
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
    client.subscribe("deskSwitch");
    client.subscribe("deskColor");
    client.subscribe("centralaterminal");
    client.subscribe("12");
    client.subscribe("5");
  }
}

void colorWipe(uint32_t color, int wait, int first, int last)
{
  for (int i = first; i < last; i++)
  {
    stripL.setPixelColor(i, color);
    stripP.setPixelColor(i, color);
    stripL.show();
    stripP.show();
  
    delay(wait);
  }
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
