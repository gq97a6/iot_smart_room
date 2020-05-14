#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiAP.h>

// Set these to your desired credentials.
const char* ssid = "2.4G-Vectra-WiFi-8F493A";
const char* password = "brygida71";

void setup()
{
  WiFi.softAP(ssid, password);
}

void loop()
{
  delay(10);
}
