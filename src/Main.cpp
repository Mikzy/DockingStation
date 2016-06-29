#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <SPI.h>
#include <MFRC522.h>

#define RST_PIN         4
#define SS_PIN          2

typedef enum {open = 0, closed = 1} state;

const char* ssid = "wireless";
const char* password = "oklahoma";

char key[16];

ESP8266WebServer server(80);

MFRC522 mfrc522(SS_PIN, RST_PIN);  // Create MFRC522 instance

const int hall = 16;
const int motorDirPin[2] = {5, 0}; //motorDirPin[0] = 12, motorDirPin[1] = 13
const int pwmPin = 15;
const int sence = A0;

void checkClose(void);
void close(void);

void dump_byte_array(byte *buffer, byte bufferSize) {
  for (byte i = 0; i < bufferSize; i++) {
    key[i] = buffer[i];
  }
}

void motor(int dir){
  switch (dir) {
    case 0: //sluk både pin 12 og 13 og juice
      digitalWrite(motorDirPin[0], 0);
      digitalWrite(motorDirPin[1], 0);
      digitalWrite(pwmPin, 0); // 0 juice
      break;
    case 1: // tænd pin 12 og sluk pin 13 motor kører den ene vej
      digitalWrite(motorDirPin[0], 1);
      digitalWrite(motorDirPin[1], 0);
      digitalWrite(pwmPin, 1);
      break;
    case -1: // sluk pin 12 og tænd pin 13 motor kører den anden vej
      digitalWrite(motorDirPin[0], 0); // slukker motorDirPin[0] som er lig pin 12
      digitalWrite(motorDirPin[1], 1); // tænder motorDirPin[1] som er lig pin 13
      digitalWrite(pwmPin, 1); // full juice
      break;
  }
}

void handleRoot() {
  server.send(200, "text/plain", "hello from esp8266!");
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
  WiFi.begin(ssid, password);
  Serial.println("");

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  if (MDNS.begin("esp8266")) {
    Serial.println("MDNS responder started");
  }

  server.on("/", handleRoot);

  server.on("/inline", [](){
    server.send(200, "text/plain", "this works as well");
  });

  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started");
  String hostname = "motor";
  ArduinoOTA.setHostname((const char *)hostname.c_str());
  ArduinoOTA.begin();

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
        memset(key, 0, 16);
    }
}

void checkClose() {
    tim = millis();
    closeFunc = close;
}

void loop(void){
  server.handleClient();
  ArduinoOTA.handle();

  int senceValue = analogRead(sence);
  int swValue = 0;

  if(String(key) != String("")) {
    swValue = 1;
  }

  if(tablet == open && !digitalRead(hall) && dir == -1 && millis() > (last + 5000)) {
    closeFunc();
  }

  else if(tablet == closed && swValue && dir) {
    motor(-1);
    dir = -1;
    last = millis(); 
    memset(key, 0, 16);
  }

  if(senceValue > 110 && millis() > (last + 500)) {
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
    memset(key, 0, 16);
  }


  if ( !mfrc522.PICC_IsNewCardPresent() || (!(millis() > (2000 + lastCard))  )) {
    return;
  }
  // Select one of the cards
  if ( ! mfrc522.PICC_ReadCardSerial()) {
    return;
  }

  dump_byte_array(mfrc522.uid.uidByte, mfrc522.uid.size);
  lastCard = millis();
  
}
