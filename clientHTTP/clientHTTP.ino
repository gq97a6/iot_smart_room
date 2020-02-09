#include <WiFi.h>
#include <HTTPClient.h>

//HTTP server acces point details
const char* ssid = "ESP32-Access-Point";
const char* password = "123456789";

//Your IP address or domain name with URL path
const char* serverNameTemp = "http://192.168.4.1/temperature";
const char* serverNameHumi = "http://192.168.4.1/humidity";
const char* serverNamePres = "http://192.168.4.1/pressure";

void loop()
{
  temperature = httpGETRequest(serverNameTemp);
  humidity = httpGETRequest(serverNameHumi);
  pressure = httpGETRequest(serverNamePres);
}

String httpGETRequest(const char* serverName)
{
  HTTPClient http;
    
  //Your IP address with path or Domain name with URL path 
  http.begin(serverName);
  
  //Send HTTP POST request
  int httpResponseCode = http.GET();
  String payload = "--"; 
  
  if (httpResponseCode>0)
  {
    payload = http.getString();
  }
  
  http.end();

  return payload;
}
