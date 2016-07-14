#include <Arduino.h>
#include <WiFiUdp.h>
#include <FS.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <SPI.h>
#include <MFRC522.h>
#include <ESP8266HTTPClient.h>


#define IP              "vps297192.ovh.net"
#define HOSTNAME        "Dock"
#define RST_PIN         4
#define SS_PIN          2

typedef enum {open = 0, closed = 1} state;

const char* ap_default_ssid = "esp8266"; ///< Default SSID.
const char* ap_default_psk = "esp8266esp8266"; ///< Default PSK.

String station_ssid = "";
String station_psk = "";
String station_id = "";

HTTPClient http;

String key = "";

ESP8266WebServer server(80);

MFRC522 mfrc522(SS_PIN, RST_PIN);  // Create MFRC522 instance

const int hall = 16;
const int motorDirPin[2] = {5, 0}; //motorDirPin[0] = 12, motorDirPin[1] = 13
const int pwmPin = 15;
const int sence = A0;

boolean ready = false;

void checkClose(void);
void close(void);

String dump_byte_array(byte *buffer, byte bufferSize) {
  String str = "";
  for (byte i = 0; i < bufferSize; i++) {
    str += String(buffer[i] < 0x10 ? "0" : "");
    str += String(buffer[i], HEX);
  }
  return str;
}

void motor(int dir){
  switch (dir) {
    case 0: //sluk både pin 12 og 13 og juice
      digitalWrite(motorDirPin[0], 0);
      digitalWrite(motorDirPin[1], 0);
      analogWrite(pwmPin, 0); // 0 juice
      break;
    case 1: // tænd pin 12 og sluk pin 13 motor kører den ene vej
      digitalWrite(motorDirPin[0], 1);
      digitalWrite(motorDirPin[1], 0);
      analogWrite(pwmPin, 426);
      break;
    case -1: // sluk pin 12 og tænd pin 13 motor kører den anden vej
      digitalWrite(motorDirPin[0], 0); // slukker motorDirPin[0] som er lig pin 12
      digitalWrite(motorDirPin[1], 1); // tænder motorDirPin[1] som er lig pin 13
      analogWrite(pwmPin, 426); // full juice
      break;
  }
}

bool loadConfig(String *ssid, String *pass, String *id)
{
  // open file for reading.
  File configFile = SPIFFS.open("/cl_conf.txt", "r");
  if (!configFile)
  {
    Serial.println("Failed to open cl_conf.txt.");

    return false;
  }

  // Read content from config file.
  String content = configFile.readString();
  configFile.close();

  content.trim();

  // Check if ther is a second line available.
  int8_t pos = content.indexOf("\r\n");
  uint8_t le = 2;
  // check for linux and mac line ending.
  if (pos == -1)
  {
    le = 1;
    pos = content.indexOf("\n");
    if (pos == -1)
    {
      pos = content.indexOf("\r");
    }
  }

  // If there is no second line: Some information is missing.
  if (pos == -1)
  {
    Serial.println("Infvalid content.");
    Serial.println(content);

    return false;
  }

  String con2 = content.substring(pos + le);
  // Check if ther is a third line available.
  int8_t pos2 = con2.indexOf("\r\n");
  uint8_t le2 = 2;
  // check for linux and mac line ending.
  if (pos2 == -1)
  {
    le2 = 1;
    pos2 = con2.indexOf("\n");
    if (pos2 == -1)
    {
      pos2 = con2.indexOf("\r");
    }
  }

  // If there is no second line: Some information is missing.
  if (pos2 == -1)
  {
    Serial.println("Infvalid content. no third line");
    Serial.println(content);

    return false;
  }
  // Store SSID and PSK into string vars.
  *ssid = content.substring(0, pos);
  *pass = con2.substring(0, pos2);
  *id = con2.substring(pos2 + le2);

  ssid->trim();
  pass->trim();
  id->trim();

  Serial.println(*ssid);
  Serial.println(*pass);
  Serial.println(*id);

  return true;
} // loadConfig


bool saveConfig(String *ssid, String *pass, String *id)
{
  // Open config file for writing.
  File configFile = SPIFFS.open("/cl_conf.txt", "w");
  if (!configFile)
  {
    Serial.println("Failed to open cl_conf.txt for writing");

    return false;
  }

  // Save SSID and PSK.
  configFile.println(*ssid);
  configFile.println(*pass);
  configFile.println(*id);

  configFile.close();

  return true;
} // saveConfig


void handleRoot() {
    String respons = "<html><body><form action='/change'>ssid:<br><input type='text' value='" + station_ssid  +  "' name='ssid'></input><br>password:<br><input type='password' name='password' value='" + station_psk + "'></input><br/>Station id:<br/><input type='text' name='id' value='"+ station_id +"'></input><br><input type='submit' value'change'></input></form></body></html>";
    server.send(200, "text/html", respons);
}

void changeFunc() {
    String newSsid = server.arg( "ssid" );
    String newPass = server.arg( "password" );
    String newId = server.arg("id");

    bool test = saveConfig(&newSsid, &newPass, &newId);
    if(test) {
        server.send(200, "text/html", "password saved. restarting"); ESP.restart();
        return;
    }
    server.send(200, "text/html", "sonthing went wrong.");
}

