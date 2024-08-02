// Alisio air quality monitor
// ESP8266 code for the Alisio air quality monitor
// This code reads the PMS5003 sensor and the AHTX0 sensor
// and sends the data to an MQTT broker
// The data is also displayed on an LCD screen
// The LCD screen color changes based on the PM2.5 value

// included libraries
#include <Wire.h>
#include <SoftwareSerial.h>
#include <Waveshare_LCD1602_RGB.h>
#include <PMserial.h>
#include <Adafruit_AHTX0.h>
#include <EspMQTTClient.h>
#include <WiFiUdp.h>
#include <NTPClient.h>

// include the configuration file
#include "mqttconfig.h"

// include the calibration file
#include "cal.h"

// csv buffer
char csv_buffer[256];

// LCD parameters
Waveshare_LCD1602_RGB lcd(16,2);  //16 characters and 2 lines of show
char buf_temp[2];
char buf_pm[12];
char buf_rh[2];

// custom chars
uint8_t subs_1[8] = {0x00, 0x00, 0x00, 0x04, 0x0C, 0x04, 0x04, 0x0E}; // subscript 1
uint8_t subs_2[8] = {0x00, 0x00, 0x00, 0x08, 0x14, 0x04, 0x08, 0x1D}; // subscript 2.
uint8_t subs_0[8] = {0x00, 0x00, 0x00, 0x08, 0x14, 0x14, 0x14, 0x08}; // subscript 0
uint8_t subs_5[8] = {0x00, 0x00, 0x00, 0x1C, 0x10, 0x1C, 0x04, 0x1C}; // subscript 5

uint8_t smallT[8] = {0x00, 0x0E, 0x04, 0x04, 0x04, 0x04, 0x00, 0x00}; // small T
uint8_t degs_C[8] = {0x00, 0x16, 0x08, 0x08, 0x08, 0x06, 0x00, 0x00}; // degrees C
uint8_t rHum_1[8] = {0x00, 0x02, 0x02, 0x1B, 0x12, 0x12, 0x00, 0x00}; // first half of the rH% glyph
uint8_t rHum_2[8] = {0x00, 0x15, 0x11, 0x12, 0x14, 0x15, 0x00, 0x00}; // second half of the rh% glyph

// PMS5003 parameters
SerialPM pms(PMSx003,14,12);

// AHTX0 parameters
Adafruit_AHTX0 aht;

// MQTT and WiFi parameters
EspMQTTClient mqtt;
char mqtt_topic[256];

// NTP parameters
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 3600, 60000);
char str_timestamp[20];

// update frequency in seconds
int update_freq = 10;

// setup function
void setup() {
  // initialize the serial console
  Serial.begin(115200);

  // initialize the mqtt client
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
  
  // initialize the lcd
  Serial.println("Initializing LCD");
  lcd.init();
  lcd.setCursor(0,0);
  lcd.send_string("Alisio sensor");
  lcd.setCursor(0,1);
  lcd.send_string("Initializing... ");

  // store the custom characters into the CGRAM
  lcd.customSymbol(0, subs_1);
  lcd.customSymbol(1, subs_2);
  lcd.customSymbol(2, subs_5);
  lcd.customSymbol(3, subs_0);
  lcd.customSymbol(4, smallT);
  lcd.customSymbol(5, degs_C);
  lcd.customSymbol(6, rHum_1);
  lcd.customSymbol(7, rHum_2);

  // initialize the PMS
  Serial.println("Initializing the PMS software serial port");
  lcd.setCursor(0,1);
  lcd.send_string("PM sensor       ");
  pms.init();

  // initialize the T/RH sensor
  Serial.println("Initializing the T/RH sensor");
  lcd.setCursor(0,1);
  lcd.send_string("T/RH sensor     ");
  aht.begin();

  // now wait for wifi
  Serial.println("Waiting for WiFi->MQTT connection");
}

// main loop
void loop() {
  // Call the MQTT loop
  mqtt.loop();
  if (!mqtt.isConnected()) {
    readSensors();
  }
}

