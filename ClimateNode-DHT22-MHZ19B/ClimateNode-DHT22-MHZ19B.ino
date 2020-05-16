#include <WiFiSettings.h>
#include <LittleFS.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <SPI.h>
#include <DHT.h>
#include <MHZ19.h>
#include <SoftwareSerial.h>

#define Sprintf(f, ...) ({ char* s; asprintf(&s, f, __VA_ARGS__); String r = s; free(s); r; })

/* Device specific configuration */

#define PORTAL_TRIGGER_PIN D8

#define DHT_PIN D4
#define DHT_TYPE DHT22

#define MHZ19_TX_PIN D1
#define MHZ19_RX_PIN D2
SoftwareSerial mhzSerial(MHZ19_RX_PIN, MHZ19_TX_PIN);
MHZ19 mhz19;

/* Default configuration */

const char* d_mqtt_host = "127.0.0.1";
const int   d_mqtt_port = 1883;
const char* d_mqtt_root = "my_mqtt_root";
const char* d_connect_topic = "/debug/node_connect";
const char* d_debug_topic = "/debug";
const char* d_influx_topic = "/influx";

/* Configuration variables (are set by the WiFiSettings portal) */

String mqtt_host;
int mqtt_port;
String mqtt_root;
String node_name;
String temperature_topic;
String RH_topic;
String co2_topic;
String connect_topic;
String call_topic;
int dht22_interval;
int mhz19_interval;
int temp_offset;

bool influx_enabled;
String influx_topic, influx_temperature_measurement, influx_humidity_measurement, influx_co2_measurement;

/* Global variables/instances */

float dht_temp, humidity;
int co2_ppm;
float mhz19_temp;

DHT dht22(DHT_PIN, DHT_TYPE);

WiFiClient espClient;
PubSubClient mqttClient(espClient);


void setup()
{
    Serial.begin(115200);

    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);

    pinMode(PORTAL_TRIGGER_PIN, INPUT);

    LittleFS.begin();
    setup_wifi();
    setup_ota();

    mqttClient.setServer(mqtt_host.c_str(), mqtt_port);
    // mqttClient.setCallback(mqtt_callback);

    // initialize temperature/humidity sensor
    dht22.begin();

    // initialize CO2 sensor
    mhzSerial.begin(9600);
    mhz19.begin(mhzSerial);
    mhz19.autoCalibration();
}

void loop()
{
    static bool first_measurement = true;
    static unsigned long last_dht22_measurement_time = 0, last_mhz19_measurement_time = 0;

    ArduinoOTA.handle();

    if (!mqttClient.connected()) {
        connect_mqtt();
    }
    mqttClient.loop();

    if (digitalRead(PORTAL_TRIGGER_PIN) == HIGH) {
        WiFiSettings.portal();
    }

    if ((millis() > last_dht22_measurement_time + dht22_interval) || first_measurement) {
        last_dht22_measurement_time = millis();

        digitalWrite(LED_BUILTIN, LOW);

        /* Reading temperature for humidity takes about 250 milliseconds!
         * Sensor readings may also be up to 2 seconds 'old' (it's a very slow sensor) */
        dht22_get_measurement();

        mqtt_publish(temperature_topic, Sprintf("%.1f °C", dht_temp).c_str(), true);
        mqtt_publish(RH_topic, Sprintf("%.1f %%RH", humidity).c_str(), true);
        influx_publish(influx_temperature_measurement, Sprintf("temperature=%.1f", dht_temp), "sensor=\"DHT22\"");
        influx_publish(influx_humidity_measurement, Sprintf("RH=%.1f", humidity), "sensor=\"DHT22\"");

        Serial.println();
        digitalWrite(LED_BUILTIN, HIGH);
    }

    if ((millis() > last_mhz19_measurement_time + mhz19_interval) || first_measurement) {
        last_mhz19_measurement_time = millis();

        digitalWrite(LED_BUILTIN, LOW);

        /* Get latest temperature and CO2 readings from MH-Z19 sensor */
        mhz19_get_measurement();

        mqtt_publish(co2_topic, Sprintf("%d ppm", co2_ppm).c_str(), true);
        // mqtt_publish(temperature_topic, Sprintf("%.1f °C"), true);
        influx_publish(influx_co2_measurement, Sprintf("co2=%d", co2_ppm), "sensor=\"MH-Z19\"");
        influx_publish(influx_temperature_measurement, Sprintf("temperature=%.1f", mhz19_temp), "sensor=\"MH-Z19\"");

        Serial.println();
        digitalWrite(LED_BUILTIN, HIGH);
    }

    first_measurement = false;
}


void dht22_get_measurement()
{
    // Reading temperature for humidity takes about 250 milliseconds!
    // Sensor readings may also be up to 2 seconds 'old' (it's a very slow sensor)
    dht_temp = dht22.readTemperature() + temp_offset;  // Read temperature as Celcius
    humidity = dht22.readHumidity();  // Read humidity (percent)

    Serial.printf("DHT22: %.1f°C\r\n", dht_temp);
    Serial.printf("DHT22: %.1f%%RH\r\n", humidity);

    // Check if any reads failed and exit early (to try again).
    if (isnan(humidity) || isnan(dht_temp)) {
        Serial.println("Failed to read from DHT sensor!");
        return;
    }
}

