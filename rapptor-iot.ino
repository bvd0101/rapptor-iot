#include <WiFi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <string.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <U8g2_for_Adafruit_GFX.h>

#define TFT_SCLK 14
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_CS 15
#define TFT_DC 2
#define TFT_RST -1
#define TFT_BL 21

Adafruit_ST7789 tft = Adafruit_ST7789(&SPI, TFT_CS, TFT_DC, TFT_RST);
U8G2_FOR_ADAFRUIT_GFX u8g2_for_adafruit_gfx;

#define HISTORY_SIZE 60
int rssiHistory[HISTORY_SIZE];

const int GRAPH_X_START = 0;
const int GRAPH_Y_START = 40;
const int GRAPH_HEIGHT = 240 - GRAPH_Y_START;
const int GRAPH_WIDTH = 320 - GRAPH_X_START;

const int Y_GRAPH_PADDING_BOTTOM = 5;
const int Y_GRAPH_PADDING_TOP = 5;

int y_20_line_val;
int y_40_line_val;
int y_60_line_val;
int y_80_line_val;
int y_100_line_val;

bool wasConnected = false;
bool mainDisplayActive = false;
bool isDisconnectedState = false;

unsigned long lastUpdateTime = 0;
const long updateInterval = 1000;

String configuredSsid = "";
String configuredPassword = "";
bool wifiConfiguredSuccessfully = false;

const uint16_t SCREEN_BG_COLOR = ST77XX_BLACK;
const uint16_t SCREEN_TEXT_COLOR = ST77XX_WHITE;
const uint16_t GRAPH_BG_COLOR = ST77XX_BLACK;
const uint16_t GRAPH_BORDER_COLOR = ST77XX_WHITE;

const uint16_t GRAPH_REF_LINE_20_COLOR = ST77XX_GREEN;
const uint16_t GRAPH_REF_LINE_40_COLOR = ST77XX_YELLOW;
const uint16_t GRAPH_REF_LINE_60_COLOR = ST77XX_YELLOW;
const uint16_t GRAPH_REF_LINE_80_COLOR = ST77XX_RED;
const uint16_t GRAPH_REF_LINE_100_COLOR = ST77XX_RED;
const uint16_t GRAPH_VERTICAL_LINE_COLOR = 0x2104;

const uint16_t RSSI_GOOD_COLOR = ST77XX_GREEN;
const uint16_t RSSI_MEDIUM_COLOR = ST77XX_YELLOW;
const uint16_t RSSI_WEAK_COLOR = ST77XX_RED;

const uint16_t TIME_COLOR = ST77XX_WHITE;

const int MAX_SSID_DISPLAY_LEN = 30;

WebServer server(80);
DNSServer dnsServer;

const char *AP_SSID = "Rapptor IOT";
const char *AP_PASSWORD = "vandiepadmin";
bool configModeActive = false;

#define CONFIG_BUTTON_PIN 0
unsigned long lastButtonStateChangeTime = 0;
int lastButtonReading = HIGH;
int buttonState = HIGH;
bool firstPressDetected = false;
unsigned long firstPressTime = 0;
const unsigned long LONG_PRESS_DURATION = 1500;

Preferences preferences;

WiFiUDP ntpUDP;
const char* ntpServer = "time.google.com";
const long timeOffset = 7 * 3600;
NTPClient timeClient(ntpUDP, ntpServer, timeOffset);

bool restarting = false;
int restartCountdown = 0;
unsigned long lastCountdownUpdateTime = 0;
const long countdownUpdateInterval = 1000;

String getDisplaySsid(String ssid, int maxLength = MAX_SSID_DISPLAY_LEN) {
  if (ssid.length() > maxLength) {
    return ssid.substring(0, maxLength - 3) + "...";
  }
  return ssid;
}

