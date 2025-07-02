#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <WebServer.h>
#include "time.h"
#include "driver/adc.h"

// ------------------------ CONFIGURATION ------------------------

const char* WIFI_SSID = "TP-Link_2.4GHz_318A4E";
const char* WIFI_PASSWORD = "";

const char* MQTT_BROKER = "test.mosquitto.org";  // Or IP like "44.195.202.69"
const int MQTT_PORT = 1883;
const char* DEVICE_ID_PREFIX = "esp32-audio";

#define ADC_CHANNEL ADC1_CHANNEL_6  // GPIO 34
#define SAMPLE_RATE 8000            // Hz
#define RECORD_DURATION_SECONDS 3
#define AMPLITUDE_THRESHOLD 500
#define FILENAME_PREFIX "/recording"
#define GAIN_FACTOR 8               // Software gain

// ------------------------ GLOBALS ------------------------
volatile bool isRecording = false;
const int TOTAL_SAMPLES = SAMPLE_RATE * RECORD_DURATION_SECONDS;
const int SAMPLING_PERIOD_US = round(1000000.0 / SAMPLE_RATE);

int adc_dc_offset = 0;
char deviceId[25];
char mqttTopic[50];

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
WebServer server(80);  // Web server on port 80

// ------------------------ FUNCTION DECLARATIONS ------------------------

void connectWiFi();
void initTime();
void connectMQTT();
void calibrateDCOffset();
void recordAndAlert(int triggerAmplitude);
void publishMqttAlert(const char* filename, float amplitude);
void clearOldRecordings();
void setupWebServer();

// ------------------------ SETUP ------------------------

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n--- ESP32 Sound Detection, MQTT & Web Server ---");

  uint64_t chipid = ESP.getEfuseMac();
  snprintf(deviceId, 25, "%s-%04X", DEVICE_ID_PREFIX, (uint16_t)(chipid >> 32));
  snprintf(mqttTopic, 50, "esp32/audio_alerts/%s", deviceId);

  connectWiFi();
  initTime();
  setupWebServer();

  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);

  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC_CHANNEL, ADC_ATTEN_DB_11);

  if (!SPIFFS.begin(true)) {
    Serial.println("FATAL: Could not mount SPIFFS");
    while (1) delay(1000);
  }

  clearOldRecordings();
  calibrateDCOffset();
  Serial.printf("Ready. Listening for sound > %d amplitude\n", AMPLITUDE_THRESHOLD);
}

// ------------------------ LOOP ------------------------

void loop() {
  if (!mqttClient.connected()) {
    connectMQTT();
  }
  mqttClient.loop();
  server.handleClient();  // Handle web requests

  if (!isRecording) {
    int adc_value = adc1_get_raw(ADC_CHANNEL);
    int amplitude = abs(adc_value - adc_dc_offset);

    if (amplitude > AMPLITUDE_THRESHOLD) {
      isRecording = true;  // Lock
      recordAndAlert(amplitude);
      isRecording = false; // Unlock
      delay(2000);         // Avoid retrigger
    }
  }

  delay(1);
}


// ------------------------ FUNCTIONS ------------------------

void connectWiFi() {
  Serial.print("Connecting to WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("\nWiFi Connected.");
  Serial.println("IP Address: " + WiFi.localIP().toString());
}

void initTime() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("Syncing time...");
  while (time(nullptr) < 10000) {
    delay(500); Serial.print(".");
  }
  Serial.println(" done.");
}

void connectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Connecting to MQTT...");
    if (mqttClient.connect(deviceId)) {
      Serial.println("connected");
    } else {
      Serial.print("Failed. Code=");
      Serial.println(mqttClient.state());
      delay(5000);
    }
  }
}

void calibrateDCOffset() {
  Serial.print("Calibrating mic DC offset... ");
  long sum = 0;
  for (int i = 0; i < 1024; i++) {
    sum += adc1_get_raw(ADC_CHANNEL);
    delay(1);
  }
  adc_dc_offset = sum / 1024;
  Serial.printf("Done. Offset = %d\n", adc_dc_offset);
}

