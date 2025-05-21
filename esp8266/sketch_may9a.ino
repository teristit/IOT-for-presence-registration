#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>

const char* serverUrl = "http://teristit.pythonanywhere.com/api/attendance";
const char* serverUrlLastEvent = "http://teristit.pythonanywhere.com/api/last_event";
const char* serverUrlHealth = "http://teristit.pythonanywhere.com/api/health";
const char* apiKey = "secret-key-123"; 
const char* apiDeviceID = "ESP8266_01"; 
// Настройки дисплея
#define TFT_DC    D4
#define TFT_RST   D3
Adafruit_ST7789 tft = Adafruit_ST7789(-1, TFT_DC, TFT_RST);

String lastEventTime = "--:--:--";

// Датчик касания TTP223
#define TOUCH_PIN D1
unsigned long touchStartTime = 0;
bool configMode = false;
bool touchActive = false;

unsigned long lastWifiCheckTime = 0;
const unsigned long wifiCheckInterval = 10000;

int wifiReconnectAttempts = 0;
const int maxReconnectAttempts = 5;

// Веб-сервер
ESP8266WebServer server(80);

// Структура для хранения настроек
struct Settings {
  char ssid[32];
  char password[64];
};
Settings settings;

void setup() {
  Serial.begin(115200);
  
  // Инициализация дисплея
  tft.init(240, 240, SPI_MODE2);
  tft.setRotation(2);
  tft.fillScreen(ST77XX_BLACK);
  
  // Инициализация датчика касания
  pinMode(TOUCH_PIN, INPUT);
  
  // Инициализация EEPROM
  EEPROM.begin(sizeof(Settings));
  loadSettings();
  
  if (!connectWiFi()) {
    startConfigMode();
  } else {
    showIPAddress();
    String lastEvent = getLastEventFromServer();
    if (!lastEvent.isEmpty()) {
      displayLastEvent(lastEvent);
    } else {
      displayLastEvent("none");
    }
  }
}

void loop() {
  // Обработка касания
  handleTouch();
  
  if (configMode) {
    server.handleClient();
  }
  else {
    // Проверка соединения WiFi
    if (millis() - lastWifiCheckTime >= wifiCheckInterval) {
      lastWifiCheckTime = millis();
      
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi connection lost. Reconnecting...");
        tft.fillRect(0, 0, 240, 30, ST77XX_BLACK);
        tft.setCursor(10, 10);
        tft.setTextColor(ST77XX_RED);
        tft.print("Reconnecting WiFi...");
        
        WiFi.disconnect();
        if (!connectWiFi()) {
          wifiReconnectAttempts++;
          Serial.print("Failed to reconnect. Attempt ");
          Serial.println(wifiReconnectAttempts);
          
          if (wifiReconnectAttempts >= maxReconnectAttempts) {
            Serial.println("Max attempts reached. Restarting...");
            ESP.restart();
          }
        }
        else {
          // Восстановили соединение
          tft.fillRect(0, 0, 240, 30, ST77XX_BLACK);
          tft.setCursor(10, 10);
          tft.setTextColor(ST77XX_GREEN);
          tft.print("Connected: ");
          tft.print(settings.ssid);
        }
      }
    }
  }
}
void handleTouch() {
  int touchState = digitalRead(TOUCH_PIN);
  
  if (touchState == HIGH && !touchActive) {
    touchStartTime = millis();
    touchActive = true;
    displayTouchHint();
  }
  
  // Короткое касание (регистрация события)
  if (touchState == LOW && millis() - touchStartTime < 1000) {
    touchActive = false;
    String lastEvent = getLastEventFromServer();
    String newEvent = (lastEvent == "in") ? "out" : "in";
    sendAttendanceData(newEvent);
    tft.fillRect(0, 200, 240, 40, ST77XX_BLACK);
  }
  
  // Длинное касание (режим конфигурации)
  if (touchActive && touchState == HIGH && millis() - touchStartTime > 3000) {
    touchActive = false;
    if (!configMode) {
      startConfigMode();
    }
    tft.fillRect(0, 200, 240, 40, ST77XX_BLACK);
  }
}

String getLastEventFromServer() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected");
    return "";
  }

  WiFiClient client;
  HTTPClient http;
  
  String url = String(serverUrlLastEvent) + "/" + String(apiDeviceID);
  http.begin(client, url);
  http.addHeader("X-API-KEY", apiKey);
  http.addHeader("Device-ID", apiDeviceID);
  
  Serial.println("Requesting: " + url);
  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    Serial.println("Received: " + payload);

    DynamicJsonDocument doc(256); 
    DeserializationError error = deserializeJson(doc, payload);
    
    if (error) {
      Serial.print("JSON parse failed: ");
      Serial.println(error.c_str());
      return "";
    }

    if (doc.containsKey("last_event") && doc["last_event"].is<String>()) {
      // Извлекаем время из timestamp (формат: 2025-05-21T20:29:42.751466)
      if (doc.containsKey("timestamp")) {
        String timestamp = doc["timestamp"].as<String>();
        // Извлекаем только время (часы:минуты:секунды)
        lastEventTime = timestamp.substring(11, 19);
      }
      return doc["last_event"].as<String>();
    }
  } else {
    Serial.printf("HTTP error: %d\n", httpCode);
  }
  
  http.end();
  return "";
}


