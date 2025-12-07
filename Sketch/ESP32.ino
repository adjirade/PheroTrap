#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <Adafruit_Sensor.h>
#include <WiFi.h>
#include "time.h"
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>

// ====================================================================
// --- KONFIGURASI PENGGUNA ---
// ====================================================================

// Kredensial Wi-Fi
const char* ssid = "XXX";
const char* password = "XXX";

// Konfigurasi MQTT (HiveMQ Cloud)
const char* mqttServer = "XXX";
const int mqttPort = 8883;
const char* mqttUser = "XXX";
const char* mqttPassword = "XXX";
const char* mqttTopicWereng = "/pest";
const char* mqttTopicTemperature = "/temperature";
const char* mqttTopicHumidity = "/humidity";
const char* mqttTopicLampu = "/lampu";
const char* mqttClientId = "ESP32_Phero_Client";

// Konfigurasi Waktu NTP (WIB = GMT+7)
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7 * 3600;
const int daylightOffset_sec = 0;

// Google Sheets URL
const char* GOOGLE_SCRIPT_URL = "XXX";

// Pin Definitions
#define DHT_PIN 32      
#define RELAY_PIN 23
#define DHT_TYPE DHT22

// Konstanta Kontrol Relay
#define RELAY_ON_HOUR 17
#define RELAY_OFF_HOUR 7

// LCD Settings
const byte LCD_I2C_ADDR = 0x27; 
const float TEMPERATURE_OFFSET = -10.5;

// Task Intervals (milliseconds)
#define DHT_READ_INTERVAL 5000
#define LCD_UPDATE_INTERVAL 5000
#define RELAY_CHECK_INTERVAL 10000
#define MQTT_LOOP_INTERVAL 10
#define TIME_RESYNC_INTERVAL 3600000
#define GSHEET_INTERVAL 30000
#define MQTT_PUBLISH_INTERVAL 500

// WiFi Timeout
#define WIFI_CONNECT_TIMEOUT 20000

// ====================================================================
// --- Inisialisasi Objek & Variabel Global ---
// ====================================================================
DHT dht(DHT_PIN, DHT_TYPE);
LiquidCrystal_I2C lcd(LCD_I2C_ADDR, 16, 2);
WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);

// Variabel Global dengan Mutex Protection
SemaphoreHandle_t xDataMutex;

struct SystemData {
  float temperature = 0.0;
  float humidity = 0.0;
  int werengCount = 0;
  bool relayState = false;
  bool wifiConnected = false;
  bool timeSync = false;
  bool dhtError = false;
} sysData;

// ⭐ MODE SYSTEM
volatile bool isForceActive = false;      // Apakah ada FORCE override?
volatile bool forceState = false;         // State yang di-FORCE (ON/OFF)
volatile unsigned long forceActivatedAt = 0; // Waktu FORCE diaktifkan (untuk logging)

// ====================================================================
// --- FUNGSI HELPER ---
// ====================================================================

SystemData getSystemData() {
  SystemData data;
  if (xSemaphoreTake(xDataMutex, portMAX_DELAY) == pdTRUE) {
    data = sysData;
    xSemaphoreGive(xDataMutex);
  }
  return data;
}

void updateWerengCount(int count) {
  if (xSemaphoreTake(xDataMutex, portMAX_DELAY) == pdTRUE) {
    if (count >= 0) sysData.werengCount = count;
    xSemaphoreGive(xDataMutex);
  }
}

void updateRelayState(bool state) {
  if (xSemaphoreTake(xDataMutex, portMAX_DELAY) == pdTRUE) {
    sysData.relayState = state;
    xSemaphoreGive(xDataMutex);
  }
}

// ====================================================================
// --- FUNGSI CEK SCHEDULE ---
// ====================================================================
bool shouldRelayBeOn(int hour) {
  // ON: 17:00 - 07:00 (malam)
  // OFF: 07:00 - 17:00 (siang)
  if (hour >= RELAY_ON_HOUR || hour < RELAY_OFF_HOUR) {
    return true;  // Malam, seharusnya ON
  } else {
    return false; // Siang, seharusnya OFF
  }
}

