#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESPAsyncWebServer.h>

// ===========================
// WiFi & Gist (NON-CACHE)
// ===========================
const char* ssid = "XXX";
const char* password = "XXX";
const char* gist_url = "XXX";

// ===========================
// Kamera (AI Thinker)
// ===========================
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

// ===========================
// FSM State
// ===========================
enum State {
  STATE_WIFI_CONNECT,
  STATE_FETCH_URL,
  STATE_CAPTURE,
  STATE_UPLOAD,
  STATE_WAIT
};

State currentState = STATE_WIFI_CONNECT;
unsigned long stateTimer = 0;
String server_url = "";
String logBuffer = "";
AsyncWebServer server(80);

// ===========================
// Fungsi logging
// ===========================
void addLog(String msg) {
  Serial.println(msg);
  logBuffer += msg + "<br>";
  if (logBuffer.length() > 4000) logBuffer = logBuffer.substring(logBuffer.length() - 4000);
}

// ===========================
// Ambil URL dari Gist (non-cache)
// ===========================
String fetchServerURL() {
  HTTPClient http;
  String full_gist = String(gist_url) + "?t=" + String(millis());  // hindari cache
  http.begin(full_gist);
  int code = http.GET();
  String url = "";

  if (code == 200) {
    url = http.getString();
    url.trim();
    addLog("🌐 URL server dari Gist: " + url);
  } else {
    addLog("⚠️ Gagal fetch Gist (kode " + String(code) + ")");
  }
  http.end();
  return url;
}

// ===========================
// Upload Foto ke Server
// ===========================
void uploadPhoto(String server_url, const uint8_t* img, size_t len) {
  if (server_url == "" || !img || len == 0) {
    addLog("⚠️ Upload dibatalkan (data kosong)");
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    addLog("⚠️ Upload dibatalkan (WiFi terputus)");
    return;
  }

  String endpoint = server_url + "/upload";
  HTTPClient http;
  http.begin(endpoint);
  http.addHeader("Content-Type", "image/jpeg");

  int code = http.POST((uint8_t*)img, len);

  if (code > 0) {
    addLog("✅ Upload sukses: " + String(code));
  } else {
    addLog("❌ Upload gagal: " + http.errorToString(code));
  }
  http.end();
}

// ===========================
// WiFi Auto Reconnect
// ===========================
void checkWiFiConnection() {
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck < 8000) return;
  lastCheck = millis();

  if (WiFi.status() != WL_CONNECTED) {
    addLog("⚠️ WiFi terputus, mencoba reconnect...");
    WiFi.disconnect();
    WiFi.reconnect();

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) {
      delay(500);
    }

    if (WiFi.status() == WL_CONNECTED) {
      addLog("✅ WiFi tersambung kembali! IP: " + WiFi.localIP().toString());
    } else {
      addLog("❌ Gagal reconnect WiFi, akan coba lagi...");
    }
  }
}

// ===========================
// Setup Kamera
// ===========================
bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  config.frame_size = FRAMESIZE_HD;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    addLog("⚠️ Kamera gagal init (HD), fallback ke SVGA...");
    esp_camera_deinit();
    config.frame_size = FRAMESIZE_SVGA;
    err = esp_camera_init(&config);
  }

  if (err == ESP_OK) {
    addLog("📷 Kamera siap digunakan");
    return true;
  } else {
    addLog("❌ Kamera gagal init (0x" + String(err, HEX) + ")");
    return false;
  }
}

// ===========================
// Setup WebServer (non-blocking)
// ===========================
void setupWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    String html = "<html><head><meta http-equiv='refresh' content='2'/>"
                  "<style>body{font-family:monospace;background:#111;color:#0f0;}</style></head><body>"
                  "<h3>📡 ESP32-CAM Monitor</h3><hr>"
                  + logBuffer +
                  "</body></html>";
    request->send(200, "text/html", html);
  });

  server.begin();
  addLog("🌍 WebServer aktif di http://" + WiFi.localIP().toString());
}

// ===========================
// SETUP
// ===========================
void setup() {
  Serial.begin(115200);
  delay(500);

  addLog("🚀 ESP32-CAM FreeRTOS Starting...");
  WiFi.begin(ssid, password);
  addLog("📡 Menghubungkan ke WiFi...");

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED) {
    addLog("✅ WiFi terhubung: " + WiFi.localIP().toString());
  } else {
    addLog("❌ WiFi gagal tersambung!");
  }

  initCamera();
  setupWebServer();
}

// ===========================
// LOOP
// ===========================
void loop() {
  checkWiFiConnection();

  switch (currentState) {
    case STATE_WIFI_CONNECT:
      currentState = STATE_FETCH_URL;
      break;

    case STATE_FETCH_URL:
      server_url = fetchServerURL();
      if (server_url != "") currentState = STATE_CAPTURE;
      delay(2000);
      break;

    case STATE_CAPTURE: {
      addLog("📸 Mengambil foto...");
      camera_fb_t *fb = esp_camera_fb_get();
      if (!fb) {
        addLog("❌ Gagal ambil foto, ulang...");
        delay(3000);
      } else {
        addLog("📦 Foto siap (" + String(fb->len) + " byte)");
        uploadPhoto(server_url, fb->buf, fb->len);
        esp_camera_fb_return(fb);
        stateTimer = millis();
        currentState = STATE_WAIT;
      }
      break;
    }

    case STATE_WAIT:
      if (millis() - stateTimer > 10000) {
        currentState = STATE_FETCH_URL;
      }
      break;
  }
}
