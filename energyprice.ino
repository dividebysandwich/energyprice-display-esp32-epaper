#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <SPI.h>
#include <GxEPD.h>
#include <GxGDEW042T2/GxGDEW042T2.cpp>
#include <GxIO/GxIO_SPI/GxIO_SPI.cpp>
#include <GxIO/GxIO.cpp>
#include "esp_system.h"
#include "time.h"

#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Fonts/FreeSans9pt7b.h>

#define ONE_WIRE_BUS 15 

GxIO_Class io(SPI, SS, 17, 16);
GxEPD_Class display(io, 16, 4); 
hw_timer_t *timer = NULL;

const char* ssid     = "";
const char* password = "";

const char* host = "i.spottyenergie.at";

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 0;
const int   daylightOffset_sec = 0;

const int GRAPH_SIZE = 130;
int useGraph[GRAPH_SIZE];

void IRAM_ATTR resetModule(){
    printf("reboot\n");
    esp_restart();
}

void setup()
{
    Serial.begin(115200);
    delay(10);
    display.init();

    display.fillScreen(GxEPD_WHITE);
    bigText(90, 105, "Price Monitor");
    smallText(90, 135, "Loading...");
    display.update();

    // We start by connecting to a WiFi network

    Serial.println();
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);

    // Watchdog timer
    timer = timerBegin(100000); //timer 0, div 80
    timerAttachInterrupt(timer, &resetModule); //reset ESP if timer runs out
    timerAlarm(timer, 60000000*15, false, 0); //timeout after 10 minutes
}

void wifiReconnect() {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.begin(ssid, password);
    int i = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.print(".");
        i = i + 1;
        if (i > 20) {
          esp_restart();
        }
    }
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
  }
}
int value = 0;

void loop()
{
    timerWrite(timer, 0); //reset watchdog timer
    delay(5000);
    wifiReconnect();

    // Init and get the time
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo)){
      Serial.println("Failed to obtain time");
      return;
    }

    Serial.println(&timeinfo, "%Y-%m-%dT%H:%M:%SZ");
    Serial.print("Month: ");
    Serial.println(&timeinfo, "%B");
    Serial.print("Day of Month: ");
    Serial.println(&timeinfo, "%d");
    Serial.print("Year: ");
    Serial.println(&timeinfo, "%Y");
    Serial.print("Hour: ");
    Serial.println(&timeinfo, "%H");
    Serial.print("Minute: ");
    Serial.println(&timeinfo, "%M");
    Serial.print("Second: ");
    Serial.println(&timeinfo, "%S");

    Serial.print("connecting to ");
    Serial.println(host);

    // Use WiFiClient class to create TCP connections
    WiFiClientSecure client;
    client.setInsecure();
    if (!client.connect(host, 443)) {
        Serial.println("connection failed");
        delay(30000);
        return;
    }

    Serial.println("Getting Data");
    // This will send the request to the server
    client.print(String("GET ") + "/api/prices/public" + " HTTP/1.1\r\n" +
                 "Host: " + host + "\r\n" +
                 "Connection: Keep-Alive\r\n\r\n");
    unsigned long timeout = millis();
    while (client.available() == 0) {
        if (millis() - timeout > 5000) {
            Serial.println(">>> Client Timeout !");
            client.stop();
            return;
        }
    }
    display.fillScreen(GxEPD_WHITE);

    if (client.available()) {
        readPastHeader(&client);
        Serial.println("Price data received!");

        while (client.available()) {
          String data = client.readStringUntil('\n');
          if (data.length() > 100) {
            String raw_json = data;
            Serial.println("Json: "+raw_json);
            JsonDocument doc;
            // Deserialize the JSON document
            DeserializationError error = deserializeJson(doc, raw_json);

            // Test if parsing succeeds
            if (error) {
              Serial.print(F("deserializeJson() failed: "));
              Serial.println(error.f_str());
              return;
            }
            double maxprice = 0.0;
            double minprice = 0.0;
            // get price extrema
            for (uint16_t n=0; n<sizeof(doc); n++) {
              double price = doc[n]["price"];
              if (price > maxprice)
                maxprice = price;
              if (price < minprice)
                minprice = price;
            }

            // draw histogram
            double previous_yf = -9999.0;
            for (uint16_t n=12; n<sizeof(doc); n++) {
              const char* timestamp = doc[n]["from"];
              double price = doc[n]["price"];
              double yf = 280.0-(price / (maxprice - minprice) * 280.0);
              if (previous_yf == -9999.0)
              {
                previous_yf = yf;
              }
              else
              {
                drawVerticalLine(40+((n-12)*6), (uint16_t)yf, (int)(previous_yf - yf), 1, GxEPD_BLACK);
                time_t temptime = time(NULL);
                struct tm temptm = *localtime(&temptime);
                strptime(timestamp, "%Y-%m-%dT%H:%M:%SZ", &temptm);
                // If the data is of the current hour, mark it with a line, arrow and current price
                if (temptm.tm_mday == timeinfo.tm_mday &&
                  temptm.tm_mon == timeinfo.tm_mon &&
                  temptm.tm_hour == timeinfo.tm_hour) {
                    char pricelabel[10];
                    sprintf(pricelabel,"%.2f ct", price);
                    smallText(45+((n-12)*6), 0, pricelabel);
                    drawVerticalLine(40+((n-12)*6)+3, (uint16_t)yf, -(int)yf+10, 2, GxEPD_BLACK);
                    drawArrow(40+((n-12)*6)+3, (uint16_t)yf);
                  }
              }
              // Connect the previous vertical line with a horizontal line.
              drawHorizontalLine(40+((n-12)*6), (uint16_t)yf, 6, 1, GxEPD_BLACK);
              previous_yf = yf;
            }

            // Max price on the top left corner
            tinyText(0, 5, String(maxprice)+"ct");

            // Draw scale lines in 5 cent increments
            for (int i=-20; i<=200; i+=5)
            {
              char pricelabel[10];
              sprintf(pricelabel,"%d ct", i);
              drawHorizontalLine(5, (uint16_t)(280.0 - ((float)i / (maxprice-minprice) * 280.0)), 400, 3, GxEPD_BLACK);
              tinyText(0, (uint16_t)(285.0 - ((float)i / (maxprice-minprice) * 280.0)), pricelabel);
            }
          }

        }
    }

    Serial.println("Price histogram completed!");
    display.update();
    Serial.println();
    Serial.println("closing connection");
    client.stop();
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);

    delay(1000*60*10);


}