void displayCenteredMessageU8g2(String message, uint16_t color = ST77XX_WHITE, const uint8_t* font = u8g2_font_unifont_t_vietnamese2) {
  tft.fillScreen(SCREEN_BG_COLOR);
  u8g2_for_adafruit_gfx.setFontMode(1);
  u8g2_for_adafruit_gfx.setFontDirection(0);
  u8g2_for_adafruit_gfx.setFont(font);
  u8g2_for_adafruit_gfx.setForegroundColor(color);

  char messageCopy[message.length() + 1];
  strcpy(messageCopy, message.c_str());

  char *line = strtok(messageCopy, "\n");
  int lineCount = 0;
  while (line != NULL) {
    lineCount++;
    line = strtok(NULL, "\n");
  }

  int line_height = u8g2_for_adafruit_gfx.getFontAscent() - u8g2_for_adafruit_gfx.getFontDescent();
  line_height += 4;

  int totalMessageHeight = lineCount * line_height;
  int16_t startY = (tft.height() - totalMessageHeight) / 2;
  if (startY < 0) startY = 0;

  strcpy(messageCopy, message.c_str());
  line = strtok(messageCopy, "\n");
  int currentLineNum = 0;
  while (line != NULL) {
    uint16_t textWidth = u8g2_for_adafruit_gfx.getUTF8Width(line);
    int16_t centerX = (tft.width() - textWidth) / 2;
    int16_t currentY = startY + (currentLineNum * line_height) + u8g2_for_adafruit_gfx.getFontAscent();
    u8g2_for_adafruit_gfx.drawUTF8(centerX, currentY, line);
    line = strtok(NULL, "\n");
    currentLineNum++;
  }
}

void displayWarningU8g2(String message) {
  displayCenteredMessageU8g2(message, RSSI_WEAK_COLOR, u8g2_font_unifont_t_vietnamese2);
}

void drawStaticGraphElements() {
    tft.fillRect(GRAPH_X_START, GRAPH_Y_START, GRAPH_WIDTH, GRAPH_HEIGHT, GRAPH_BG_COLOR);
    tft.drawRect(GRAPH_X_START, GRAPH_Y_START, GRAPH_WIDTH - 1, GRAPH_HEIGHT - 1, GRAPH_BORDER_COLOR);

    int y_drawing_area_top = GRAPH_Y_START + Y_GRAPH_PADDING_TOP;
    int y_drawing_area_bottom = GRAPH_Y_START + GRAPH_HEIGHT - 1 - Y_GRAPH_PADDING_BOTTOM;

    u8g2_for_adafruit_gfx.setFont(u8g2_font_unifont_t_vietnamese2);
    u8g2_for_adafruit_gfx.setFontDirection(0);
    u8g2_for_adafruit_gfx.setFontMode(0);

    int x_line_end = GRAPH_X_START + GRAPH_WIDTH - 1;
    const int LABEL_LEFT_OFFSET = 5;
    const int LABEL_Y_GAP_FROM_LINE = 5;
    int fontDescent = u8g2_for_adafruit_gfx.getFontDescent();

    y_20_line_val = map(-20, -100, 0, y_drawing_area_bottom, y_drawing_area_top);
    tft.drawLine(GRAPH_X_START, y_20_line_val, x_line_end, y_20_line_val, GRAPH_REF_LINE_20_COLOR);
    u8g2_for_adafruit_gfx.setForegroundColor(GRAPH_REF_LINE_20_COLOR);
    u8g2_for_adafruit_gfx.setBackgroundColor(GRAPH_BG_COLOR);
    String label_20 = "-20dBm";
    u8g2_for_adafruit_gfx.drawUTF8(GRAPH_X_START + LABEL_LEFT_OFFSET, y_20_line_val - LABEL_Y_GAP_FROM_LINE - abs(fontDescent), label_20.c_str());

    y_40_line_val = map(-40, -100, 0, y_drawing_area_bottom, y_drawing_area_top);
    tft.drawLine(GRAPH_X_START, y_40_line_val, x_line_end, y_40_line_val, GRAPH_REF_LINE_40_COLOR);
    u8g2_for_adafruit_gfx.setForegroundColor(GRAPH_REF_LINE_40_COLOR);
    u8g2_for_adafruit_gfx.setBackgroundColor(GRAPH_BG_COLOR);
    String label_40 = "-40dBm";
    u8g2_for_adafruit_gfx.drawUTF8(GRAPH_X_START + LABEL_LEFT_OFFSET, y_40_line_val - LABEL_Y_GAP_FROM_LINE - abs(fontDescent), label_40.c_str());

    y_60_line_val = map(-60, -100, 0, y_drawing_area_bottom, y_drawing_area_top);
    tft.drawLine(GRAPH_X_START, y_60_line_val, x_line_end, y_60_line_val, GRAPH_REF_LINE_60_COLOR);
    u8g2_for_adafruit_gfx.setForegroundColor(GRAPH_REF_LINE_60_COLOR);
    u8g2_for_adafruit_gfx.setBackgroundColor(GRAPH_BG_COLOR);
    String label_60 = "-60dBm";
    u8g2_for_adafruit_gfx.drawUTF8(GRAPH_X_START + LABEL_LEFT_OFFSET, y_60_line_val - LABEL_Y_GAP_FROM_LINE - abs(fontDescent), label_60.c_str());

    y_80_line_val = map(-80, -100, 0, y_drawing_area_bottom, y_drawing_area_top);
    tft.drawLine(GRAPH_X_START, y_80_line_val, x_line_end, y_80_line_val, GRAPH_REF_LINE_80_COLOR);
    u8g2_for_adafruit_gfx.setForegroundColor(GRAPH_REF_LINE_80_COLOR);
    u8g2_for_adafruit_gfx.setBackgroundColor(GRAPH_BG_COLOR);
    String label_80 = "-80dBm";
    u8g2_for_adafruit_gfx.drawUTF8(GRAPH_X_START + LABEL_LEFT_OFFSET, y_80_line_val - LABEL_Y_GAP_FROM_LINE - abs(fontDescent), label_80.c_str());

    y_100_line_val = map(-100, -100, 0, y_drawing_area_bottom, y_drawing_area_top);
    tft.drawLine(GRAPH_X_START, y_100_line_val, x_line_end, y_100_line_val, GRAPH_REF_LINE_100_COLOR);
    u8g2_for_adafruit_gfx.setForegroundColor(GRAPH_REF_LINE_100_COLOR);
    u8g2_for_adafruit_gfx.setBackgroundColor(GRAPH_BG_COLOR);
    String label_100 = "-100dBm";
    u8g2_for_adafruit_gfx.drawUTF8(GRAPH_X_START + LABEL_LEFT_OFFSET, y_100_line_val - LABEL_Y_GAP_FROM_LINE - abs(fontDescent), label_100.c_str());

    u8g2_for_adafruit_gfx.setFontMode(1);
}