// MQTT connection established callback
void onConnectionEstablished() {
  // publish the connection status to MQTT and serial
  Serial.println("Connected to MQTT broker");
  lcd.setCursor(0,1);
  lcd.send_string("MQTT broker     ");
  sprintf(mqtt_topic, "%s/status", mqtt.getMqttClientName());
  mqtt.publish(mqtt_topic, "connected", true);

  // // listen to the mqtt topic "rgb_color" and set the LCD color
  // // useful for getting the right colors for the AQI scale given the awful color rendering of the LCD
  // sprintf(mqtt_topic, "%s/rgb_color", mqtt.getMqttClientName());
  // mqtt.subscribe(mqtt_topic, [](const String &payload) {
  //   Serial.println("Received payload: " + payload);
  //   int r = payload.substring(0,3).toInt();
  //   int g = payload.substring(4,7).toInt();
  //   int b = payload.substring(8,11).toInt();
  //   lcd.setRGB(r,g,b);
  // });

  // HASS auto-discovery
  sendDiscoveryMessages();

  // start the NTP client and set to UTC
  timeClient.begin();
  timeClient.setTimeOffset(0);

  // start the csv output to the serial console
  Serial.println("timestamp,pm01,pm25,pm10,n0p3,n0p5,n1p0,n2p5,n5p0,n10p0,temp,rhum");

  // execute the main sensor function as a delayed instruction
  mqtt.executeDelayed(update_freq * 1000, readSensors);
}

// main sensor function
void readSensors() {
  // read the PM sensor
  pms.read();

  // read the T/RH
  sensors_event_t humidity, temp;
  aht.getEvent(&humidity, &temp);
  // calibrate
  temp.temperature = temp.temperature * tempSlope + tempIntercept;
  humidity.relative_humidity = humidity.relative_humidity * rhumSlope + rhumIntercept;

  // update the LCD
  updateLCD(
    pms.pm01, pms.pm25, pms.pm10,
    temp.temperature,humidity.relative_humidity
  );

  // set the lcd color based on the PM2.5 value according to the AQI scale
  setRGBAQI(pms.pm25);

  if (mqtt.isConnected()) {
    // get the timestamp
    getTimestamp();
    
    // write the csv record to the console
    sprintf(csv_buffer, "%s,%u,%u,%u,%u,%u,%u,%u,%u,%u,%f,%f",
      str_timestamp,
      pms.pm01, pms.pm25, pms.pm10,
      pms.n0p3, pms.n0p5, pms.n1p0, pms.n2p5, pms.n5p0, pms.n10p0,
      temp.temperature,humidity.relative_humidity
    );
    Serial.println(csv_buffer);

    // publish the sensor values to MQTT
    sprintf(mqtt_topic, "%s/temp", mqtt.getMqttClientName());
    mqtt.publish(mqtt_topic, String(temp.temperature,DEC), true);
    sprintf(mqtt_topic, "%s/rhum", mqtt.getMqttClientName());
    mqtt.publish(mqtt_topic, String(humidity.relative_humidity,DEC), true);
    sprintf(mqtt_topic, "%s/pm01", mqtt.getMqttClientName());
    mqtt.publish(mqtt_topic, String(pms.pm01,DEC), true);
    sprintf(mqtt_topic, "%s/pm25", mqtt.getMqttClientName());
    mqtt.publish(mqtt_topic, String(pms.pm25,DEC), true);
    sprintf(mqtt_topic, "%s/pm10", mqtt.getMqttClientName());
    mqtt.publish(mqtt_topic, String(pms.pm10,DEC), true);
  
    // execute the main sensor function as a delayed instruction
    // this will keep the loop running
    mqtt.executeDelayed(update_freq * 1000, readSensors);
  } else {
    // delay using the delay function
    delay(update_freq * 1000);
  }
}

// build a timestamp string from the NTP client
void getTimestamp(){
  // get the current time
  timeClient.update();

  // get the epoch time and build a time structure with it
  time_t epoch_time = timeClient.getEpochTime();
  struct tm *ptm = gmtime((time_t *)&epoch_time);

  // build the timestamp string
  int dt_day = ptm->tm_mday;
  int dt_month = ptm->tm_mon + 1;
  int dt_year = ptm->tm_year + 1900;
  int dt_hour = ptm->tm_hour;
  int dt_minute = ptm->tm_min;
  int dt_second = ptm->tm_sec;
  sprintf(
    str_timestamp,
    "%04u-%02u-%02uT%02u:%02u:%02u",
    dt_year, dt_month, dt_day, dt_hour, dt_minute, dt_second
  );
}

