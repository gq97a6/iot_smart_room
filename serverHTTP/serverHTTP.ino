#include "WiFi.h"
#include "ESPAsyncWebServer.h"

//Access point details
const char* ssid = "ESP32-Access-Point";
const char* password = "123456789";

AsyncWebServer server(80);

void setup()
{
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  
  server.on("/temperature", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", readTemp().c_str());
  });
  server.on("/humidity", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", readHumi().c_str());
  });
  server.on("/pressure", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", readPres().c_str());
  });
  
  //Start HTTP server
  server.begin();
}