void drawRssiGraph() {
  int y_drawing_area_top = GRAPH_Y_START + Y_GRAPH_PADDING_TOP;
  int y_drawing_area_bottom = GRAPH_Y_START + GRAPH_HEIGHT - 1 - Y_GRAPH_PADDING_BOTTOM;

  tft.fillRect(GRAPH_X_START, GRAPH_Y_START, GRAPH_WIDTH, GRAPH_HEIGHT, GRAPH_BG_COLOR);
  drawStaticGraphElements();

  u8g2_for_adafruit_gfx.setFontMode(1);

  const int MAJOR_VERTICAL_LINE_INTERVAL = 5;
  const int GRAPH_TIME_SPAN_SECONDS = HISTORY_SIZE;

  u8g2_for_adafruit_gfx.setFont(u8g2_font_unifont_t_vietnamese2);
  uint16_t max_label_width = u8g2_for_adafruit_gfx.getUTF8Width("-100dBm");
  const int VERTICAL_LINE_X_START_OFFSET = max_label_width + 5 + 5;

  int y_vertical_start = GRAPH_Y_START + 1;
  int y_vertical_end = GRAPH_Y_START + GRAPH_HEIGHT - 2;

  for (int sec_ago = 0; sec_ago <= GRAPH_TIME_SPAN_SECONDS; sec_ago += MAJOR_VERTICAL_LINE_INTERVAL) {
    int current_x = GRAPH_X_START + 1 + ((GRAPH_WIDTH - 2) * sec_ago / GRAPH_TIME_SPAN_SECONDS);
    if (current_x >= VERTICAL_LINE_X_START_OFFSET) {
        for(int y_pixel = y_vertical_start; y_pixel <= y_vertical_end; y_pixel++) {
            tft.drawPixel(current_x, y_pixel, GRAPH_VERTICAL_LINE_COLOR);
        }
    }
  }

  for (int i = 0; i < HISTORY_SIZE - 1; i++) {
    int y1 = map(rssiHistory[i], -100, 0, y_drawing_area_bottom, y_drawing_area_top);
    int y2 = map(rssiHistory[i + 1], -100, 0, y_drawing_area_bottom, y_drawing_area_top);

    int x1 = GRAPH_X_START + 1 + ((GRAPH_WIDTH - 2) * i / (HISTORY_SIZE - 1));
    int x2 = GRAPH_X_START + 1 + ((GRAPH_WIDTH - 2) * (i + 1) / (HISTORY_SIZE - 1));

    uint16_t lineColor;
    if (rssiHistory[i + 1] >= -30) {
      lineColor = RSSI_GOOD_COLOR;
    } else if (rssiHistory[i + 1] >= -70) {
      lineColor = RSSI_MEDIUM_COLOR;
    } else {
      lineColor = RSSI_WEAK_COLOR;
    }

    int dx = abs(x2 - x1);
    int dy = abs(y2 - y1);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx - dy;

    int current_x = x1;
    int current_y = y1;

    while (true) {
        tft.drawPixel(current_x, current_y, lineColor);
        if (current_x == x2 && current_y == y2) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; current_x += sx; }
        if (e2 < dx) { err += dx; current_y += sy; }
    }
  }
}