// ====================================================================
// --- CALLBACK MQTT ---
// ====================================================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  char buffer[32];
  length = min(length, (unsigned int)31);
  memcpy(buffer, payload, length);
  buffer[length] = '\0';
  
  Serial.printf("[MQTT] Topic: %s | Data: %s\n", topic, buffer);
  
  // Handle topic /pest untuk wereng count
  if (strcmp(topic, mqttTopicWereng) == 0) {
    int count = atoi(buffer);
    updateWerengCount(count);
    Serial.printf("[MQTT] Wereng count updated: %d\n", count);
  }
  
  // ⭐ Handle topic /lampu untuk FORCE control
  else if (strcmp(topic, mqttTopicLampu) == 0) {
    struct tm timeinfo;
    bool commandOn = (strcmp(buffer, "ON") == 0 || strcmp(buffer, "1") == 0);
    bool commandOff = (strcmp(buffer, "OFF") == 0 || strcmp(buffer, "0") == 0);
    
    if (!commandOn && !commandOff) {
      Serial.println("[MQTT] Invalid command, ignored");
      return;
    }
    
    if (getLocalTime(&timeinfo)) {
      int currentHour = timeinfo.tm_hour;
      bool scheduleState = shouldRelayBeOn(currentHour);
      bool requestedState = commandOn;
      
      Serial.printf("\n[FORCE] ========================================\n");
      Serial.printf("[FORCE] Time: %02d:%02d | Schedule: %s\n", 
                    currentHour, timeinfo.tm_min,
                    scheduleState ? "ON" : "OFF");
      Serial.printf("[FORCE] User command: %s\n", requestedState ? "ON" : "OFF");
      
      if (requestedState != scheduleState) {
        // User minta berbeda dari schedule → AKTIFKAN FORCE
        isForceActive = true;
        forceState = requestedState;
        forceActivatedAt = millis();
        
        digitalWrite(RELAY_PIN, forceState ? HIGH : LOW);
        updateRelayState(forceState);
        
        Serial.printf("[FORCE] ✓ FORCE ACTIVATED → Relay: %s\n", forceState ? "ON" : "OFF");
        Serial.printf("[FORCE] Will follow schedule again at %s\n",
                      forceState ? "07:00" : "17:00");
      } else {
        // User minta sama dengan schedule → BATALKAN FORCE (kembali ke AUTO)
        if (isForceActive) {
          isForceActive = false;
          Serial.println("[FORCE] ✓ FORCE CANCELLED → Back to AUTO schedule");
        } else {
          Serial.println("[FORCE] ℹ Already following schedule");
        }
        
        digitalWrite(RELAY_PIN, scheduleState ? HIGH : LOW);
        updateRelayState(scheduleState);
      }
      
      Serial.printf("[FORCE] Current status: %s | FORCE: %s\n",
                    digitalRead(RELAY_PIN) ? "ON" : "OFF",
                    isForceActive ? "ACTIVE" : "INACTIVE");
      Serial.printf("[FORCE] ========================================\n\n");
      
    } else {
      Serial.println("[MQTT] Cannot process command - Time not synced");
    }
  }
}

// ====================================================================
// --- RECONNECT MQTT ---
// ====================================================================
void reconnectMQTT() {
  int retryCount = 0;
  const int maxRetries = 3;
  
  while (!mqttClient.connected() && retryCount < maxRetries) {
    Serial.printf("[MQTT] Koneksi attempt %d/%d...", retryCount + 1, maxRetries);
    
    if (mqttClient.connect(mqttClientId, mqttUser, mqttPassword)) {
      Serial.println(" Berhasil!");
      
      // Subscribe ke topic wereng
      mqttClient.subscribe(mqttTopicWereng);
      Serial.printf("[MQTT] Subscribed ke: %s\n", mqttTopicWereng);
      
      // ✅ TAMBAH: Subscribe ke topic lampu (untuk kontrol manual)
      mqttClient.subscribe(mqttTopicLampu);
      Serial.printf("[MQTT] Subscribed ke: %s\n", mqttTopicLampu);
      
      // ✅ TAMBAH: Subscribe ke topic mode (opsional)
      mqttClient.subscribe("/mode");
      Serial.printf("[MQTT] Subscribed ke: /mode\n");
      
      return;
    } else {
      Serial.printf(" Gagal! RC=%d\n", mqttClient.state());
      retryCount++;
      
      if (retryCount < maxRetries) {
        vTaskDelay(pdMS_TO_TICKS(5000));
      }
    }
  }
  
  if (!mqttClient.connected()) {
    Serial.println("[MQTT] Gagal koneksi setelah retry.");
  }
}