// update the LCD with the PM values and the T/RH values
void updateLCD(
  int pm02,
  int pm25,
  int pm10,
  float temp,
  float rhum
){
  // send to LCD
  // line 0
  lcd.setCursor(0,0);
  lcd.send_string("PM");
  lcd.write_char(0);
  lcd.send_string(" PM");
  lcd.write_char(1);
  lcd.write_char(2);
  lcd.send_string("PM");
  lcd.write_char(0);
  lcd.write_char(3);
  lcd.write_char(4);
  lcd.write_char(5);
  sprintf(buf_temp, "%2.0f", temp);
  lcd.send_string(buf_temp);
  // line 1
  lcd.setCursor(0,1);
  sprintf(buf_pm, "%3u %3u %3u ", pm02, pm25, pm10);
  lcd.send_string(buf_pm);
  lcd.write_char(6);
  lcd.write_char(7);
  sprintf(buf_rh, "%2.0f", rhum);
  lcd.send_string(buf_rh);
}

// set the lcd color based on the PM2.5 value according to the AQI scale
void setRGBAQI(int pm25){
    // if blocks for AQI scale
    // // US EPA color scale
    // if        (pm25 < 11) {
    //   lcd.setRGB( 57,243, 136);
    // } else if (pm25 < 23) {
    //   lcd.setRGB(  0,238, 23);
    // } else if (pm25 < 35) {
    //   lcd.setRGB( 13,163, 63);
    // } else if (pm25 < 41) {
    //   lcd.setRGB(255,255,  0);
    // } else if (pm25 < 47) {
    //   lcd.setRGB(255,192,  0);
    // } else if (pm25 < 53) {
    //   lcd.setRGB(253,141, 60);
    // } else if (pm25 < 58) {
    //   lcd.setRGB(243,135,111);
    // } else if (pm25 < 64) {
    //   lcd.setRGB(255,  0,  0);
    // } else if (pm25 < 70) {
    //   lcd.setRGB(192,  0,  0);
    // } else {
    //   lcd.setRGB(207, 70,218);
    // }

    // US EPA color scale modified for better rendering
    if        (pm25 < 11) {
      lcd.setRGB( 57,243, 34);
    } else if (pm25 < 23) {
      lcd.setRGB(  0,238,  0);
    } else if (pm25 < 35) {
      lcd.setRGB( 13,163, 00);
    } else if (pm25 < 41) {
      lcd.setRGB(255,255,  0);
    } else if (pm25 < 47) {
      lcd.setRGB(255,192,  0);
    } else if (pm25 < 53) {
      lcd.setRGB(255,120,  0);
    } else if (pm25 < 58) {
      lcd.setRGB(255,100,  0);
    } else if (pm25 < 64) {
      lcd.setRGB(255,  0,  0);
    } else if (pm25 < 70) {
      lcd.setRGB(192,  0,  0);
    } else {
      lcd.setRGB(255,  0, 50);
    }

    // // CARTO Prism color scale
    // if        (pm25 < 11) {
    //   lcd.setRGB(29, 105, 150);
    // } else if (pm25 < 23) {
    //   lcd.setRGB(56, 166, 165);
    // } else if (pm25 < 35) {
    //   lcd.setRGB(15, 133, 84);
    // } else if (pm25 < 41) {
    //   lcd.setRGB(115, 175, 72);
    // } else if (pm25 < 47) {
    //   lcd.setRGB(237, 173, 8);
    // } else if (pm25 < 53) {
    //   lcd.setRGB(225, 124, 5);
    // } else if (pm25 < 58) {
    //   lcd.setRGB(204, 80, 62);
    // } else if (pm25 < 64) {
    //   lcd.setRGB(148, 52, 110);
    // } else if (pm25 < 70) {
    //   lcd.setRGB(111, 64, 112);
    // } else {
    //   lcd.setRGB(153, 78, 149);
    // }
}

