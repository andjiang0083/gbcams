/**
 * CAMS3-5MP — Simple MJPEG Stream (blocking WebServer)
 *
 * fb_count=2, QVGA, quality=8
 */
#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>

#define LED_PIN 14
#define PART_BOUNDARY "123456789000000000000987654321"

#define PWDN_GPIO_NUM    -1
#define RESET_GPIO_NUM   21
#define XCLK_GPIO_NUM    11
#define SIOD_GPIO_NUM    17
#define SIOC_GPIO_NUM    41
#define Y9_GPIO_NUM      13
#define Y8_GPIO_NUM      4
#define Y7_GPIO_NUM      10
#define Y6_GPIO_NUM      5
#define Y5_GPIO_NUM      7
#define Y4_GPIO_NUM      16
#define Y3_GPIO_NUM      15
#define Y2_GPIO_NUM      6
#define VSYNC_GPIO_NUM   42
#define HREF_GPIO_NUM    18
#define PCLK_GPIO_NUM    12

WebServer server(80);
static bool g_camOK = false;
static unsigned long g_frameCount = 0;

void setup() {
  Serial.begin(115200); delay(100);
  Serial.println("\n[GBCAMS] UnitCamS3 AP-HTTP Sender (Simple Stream fb2 Q8)");
  Serial.printf("PSRAM: %d KB\n", ESP.getPsramSize() / 1024);

  pinMode(RESET_GPIO_NUM, OUTPUT);
  digitalWrite(RESET_GPIO_NUM, LOW); delay(50);
  digitalWrite(RESET_GPIO_NUM, HIGH); delay(200);

  camera_config_t config = {};
  config.ledc_channel  = LEDC_CHANNEL_0;
  config.ledc_timer    = LEDC_TIMER_0;
  config.pin_d0        = Y2_GPIO_NUM;
  config.pin_d1        = Y3_GPIO_NUM;
  config.pin_d2        = Y4_GPIO_NUM;
  config.pin_d3        = Y5_GPIO_NUM;
  config.pin_d4        = Y6_GPIO_NUM;
  config.pin_d5        = Y7_GPIO_NUM;
  config.pin_d6        = Y8_GPIO_NUM;
  config.pin_d7        = Y9_GPIO_NUM;
  config.pin_xclk      = XCLK_GPIO_NUM;
  config.pin_pclk      = PCLK_GPIO_NUM;
  config.pin_vsync     = VSYNC_GPIO_NUM;
  config.pin_href      = HREF_GPIO_NUM;
  config.pin_sccb_sda  = SIOD_GPIO_NUM;
  config.pin_sccb_scl  = SIOC_GPIO_NUM;
  config.pin_pwdn      = PWDN_GPIO_NUM;
  config.pin_reset     = -1;
  config.sccb_i2c_port = 1;
  config.xclk_freq_hz  = 20000000;
  config.pixel_format  = PIXFORMAT_JPEG;
  config.frame_size    = FRAMESIZE_QVGA;      // 320x240
  config.jpeg_quality  = 8;                   // good balance
  config.fb_count      = 3;                   // ★ triple buffer
  config.grab_mode     = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location   = CAMERA_FB_IN_PSRAM;

  esp_err_t err = esp_camera_init(&config);
  Serial.printf("cam_init = 0x%x\n", err);
  if (err == ESP_OK) {
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
      s->set_framesize(s, FRAMESIZE_QVGA);
      s->set_quality(s, 8);
      g_camOK = true;
      Serial.printf("Camera OK! PID=0x%04x fb_count=2\n", s->id.PID);
    }
  }
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, g_camOK ? HIGH : LOW);

  WiFi.mode(WIFI_AP);
  WiFi.softAP("UnitCamS3-WiFi", "", 1, 0, 1);
  Serial.printf("AP: UnitCamS3-WiFi @ %s\n", WiFi.softAPIP().toString().c_str());

  // Routes
  server.on("/api/v1/capture", []() {
    if (!g_camOK) { server.send(503); return; }
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) { server.send(500); return; }
    WiFiClient cl = server.client();
    String h = "HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\n"
               "Content-Length: " + String(fb->len) + "\r\n"
               "Access-Control-Allow-Origin: *\r\nCache-Control: no-cache\r\n\r\n";
    cl.write(h.c_str(), h.length());
    cl.write(fb->buf, fb->len);
    esp_camera_fb_return(fb);
  });

  server.on("/api/v1/stream", []() {
    if (!g_camOK) { server.send(503); return; }
    WiFiClient cl = server.client();
    // Send HTTP response + first frame in one go
    char resp[512];
    int rlen = snprintf(resp, sizeof(resp),
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: multipart/x-mixed-replace;boundary=" PART_BOUNDARY "\r\n"
      "Access-Control-Allow-Origin: *\r\n"
      "Cache-Control: no-cache\r\n"
      "Connection: keep-alive\r\n\r\n");
    cl.write((uint8_t*)resp, rlen);
    
    g_frameCount = 0;
    unsigned long t0 = millis();
    char header[256];
    while (cl.connected()) {
      camera_fb_t* fb = esp_camera_fb_get();
      if (!fb) { delay(5); continue; }

      // Build everything in one shot
      int hlen = snprintf(header, sizeof(header),
        "\r\n--" PART_BOUNDARY "\r\n"
        "Content-Type: image/jpeg\r\n"
        "Content-Length: %u\r\n\r\n", fb->len);
      cl.write((uint8_t*)header, hlen);
      cl.write(fb->buf, fb->len);

      esp_camera_fb_return(fb);
      g_frameCount++;

      unsigned long now = millis();
      if (now - t0 >= 5000) {
        Serial.printf("[cam] %u frames in 5s = %.1f fps\n",
                      (unsigned)g_frameCount, g_frameCount / 5.0f);
        g_frameCount = 0;
        t0 = now;
      }
    }
    Serial.println("[GBCAMS] Stream client disconnected");
  });

  server.on("/api/v1/control", []() {
    if (!server.hasArg("var") || !server.hasArg("val")) { server.send(404); return; }
    String var = server.arg("var");
    int val = server.arg("val").toInt();
    sensor_t* s = esp_camera_sensor_get();
    if (!s) { server.send(501); return; }
    int res = 0;
    if      (var == "framesize")   res = s->set_framesize(s, (framesize_t)val);
    else if (var == "quality")     res = s->set_quality(s, val);
    else if (var == "contrast")    res = s->set_contrast(s, val);
    else if (var == "brightness")  res = s->set_brightness(s, val);
    else if (var == "saturation")  res = s->set_saturation(s, val);
    else { server.send(404, "text/plain", "unknown"); return; }
    server.send(200, "text/plain", res == 0 ? "ok" : "fail");
  });

  server.begin();
  Serial.println("HTTP server ready");
}

void loop() {
  server.handleClient();
  static unsigned long lastBlink = 0;
  if (millis() - lastBlink > 2000) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    lastBlink = millis();
  }
}