void handleNotFound(){
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void setup(void){
  pinMode(hall,INPUT);
  pinMode(motorDirPin[0], OUTPUT);
  pinMode(motorDirPin[1], OUTPUT);
  pinMode(pwmPin, OUTPUT);

  Serial.begin(115200);
  Serial.println("");

  server.on("/", handleRoot);
  server.on("/change", changeFunc);

  // Set Hostname.
  String hostname(HOSTNAME);
  WiFi.hostname(hostname);

  // Print hostname.
  Serial.println("Hostname: " + hostname);
  //Serial.println(WiFi.hostname());


  // Initialize file system.
  if (!SPIFFS.begin())
  {
    Serial.println("Failed to mount file system");
    return;
  }

  // Load wifi connection information.
  if (! loadConfig(&station_ssid, &station_psk, &station_id))
  {
    station_ssid = "";
    station_psk = "";

    Serial.println("No WiFi connection information available.");
  }

  // Check WiFi connection
  // ... check mode
  if (WiFi.getMode() != WIFI_STA)
  {
    WiFi.mode(WIFI_STA);
    delay(10);
  }

  // ... Compare file config with sdk config.
  if (WiFi.SSID() != station_ssid || WiFi.psk() != station_psk)
  {
    Serial.println("WiFi config changed.");

    // ... Try to connect to WiFi station.
    WiFi.begin(station_ssid.c_str(), station_psk.c_str());

    // ... Pritn new SSID
    Serial.print("new SSID: ");
    Serial.println(WiFi.SSID());

    // ... Uncomment this for debugging output.
    //WiFi.printDiag(Serial);
  }
  else
  {
    // ... Begin with sdk config.
    WiFi.begin();
  }

  Serial.println("Wait for WiFi connection.");

  // ... Give ESP 10 seconds to connect to station.
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000)
  {
    Serial.write('.');
    //Serial.print(WiFi.status());
    delay(500);
  }
  Serial.println();

  // Check connection
  if(WiFi.status() == WL_CONNECTED)
  {
    // ... print IP Address
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    ready = true;
  }
  else
  {
    Serial.println("Can not connect to WiFi station. Go into AP mode.");
    ready = false;
    // Go into software AP mode.
    WiFi.mode(WIFI_AP);

    delay(10);

    WiFi.softAP(ap_default_ssid, ap_default_psk);

    Serial.print("IP address: ");
    Serial.println(WiFi.softAPIP());
  }

  String hostname2(HOSTNAME);
  WiFi.hostname(hostname2);

  // Start OTA server.
  ArduinoOTA.setHostname((const char *)hostname.c_str());
  ArduinoOTA.begin();
  server.begin();

	SPI.begin();			// Init SPI bus
	mfrc522.PCD_Init();		// Init MFRC522
	mfrc522.PCD_DumpVersionToSerial();	// Show details of PCD - MFRC522 Card Reader details
}

long last = 0;
long lastCard = 0;
int dir = -1;
state tablet = open;
long tim = millis();
void (*closeFunc)(void) = checkClose;


void close(void) {
    if(millis() > (tim + 1500)) {
        motor(1);
        dir = 1;
        last = millis();
        closeFunc = checkClose;
        while( mfrc522.PICC_IsNewCardPresent() ) {
            mfrc522.PICC_ReadCardSerial();
        }
        key = "";
    }
}

void checkClose() {
    tim = millis();
    closeFunc = close;
}

boolean checkKey(String key) {
    http.begin("http://" + String(IP) + "/validate?id=" + station_id + "&rfid=" + key);
    http.GET();
    String str = http.getString();
    http.end();
    Serial.println(str);
    if(str.indexOf("ok") != -1) { return true; }
    return false;
}

void loop(void){
  server.handleClient();
  ArduinoOTA.handle();

  int senceValue = analogRead(sence);
  int swValue = 0;

  if(key != String("")) {
    Serial.println(key);
    if (checkKey(key))
        swValue = 1;
    while( mfrc522.PICC_IsNewCardPresent() ) {
          mfrc522.PICC_ReadCardSerial();
    }
    key = "";
  }

  if(tablet == open && !digitalRead(hall) && dir == -1 && millis() > (last + 5000)) {
    closeFunc();
  }

  else if(tablet == closed && swValue && dir) {
    motor(-1);
    dir = -1;
    last = millis();
    key = "";
  }

  if(senceValue > 850 && millis() > (last + 500)) {
      if(tablet == open) {
        tablet = closed;
      }
      else {
        tablet = open;
      }
      motor(0);
      last = millis();


  }

  if (tablet == open) {
    while( mfrc522.PICC_IsNewCardPresent() ) {
        mfrc522.PICC_ReadCardSerial();
    }
    key = "";
  }


  if ( !mfrc522.PICC_IsNewCardPresent() || (!(millis() > (2000 + lastCard))  )) {
    return;
  }
  // Select one of the cards
  if ( ! mfrc522.PICC_ReadCardSerial()) {
    return;
  }

  key = dump_byte_array(mfrc522.uid.uidByte, mfrc522.uid.size);
  lastCard = millis();

}
