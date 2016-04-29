#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiUdp.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <TimeLib.h>

#define RELAY1_PIN D5
// don't change these constants, override them in conf.h

#define ONE_WIRE_BUS D4  // DS18B20 pin
#define WIFI_AP_NAME "Insert AP Name"
#define WIFI_AP_PASSWORD "Insert AP Password"
#define API_BASE_URL "Insert Base URL"
#define API_DEVICE_NAME "Insert Device Name"
#define API_SEND_EVERY_SECONDS 15
#define DEBUG_OUTPUT true
#define PROBE1_ADDRESS { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define PROBE2_ADDRESS { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define TIME_IS_STALE_AFTER_SECONDS 3600

#include "conf.h"

const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets
#define NTP_LOCAL_PORT 4433
WiFiUDP udp;

unsigned long lastEpoch = 0, lastEpochMillis = 0;

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature DS18B20(&oneWire);


// send an NTP request to the time server at the given address
unsigned long sendNTPpacket(IPAddress& address)
{
  Serial.println("sending NTP packet...");
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}

unsigned long fetchEpoch() {
  //get a random server from the pool
  IPAddress timeServerIP;
  WiFi.hostByName("time.nist.gov", timeServerIP); 
  sendNTPpacket(timeServerIP); // send an NTP packet to a time server
  // wait to see if a reply is available
  delay(1000);
  
  int cb = udp.parsePacket();
  if (!cb) {
    Serial.println("no NTP packet yet");
    return 0;
  }
  else {
    Serial.print("NTP packet received, length=");
    Serial.println(cb);
    // We've received a packet, read the data from it
    udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

    //the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, esxtract the two words:

    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;
    Serial.print("Seconds since Jan 1 1900 = " );
    Serial.println(secsSince1900);

    // now convert NTP time into everyday time:
    // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
    const unsigned long seventyYears = 2208988800UL;
    // subtract seventy years:
    unsigned long epoch = secsSince1900 - seventyYears;
    return epoch;
  }
}

String getISO8601() {
  if (lastEpoch == 0 || ((millis() - lastEpochMillis) > ((TIME_IS_STALE_AFTER_SECONDS)*1000) )) {
    lastEpoch = fetchEpoch();
    lastEpochMillis = millis();
  }
  
  if (lastEpoch>0) {
    unsigned long epoch = lastEpoch + ((millis() - lastEpochMillis)/1000);
    int y = year(epoch);
    int m = month(epoch);
    int d = day(epoch);
    String iso8601 = "" + String(year(epoch)) + "-" + zeroPad(month(epoch)) + "-" + zeroPad(day(epoch)) + "T" + zeroPad(hour(epoch)) + ":" + zeroPad(minute(epoch)) + ":" + zeroPad(second(epoch)) + ".000";
    return iso8601;
  } else {
    return "";
  }
}

String zeroPad(int in) {
  String res = "";
  if (in<10) res += "0";
  res += in;
  return res;
}
String fullUrlWrite = String(API_BASE_URL) + "/cloud/api/site/" + String(API_DEVICE_NAME) + "/EMS";
String fullUrlRead = String(API_BASE_URL) + "/cloud/api/site/" + String(API_DEVICE_NAME) + "/merged";

void setup() {
  pinMode(RELAY1_PIN, OUTPUT);
  digitalWrite(RELAY1_PIN, HIGH);
  
  Serial.begin(115200);
  Serial.setDebugOutput(DEBUG_OUTPUT);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_AP_NAME, WIFI_AP_PASSWORD);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nWiFi connected");

  Serial.print("Local IP: ");
  Serial.println(WiFi.localIP());

  Serial.println("Starting UDP");
  udp.begin(NTP_LOCAL_PORT);
  Serial.print("Local port: ");
  Serial.println(udp.localPort());

  byte addr[8];
  Serial.println("Looking for 1-Wire devices...");
  while(oneWire.search(addr)) {
    Serial.print("\nFound 1-Wire device with address: ");
    for(int i = 0; i < 8; i++) {
      Serial.print("0x");
      if (addr[i] < 16) {
        Serial.print('0');
      }
      Serial.print(addr[i], HEX);
      if (i < 7) {
        Serial.print(", ");
      }
    }
  }
  Serial.println("\n1-Wire scan complete");
  oneWire.reset_search();
  DS18B20.begin();

  Serial.print("PUT URL: ");
  Serial.println(fullUrlWrite);

  Serial.print("\nGET URL: ");
  Serial.println(fullUrlRead);
}

unsigned long lastSentMillis = 0;

DeviceAddress Probe01 = PROBE1_ADDRESS;
DeviceAddress Probe02 = PROBE2_ADDRESS;

