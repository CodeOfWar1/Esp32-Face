#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include "FS.h"
#include "SD_MMC.h"
#include <WebServer.h>

// WiFi credentials
const char* ssid = "TECNO SPARK 10 Pro";
const char* password = "12345678";

// Your server endpoint
const char* upload_url = "http://10.251.211.135:5000/scanned-face";

// Timing
const unsigned long CAPTURE_DURATION = 10 * 1000;
const unsigned long CAPTURE_INTERVAL = 5000;

// Chunk size for POSTing
#define CHUNK_SIZE 1024

WebServer server(80);
bool capturing = false;
unsigned long captureStartTime = 0;
unsigned long lastCaptureTime = 0;

// Camera pins (AI Thinker)
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22
#define FLASH_LED_PIN      4

void startCapture() {
  capturing = true;
  captureStartTime = millis();
  lastCaptureTime = 0;
  Serial.println("📸 Capture started for 30 seconds...");
  server.send(200, "text/plain", "Started capturing for 30 seconds.");
}

void setup() {
  Serial.begin(115200);

  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\n✅ WiFi connected.");
  Serial.println(WiFi.localIP());

  if (!SD_MMC.begin()) {
    Serial.println("❌ SD Card Mount Failed");
    return;
  }

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 10;
  config.fb_count = 1;

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("❌ Camera init failed");
    return;
  }

  server.on("/start", startCapture);
  server.begin();
  Serial.println("🌐 Web server started.");
}

void captureAndSaveImage(String path) {
  digitalWrite(FLASH_LED_PIN, HIGH);
  delay(100);

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("❌ Camera capture failed");
    return;
  }

  File file = SD_MMC.open(path.c_str(), FILE_WRITE);
  if (!file) {
    Serial.println("❌ Failed to open file for writing");
    esp_camera_fb_return(fb);
    return;
  }

  file.write(fb->buf, fb->len);
  file.close();
  esp_camera_fb_return(fb);

  digitalWrite(FLASH_LED_PIN, LOW);
  Serial.println("✅ Image saved to SD: " + path);
}

void postImageInChunks(String path) {
  File file = SD_MMC.open(path);
  if (!file) {
    Serial.println("❌ Failed to open file for upload");
    return;
  }

  uint8_t buffer[CHUNK_SIZE];
  int chunkIndex = 0;
  int totalSize = file.size();
  int totalChunks = (totalSize + CHUNK_SIZE - 1) / CHUNK_SIZE;


  while (file.available()) {
    size_t len = file.read(buffer, CHUNK_SIZE);
    if (len == 0) break;

    HTTPClient http;
    http.begin(upload_url);
    http.addHeader("Content-Type", "application/octet-stream");
    http.addHeader("X-Chunk-Index", String(chunkIndex)); // Optional: for tracking on server

    // Important: POST with raw binary data from buffer
    int code = http.POST(buffer, len);

    if (code > 0) {
      Serial.printf("✅ Chunk %d sent (%d bytes) | Response: %d\n", chunkIndex, len, code);
    } else {
      Serial.printf("❌ Chunk %d failed: %s\n", chunkIndex, http.errorToString(code).c_str());
      break;
    }

    http.end();
    chunkIndex++;
    delay(100); // Slight delay to avoid overwhelming the server
  }

  file.close();
  Serial.println("📤 Finished sending image in chunks.");
}

void loop() {
  server.handleClient();

  if (capturing) {
    unsigned long now = millis();
    if (now - captureStartTime < CAPTURE_DURATION) {
      if (now - lastCaptureTime >= CAPTURE_INTERVAL) {
        lastCaptureTime = now;

        String path = "/capture.jpg";
        captureAndSaveImage(path);
        delay(100); // Give SD time to settle
        postImageInChunks(path);
      }
    } else {
      Serial.println("🛑 Done capturing.");
      capturing = false;
    }
  }
}