// ====================================================================
// --- Publish Data ke MQTT ---
// ====================================================================
void publishToMQTT() {
  if (!mqttClient.connected()) {
    return;
  }
  
  SystemData data = getSystemData();
  
  // 1. Publish Temperature
  char tempStr[10];
  snprintf(tempStr, sizeof(tempStr), "%.1f", data.temperature);
  mqttClient.publish(mqttTopicTemperature, tempStr, true);
  
  // 2. Publish Humidity
  char humStr[10];
  snprintf(humStr, sizeof(humStr), "%.1f", data.humidity);
  mqttClient.publish(mqttTopicHumidity, humStr, true);
  
  // 3. Publish Relay/Lampu Status
  const char* relayStatus = data.relayState ? "ON" : "OFF";
  mqttClient.publish(mqttTopicLampu, relayStatus, true);
  
  // Log ringkas setiap 30 detik
  static unsigned long lastLog = 0;
  if (millis() - lastLog > 30000) {
    Serial.printf("[MQTT] Published → T:%.1f H:%.1f R:%s\n",
                  data.temperature, data.humidity, relayStatus);
    lastLog = millis();
  }
}

// ====================================================================
// --- TASK 1: SYNC TIME & WIFI ---
// ====================================================================
void TaskSyncTime(void *pvParameters) {
  (void) pvParameters;
  
  Serial.println("[WiFi] Memulai koneksi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  unsigned long startAttempt = millis();
  
  while (WiFi.status() != WL_CONNECTED && 
         millis() - startAttempt < WIFI_CONNECT_TIMEOUT) {
    vTaskDelay(pdMS_TO_TICKS(500));
    Serial.print(".");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Terhubung!");
    Serial.printf("[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("[WiFi] RSSI: %d dBm\n", WiFi.RSSI());
    
    if (xSemaphoreTake(xDataMutex, portMAX_DELAY) == pdTRUE) {
      sysData.wifiConnected = true;
      xSemaphoreGive(xDataMutex);
    }
    
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    Serial.println("[Time] Menunggu sinkronisasi NTP...");
    
    struct tm timeinfo;
    int attempts = 0;
    while (!getLocalTime(&timeinfo) && attempts < 10) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      attempts++;
    }
    
    if (getLocalTime(&timeinfo)) {
      Serial.println("[Time] NTP berhasil disinkronkan!");
      char timeStr[64];
      strftime(timeStr, sizeof(timeStr), "%A, %d %B %Y %H:%M:%S", &timeinfo);
      Serial.printf("[Time] Waktu sekarang: %s WIB\n", timeStr);
      
      if (xSemaphoreTake(xDataMutex, portMAX_DELAY) == pdTRUE) {
        sysData.timeSync = true;
        xSemaphoreGive(xDataMutex);
      }
    } else {
      Serial.println("[Time] Gagal sinkronisasi NTP!");
    }
  } else {
    Serial.println("\n[WiFi] Gagal terhubung!");
  }
  
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(TIME_RESYNC_INTERVAL));
    
    if (WiFi.status() == WL_CONNECTED) {
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
      Serial.println("[Time] Re-sync NTP dilakukan.");
    }
  }
}