// set the base JSON strings for the sensors
String jsonSensorTemp(
"{\
  \"name\": \"Alisio Temperature\",\
  \"unique_id\": \"alisio_temp\",\
  \"object_id\": \"temp\",\
  \"icon\": \"mdi:thermometer\",\
  \"unit_of_measurement\": \"°C\",\
  \"device_class\": \"temperature\",\
  \"state_class\": \"measurement\",\
  \"qos\": \"0\",\
  \"state_topic\": \"mqttClientName/temp\",\
  \"force_update\": \"true\",\
  \"device\": {\
  \"identifiers\": [\
  \"mqttClientName\" ],\
  \"name\": \"Alisio\",\
  \"model\": \"A001\",\
  \"manufacturer\": \"PabloGRB\",\
  \"suggested_area\": \"Home\"}\
}");
String jsonSensorRhum(
"{\
  \"name\": \"Alisio Relative Humidity\",\
  \"unique_id\": \"alisio_rhum\",\
  \"object_id\": \"rhum\",\
  \"icon\": \"mdi:water-percent\",\
  \"unit_of_measurement\": \"%\",\
  \"device_class\": \"humidity\",\
  \"state_class\": \"measurement\",\
  \"qos\": \"0\",\
  \"state_topic\": \"mqttClientName/rhum\",\
  \"force_update\": \"true\",\
  \"device\": {\
  \"identifiers\": [\
  \"mqttClientName\"],\
  \"name\": \"Alisio\",\
  \"model\": \"A001\",\
  \"manufacturer\": \"PabloGRB\",\
  \"suggested_area\": \"Home\"}\
}");
String jsonSensorPM01(
"{\
  \"name\": \"Alisio PM1.0\",\
  \"unique_id\": \"alisio_pm01\",\
  \"object_id\": \"pm01\",\
  \"icon\": \"mdi:blur\",\
  \"unit_of_measurement\": \"µg/m³\",\
  \"device_class\": \"PM1\",\
  \"state_class\": \"measurement\",\
  \"qos\": \"0\",\
  \"state_topic\": \"mqttClientName/pm01\",\
  \"force_update\": \"true\",\
  \"device\": {\
  \"identifiers\": [\
  \"mqttClientName\"],\
  \"name\": \"Alisio\",\
  \"model\": \"A001\",\
  \"manufacturer\": \"PabloGRB\",\
  \"suggested_area\": \"Home\"}\
}");
String jsonSensorPM25(
"{\
  \"name\": \"Alisio PM2.5\",\
  \"unique_id\": \"alisio_pm25\",\
  \"object_id\": \"pm25\",\
  \"icon\": \"mdi:blur\",\
  \"unit_of_measurement\": \"µg/m³\",\
  \"device_class\": \"PM25\",\
  \"state_class\": \"measurement\",\
  \"qos\": \"0\",\
  \"state_topic\": \"mqttClientName/pm25\",\
  \"force_update\": \"true\",\
  \"device\": {\
  \"identifiers\": [\
  \"mqttClientName\"],\
  \"name\": \"Alisio\",\
  \"model\": \"A001\",\
  \"manufacturer\": \"PabloGRB\",\
  \"suggested_area\": \"Home\"}\
}");
String jsonSensorPM10(
"{\
  \"name\": \"Alisio PM10\",\
  \"unique_id\": \"alisio_pm10\",\
  \"object_id\": \"pm10\",\
  \"icon\": \"mdi:blur\",\
  \"unit_of_measurement\": \"µg/m³\",\
  \"device_class\": \"PM10\",\
  \"state_class\": \"measurement\",\
  \"qos\": \"0\",\
  \"state_topic\": \"mqttClientName/pm10\",\
  \"force_update\": \"true\",\
  \"device\": {\
  \"identifiers\": [\
  \"mqttClientName\"],\
  \"name\": \"Alisio\",\
  \"model\": \"A001\",\
  \"manufacturer\": \"PabloGRB\",\
  \"suggested_area\": \"Home\"}\
}");

void sendDiscoveryMessages() {
    // HASS auto-discovery messages
  Serial.println("Sending HASS auto-discovery messages");
  lcd.setCursor(0, 1);
  lcd.send_string("HASS auto-disc  ");
  
  // replace the mqttClientName placeholder with the actual client name
  jsonSensorTemp.replace("mqttClientName", mqtt.getMqttClientName());
  jsonSensorRhum.replace("mqttClientName", mqtt.getMqttClientName());
  jsonSensorPM01.replace("mqttClientName", mqtt.getMqttClientName());
  jsonSensorPM25.replace("mqttClientName", mqtt.getMqttClientName());
  jsonSensorPM10.replace("mqttClientName", mqtt.getMqttClientName());
  // publish the discovery messages
  delay(50);
  mqtt.publish("homeassistant/sensor/alisio_temp/config", jsonSensorTemp, true);
  delay(50);
  mqtt.publish("homeassistant/sensor/alisio_rhum/config", jsonSensorRhum, true);
  delay(50);
  mqtt.publish("homeassistant/sensor/alisio_pm01/config", jsonSensorPM01, true);
  delay(50);
  mqtt.publish("homeassistant/sensor/alisio_pm25/config", jsonSensorPM25, true);
  delay(50);
  mqtt.publish("homeassistant/sensor/alisio_pm10/config", jsonSensorPM10, true);
  delay(50);
}