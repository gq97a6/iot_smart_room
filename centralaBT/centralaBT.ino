//-------------------------------------------------------------------------------- Bluetooth
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

BLEServer* bleServer = NULL;

BLECharacteristic* terminal = NULL;
BLECharacteristic* pressure = NULL;
BLECharacteristic* humidity = NULL;
BLECharacteristic* temperature = NULL;

bool deviceConnected = false;

#define SERVICE_UUID "7d8c4032-c42f-4d77-a490-3de70b93e884"

#define TERMINAL_CHAR_UUID "c25c5e15-72e8-47f7-b250-8a137a62b431"
#define TEMP_CHAR_UUID "fe04b216-8ea3-4739-8a72-4661c7645387"
#define HUMI_CHAR_UUID "2d07ce59-3d7d-460b-8c35-887d68766cd6"
#define PRES_CHAR_UUID "d2126ded-bddb-4766-b445-db3d4f089986"

class MyServerCallbacks: public BLEServerCallbacks
{
    void onConnect(BLEServer* bleServer)
    {
      deviceConnected = true;
    };

    void onDisconnect(BLEServer* bleServer)
    {
      deviceConnected = false;

      delay(500); // give the bluetooth stack the chance to get things ready
      bleServer->startAdvertising(); // restart advertising
    }
};

class charCallback: public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *terminal)
  {
    std::string value = terminal->getValue();

    if (value.length() > 0)
    {
      for (int i = 0; i < value.length(); i++)
      {
        Serial.print(value[i]);
      }
    }
  }
};

//-------------------------------------------------------------------------------- EEPROM
#include <Preferences.h>
Preferences preferences;

//LED strips
#include <FastLED.h>
CRGB stripL[78];
CRGB stripP[78];

#define FRAMES_PER_SECOND 120
bool gReverseDirection = false;
uint8_t gHue = 0;

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

//WiFi details
const char* ssid = "2.4G-Vectra-WiFi-8F493A";
const char* password = "brygida71";
WiFiClient wifiClient;
PubSubClient client(wifiClient);
IPAddress ip (8, 8, 8, 8); // The remote ip to ping

//Variables
bool state; //Button state
double buttonsTs[5] {0, 0, 0, 0, 0}; //Timestamp to last change
int buttons[5][5] =
{ {27, 0, 0, 0, 0},
  {26, 0, 0, 0, 0},
  {25, 0, 0, 0, 0},
  {33, 0, 0, 0, 0},
  {32, 0, 0, 0, 0}
};
//Pin, state, filtered, ascending, descending

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
char temp[8];
char humi[8];
char pres[8];
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
int bedColor = 255;
int deskColor = 255;
byte anim;

int i; int ii; char charArray[8]; byte byteArray[8]; int poten;
void setup()
{
  analogReadResolution(2);

  pinMode(2, OUTPUT); //Build-in diode

  pinMode(15, OUTPUT); //5VDC supply
  pinMode(4, OUTPUT); //12VDC supply
  digitalWrite(15, HIGH);
  digitalWrite(4, HIGH);

  for (i = 0; i <= 4; i++)
  {
    pinMode(buttons[i][0], INPUT_PULLUP);
  }
  pinMode(34, INPUT); //Potn

  FastLED.addLeds<WS2812B, 18, GRB>(stripL, 78).setCorrection(TypicalLEDStrip);
  FastLED.addLeds<WS2812B, 5, GRB>(stripP, 78).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(255);

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

  btSetup();
  ArduinoOTA.begin();
}

void loop()
{
  conErrorHandle(); //OTA and connection maintenance

  if (WiFi.status() == WL_CONNECTED)
  {
    ArduinoOTA.handle();
  }

  String(bme.readTemperature()).toCharArray(temp, 8);
  String(bme.readHumidity()).toCharArray(humi, 8);
  String(bme.readPressure() / 100.0F).toCharArray(pres, 8);
  
  for (int i = 0; i < 8; i++)
  {
    byteArray[i] = byte(temp[i]);
  }
  temperature->setValue(byteArray, 8);

  for (int i = 0; i < 8; i++)
  {
    byteArray[i] = byte(humi[i]);
  }
  pressure->setValue(byteArray, 8);

  for (int i = 0; i < 8; i++)
  {
    byteArray[i] = byte(pres[i]);
  }
  humidity->setValue(byteArray, 8);

  if (millis() >= updateAlarm)
  {
    updateAlarm = millis() + updateFreq;

    client.publish("temp", temp);
    client.publish("humi", humi);
    client.publish("pres", pres);
  }

  buttonsHandle();

  //Top bar control
  if (buttons[3][3] && !buttons[0][2] && !buttons[1][2] && !buttons[2][2] && !buttons[4][2] && millis() >= topBarButtonAlarm)
  {
    topBarButtonAlarm = millis() + topBarButtonFreq;

    if (topBar)
    {
      topBar = 0;
      c12 = 0;
      digitalWrite(4, HIGH);
      client.publish("c12", "0");
    }
    else
    {
      topBar = 1;
      c12 = 1;
      digitalWrite(4, LOW);
      client.publish("c12", "1");
    }
  }

  //Fan control //0(0) 1-160(1) 161-175(X) 176-335(2) 336-350(X) 351-511(3)
  if (buttons[2][3] && !buttons[0][2] && !buttons[1][2] && !buttons[3][2] && !buttons[4][2])
  {
    poten = analogRead(34);
    if (poten == 0)
    {
      client.publish("fan", "0");
    }
    else if (poten >= 1 && poten <= 160)
    {
      client.publish("fan", "1");
    }
    else if (poten >= 176 && poten <= 335)
    {
      client.publish("fan", "2");
    }
    else if (poten >= 351 && poten <= 511)
    {
      client.publish("fan", "3");
    }
  }

  //Heating control
  if (buttons[0][3] && !buttons[1][2] && !buttons[2][2] && !buttons[3][2] && !buttons[4][2] && millis() >= heatButtonAlarm)
  {
    poten = analogRead(34);
    if (poten == 0)
    {
      client.publish("heatControl", "cold");
    }
    else if (poten >= 1 && poten <= 160)
    {
      client.publish("heatControl", "auto");
    }
    else if (poten >= 176 && poten <= 335)
    {
      client.publish("heatControl", "heatup");
    }
    else if (poten >= 351 && poten <= 511)
    {
      client.publish("heatControl", "heat");
    }
  }

  //Restart
  if (analogRead(34) == 0 && buttons[0][2] && buttons[1][2] && buttons[2][2] && buttons[3][2] && buttons[4][1])
  {
    ESP.restart();
  }

  //Led strips
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
  else if (topicStr == "topBar")
  {
    if (payloadStr == "1")
    {
      topBar = 1;
      c12 = 1;
      client.publish("c12", "1");
      digitalWrite(4, LOW);
    }
    else if (payloadStr == "0")
    {
      topBar = 0;
      c12 = 0;
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
    colorWipe(deskColor, 20, 0, 77);
  }
  else if (topicStr == "deskAnim")
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
    client.subscribe("deskAnim");
  }
}