// ====================================================================
// --- TASK 2: READ DHT22 SENSOR ---
// ====================================================================
void TaskReadDHT(void *pvParameters) {
  (void) pvParameters;
  
  vTaskDelay(pdMS_TO_TICKS(2000));
  
  for (;;) {
    float temp = dht.readTemperature();
    float hum = dht.readHumidity();
    
    if (isnan(temp) || isnan(hum)) {
      Serial.println("[DHT] Error: Gagal baca sensor!");
      
      if (xSemaphoreTake(xDataMutex, portMAX_DELAY) == pdTRUE) {
        sysData.dhtError = true;
        xSemaphoreGive(xDataMutex);
      }
    } else {
      float correctedTemp = temp + TEMPERATURE_OFFSET;
      
      Serial.printf("[DHT] Suhu: %.1f°C | Kelembaban: %.1f%%\n", correctedTemp, hum);
      
      if (xSemaphoreTake(xDataMutex, portMAX_DELAY) == pdTRUE) {
        sysData.temperature = correctedTemp;
        sysData.humidity = hum;
        sysData.dhtError = false;
        xSemaphoreGive(xDataMutex);
      }
    }
    
    vTaskDelay(pdMS_TO_TICKS(DHT_READ_INTERVAL));
  }
}

// ====================================================================
// --- TASK 3: CONTROL RELAY (FORCE MODE SYSTEM) ---
// ====================================================================
void TaskControlRelay(void *pvParameters) {
  (void) pvParameters;
  struct tm timeinfo;
  static int lastHour = -1;
  
  // Test relay saat startup
  Serial.println("\n[Relay] ========== TESTING RELAY ==========");
  digitalWrite(RELAY_PIN, HIGH);
  updateRelayState(true);
  Serial.println("[Relay] Relay set HIGH (test 2 detik)");
  vTaskDelay(pdMS_TO_TICKS(2000));
  digitalWrite(RELAY_PIN, LOW);
  updateRelayState(false);
  Serial.println("[Relay] Relay set LOW (test selesai)");
  Serial.println("[Relay] ========================================\n");
  vTaskDelay(pdMS_TO_TICKS(1000));
  
  // Tunggu WiFi & Time sync
  int waitCount = 0;
  while (waitCount < 60) {
    SystemData data = getSystemData();
    if (data.wifiConnected && data.timeSync) break;
    
    Serial.printf("[Relay] Menunggu WiFi & Time sync... (%d/60)\n", waitCount);
    vTaskDelay(pdMS_TO_TICKS(5000));
    waitCount++;
  }
  
  SystemData checkData = getSystemData();
  if (!checkData.wifiConnected || !checkData.timeSync) {
    Serial.println("[Relay] WARNING: WiFi/Time not ready!");
  } else {
    Serial.println("[Relay] ✓ Task Control Relay aktif!");
    Serial.println("[Relay] Mode: AUTO with FORCE override via MQTT");
    Serial.printf("[Relay] Schedule: ON at %d:00, OFF at %d:00\n", 
                  RELAY_ON_HOUR, RELAY_OFF_HOUR);
  }
  
  for (;;) {
    if (getLocalTime(&timeinfo)) {
      int currentHour = timeinfo.tm_hour;
      bool scheduleState = shouldRelayBeOn(currentHour);
      bool finalRelayState;
      
      // ⭐ CEK APAKAH ADA FORCE ACTIVE
      if (isForceActive) {
        // Ada FORCE → pakai force state
        finalRelayState = forceState;
        
        // Cek apakah sudah waktunya toggle (untuk cancel FORCE)
        if ((forceState == true && currentHour == RELAY_OFF_HOUR) ||
            (forceState == false && currentHour == RELAY_ON_HOUR)) {
          
          // Waktu toggle tercapai → Cancel FORCE, ikut schedule
          isForceActive = false;
          finalRelayState = scheduleState;
          
          Serial.println("\n[Relay] ========================================");
          Serial.printf("[Relay] Time: %02d:%02d - Schedule toggle time reached\n",
                        currentHour, timeinfo.tm_min);
          Serial.println("[Relay] ✓ FORCE CANCELLED → Back to AUTO schedule");
          Serial.printf("[Relay] Relay switched to: %s\n", finalRelayState ? "ON" : "OFF");
          Serial.println("[Relay] ========================================\n");
        }
        
      } else {
        // Tidak ada FORCE → ikut schedule
        finalRelayState = scheduleState;
      }
      
      // Apply perubahan relay jika berbeda
      int currentPinState = digitalRead(RELAY_PIN);
      if (currentPinState != finalRelayState) {
        digitalWrite(RELAY_PIN, finalRelayState ? HIGH : LOW);
        updateRelayState(finalRelayState);
        
        Serial.printf("[Relay] ⚡ State changed to: %s | Mode: %s\n", 
                      finalRelayState ? "ON" : "OFF",
                      isForceActive ? "FORCE" : "AUTO");
      }
      
      // Log status setiap jam berganti
      if (currentHour != lastHour) {
        Serial.printf("[Relay] %02d:%02d | Schedule: %s | Relay: %s | FORCE: %s\n",
                      currentHour, timeinfo.tm_min,
                      scheduleState ? "ON" : "OFF",
                      finalRelayState ? "ON" : "OFF",
                      isForceActive ? "ACTIVE" : "INACTIVE");
        lastHour = currentHour;
      }
      
    } else {
      Serial.println("[Relay] Time not available, relay OFF");
      digitalWrite(RELAY_PIN, LOW);
      updateRelayState(false);
    }
    
    vTaskDelay(pdMS_TO_TICKS(RELAY_CHECK_INTERVAL));
  }
}