float round_to_dp( float in_value, int decimal_place )
{
  float multiplier = powf( 10.0f, decimal_place );
  in_value = roundf( in_value * multiplier ) / multiplier;
  return in_value;
}
  
bool readPastHeader(WiFiClientSecure *pClient)
{
  bool bIsBlank = true;
  while(true)
  {
    if (pClient->available()) 
    {
     char c = pClient->read();
     if(c=='\r' && bIsBlank)
     {
       // throw away the /n
       c = pClient->read();
       return true;
     }
     if(c=='\n')
       bIsBlank = true;
     else if(c!='\r')
       bIsBlank = false;
    }
  }
}

void drawHorizontalLine(uint16_t startx, uint16_t starty, int width, int increment, uint16_t color)
{
  if (starty > 0 && starty < 300)
  {
    for (uint16_t i=startx; i<startx + width; i+=increment) 
    {
      display.drawPixel(i, starty, color);
    }
  }
}

void drawVerticalLine(uint16_t startx, uint16_t starty, int height, int increment, uint16_t color)
{
  if (height > 0)
  {
    for (uint16_t i=starty; i<starty + height; i+=increment) 
    {
      display.drawPixel(startx, i, color);
    }
  } 
  else
  {
    for (uint16_t i=starty; i>starty + height; i--) 
    {
      display.drawPixel(startx, i, GxEPD_BLACK);
    }
  }
}

void drawArrow(uint16_t x, uint16_t y)
{
  for (int i=0; i<=6; i++)
  {
    for (int w=0; w<=i; w++)
    {
      display.drawPixel(x+w, y-i, GxEPD_BLACK);
      display.drawPixel(x-w, y-i, GxEPD_BLACK);
    }
  }
}

void tinyText(uint16_t x, uint16_t y, String text)
{
  if (x<0 || y<0 || x > 400 || y > 300)
  {
    return;
  }
  const char* name = "FreeSans9pt7b";
  const GFXfont* f = &FreeSans9pt7b;
  
  display.setRotation(0);
  display.setFont(f);
  display.setTextColor(GxEPD_BLACK);

  display.setCursor(x, y+9);
  display.print(text); 
} 

void smallText(uint16_t x, uint16_t y, String text)
{
  const char* name = "FreeSans12pt7b";
  const GFXfont* f = &FreeSans12pt7b;
  
  display.setRotation(0);
  display.setFont(f);
  display.setTextColor(GxEPD_BLACK);

  display.setCursor(x, y+20);
  display.print(text); 
} 

void smallTextWhite(uint16_t x, uint16_t y, String text)
{
  const char* name = "FreeSans12pt7b";
  const GFXfont* f = &FreeSans12pt7b;
  
  display.setRotation(0);
  display.setFont(f);
  display.setTextColor(GxEPD_WHITE);

  display.setCursor(x, y+20);
  display.print(text); 
} 

void bigText(uint16_t x, uint16_t y, String text)
{
  const char* name = "FreeSans18pt7b";
  const GFXfont* f = &FreeSans18pt7b;
  
  display.setRotation(0);
  display.setFont(f);
  display.setTextColor(GxEPD_BLACK);

  display.setCursor(x, y+25);
  display.print(text); 
} 