void saveCredentials() {
  preferences.begin("wifi-creds", false);
  preferences.putString("ssid", configuredSsid);
  preferences.putString("pass", configuredPassword);
  preferences.end();
}

void loadCredentials() {
  preferences.begin("wifi-creds", false);
  configuredSsid = preferences.getString("ssid", "");
  configuredPassword = preferences.getString("pass", "");
  preferences.end();
}

void handleRoot() {
  // Hiển thị thông báo đang quét trên màn hình TFT để người dùng biết
  displayCenteredMessageU8g2("Đang quét Wi-Fi...", SCREEN_TEXT_COLOR);
  
  // Quét các mạng Wi-Fi có sẵn
  String availableNetworks = "";
  int n = WiFi.scanNetworks();
  
  // Quét xong, trả lại màn hình thông báo ban đầu
  displayCenteredMessageU8g2("Kết nối Wi-Fi\n" + String(AP_SSID) + "\nMật khẩu: " + String(AP_PASSWORD) + "\nTruy cập: 192.168.4.1", ST77XX_WHITE);

  if (n == 0) {
      availableNetworks = "<option value=''>Không tìm thấy mạng nào</option>";
  } else {
      for (int i = 0; i < n; ++i) {
          String ssid = WiFi.SSID(i);
          // Thêm một thẻ <option> cho mỗi mạng tìm thấy, kèm theo cường độ tín hiệu
          availableNetworks += "<option value='" + ssid + "'>" + ssid + " (" + WiFi.RSSI(i) + "dBm)</option>\n";
      }
  }

  // Bắt đầu tạo chuỗi HTML cho trang web
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1>";
  html += "<title>Cấu hình WiFi cho ESP32</title>";
  html += "<style>";
  html += "body{font-family: Arial, sans-serif; text-align: center; margin-top: 50px; background-color: #f0f0f0;}";
  html += ".container{background-color: #fff; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); display: inline-block;}";
  html += "input[type=text], input[type=password], select{width: 90%; padding: 10px; margin: 8px 0; display: inline-block; border: 1px solid #ccc; border-radius: 4px; box-sizing: border-box;}";
  html += "input[type=submit]{width: 90%; background-color: #4CAF50; color: white; padding: 14px 20px; margin: 8px 0; border: none; border-radius: 4px; cursor: pointer;}";
  html += "input[type=submit]:hover{background-color: #45a049;}";
  html += ".status-message{color: blue; font-weight: bold; margin-top: 15px;}";
  html += "</style>";
  html += "</head><body>";
  html += "<div class='container'>";
  html += "<h2>Cấu hình Wi-Fi cho ESP32</h2>";
  html += "<form action='/submit' method='post'>";
  
  // ---- PHẦN THAY ĐỔI ----
  // Thêm danh sách dropdown để chọn SSID
  html += "Chọn SSID từ danh sách:<br>";
  html += "<select id='ssid_select'>";
  html += "<option value=''>-- Chọn mạng có sẵn --</option>";
  html += availableNetworks; // Chèn danh sách mạng đã quét vào đây
  html += "</select><br>";
  html += "Hoặc nhập tên SSID:<br>";
  // Ô nhập văn bản SSID vẫn được giữ lại để người dùng có thể nhập tay hoặc khi mạng bị ẩn
  html += "<input type='text' name='ssid' value='" + configuredSsid + "' placeholder='Tên Wi-Fi (SSID)'><br>";
  // ---- KẾT THÚC PHẦN THAY ĐỔI ----
  
  html += "Mật khẩu:<br><input type='password' name='password'><br>";
  html += "<input type='submit' value='Xác nhận'>";
  html += "</form>";
  html += "<div id='status' class='status-message'></div>";
  html += "</div>";
  
  // Thêm mã JavaScript để cập nhật ô SSID khi chọn từ dropdown
  html += "<script>";
  html += "document.getElementById('ssid_select').addEventListener('change', function() {";
  html += "  if (this.value) { document.getElementsByName('ssid')[0].value = this.value; }";
  html += "});";
  html += "document.querySelector('form').addEventListener('submit', function(e) {";
  html += "e.preventDefault();";
  html += "var formData = new FormData(this);";
  html += "fetch(this.action, {";
  html += "method: 'POST',";
  html += "body: new URLSearchParams(formData)";
  html += "}).then(response => response.text())";
  html += ".then(data => {";
  html += "document.getElementById('status').innerHTML = data;";
  html += "if (data.includes('Kết nối thất bại!')) {";
  html += "document.getElementById('status').innerHTML += '<br>Vui lòng kiểm tra lại thông tin và thử lại.';";
  html += "}";
  html += "})";
  html += ".catch(error => console.error('Error:', error));";
  html += "});";
  html += "</script>";
  
  html += "</body></html>";
  server.send(200, "text/html; charset=UTF-8", html);
}

