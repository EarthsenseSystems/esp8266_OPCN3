// Alisio air quality monitor with OPC-N3 sensor
// ESP code for the Alisio enhanced air quality monitor
// This code reads the OPC-N3 sensor
// and sends the data to an MQTT broker

#include <EspMQTTClient.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <SPI.h>
#include <OPCN3.h>

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

  // opcn3.begin();
  // delay(1000);
  // Serial.println("OPC-N3: Read serial number");
  // opcn3.readSerialNumber();
  // delay(1000);
  // Serial.println("OPC-N3: DAC and power status");
  // opcn3.readDACandPowerStatus();
  // delay(1000);
  // Serial.println("OPC-N3: Start and stop fan");
  // opcn3.setFanDigitalPotShutdownState(true);
  // delay(1000);
  // Serial.println("OPC-N3: Start laser");
  // opcn3.setLaserDigitalPotShutdownState(true);
  // delay(1000);
  // Serial.println("OPC-N3: Histogram reset");
  // bool histogramReset = opcn3.resetHistogram();
  // Serial.println(histogramReset);
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
  // // HASS auto-discovery
  // sendDiscoveryMessages();

  // start the NTP client and set to UTC
  timeClient.begin();
  timeClient.setTimeOffset(0);

  // start the csv output to the serial console
  Serial.println("timestamp,temp,rhum,pm1,pm2.5,pm10");

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
    sprintf(csvBuffer, "%s,%f,%f,%f,%f,%f", 
      strTimestamp,
      temp,rhum,
      opcn3Hist.pm1,opcn3Hist.pm2_5,opcn3Hist.pm10
      );

    Serial.println(csvBuffer);

    // // publish the sensor values to MQTT
    // sprintf(mqtt_topic, "%s/temp", mqtt.getMqttClientName());
    // mqtt.publish(mqtt_topic, String(temp.temperature,DEC), true);
    // sprintf(mqtt_topic, "%s/rhum", mqtt.getMqttClientName());
    // mqtt.publish(mqtt_topic, String(humidity.relative_humidity,DEC), true);
    // sprintf(mqtt_topic, "%s/pm01", mqtt.getMqttClientName());
    // mqtt.publish(mqtt_topic, String(pms.pm01,DEC), true);
    // sprintf(mqtt_topic, "%s/pm25", mqtt.getMqttClientName());
    // mqtt.publish(mqtt_topic, String(pms.pm25,DEC), true);
    // sprintf(mqtt_topic, "%s/pm10", mqtt.getMqttClientName());
    // mqtt.publish(mqtt_topic, String(pms.pm10,DEC), true);
  
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