void displayLastEvent(String eventType) {
  tft.fillRect(0, 100, 240, 60, ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  
  tft.setCursor(10, 110);
  tft.print("Last event: ");
  if (eventType == "none") {
    tft.print("No data");
  } else {
    tft.print(eventType == "in" ? "IN" : "OUT");
  }
  
  tft.setCursor(10, 130);
  tft.print("Time: ");
  tft.print(lastEventTime); // Показываем время вместо следующего события
}

bool connectWiFi() {
  if (strlen(settings.ssid) == 0) return false;
  
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(10, 10);
  tft.print("Connecting to ");
  tft.println(settings.ssid);
  
  WiFi.begin(settings.ssid, settings.password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    tft.print(".");
    Serial.print(".");
    attempts++;
    if (attempts % 10 == 0) {
      WiFi.disconnect();
      delay(100);
      WiFi.begin(settings.ssid, settings.password);
    }
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    tft.println("\nConnected!");
    Serial.println("\nConnected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    return true;
  }
  
  tft.println("\nFailed!");
  Serial.println("\nFailed to connect!");
  return false;
}

void startConfigMode() {
  configMode = true;
  WiFi.softAP("TouchConfig");
  
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_YELLOW);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("CONFIG MODE");
  tft.setTextSize(1);
  tft.setCursor(10, 40);
  tft.println("AP: TouchConfig");
  tft.setCursor(10, 60);
  tft.println("IP: 192.168.4.1");
  tft.setCursor(10, 90);
  tft.println("Touch sensor ready");
  
  startWebServer();
}

void startWebServer() {
  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.begin();
}

void handleRoot() {
  String html = "<html><body>";
  html += "<h1>WiFi Touch Configuration</h1>";
  html += "<form action='/save' method='POST'>";
  html += "SSID: <input type='text' name='ssid' value=''><br>";
  html += "Password: <input type='password' name='password' value=''><br>";
  html += "<input type='submit' value='Save'>";
  html += "</form></body></html>";
  
  server.send(200, "text/html", html);
}

void handleSave() {
  if (server.hasArg("ssid") && server.hasArg("password")) {
    strncpy(settings.ssid, server.arg("ssid").c_str(), sizeof(settings.ssid));
    strncpy(settings.password, server.arg("password").c_str(), sizeof(settings.password));
    saveSettings();
    
    server.send(200, "text/html", "<h1>Settings saved! Device will restart...</h1>");
    delay(1000);
    ESP.restart();
  } else {
    server.send(400, "text/html", "<h1>Bad Request</h1>");
  }
}

void loadSettings() {
  EEPROM.get(0, settings);
  if (strlen(settings.ssid) == 0) {
    strcpy(settings.ssid, "");
    strcpy(settings.password, "");
  }
}

void saveSettings() {
  EEPROM.put(0, settings);
  EEPROM.commit();
}

void showIPAddress() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(1);
  tft.setCursor(10, 10);
  tft.print("Connected: ");
  tft.println(settings.ssid);
  tft.setCursor(10, 30);
  tft.print("IP: ");
  tft.println(WiFi.localIP());
  tft.setCursor(10, 60);
  tft.println("Touch and hold");
  tft.println("for config mode");
}

void sendAttendanceData(String eventType) {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClient client;
    HTTPClient http;
    
    http.begin(client, serverUrl);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-API-KEY", apiKey);
    http.addHeader("Device-ID", apiDeviceID);
    
    String payload = "{\"device_id\":\"ESP8266_01\",\"event_type\":\"" + eventType + "\"}";
    
    int httpCode = http.POST(payload);
    
    if (httpCode > 0) {
      String response = http.getString();
      Serial.println(response);
      
      // Обновляем время последнего события
      if (httpCode == 201) {
        // Парсим время из ответа сервера
        DynamicJsonDocument doc(256);
        DeserializationError error = deserializeJson(doc, response);
        if (!error && doc.containsKey("event_time")) {
          lastEventTime = doc["event_time"].as<String>();
        }
      }
      
      // Обновляем дисплей
      displayLastEvent(eventType);
      
      tft.fillRect(0, 150, 240, 40, ST77XX_BLACK);
      tft.setTextColor(ST77XX_GREEN);
      tft.setCursor(10, 160);
      tft.print("Status: ");
      tft.print(httpCode == 201 ? "OK" : "Error");
    }
    http.end();
  }
}

void displayTouchHint() {
  tft.fillRect(0, 200, 240, 40, ST77XX_BLACK);
  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(10, 210);
  tft.print("Hold for config...");
}

void displayEventSent(String eventType) {
  tft.fillRect(0, 150, 240, 40, ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(10, 160);
  tft.print("Sent: ");
  tft.print(eventType == "in" ? "IN" : "OUT");
}
