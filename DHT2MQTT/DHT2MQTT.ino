#include <WiFiSettings.h>
#include <LittleFS.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <SPI.h>
#include <DHT.h>

#define Sprintf(f, ...) ({ char* s; asprintf(&s, f, __VA_ARGS__); String r = s; free(s); r; })

#define DHTPIN D4
#define DHTTYPE DHT22

DHT sensor(DHTPIN, DHTTYPE);

char* d_mqtt_host = "127.0.0.1";
int   d_mqtt_port = 1883;
char* d_mqtt_root = "my_mqtt_root";
char* d_connect_topic = "/debug/node_connect";
char* d_debug_topic = "/debug";
char* d_influx_topic = "/influx";

/* Configuration variables (are set by the WiFiSettings portal) */

String mqtt_host;
int mqtt_port;
String mqtt_root;
String node_name;
String temperature_topic;
String RH_topic;
String connect_topic;
String call_topic;
int interval;
int temp_offset;

bool influx_enabled;
String influx_topic, influx_temperature_measurement, influx_humidity_measurement;

/* Global variables/instances */

unsigned long last_measurement = 0;
float humidity, temp_c;

WiFiClient espClient;
PubSubClient mqttClient(espClient);
char mqtt_message[50];


void setup() {
    Serial.begin(115200);

    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);

    pinMode(D8, INPUT);

    LittleFS.begin();
    setup_wifi();
    setup_ota();

    mqttClient.setServer(mqtt_host.c_str(), mqtt_port);
    // mqttClient.setCallback(mqtt_callback);

    sensor.begin();     // initialize temperature sensor
}

void loop() {
    ArduinoOTA.handle();

    if (!mqttClient.connected()) {
        connect_mqtt();
    }
    mqttClient.loop();

    if (digitalRead(D8) == HIGH) {
        WiFiSettings.portal();
    }

    if (!(millis() % interval)){
        digitalWrite(LED_BUILTIN, LOW);

        // Reading temperature for humidity takes about 250 milliseconds!
        // Sensor readings may also be up to 2 seconds 'old' (it's a very slow sensor)
        get_temperature();

        // Temperature -> MQTT topic
        sprintf(mqtt_message, "%.2f °C", temp_c);
        mqtt_publish(temperature_topic, mqtt_message, true);

        // Temperature -> Influx
        sprintf(mqtt_message, "%s,node=\"%s\" temperature=%.2f", influx_temperature_measurement.c_str(), node_name.c_str(), temp_c);
        mqtt_publish(influx_topic, mqtt_message, false);

        // RH -> MQTT topic
        sprintf(mqtt_message, "%.2f %%", humidity);
        mqtt_publish(RH_topic, mqtt_message, true);

        // RH -> Influx
        sprintf(mqtt_message, "%s,node=\"%s\" RH=%.2f", influx_humidity_measurement.c_str(), node_name.c_str(), humidity);
        mqtt_publish(influx_topic, mqtt_message, false);

        digitalWrite(LED_BUILTIN, HIGH);
    }
}


void get_temperature() {
    last_measurement = millis();

    // Reading temperature for humidity takes about 250 milliseconds!
    // Sensor readings may also be up to 2 seconds 'old' (it's a very slow sensor)
    humidity = sensor.readHumidity();  // Read humidity (percent)
    temp_c = sensor.readTemperature() + temp_offset;  // Read temperature as Celcius

    // Check if any reads failed and exit early (to try again).
    if (isnan(humidity) || isnan(temp_c)) {
        Serial.println("Failed to read from DHT sensor!");
        return;
    } else {
        last_measurement = millis();
    }
}


bool mqtt_publish(const String topic_path, const char* message, bool retain)
{
    char topic[50]; strcpy(topic, (mqtt_root + topic_path).c_str());

    Serial.printf("publishing to %s: %s\r\n", topic, message);
    return mqttClient.publish(topic, message, retain);
}


