// Alisio air quality monitor with OPC-N3 sensor
// ESP code for the Alisio enhanced air quality monitor
// This code reads the OPC-N3 sensor
// and sends the data to an MQTT broker

#include <EspMQTTClient.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <SPI.h>
#include <OPCN3.h>
#include <ArduinoJson.h>

// include the configuration file
#include "mqttconfig.h"

// include the calibration file
#include "cal.h"

// csv buffer
char csvBuffer[256];

// OPC-N3 sensor parameters
OPCN3 opcn3(D8);

// MQTT and WiFi parameters
EspMQTTClient mqtt;
char mqttTopic[256];

// NTP parameters
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 3600, 60000);
char strTimestamp[20];

// update frequency in seconds
int updateFreq = 10;

// setup function
void setup() {
  // initialize the serial console
  Serial.begin(115200);

  // initialize the mqtt client
  Serial.println("Initializing the MQTT client");
  mqtt.setWifiCredentials(
    wifiSSID.c_str(),
    wifiPassword.c_str()
  );
  mqtt.setMqttServer(
    mqttBroker.c_str(),
    mqttUser.c_str(),
    mqttPassword.c_str(),
    mqttPort
  );
  mqtt.setMqttClientName(mqttClientName.c_str());
  mqtt.setMaxPacketSize(1024); // larger packet size to send the auto discovery messages
  
  // // SPI pin testing
  // // Set all SPI pins to output for testing
  // pinMode(D8, OUTPUT);
  // pinMode(D7, OUTPUT);
  // pinMode(D6, OUTPUT);
  // pinMode(D5, OUTPUT);
  // delay(10000);
  // // flip the CS pin
  // Serial.println("SPI: Flip CS pin High");
  // digitalWrite(D8, HIGH);
  // delay(10000);
  // Serial.println("SPI: Flip CS pin Low");
  // digitalWrite(D8, LOW);
  // delay(10000);
  // // flip the PICO pin
  // Serial.println("SPI: Flip PICO pin High");
  // digitalWrite(D7, HIGH);
  // delay(10000);
  // Serial.println("SPI: Flip PICO pin Low");
  // digitalWrite(D7, LOW);
  // delay(10000);
  // // flip the POCI pin
  // Serial.println("SPI: Flip POCI pin High");
  // digitalWrite(D6, HIGH);
  // delay(10000);
  // Serial.println("SPI: Flip POCI pin Low");
  // digitalWrite(D6, LOW);
  // delay(10000);
  // // flip the SCK pin
  // Serial.println("SPI: Flip SCK pin High");
  // digitalWrite(D5, HIGH);
  // delay(10000);
  // Serial.println("SPI: Flip SCK pin Low");
  // digitalWrite(D5, LOW);
  // delay(10000);

  // initialize the OPC-N3 sensor
  delay(1000);
  Serial.println("OPC-N3: Setup SPI connection");
  opcn3.initialize();
  // opcn3.initialize("debug");
  delay(1000);

  // now wait for wifi
  Serial.println("Waiting for WiFi->MQTT connection");
}

// main loop
void loop() {
  // Call the MQTT loop
  mqtt.loop();
}

// MQTT connection established callback
void onConnectionEstablished() {
  // publish the connection status to MQTT and serial
  Serial.println("Connected to MQTT broker");
  sprintf(mqttTopic, "%s/status", mqtt.getMqttClientName());
  mqtt.publish(mqttTopic, "connected", true);
  // HASS auto-discovery
  sendDiscoveryMessages();

  // start the NTP client and set to UTC
  timeClient.begin();
  timeClient.setTimeOffset(0);

  // start the csv output to the serial console
  String header = "timestamp,temp,rhum,pm1,pm2.5,pm10";
  char binBuffer[2];
  for (int i =0; i < 24; i++) {
    header += ",bin";
    if (i < 10) header += "0";
    header += i;
  }
  Serial.println(header);

  // execute the main sensor function as a delayed instruction
  mqtt.executeDelayed(updateFreq * 1000, readSensors);
}

