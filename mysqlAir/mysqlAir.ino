#include <MySQL_Connection.h>
#include <MySQL_Cursor.h>
#include <WiFi.h>

//UDP
#include "WiFi.h"
#include "AsyncUDP.h"
AsyncUDP udp;

const char* ssid = "Wi-Fi 2.4GHz";
const char* password = "ceF78*Tay90!hiQ13@";

char user[] = "esp";
char passwordSQL[] = "123";

IPAddress server_addr (192, 168, 0, 125); // IP of the MySQL server here
WiFiClient wifiClient;

char charArray[80];
String queryS;

void setup()
{
  Serial.begin(115200);
  WiFi.begin(ssid, password);

  if(udp.listen(54091))
  {
    udp.onPacket([](AsyncUDPPacket packet)
    {
      String adr = "";
      adr += (char)packet.data()[0];
      adr += (char)packet.data()[1];
      adr += (char)packet.data()[2];

      if(adr == "glb")
      {
        String cmd = "";
        for (int i = 3; i < packet.length(); i++)
        {
          cmd += (char)packet.data()[i];
        }
        
        terminal(cmd);
      }
    });
  }
}

void loop()
{
  delay(10);
  //mysqlQuery("INSERT INTO esp32.dane (wynik) VALUES ('Hello, Arduino!2')");
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

  //-------------------------------------------------------------------------------- Set one color for whole strip
  if(parmA == "air") //Temperature, humidity, pressure
  {
    queryS = "INSERT INTO dane.air (temp, humi, prea) VALUES (" + String(parmB) + ", " + String(parmC) + ", " + String(parmD) + ")";
    queryS.toCharArray(charArray, 80);
    mysqlQuery(charArray);
  }
}

void mysqlQuery(char* query)
{
  MySQL_Connection conn((Client *)&wifiClient);
  
  if (conn.connect(server_addr, 3306, user, passwordSQL))
  {
    MySQL_Cursor *cur_mem = new MySQL_Cursor(&conn);
    cur_mem->execute(query);
    delete cur_mem;
  }
}