void handleSubmit() {
  if (server.hasArg("ssid") && server.hasArg("password")) {
    configuredSsid = server.arg("ssid");
    configuredPassword = server.arg("password");
    saveCredentials();
    displayCenteredMessageU8g2("Đã nhận thông tin!", ST77XX_GREEN);
    delay(1500);
    displayCenteredMessageU8g2("Đang kết nối...", SCREEN_TEXT_COLOR);

    WiFi.mode(WIFI_STA);
    WiFi.begin(configuredSsid.c_str(), configuredPassword.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 15) {
      delay(1000);
      attempts++;
    }

    String responseMessage;
    if (WiFi.status() == WL_CONNECTED) {
      wifiConfiguredSuccessfully = true;
      responseMessage = "Kết nối thành công! IP: " + WiFi.localIP().toString();
      restarting = true;
      restartCountdown = 4;
      lastCountdownUpdateTime = millis();
    } else {
      wifiConfiguredSuccessfully = false;
      responseMessage = "Kết nối thất bại!";
    }
    server.send(200, "text/plain", responseMessage);
  } else {
    server.send(400, "text/plain", "Yêu cầu không hợp lệ: Thiếu SSID hoặc mật khẩu.");
  }
}

void handleSwitchMode() {
  server.send(200, "text/plain", "Chuyển sang chế độ theo dõi. Thiết bị sẽ khởi động lại.");
  configModeActive = false;
  WiFi.softAPdisconnect(true);
  server.stop();
  dnsServer.stop();
  delay(100);
  ESP.restart();
}

void handleNotFound() {
  server.send(404, "text/plain", "Không tìm thấy trang.");
}

void enterConfigMode() {
  displayCenteredMessageU8g2("Kết nối Wi-Fi\n" + String(AP_SSID) + "\nMật khẩu: " + String(AP_PASSWORD) + "\nTruy cập: 192.168.4.1", ST77XX_WHITE);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

  dnsServer.start(53, "*", apIP);

  server.on("/", handleRoot);
  server.on("/submit", HTTP_POST, handleSubmit);
  server.on("/switchmode", HTTP_POST, handleSwitchMode);
  server.onNotFound(handleNotFound);

  server.begin();
  configModeActive = true;
}