String getXML() {
  HTTPClient http;
  
  http.begin(fullUrlRead);
  int httpGetCode = http.GET();
  if(httpGetCode > 0) {
    // HTTP header has been send and Server response header has been handled
    Serial.printf("GET returned code: %d\n", httpGetCode);

    // file found at server
    if(httpGetCode == HTTP_CODE_OK) {
      String payload = http.getString();
      http.end();
      Serial.println("GET Received:");
      Serial.println(payload);
      Serial.println();
      return payload;
    }
  }
  Serial.printf("GET failed, error: %s\n", http.errorToString(httpGetCode).c_str());
  http.end();
  return "";
}

String firstGet = "";
float cv = 0;
float ratSpFixedIn = 10.10;

void loop() {
  HTTPClient http;
  
  if ( (millis() - lastSentMillis) < ( (API_SEND_EVERY_SECONDS) * 1000 ) ) {
    return;
  }

  String preGet = getXML();

  if (preGet == "") {
    delay(1000);
    return;
  }

  int idx = preGet.indexOf("RatSpFixed");
  if (idx >=0) {
    String chunk = preGet.substring(idx+40, idx+40+10);
    int idx2 = chunk.indexOf("\"");
    if (idx2 >=0) {
      String chunk2 = chunk.substring(0,idx2);
      ratSpFixedIn = chunk2.toFloat();
    }
  }

  Serial.print("RAT SP IN: ");
  Serial.println(String(ratSpFixedIn));

  String curDate = getISO8601();
  if (curDate == "") {
    delay(500);
    return;
  }
  
  DS18B20.requestTemperatures();

  float rat = DS18B20.getTempF(Probe01);
  float sat = DS18B20.getTempF(Probe02);
    
  Serial.print("RAT Temp: ");
  Serial.println(rat);
  Serial.print("SAT Temp: ");
  Serial.println(sat);

  if (rat > ratSpFixedIn) {
    cv = 100;
    digitalWrite(RELAY1_PIN, LOW);
    Serial.println("Driving relay LOW");
  } else {
    cv = 0;
    digitalWrite(RELAY1_PIN, HIGH);
    Serial.println("Driving relay HIGH");
  }
  float ssp = 0; float satSpFromEms = 0; float sspSpFromEms = 0; String supplyFanProof = "true"; float supplyFanSpeed = 0; float chwSupTemp = 0;

  String xml = "<LCS timestamp=\"" + curDate + "\" vendorVersion=\"2016.4.8.999\">" + 
    "<data n=\"\"><p n=\"" + String(API_DEVICE_NAME) + "\">" +
      "<p n=\"IntegrationPoints\">" + 
        "<p n=\"Ahu03\">" +
          "<p n=\"Rat\"><p n=\"in10\"><p n=\"value\" v=\"" + String(rat) + "\"/></p></p>" + 
          "<p n=\"Sat\"><p n=\"in10\"><p n=\"value\" v=\"" + String(sat) + "\"/></p></p>" + 
          "<p n=\"Ssp\"><p n=\"in10\"><p n=\"value\" v=\"" + String(ssp) + "\"/></p></p>" + 
          "<p n=\"SatSpFromEMS\"><p n=\"in10\"><p n=\"value\" v=\"" + String(satSpFromEms) + "\"/></p></p>" + 
          "<p n=\"SspSpFromEMS\"><p n=\"in10\"><p n=\"value\" v=\"" + String(sspSpFromEms) + "\"/></p></p>" + 
          "<p n=\"Cv\"><p n=\"in10\"><p n=\"value\" v=\"" + String(cv) + "\"/></p></p>" + 
          "<p n=\"SfPrf\"><p n=\"in10\"><p n=\"value\" v=\"" + supplyFanProof + "\"/></p></p>" + 
          "<p n=\"SfSpd\"><p n=\"in10\"><p n=\"value\" v=\"" + String(supplyFanSpeed) + "\"/></p></p>" + 
        "</p>" +
        "<p n=\"Plant\">" +
          "<p n=\"plantInputs\">" +
            "<p n=\"ChwSupTemp\"><p n=\"in10\"><p n=\"value\" v=\"" + String(chwSupTemp) + "\"/></p></p>" +
          "</p>" +
        "</p>" +
      "</p>" + 
      "<p n=\"Graphics\">" +
        "<p n=\"Ahu03\">" +
          "<p n=\"Points\">" +
            "<p n=\"common\">" +
              "<p n=\"RatSpFixed\"><p n=\"in10\"><p n=\"value\" v=\"" + String(ratSpFixedIn) + "\"/></p></p>" +
            "</p>" +
          "</p>" +
        "</p>" +
      "</p>" +
    "</p></data></LCS>";

  Serial.println("PUTting XML:");
  Serial.println(xml);
  Serial.println();
  
  http.begin(fullUrlWrite);
  http.addHeader("Content-Type", "text/xml");
  int httpPutCode = http.sendRequest("PUT", xml);
  Serial.printf("PUT returned code: %d\n", httpPutCode);
  //if(httpPutCode == HTTP_CODE_OK) {
    String payload = http.getString();
    Serial.println("PUT Received:");
    Serial.println(payload);
    Serial.println();
  //}
  http.end();

  lastSentMillis = millis();
}