void mqtt_callback(char* topic, byte* payload, unsigned int length) {
    payload[length] = 0;
    String message = (char*)payload;

    Serial.printf("Message arrived on topic: [%s], %s\r\n", topic, message.c_str());

    // if(message == node_name + ", temperature, c"){
    //     get_temperature();

    //     sprintf(mqtt_message, "%.2f °C");
    //     mqtt_publish(temperature_topic, mqtt_message, true);
    // } else if (message == node_name + ", humidity"){
    //     get_temperature();

    //     sprintf(mqtt_message, "%.2f%%", humidity);
    //     mqtt_publish(RH_topic, mqtt_message, true);
    // }
}


// WiFi and MQTT setup functions

void setup_wifi() {
    WiFiSettings.hostname = Sprintf("ClimateNode-%06" PRIx32, ESP.getChipId());

    mqtt_host = WiFiSettings.string("mqtt-host",      d_mqtt_host,           F("MQTT server host"));
    mqtt_port = WiFiSettings.integer("mqtt-port",     d_mqtt_port,           F("MQTT server port"));
    mqtt_root = WiFiSettings.string("mqtt-root",      d_mqtt_root,           F("MQTT topic root"));
    node_name = WiFiSettings.string("mqtt-node-name", WiFiSettings.hostname, F("MQTT node name (restart first after changing!)"));
    // connect_topic = WiFiSettings.string("mqtt-connect-topic",     d_connect_topic,       F("MQTT connect notification topic"));
    connect_topic = d_connect_topic;

    String d_temp_topic = "/temperature/" + node_name;
    String d_RH_topic   = "/humidity/" + node_name;
    temperature_topic = WiFiSettings.string("mqtt-temperature-topic", d_temp_topic, F("MQTT temperature topic"));
    RH_topic          = WiFiSettings.string("mqtt-humidity-topic",    d_RH_topic,   F("MQTT RH topic"));

    influx_enabled                 = WiFiSettings.checkbox("influx-enabled",               false,            F("Enable influx"));
    influx_topic                   = WiFiSettings.string("influx-topic",                   d_influx_topic,   F("Influx MQTT topic"));
    influx_temperature_measurement = WiFiSettings.string("influx-temperature-measurement", F("temperature"), F("Temperature influx measurement name"));
    influx_humidity_measurement    = WiFiSettings.string("influx-humidity-measurement",    F("humidity"),    F("Humidity influx measurement name"));

    interval    = WiFiSettings.integer("measure-interval",   10, 3600, 30, F("Measuring interval [s] (shorter = less accurate)"));
    temp_offset = WiFiSettings.integer("temperature-offset", -5, 5,     0, F("Temperature offset [°C]"));

    interval *= 1000;   // seconds -> milliseconds

    WiFiSettings.onPortal = []() {
        setup_ota();
    };
    WiFiSettings.onPortalWaitLoop = []() {
        ArduinoOTA.handle();

        if (!(millis() % 200))
            digitalWrite(LED_BUILTIN, LOW);
        else if (!(millis() % 100))
            digitalWrite(LED_BUILTIN, HIGH);
    };

    WiFiSettings.connect();

    Serial.print("Password: ");
    Serial.println(WiFiSettings.password);
}

// Set up OTA update
void setup_ota()
{
    ArduinoOTA.setHostname(WiFiSettings.hostname.c_str());
    ArduinoOTA.setPassword(WiFiSettings.password.c_str());
    ArduinoOTA.begin();
}

void connect_mqtt()
{
    unsigned long connection_lose_time = millis();

    while (!mqttClient.connected()) {   // Loop until connected
        Serial.print(F("Attempting MQTT connection..."));

        if (mqttClient.connect(WiFiSettings.hostname.c_str())) { // Attempt to connect
            Serial.println(F("connected"));

            // Post connect message to MQTT topic once connected
            sprintf(mqtt_message, "%s (re)connected after %.1fs", node_name.c_str(), (millis() - connection_lose_time) / 1000.0);
            mqtt_publish(connect_topic, mqtt_message, false);

        } else {
            Serial.printf("failed, rc=%d; try again in 5 seconds\r\n", mqttClient.state());
            delay(5000);  // Wait 5 seconds before retrying
        }
    }
}

