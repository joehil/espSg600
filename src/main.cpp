#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

#include "config.h"

const char* mqtt_server = "192.168.0.211";

const char* clientId = "espSg600";
const char* topicRoot = clientId;             // MQTT root topic for the device, keep / at the end
const double timeout = 1800e6;

WiFiClient wifiMulti;
PubSubClient client(wifiMulti);

const byte numChars = 27;
byte rxData[numChars];   // an array to store the received data
boolean newData = false;
int lastTx;
int lastRx;
int attemps = 0;
char msg[50];
unsigned int count = 0;

void data_grab() {
  byte message[] = {0x43, 0xC0, BOX_ID, 0x00, 0x00, INVERTER_ID, 0x00, 0x00, 0x00, 0x00};
  int length = sizeof(message);

  byte sum[] = { 0x00 };
  for (byte i = 0; i < length; i++) {
    sum[0] += message[i];
  }
  byte final[length + 1];
  memcpy(final, message, length);
  memcpy(&final[length], sum, sizeof(sum));

  for (byte i = 0; i <= length + 1; i++) {
    Serial2.write(final[i]);
    delayMicroseconds(2639); // 2639
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
//  Serial.print("Message arrived [");
//  Serial.print(topic);
//  Serial.print("] ");
}

void reconnect() {
  // Loop until we're reconnected
  int loopcnt = 0;
  while (!client.connected()) {
    if (client.connect(clientId)) {
      loopcnt = 0;
      strcat(msg,"/outTopic/IP");
      client.publish(msg, WiFi.localIP().toString().c_str());
      // ... and resubscribe
      strcpy(msg,clientId);
      strcat(msg,"/inTopic");
      client.subscribe(msg);
    } else {
      // Wait 5 seconds before retrying
      delay(5000);
      loopcnt++;
      if (loopcnt > 20){
        if (WiFi.status() != WL_CONNECTED) {
          ESP.restart();
        }
      }
    }
  }
}

void setup() {
  Serial.begin(9600);
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2, false);

  Serial.println("Starting...");

  pinMode(SET_PIN, OUTPUT);
  digitalWrite(SET_PIN, HIGH);
  pinMode(CS_PIN, OUTPUT);
  digitalWrite(CS_PIN, LOW);

  // Connect WiFi
  Serial.println("Connecting to WiFi");
  WiFi.hostname("espSg600");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();

  delay(1000);

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  lastTx = millis();
  lastRx = millis();
}

void recvWithStartEndMarkers() {
    static boolean recvInProgress = false;
    static byte ndx = 0;
    int startMarker = 0x43;
    //int endMarker = 0x9F; // 0x2f
    int rc;
 
    while (Serial2.available() > 0 && !newData) {
      rc = Serial2.read();

      if (rc == startMarker)
        recvInProgress = true;
      if (recvInProgress == true) {
        rxData[ndx] = rc;
        ndx++;
        if (ndx >= numChars) {
          recvInProgress = false;
          ndx = 0;
          newData = true;
        }
        //if (rc == endMarker) {
        //  recvInProgress = false;
        //  ndx = 0;
        //  newData = true;
        //}
      }
  }
}

void showNewData() {
  if (!newData) return;

  char topic[80];
  char json[1024];

  Serial.print("Got response: ");
  for (int i = 0; i < sizeof(rxData); i++)
  {
    Serial.print(rxData[i], HEX);
    Serial.print(" ");
  }

  String box_id = String(int((rxData[I_BOX_ID] << 8) | rxData[I_BOX_ID + 1]), HEX);
  String inverter_id = String(int((rxData[I_INVERTER_ID] << 24) | (rxData[I_INVERTER_ID + 1] << 16) | (rxData[I_INVERTER_ID + 2] << 8) | rxData[I_INVERTER_ID + 3]), HEX);

  const uint32_t tempTotal = rxData[10] << 24 | rxData[11] << 16 | rxData[12] << 8 | (rxData[13] & 0xFF);
  float totalGeneratedPower = *((float*)&tempTotal);

  float dcVoltage = (rxData[15] << 8 | rxData[16]) / 100.0f;
  float dcCurrent = (rxData[17] << 8 | rxData[18]) / 100.0f;
  float dcPower = dcVoltage * dcCurrent;

  float acVoltage = (rxData[19] << 8 | rxData[20]) / 100.0f;
  float acCurrent = (rxData[21] << 8 | rxData[22]) / 100.0f;
  float acPower = acVoltage * acCurrent;
  float effe = acPower / dcPower * 100.0f;

  uint16_t temperature = rxData[26]; // not fully reversed
  
  Serial.println();
  Serial.print("Box ID: ");
  Serial.print(box_id);
  Serial.print(" Inverter ID: ");
  Serial.print(inverter_id);
  Serial.print(" Voltage DC: ");
  Serial.printf("%0.2f",dcVoltage);
  Serial.print(" Current DC: ");
  Serial.printf("%0.2f",dcCurrent);
  Serial.print(" Voltage AC: ");
  Serial.printf("%0.2f",acVoltage);
  Serial.print(" Current AC: ");
  Serial.printf("%0.2f",acCurrent);
  Serial.print(" Temperatur: ");
  Serial.print(temperature);
  Serial.printf(" Wirkungsgrad: %0.2f",effe);
  Serial.printf(" Total: %0.2f",totalGeneratedPower);
  Serial.println();

  sprintf(json,"{",json);
  sprintf(json,"%s \"dcVoltage\":%.1f,",json,dcVoltage);
  sprintf(json,"%s \"dcCurrent\":%.1f,",json,dcCurrent);
  sprintf(json,"%s \"dcPower\":%.1f,",json,dcPower);
  sprintf(json,"%s \"acVoltage\":%.1f,",json,acVoltage);
  sprintf(json,"%s \"acCurrent\":%.1f,",json,acCurrent);
  sprintf(json,"%s \"acPower\":%.1f,",json,acPower);
  sprintf(json,"%s \"efficiency\":%.1f,",json,effe);
  sprintf(json,"%s \"temperature\":%d ,",json,temperature);
  sprintf(json,"%s \"boxId\":%s,",json,box_id);
  sprintf(json,"%s \"inverterId\":%s,",json,inverter_id);
  sprintf(json,"%s \"totalPower\":%.1f }",json,totalGeneratedPower);
  sprintf(topic,"%s/data",topicRoot);
  client.publish(topic,json);      

  lastRx = millis();
  attemps = 0;
  newData = false;
}

void loop() {
  if (!client.connected()) {
    reconnect();
    count=0;
  }
  client.loop();

  if (WiFi.status() != WL_CONNECTED){
    ESP.restart();
  }

  if (lastTx + 5000 < millis() && lastRx + 60000 < millis()) { //60000
    data_grab();
    attemps++;
    lastTx = millis();
    if (attemps > 3) {
      Serial.println("No response from inverter");
      delay(2000);
      ESP.deepSleep(timeout);
      attemps = 0;
      lastRx = millis();
    }
  }
  recvWithStartEndMarkers();
  showNewData();
}