void checkButtonPress() {
  int reading = digitalRead(CONFIG_BUTTON_PIN);

  if (reading != lastButtonReading) {
    lastButtonStateChangeTime = millis();
  }

  if ((millis() - lastButtonStateChangeTime) > 50) {
    if (reading != buttonState) {
      buttonState = reading;

      if (buttonState == LOW) {
        if (!firstPressDetected) {
          firstPressDetected = true;
          firstPressTime = millis();
        }
      } else {
        if (firstPressDetected && (millis() - firstPressTime) < LONG_PRESS_DURATION) {
        }
        firstPressDetected = false;
      }
    }
  }
  lastButtonReading = reading;

  if (firstPressDetected && buttonState == LOW && (millis() - firstPressTime) >= LONG_PRESS_DURATION) {
    enterConfigMode();
    firstPressDetected = false;
  }
}

void setup() {
  Serial.begin(115200);

  SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, -1);
  tft.init(240, 320);
  tft.setRotation(3);
  tft.invertDisplay(false);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  u8g2_for_adafruit_gfx.begin(tft);
  u8g2_for_adafruit_gfx.setFontMode(1);
  u8g2_for_adafruit_gfx.setFontDirection(0);

  pinMode(CONFIG_BUTTON_PIN, INPUT_PULLUP);

  tft.fillScreen(SCREEN_BG_COLOR);
  displayCenteredMessageU8g2("GIÁM SÁT RSSI ESP32", ST77XX_WHITE, u8g2_font_unifont_t_vietnamese2);
  delay(2000);

  loadCredentials();

  if (configuredSsid.length() > 0 && configuredPassword.length() > 0) {
    displayCenteredMessageU8g2("Đang kết nối...", SCREEN_TEXT_COLOR);
    delay(1500);

    WiFi.mode(WIFI_STA);
    WiFi.begin(configuredSsid.c_str(), configuredPassword.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 15) {
      delay(1000);
      attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      displayCenteredMessageU8g2("Kết nối thành công!", ST77XX_WHITE);
      delay(1500);
      displayCenteredMessageU8g2("IP: " + WiFi.localIP().toString(), ST77XX_WHITE);
      delay(2000);
      wifiConfiguredSuccessfully = true;

      timeClient.begin();

      bool ntpUpdated = false;
      int ntpAttempts = 0;
      while (ntpAttempts < 10) {
          if (timeClient.update()) {
              ntpUpdated = true;
              break;
          }
          timeClient.forceUpdate();
          delay(500);
          ntpAttempts++;
      }

      if (!ntpUpdated) {
        displayCenteredMessageU8g2("KHÔNG LẤY ĐƯỢC THỜI GIAN", RSSI_WEAK_COLOR);
      } else {
        displayCenteredMessageU8g2("Sẵn sàng theo dõi Wi-Fi.", SCREEN_TEXT_COLOR);
        delay(1000);
      }
    } else {
      displayWarningU8g2("Kết nối thất bại!");
      delay(2000);
    }
  }

  if (!wifiConfiguredSuccessfully || WiFi.status() != WL_CONNECTED) {
    displayWarningU8g2("Nhấn giữ nút BOOT\nđể cấu hình Wi-Fi.");
    while (!configModeActive) {
      checkButtonPress();
      if (configModeActive) break;
      delay(100);
    }
  }

  for (int i = 0; i < HISTORY_SIZE; i++) {
    rssiHistory[i] = -100;
  }

  wasConnected = (WiFi.status() == WL_CONNECTED);
  if (wasConnected && !restarting) {
    mainDisplayActive = true;
    tft.fillScreen(SCREEN_BG_COLOR);
    drawStaticGraphElements();
  }
}