// main sensor function
void readSensors() {
  // read the sensor data
  HistogramData opcn3Hist = opcn3.readHistogramData();
  float temp = opcn3Hist.getTempC();
  float rhum = opcn3Hist.getHumidity();

   // calibrate
  temp = temp * tempSlope + tempIntercept;
  rhum = rhum * rhumSlope + rhumIntercept;

  if (mqtt.isConnected()) {
    // get the timestamp
    getTimestamp();

    // write the csv record to the console
    String csvRecord;
    sprintf(csvBuffer, "%s,%f,%f,%f,%f,%f", 
      strTimestamp,
      temp,rhum,
      opcn3Hist.pm1,opcn3Hist.pm2_5,opcn3Hist.pm10
      );
    csvRecord = csvBuffer;
    for (int i = 0; i < 24; i++) {
      csvRecord += ",";
      csvRecord += opcn3Hist.binCounts[i];
    }
    Serial.println(csvRecord);

    // publish the sensor values to MQTT
    sprintf(mqttTopic, "%s/temp", mqtt.getMqttClientName());
    mqtt.publish(mqttTopic, String(temp,DEC), true);
    sprintf(mqttTopic, "%s/rhum", mqtt.getMqttClientName());
    mqtt.publish(mqttTopic, String(rhum,DEC), true);
    sprintf(mqttTopic, "%s/pm01", mqtt.getMqttClientName());
    mqtt.publish(mqttTopic, String(opcn3Hist.pm1,DEC), true);
    sprintf(mqttTopic, "%s/pm25", mqtt.getMqttClientName());
    mqtt.publish(mqttTopic, String(opcn3Hist.pm2_5,DEC), true);
    sprintf(mqttTopic, "%s/pm10", mqtt.getMqttClientName());
    mqtt.publish(mqttTopic, String(opcn3Hist.pm10,DEC), true);
    
    // transform the bin count data to JSON
    JsonDocument opcn3Json;
    for (int i = 0; i < 24; i++) {
      String binName = "bin";
      if (i < 10) binName += "0";
      binName += i;
      opcn3Json[binName] = opcn3Hist.binCounts[i];
    }
    // send the json to MQTT
    String mqttJson;
    serializeJson(opcn3Json, mqttJson);
    sprintf(mqttTopic, "%s/bincnt", mqtt.getMqttClientName());
    mqtt.publish(mqttTopic, mqttJson, true);

    // execute the main sensor function as a delayed instruction
    // this will keep the loop running
    mqtt.executeDelayed(updateFreq * 1000, readSensors);
  } else {
    // delay using the delay function
    delay(updateFreq * 1000);
  }
}

// build a timestamp string from the NTP client
void getTimestamp(){
  // get the current time
  timeClient.update();

  // get the epoch time and build a time structure with it
  time_t epochTime = timeClient.getEpochTime();
  struct tm *ptm = gmtime((time_t *)&epochTime);

  // build the timestamp string
  int dtDay = ptm->tm_mday;
  int dtMonth = ptm->tm_mon + 1;
  int dtYear = ptm->tm_year + 1900;
  int dtHour = ptm->tm_hour;
  int dtMinute = ptm->tm_min;
  int dtSecond = ptm->tm_sec;
  sprintf(
    strTimestamp,
    "%04u-%02u-%02uT%02u:%02u:%02u",
    dtYear, dtMonth, dtDay, dtHour, dtMinute, dtSecond
  );
}

void sendDiscoveryMessages() {
  // HASS auto-discovery messages
  Serial.println("Sending HASS auto-discovery messages");

  // Create the sensors and publish the messages
  pubAutodiscSensor("Temperature", "temp", "thermometer", "°C", "temperature");
  pubAutodiscSensor("Relative Humidity", "rhum", "water-percent", "%", "humidity");
  pubAutodiscSensor("PM1.0", "pm01", "blur", "µg/m³", "PM1");
  pubAutodiscSensor("PM2.5", "pm25", "blur", "µg/m³", "PM25");
  pubAutodiscSensor("PM10", "pm10", "blur", "µg/m³", "PM10");
  pubAutodiscSensor("Bin Counts", "bincnt", "blur", "Hz", "frequency");
}

void pubAutodiscSensor(String sensorName, String sensorAbb, String sensorIcon, String sensorUnit, String sensorClass) {
  JsonDocument autoDiscovery;
  // Create the device object
  JsonObject device = autoDiscovery["device"].to<JsonObject>();
  device["identifiers"][0] = mqtt.getMqttClientName();
  device["name"] = "Alisio OPC-N3";
  device["model"] = "A002";
  device["manufacturer"] = "PabloGRB";
  device["suggested_area"] = "Home";
  // Add sensor keys to the Json
  autoDiscovery["name"] = "Alisio " + sensorName;
  autoDiscovery["unique_id"] = String(mqtt.getMqttClientName()) + "_" + sensorAbb;
  autoDiscovery["object_id"] = sensorAbb;
  autoDiscovery["icon"] = "mdi:" + sensorIcon;
  autoDiscovery["unit_of_measurement"] = sensorUnit;
  autoDiscovery["device_class"] = sensorClass;
  autoDiscovery["state_class"] = "measurement";
  autoDiscovery["qos"] = "0";
  sprintf(mqttTopic, "%s/%s", mqtt.getMqttClientName(), sensorAbb);
  autoDiscovery["state_topic"] = mqttTopic;
  autoDiscovery["force_update"] = "true";
  // Output strings
  sprintf(mqttTopic, "homeassistant/sensor/%s_%s/config", mqtt.getMqttClientName(), sensorAbb);
  String autoDiscoveryString;
  // Serialize
  serializeJson(autoDiscovery, autoDiscoveryString);
  // Publish
  delay(50);
  mqtt.publish(mqttTopic, autoDiscoveryString, true);
  
}