void colorWipe(uint32_t color, int wait, int first, int last)
{
  for (int i = first; i <= last; i++)
  {
    stripL[i] = color;
    stripP[i] = color;
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
#define NUM_LEDS  156

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
    for (int j = 0; j < NUM_LEDS; j++)
    {
      CRGB color = HeatColor(heat[j]);

      if (j < 78)
      {
        stripP[77 - j] = color;
      }
      else
      {
        stripL[j - 78] = color;
      }
    }

    FastLED.show();
  }
}

void rainbow()
{
  if (anim == 1)
  {
    fill_rainbow(stripL, 78, gHue, 7);
    fill_rainbow(stripP, 78, gHue, 7);

    FastLED.show();
  }
}

void confetti()
{
  if (anim == 2)
  {
    fadeToBlackBy( stripL, 78, 10);
    fadeToBlackBy( stripP, 78, 10);
    int pos = random16(78);
    stripP[pos] += CHSV( gHue + random8(64), 200, 255);
    stripL[pos] = stripP[pos];

    FastLED.show();
  }
}

void sinelon()
{
  if (anim == 3)
  {
    fadeToBlackBy( stripP, 78, 20);
    fadeToBlackBy( stripL, 78, 20);

    int pos = beatsin16( 13, 0, 78 - 1 );
    stripP[pos] += CHSV( gHue, 255, 192);
    stripL[pos] = stripP[pos];

    FastLED.show();
  }
}

void bpm()
{
  if (anim == 4)
  {
    uint8_t BeatsPerMinute = 62;
    uint8_t beat = beatsin8( BeatsPerMinute, 64, 255);

    CRGBPalette16 palette = PartyColors_p;
    for ( int i = 0; i < 78; i++) //9948
    {
      stripP[i] = ColorFromPalette(palette, gHue + (i * 2), beat - gHue + (i * 10));
      stripL[i] = stripP[i];
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

void buttonsHandle()
{
  for (i = 0; i < 5; i++)
  {
    state = !digitalRead(buttons[i][0]);

    //Clear states
    buttons[i][3] = 0;
    buttons[i][4] = 0;

    if (buttons[i][1] != state)
    {
      buttons[i][1] = state; //Real state
      buttonsTs[i] = millis(); //Timestamp to last change
    }

    if (millis() - buttonsTs[i] > 20 && buttonsTs[i] != 0)
    {
      buttonsTs[i] = 0;
      buttons[i][2] = state; //Filtered state

      if (state)
      {
        buttons[i][3] = 1; //Ascending
      }
      else
      {
        buttons[i][4] = 1; //Descending
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
  //-------------------------------------------------------------------------------- Check wifi connection
  if (WiFi.status() == WL_CONNECTED)
  {
    wifiRecAtm = 0; //If fine reset reconnection atempts counter
  }
  //  else if(Ping.ping(ip, 3); //Check internet connection
  //  {
  //
  //  }
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

//-------------------------------------------------------------------------------- bleServer
void btSetup()
{
  //-------------------------------------------------------------------------------- Create the BLE Device
  BLEDevice::init("centrala");
  //-------------------------------------------------------------------------------- Create the BLE Server
  bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(new MyServerCallbacks());
  //-------------------------------------------------------------------------------- Create the BLE Service
  BLEService *centrala = bleServer->createService(SERVICE_UUID);
  //-------------------------------------------------------------------------------- Create a BLE Characteristics
  pressure = centrala->createCharacteristic(PRES_CHAR_UUID, BLECharacteristic::PROPERTY_READ);
  humidity = centrala->createCharacteristic(HUMI_CHAR_UUID, BLECharacteristic::PROPERTY_READ);
  temperature = centrala->createCharacteristic(TEMP_CHAR_UUID, BLECharacteristic::PROPERTY_READ);

  terminal = centrala->createCharacteristic(TERMINAL_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE);
  terminal->setCallbacks(new charCallback());
  //-------------------------------------------------------------------------------- Start advertising
  centrala->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  BLEDevice::startAdvertising();
}