void clearOldRecordings() {
  Serial.println("Clearing old recordings...");

  File root = SPIFFS.open("/");
  File file = root.openNextFile();

  while (file) {
    String fname = file.name();
    Serial.println("Found: " + fname);

    if (fname.startsWith("recording")) {
      String fullPath = "/" + fname;
      Serial.println("Deleting: " + fullPath);
      file.close();

      if (SPIFFS.remove(fullPath)) {
        Serial.println("Deleted: " + fullPath);
      } else {
        Serial.println("Failed to delete: " + fullPath);
      }
    } else {
      file.close();
    }

    file = root.openNextFile();
  }

  Serial.println("Done clearing recordings.");
}

void recordAndAlert(int triggerAmplitude) {
  Serial.println("----------------------------------------");
  Serial.printf("EVENT: Amplitude Triggered: %d\n", triggerAmplitude);

  char filename[64];
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  strftime(filename, sizeof(filename), "/recording_%Y%m%d_%H%M%S.bin", timeinfo);

  int16_t* buffer = (int16_t*)malloc(TOTAL_SAMPLES * sizeof(int16_t));
  if (!buffer) {
    Serial.println("ERROR: Not enough RAM for buffer!");
    return;
  }

  Serial.printf("Recording %d samples to buffer...\n", TOTAL_SAMPLES);
  unsigned long nextSampleTime = micros();

  for (int i = 0; i < TOTAL_SAMPLES; i++) {
    while (micros() < nextSampleTime);
    nextSampleTime += SAMPLING_PERIOD_US;
    int raw = adc1_get_raw(ADC_CHANNEL);
    buffer[i] = (raw - adc_dc_offset) * GAIN_FACTOR;
  }

  File file = SPIFFS.open(filename, "w");
  if (!file) {
    Serial.println("ERROR: Could not open file for writing.");
    free(buffer);
    return;
  }
  file.write((const uint8_t*)buffer, TOTAL_SAMPLES * sizeof(int16_t));
  file.close();
  free(buffer);

  Serial.printf("Recording saved to %s\n", filename);

  float normalizedAmplitude = (float)triggerAmplitude / 2048.0;
  publishMqttAlert(filename, normalizedAmplitude);

  Serial.println("----------------------------------------");
}

void publishMqttAlert(const char* filename, float amplitude) {
  Serial.print("Publishing MQTT alert... ");

  StaticJsonDocument<256> doc;
  doc["event"] = "sound_detected";
  doc["device"] = deviceId;

  char timeStr[30];
  time_t now = time(nullptr);
  strftime(timeStr, sizeof(timeStr), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
  doc["timestamp"] = timeStr;

  doc["amplitude"] = amplitude;
  doc["audio_filename"] = filename;

  char jsonBuffer[256];
  serializeJson(doc, jsonBuffer);

  if (mqttClient.publish(mqttTopic, jsonBuffer)) {
    Serial.println("Success.");
  } else {
    Serial.println("Failed.");
  }
}

void setupWebServer() {
  server.on("/", HTTP_GET, []() {
    String html = "<html><body><h2>ESP32 Audio Recordings</h2><ul>";
    html += "<p><b>Device:</b> " + String(deviceId) + "<br><b>IP:</b> " + WiFi.localIP().toString() + "</p>";
    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    while (file) {
      String fname = file.name();
      if (fname.endsWith(".bin")) {
        html += "<li><a href='" + fname + "'>" + fname + "</a> (" + String(file.size()) + " bytes)</li>";
      }
      file = root.openNextFile();
    }
    html += "</ul></body></html>";
    server.send(200, "text/html", html);
  });

  server.onNotFound([]() {
    String path = server.uri();
    if (SPIFFS.exists(path)) {
      File file = SPIFFS.open(path, "r");
      server.streamFile(file, "application/octet-stream");
      file.close();
    } else {
      server.send(404, "text/plain", "File Not Found");
    }
  });

  server.begin();
  Serial.println("Web server started. Access at: http://" + WiFi.localIP().toString());
}