// ====================================================================
// --- TASK 4: DISPLAY LCD ---
// ====================================================================
void TaskDisplayLCD(void *pvParameters) {
  (void) pvParameters;
  struct tm timeinfo;
  bool showTemperature = true;
  
  for (;;) {
    SystemData data = getSystemData();
    
    lcd.clear();
    
    // Baris 1: Time + Temperature/Humidity
    if (data.timeSync && getLocalTime(&timeinfo)) {
      char timeStr[9];
      strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
      
      lcd.setCursor(0, 0);
      lcd.print(timeStr);
      
      if (!data.dhtError) {
        if (showTemperature) {
          lcd.print(" T:");
          lcd.print((int)data.temperature);
          lcd.write(223);
          lcd.print("C");
        } else {
          lcd.print(" H:");
          lcd.print((int)data.humidity);
          lcd.print("%");
        }
      } else {
        lcd.print(" DHT:ERR");
      }
    } else {
      lcd.setCursor(2, 0);
      lcd.print("====SYNC====");
    }
    
    // Baris 2: FORCE indicator + Wereng + Relay + WiFi
    lcd.setCursor(0, 1);
    
    // FORCE indicator
    if (isForceActive) {
      lcd.print("F");  // Force active
    } else {
      lcd.print("A");  // Auto (schedule)
    }
    
    // Wereng count
    lcd.print(" W:");
    lcd.print(data.werengCount);
    
    // Relay status
    int actualPinState = digitalRead(RELAY_PIN);
    lcd.print(" R:");
    if (actualPinState) {
      lcd.print("ON");
    } else {
      lcd.print("OF");
    }
    
    // WiFi status
    lcd.print(" ");
    if (data.wifiConnected) {
      lcd.write(0xFF);  // WiFi connected icon
    } else {
      lcd.print("X");   // No WiFi
    }
    
    vTaskDelay(pdMS_TO_TICKS(LCD_UPDATE_INTERVAL));
    showTemperature = !showTemperature;
  }
}