void loop() {

  checkButtonPress();

  if (configModeActive) {
    dnsServer.processNextRequest();
    server.handleClient();
    if (restarting && millis() - lastCountdownUpdateTime > countdownUpdateInterval) {
      lastCountdownUpdateTime = millis();
      restartCountdown--;
      if (restartCountdown < 0) restartCountdown = 0;

      displayCenteredMessageU8g2("Kết nối thành công!\nKhởi động lại sau " + String(restartCountdown) + " giây", ST77XX_GREEN);

      if (restartCountdown == 0) {
        Serial.println("Restarting ESP32...");
        WiFi.softAPdisconnect(true);
        server.stop();
        dnsServer.stop();
        delay(100);
        ESP.restart();
      }
    }
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (!wasConnected) {
      tft.fillScreen(SCREEN_BG_COLOR);
      mainDisplayActive = true;
      isDisconnectedState = false;
      for (int i = 0; i < HISTORY_SIZE; i++) {
        rssiHistory[i] = -100;
      }
      lastUpdateTime = millis();
      drawStaticGraphElements();
    }
    wasConnected = true;

    if (mainDisplayActive && millis() - lastUpdateTime > updateInterval) {
      lastUpdateTime = millis();

      timeClient.update();

      int currentRssi = WiFi.RSSI();

      tft.fillRect(0, 0, tft.width(), GRAPH_Y_START, SCREEN_BG_COLOR);

      u8g2_for_adafruit_gfx.setFont(u8g2_font_unifont_t_vietnamese2);
      u8g2_for_adafruit_gfx.setFontMode(1);
      u8g2_for_adafruit_gfx.setFontDirection(0);

      u8g2_for_adafruit_gfx.setForegroundColor(SCREEN_TEXT_COLOR);
      String displaySsid = getDisplaySsid(configuredSsid);
      u8g2_for_adafruit_gfx.drawUTF8(18, u8g2_for_adafruit_gfx.getFontAscent() + 2, ("SSID: " + displaySsid).c_str());

      String currentRssiStr = "RSSI: " + String(currentRssi) + " dBm";
      uint16_t rssi_x_start = 18;
      int y_rssi_time_line = (u8g2_for_adafruit_gfx.getFontAscent() + 2) + u8g2_for_adafruit_gfx.getFontAscent() + 10;

      uint16_t rssiTextColor;
      if (currentRssi >= -30) {
        rssiTextColor = RSSI_GOOD_COLOR;
      } else if (currentRssi >= -70) {
        rssiTextColor = RSSI_MEDIUM_COLOR;
      } else {
        rssiTextColor = RSSI_WEAK_COLOR;
      }
      u8g2_for_adafruit_gfx.setForegroundColor(rssiTextColor);
      u8g2_for_adafruit_gfx.drawUTF8(rssi_x_start, y_rssi_time_line, currentRssiStr.c_str());

      u8g2_for_adafruit_gfx.setForegroundColor(TIME_COLOR);
      char timeString[9];
      timeClient.getFormattedTime().toCharArray(timeString, 9);
      String timeLabel = "Thời gian: ";

      uint16_t totalTimeInfoWidth = u8g2_for_adafruit_gfx.getUTF8Width((timeLabel + String(timeString)).c_str());
      int16_t x_time_start = tft.width() - totalTimeInfoWidth - 18;

      u8g2_for_adafruit_gfx.drawUTF8(x_time_start, y_rssi_time_line, (timeLabel + String(timeString)).c_str());

      for (int i = 0; i < HISTORY_SIZE - 1; i++) {
        rssiHistory[i] = rssiHistory[i + 1];
      }
      rssiHistory[HISTORY_SIZE - 1] = currentRssi;

      drawRssiGraph();

      if (currentRssi < -90) {
        if (!isDisconnectedState) {
          tft.fillRect(0, 0, tft.width(), GRAPH_Y_START, SCREEN_BG_COLOR);
          const int WEAK_SIGNAL_SSID_LEN = 15;
          displayWarningU8g2("TÍN HIỆU RẤT YẾU!\n" + getDisplaySsid(configuredSsid, WEAK_SIGNAL_SSID_LEN) + "\n" + String(currentRssi) + " dBm");
          delay(1000);
          tft.fillScreen(SCREEN_BG_COLOR);
          mainDisplayActive = true;
          drawStaticGraphElements();
        }
      }
    }
  } else {
    if (wasConnected) {
      isDisconnectedState = true;
      mainDisplayActive = false;
    }
    wasConnected = false;

    if (isDisconnectedState) {
      displayWarningU8g2("MẤT KẾT NỐI WIFI!");
    }
    delay(1000);
  }
}