// // set the base JSON strings for the sensors
// String jsonSensorTemp(
// "{\
//   \"name\": \"Alisio Temperature\",\
//   \"unique_id\": \"alisio_temp\",\
//   \"object_id\": \"temp\",\
//   \"icon\": \"mdi:thermometer\",\
//   \"unit_of_measurement\": \"°C\",\
//   \"device_class\": \"temperature\",\
//   \"state_class\": \"measurement\",\
//   \"qos\": \"0\",\
//   \"state_topic\": \"mqttClientName/temp\",\
//   \"force_update\": \"true\",\
//   \"device\": {\
//   \"identifiers\": [\
//   \"mqttClientName\" ],\
//   \"name\": \"Alisio\",\
//   \"model\": \"A001\",\
//   \"manufacturer\": \"PabloGRB\",\
//   \"suggested_area\": \"Home\"}\
// }");
// String jsonSensorRhum(
// "{\
//   \"name\": \"Alisio Relative Humidity\",\
//   \"unique_id\": \"alisio_rhum\",\
//   \"object_id\": \"rhum\",\
//   \"icon\": \"mdi:water-percent\",\
//   \"unit_of_measurement\": \"%\",\
//   \"device_class\": \"humidity\",\
//   \"state_class\": \"measurement\",\
//   \"qos\": \"0\",\
//   \"state_topic\": \"mqttClientName/rhum\",\
//   \"force_update\": \"true\",\
//   \"device\": {\
//   \"identifiers\": [\
//   \"mqttClientName\"],\
//   \"name\": \"Alisio\",\
//   \"model\": \"A001\",\
//   \"manufacturer\": \"PabloGRB\",\
//   \"suggested_area\": \"Home\"}\
// }");
// String jsonSensorPM01(
// "{\
//   \"name\": \"Alisio PM1.0\",\
//   \"unique_id\": \"alisio_pm01\",\
//   \"object_id\": \"pm01\",\
//   \"icon\": \"mdi:blur\",\
//   \"unit_of_measurement\": \"µg/m³\",\
//   \"device_class\": \"PM1\",\
//   \"state_class\": \"measurement\",\
//   \"qos\": \"0\",\
//   \"state_topic\": \"mqttClientName/pm01\",\
//   \"force_update\": \"true\",\
//   \"device\": {\
//   \"identifiers\": [\
//   \"mqttClientName\"],\
//   \"name\": \"Alisio\",\
//   \"model\": \"A001\",\
//   \"manufacturer\": \"PabloGRB\",\
//   \"suggested_area\": \"Home\"}\
// }");
// String jsonSensorPM25(
// "{\
//   \"name\": \"Alisio PM2.5\",\
//   \"unique_id\": \"alisio_pm25\",\
//   \"object_id\": \"pm25\",\
//   \"icon\": \"mdi:blur\",\
//   \"unit_of_measurement\": \"µg/m³\",\
//   \"device_class\": \"PM25\",\
//   \"state_class\": \"measurement\",\
//   \"qos\": \"0\",\
//   \"state_topic\": \"mqttClientName/pm25\",\
//   \"force_update\": \"true\",\
//   \"device\": {\
//   \"identifiers\": [\
//   \"mqttClientName\"],\
//   \"name\": \"Alisio\",\
//   \"model\": \"A001\",\
//   \"manufacturer\": \"PabloGRB\",\
//   \"suggested_area\": \"Home\"}\
// }");
// String jsonSensorPM10(
// "{\
//   \"name\": \"Alisio PM10\",\
//   \"unique_id\": \"alisio_pm10\",\
//   \"object_id\": \"pm10\",\
//   \"icon\": \"mdi:blur\",\
//   \"unit_of_measurement\": \"µg/m³\",\
//   \"device_class\": \"PM10\",\
//   \"state_class\": \"measurement\",\
//   \"qos\": \"0\",\
//   \"state_topic\": \"mqttClientName/pm10\",\
//   \"force_update\": \"true\",\
//   \"device\": {\
//   \"identifiers\": [\
//   \"mqttClientName\"],\
//   \"name\": \"Alisio\",\
//   \"model\": \"A001\",\
//   \"manufacturer\": \"PabloGRB\",\
//   \"suggested_area\": \"Home\"}\
// }");

// void sendDiscoveryMessages() {
//     // HASS auto-discovery messages
//   Serial.println("Sending HASS auto-discovery messages");
//   lcd.setCursor(0, 1);
//   lcd.send_string("HASS auto-disc  ");
  
//   // replace the mqttClientName placeholder with the actual client name
//   jsonSensorTemp.replace("mqttClientName", mqtt.getMqttClientName());
//   jsonSensorRhum.replace("mqttClientName", mqtt.getMqttClientName());
//   jsonSensorPM01.replace("mqttClientName", mqtt.getMqttClientName());
//   jsonSensorPM25.replace("mqttClientName", mqtt.getMqttClientName());
//   jsonSensorPM10.replace("mqttClientName", mqtt.getMqttClientName());
//   // publish the discovery messages
//   delay(50);
//   mqtt.publish("homeassistant/sensor/alisio_temp/config", jsonSensorTemp, true);
//   delay(50);
//   mqtt.publish("homeassistant/sensor/alisio_rhum/config", jsonSensorRhum, true);
//   delay(50);
//   mqtt.publish("homeassistant/sensor/alisio_pm01/config", jsonSensorPM01, true);
//   delay(50);
//   mqtt.publish("homeassistant/sensor/alisio_pm25/config", jsonSensorPM25, true);
//   delay(50);
//   mqtt.publish("homeassistant/sensor/alisio_pm10/config", jsonSensorPM10, true);
//   delay(50);
// }