void mhz19_get_measurement() {
    int i = 0;
    do {    // retry until good reading
        co2_ppm = mhz19.getCO2();
        delay(1); i++;
    } while (co2_ppm == 0);

    mhz19_temp = mhz19.getTemperature(true);

    Serial.printf("MH-Z19: %d ppm (attempt %d)\r\n", co2_ppm, i);
    Serial.printf("MH-Z19: %.1f°C\r\n", mhz19_temp);
}


bool mqtt_publish(const String topic_path, const char* message, bool retain)
{
    char topic[80]; strcpy(topic, (mqtt_root + topic_path).c_str());

    Serial.printf("publishing to %s: %s\r\n", topic, message);
    return mqttClient.publish(topic, message, retain);
}


void influx_publish(const String measurement, const String fields, const String tags)
{
    String influx_line = Sprintf("%s,node=\"%s\"%s%s ", measurement.c_str(), node_name.c_str(), tags.length() ? "," : "", tags.c_str()) + fields;
    mqtt_publish(influx_topic, influx_line.c_str(), false);
}

void mqtt_callback(char* topic, byte* payload, unsigned int length)
{
    payload[length] = 0;
    String message = (char*)payload;

    Serial.printf("Message arrived on topic: [%s], %s\r\n", topic, message.c_str());

    // if(message == node_name + ", temperature, c"){
    //     dht22_get_measurement();

    //     mqtt_publish(temperature_topic, Sprintf("%.1f °C", dht_temp).c_str(), true);
    // } else if (message == node_name + ", humidity"){
    //     dht22_get_measurement();

    //     mqtt_publish(RH_topic, Sprintf("%.1f %%RH", humidity).c_str(), true);
    // }
}


// WiFi and MQTT setup functions

void setup_wifi()
{
    WiFiSettings.hostname = Sprintf("ClimateNode-%06" PRIx32, ESP.getChipId());

    mqtt_host = WiFiSettings.string("mqtt-host",      d_mqtt_host,           F("MQTT server host"));
    mqtt_port = WiFiSettings.integer("mqtt-port",     d_mqtt_port,           F("MQTT server port"));
    mqtt_root = WiFiSettings.string("mqtt-root",      d_mqtt_root,           F("MQTT topic root"));
    node_name = WiFiSettings.string("mqtt-node-name", WiFiSettings.hostname, F("MQTT node name (restart first after changing!)"));
    connect_topic = d_connect_topic;

    String d_temp_topic = "/temperature/" + node_name;
    String d_RH_topic   = "/humidity/" + node_name;
    String d_co2_topic  = "/co2/" + node_name;
    temperature_topic = WiFiSettings.string("mqtt-temperature-topic", d_temp_topic, F("MQTT temperature topic"));
    RH_topic          = WiFiSettings.string("mqtt-humidity-topic",    d_RH_topic,   F("MQTT RH topic"));
    co2_topic         = WiFiSettings.string("mqtt-co2-topic",         d_co2_topic,  F("MQTT CO2 topic"));

    influx_enabled                 = WiFiSettings.checkbox("influx-enabled",               false,            F("Enable influx"));
    influx_topic                   = WiFiSettings.string("influx-topic",                   d_influx_topic,   F("Influx MQTT topic"));
    influx_temperature_measurement = WiFiSettings.string("influx-temperature-measurement", F("temperature"), F("Temperature influx measurement name"));
    influx_humidity_measurement    = WiFiSettings.string("influx-humidity-measurement",    F("humidity"),    F("Humidity influx measurement name"));
    influx_co2_measurement         = WiFiSettings.string("influx-co2-measurement",         F("co2"),         F("CO2 influx measurement name"));

    dht22_interval = WiFiSettings.integer("dht22-measure-interval", 10, 3600, 30, F("Measuring interval [s] for DHT22 sensor (shorter = less accurate)"));
    temp_offset    = WiFiSettings.integer("temperature-offset",     -5, 5,     0, F("Temperature offset [°C] for DHT22"));

    mhz19_interval = WiFiSettings.integer("mhz19-measure-interval",  1, 3600, 10, F("Measuring interval [s] for MH-Z19 sensor"));

    dht22_interval *= 1000;   // seconds -> milliseconds
    mhz19_interval *= 1000;

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
            mqtt_publish(connect_topic, Sprintf("%s (re)connected after %.1fs", node_name.c_str(), (millis() - connection_lose_time) / 1000.0).c_str(), false);

            Serial.println();
        } else {
            Serial.printf("failed, rc=%d; try again in 5 seconds\r\n", mqttClient.state());
            delay(5000);  // Wait 5 seconds before retrying
        }
    }
}

