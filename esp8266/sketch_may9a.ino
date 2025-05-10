#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>

const char* serverUrl = "http://teristit.pythonanywhere.com/api/attendance";
const char* apiKey = "secret-key-123"; // Замените на ваш ключ
// Настройки дисплея
#define TFT_DC    D4
#define TFT_RST   D3
Adafruit_ST7789 tft = Adafruit_ST7789(-1, TFT_DC, TFT_RST);

// Датчик касания TTP223
#define TOUCH_PIN D1
unsigned long touchStartTime = 0;
bool configMode = false;
bool touchActive = false;

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
    // Убрали startWebServer() так как используем API
    sendAttendanceData("boot"); // Отправка события при запуске
  }
}

void loop() {
  // Обработка касания
  handleTouch();
  
  if (configMode) {
    server.handleClient();
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
  if (touchActive && touchState == LOW) {
    touchActive = false;
    if (millis() - touchStartTime < 1000) { // Короткое нажатие
      sendAttendanceData("in"); // Или "out" в зависимости от логики
      displayEventSent("in");
    }
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

bool connectWiFi() {
  if (strlen(settings.ssid) == 0) return false;
  
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
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    tft.println("\nConnected!");
    return true;
  }
  
  tft.println("\nFailed!");
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
    
    String payload = "{\"device_id\":\"ESP8266_01\",\"event_type\":\"" + eventType + "\"}";
    
    int httpCode = http.POST(payload);
    
    if (httpCode > 0) {
      String response = http.getString();
      Serial.println(response);
      
      // Вывод на дисплей
      tft.fillRect(0, 100, 240, 40, ST77XX_BLACK);
      tft.setTextColor(ST77XX_GREEN);
      tft.setCursor(10, 110);
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