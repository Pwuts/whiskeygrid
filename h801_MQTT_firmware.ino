#include <ESP8266WiFi.h>
#include <PubSubClient.h>

WiFiClient espLampje;
PubSubClient client(espLampje);

//Pin definitions
//Debug LEDs
const int redLed = 5;
const int greenLed = 1;

//RGBWW control FETs
const int redFET = 15;
const int greenFET = 13;
const int blueFET = 12;
const int w1FET = 14;
const int w2FET = 4;

//Some info needed for proper operation
const char* chipid = "h801";

const char* mqtt_server = "192.168.1.31";
const char* ssid    = "whiskeygrid";
const char* wpa2key = "7A6U6QM0RQ0Z";

void setup() {
  //Serial.begin(115200);
  //Serial.println("Initializing..");
  pinMode(led, OUTPUT);
  WiFi.begin(ssid, wpa2key);
  client.setServer(mqtt_server, 1883);
  client.setCallback(mqttCallback);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
}

void reconnect() {
  int reconnectAttempts = 0;
  
  while (!client.connected()) {
    reconnectAttempts++;
    //Serial.print("Attempting to reestablish connection to server.. attempt ");
    //Serial.println(reconnectAttempts);
    if(client.connect(chipid)) { //try to reconnect, check if succeeds
      //Serial.println("connected"); //debug info --> serial
      client.publish("whiskeygrid/debug", chipid); //debug info --> MQTT
      client.subscribe("whiskeygrid/#"); //resubscribe to listen for incoming messages again
    } else {
      //Serial.print("failed, rc=");
      //Serial.println(client.state());
      delay(1000);
    }
    if (reconnectAttempts == 60) {
      ESP.restart();
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  char bericht[50] = "";
 
  //Serial.print("Hee ik hoor wat in [");
  //Serial.print(topic);
  //Serial.print("] ");
  for (int i = 0; i < length; i++) {
    //Serial.print((char)payload[i]);
  }
  //Serial.println();
 
  for (uint8_t pos = 0; pos < length; pos++) {
    bericht[pos] = payload[pos];
  }
 
  // Lets select a topic/payload handler
  // Some topics (buttons for example) don't need a specific payload handled, just a reaction to the topic. Saves a lot of time!

  if (strcmp(topic, "whiskeygrid/rgb/1") == 0 || strcmp(topic, "whiskeygrid/rgb") == 0) {
    if (strcmp(bericht, "aan") == 0) {
      digitalWrite(led, HIGH);
      //Serial.println("lampje aan :)");
    }
    if (strcmp(bericht, "uit") == 0) {
      digitalWrite(led, LOW);
      //Serial.println("lampje uit :(");
    }
  }
}