// ====================================================================
// --- TASK 5: MQTT OPERATIONS ---
// ====================================================================
void TaskMQTTOperations(void *pvParameters) {
  (void) pvParameters;
  
  espClient.setInsecure();
  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setKeepAlive(60);
  mqttClient.setSocketTimeout(30);
  
  while (!getSystemData().wifiConnected) {
    Serial.println("[MQTT] Menunggu WiFi...");
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
  
  Serial.println("[MQTT] Task aktif!");
  
  unsigned long lastPublishTime = 0;
  
  for (;;) {
    if (!mqttClient.connected()) {
      reconnectMQTT();
    }
    
    if (mqttClient.connected()) {
      mqttClient.loop();
      
      // Publish data secara periodik
      unsigned long currentMillis = millis();
      if (currentMillis - lastPublishTime >= MQTT_PUBLISH_INTERVAL) {
        publishToMQTT();
        lastPublishTime = currentMillis;
      }
    }
    
    vTaskDelay(pdMS_TO_TICKS(MQTT_LOOP_INTERVAL));
  }
}


// ====================================================================
// --- TASK 6: SEND TO GOOGLE SHEETS ---
// ====================================================================
void TaskSendToGoogleSheets(void *pvParameters) {
  (void) pvParameters;
  
  vTaskDelay(pdMS_TO_TICKS(10000));
  
  HTTPClient http;
  struct tm timeinfo;
  
  Serial.println("[GSheets] Task Google Sheets aktif!");
  
  for (;;) {
    SystemData data = getSystemData();
    
    if (data.wifiConnected && data.timeSync && getLocalTime(&timeinfo)) {
      
      char timestamp[30];
      strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S+07:00", &timeinfo);
      
      String payload = "{";
      payload += "\"timestamp\":\"" + String(timestamp) + "\",";
      payload += "\"temperature\":" + String(data.temperature, 1) + ",";
      payload += "\"humidity\":" + String(data.humidity, 1) + ",";
      payload += "\"wereng\":" + String(data.werengCount) + ",";
      payload += "\"relay\":" + String(data.relayState ? 1 : 0) + ",";
      payload += "\"rssi\":" + String(WiFi.RSSI()) + ",";
      payload += "\"mode\":\"" + String(isForceActive ? "FORCE" : "AUTO") + "\",";
      payload += "\"notes\":\"\"";
      payload += "}";
      
      Serial.println("[GSheets] Mengirim ke Google Sheets...");
      
      http.begin(GOOGLE_SCRIPT_URL);
      http.addHeader("Content-Type", "application/json");
      http.setTimeout(15000);
      
      int httpCode = http.POST(payload);
      
      if (httpCode > 0) {
        if (httpCode == 200 || httpCode == 302) {
          Serial.println("[GSheets] ✓ Data berhasil logged!");
        } else {
          Serial.printf("[GSheets] HTTP Code: %d\n", httpCode);
        }
      } else {
        Serial.printf("[GSheets] ✗ Error: %s\n", http.errorToString(httpCode).c_str());
      }
      
      http.end();
      
    } else {
      Serial.println("[GSheets] Skip: WiFi/Time belum siap");
    }
    
    vTaskDelay(pdMS_TO_TICKS(GSHEET_INTERVAL));
  }
}

// ====================================================================
// --- SETUP ---
// ====================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n========================================");
  Serial.println("   ESP32 WERENG DETECTION SYSTEM");
  Serial.println("   + FORCE Mode Control via MQTT");
  Serial.println("   + Google Sheets Logging");
  Serial.println("========================================\n");
  
  xDataMutex = xSemaphoreCreateMutex();
  
  if (xDataMutex == NULL) {
    Serial.println("ERROR: Gagal buat Mutex!");
    while(1);
  }
  
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  
  Serial.println("[Setup] Hardware initialized");
  Serial.printf("[Setup] Relay Pin: %d set as OUTPUT\n", RELAY_PIN);
  Serial.println("[Setup] Default Mode: AUTO Schedule");
  Serial.printf("[Setup] Schedule: ON at %d:00, OFF at %d:00\n", 
                RELAY_ON_HOUR, RELAY_OFF_HOUR);
  Serial.println("[Setup] FORCE override via MQTT /lampu");
  
  Wire.begin(21, 22);
  lcd.begin();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("  Wereng System");
  lcd.setCursor(0, 1);
  lcd.print("  FORCE Control");
  
  dht.begin();
  
  vTaskDelay(pdMS_TO_TICKS(2000));
  
  xTaskCreatePinnedToCore(TaskSyncTime, "WiFi_NTP", 4096, NULL, 5, NULL, 0);
  xTaskCreatePinnedToCore(TaskReadDHT, "DHT_Sensor", 3072, NULL, 3, NULL, 1);
  xTaskCreatePinnedToCore(TaskControlRelay, "Relay_Ctrl", 4096, NULL, 4, NULL, 0);
  xTaskCreatePinnedToCore(TaskDisplayLCD, "LCD_Display", 3072, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(TaskMQTTOperations, "MQTT_Client", 8192, NULL, 4, NULL, 1);
  xTaskCreatePinnedToCore(TaskSendToGoogleSheets, "GSheets", 8192, NULL, 2, NULL, 1);
  
  Serial.println("[Setup] All tasks created!");
  Serial.println("========================================\n");
  
  vTaskDelete(NULL);
}

void loop() {
  // Empty - semua dikerjakan oleh FreeRTOS Tasks
}
