#include <WiFiSettings.h>
#include <LittleFS.h>
#include <ArduinoOTA.h>
#include <MQTT.h>
#include <Wire.h>
#include <SPI.h>
#include <DHT.h>

#define Sprintf(f, ...) ({ char* s; asprintf(&s, f, __VA_ARGS__); String r = s; free(s); r; })

/* These forward declarations are not added by the Arduino preprocessor because they have default
 * arguments (retain and qos). See https://github.com/arduino/arduino-preprocessor/issues/12 */
bool mqtt_publish(const String &topic_path, const String &message, bool retain = true, int qos = 0);


/* Device specific configuration */

#define PORTAL_TRIGGER_PIN D8

#define DHT_PIN D4
#define DHT_TYPE DHT22

/* Default configuration */

const char* d_mqtt_host = "127.0.0.1";
const char* d_mqtt_root = "my_mqtt_root";
const char* d_connect_topic = "/debug/node_connect";
const char* d_debug_topic = "/debug";
const char* d_influx_topic = "/influx/climate";

/* Configuration variables (are set by the WiFiSettings portal) */

String mqtt_host;
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

float temp_c, humidity;

DHT sensor(DHT_PIN, DHT_TYPE);

WiFiClient espClient;
MQTTClient mqttClient;


void setup()
{
    Serial.begin(115200);

    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);

    pinMode(PORTAL_TRIGGER_PIN, INPUT);

    LittleFS.begin();
    setup_wifi();
    setup_ota();

    mqttClient.begin(mqtt_host.c_str(), espClient);
    // mqttClient.onMessage(mqtt_callback);
    connect_mqtt();

    sensor.begin();     // initialize DHT22
}

void loop()
{
    static bool first_measurement = true;
    static unsigned long last_measurement_time = 0;

    ArduinoOTA.handle();
    mqttClient.loop();

    if (!mqttClient.connected()) {
        connect_mqtt();
    }

    if (digitalRead(PORTAL_TRIGGER_PIN) == HIGH) {
        WiFiSettings.portal();
    }

    if ((millis() > last_measurement_time + interval) || first_measurement) {
        last_measurement_time = millis();

        digitalWrite(LED_BUILTIN, LOW);

        /* Reading temperature for humidity takes about 250 milliseconds!
         * Sensor readings may also be up to 2 seconds 'old' (it's a very slow sensor) */
        get_measurement();

        mqtt_publish(temperature_topic, Sprintf("%.1f °C", temp_c));
        mqtt_publish(RH_topic,          Sprintf("%.1f %%RH", humidity));
        influx_publish(influx_temperature_measurement, Sprintf("temperature=%.1f", temp_c), "sensor=\"DHT22\"");
        influx_publish(influx_humidity_measurement,    Sprintf("RH=%.1f", humidity), "sensor=\"DHT22\"");

        Serial.println();
        digitalWrite(LED_BUILTIN, HIGH);
    }

    first_measurement = false;
}


void get_measurement()
{
    // Reading temperature for humidity takes about 250 milliseconds!
    // Sensor readings may also be up to 2 seconds 'old' (it's a very slow sensor)
    temp_c = sensor.readTemperature() + temp_offset;  // Read temperature as Celcius
    humidity = sensor.readHumidity();  // Read humidity (percent)

    Serial.printf("DHT22: %.1f°C\r\n", temp_c);
    Serial.printf("DHT22: %.1f%%RH\r\n", humidity);

    // Check if any reads failed and exit early (to try again).
    if (isnan(humidity) || isnan(temp_c)) {
        Serial.println("Failed to read from DHT sensor!");
        return;
    }
}


bool mqtt_publish(const String &topic_path, const String &message, bool retain, int qos)
{
    String topic = mqtt_root + topic_path;

    Serial.printf("publishing to %s: %s\r\n", topic.c_str(), message.c_str());
    return mqttClient.publish(topic, message, retain, qos);
}


void influx_publish(const String &measurement, const String &fields, const String &tags)
{
    String influx_line = Sprintf("%s,node=\"%s\"%s%s ", measurement.c_str(), node_name.c_str(), tags.length() ? "," : "", tags.c_str()) + fields;
    mqtt_publish(influx_topic, influx_line, false, 1);
}

void mqtt_callback(const String &topic, const String &payload)
{
    Serial.printf("Message arrived on topic: [%s], %s\r\n", topic.c_str(), payload.c_str());

    // if(message == node_name + ", temperature, c"){
    //     dht22_get_measurement();

    //     mqtt_publish(temperature_topic, Sprintf("%.1f °C", dht_temp));
    // } else if (message == node_name + ", humidity"){
    //     dht22_get_measurement();

    //     mqtt_publish(RH_topic, Sprintf("%.1f %%RH", humidity));
    // }
}


// WiFi and MQTT setup functions

void setup_wifi()
{
    WiFiSettings.hostname = Sprintf("ClimateNode-%06" PRIx32, ESP.getChipId());

    mqtt_host = WiFiSettings.string("mqtt-host",      d_mqtt_host,           F("MQTT server host"));
    mqtt_root = WiFiSettings.string("mqtt-root",      d_mqtt_root,           F("MQTT topic root"));
    node_name = WiFiSettings.string("mqtt-node-name", WiFiSettings.hostname, F("MQTT node name (restart first after changing!)"));
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

    Serial.print(F("Attempting MQTT connection..."));
    while (!mqttClient.connect(node_name.c_str())) {   // Loop until connected
        Serial.print('.');
        delay(500);

        // start portal after 30 seconds without connectivity
        if (millis() - connection_lose_time > 30e3) WiFiSettings.portal();
    }

    Serial.println(F("connected"));

    // Post connect message to MQTT topic once connected
    mqtt_publish(connect_topic, Sprintf("%s (re)connected after %.1fs", node_name.c_str(), (millis() - connection_lose_time) / 1000.0), false);

    Serial.println();
}
