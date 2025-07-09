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
#include "driver/dac.h" 

// --- PIN DEFINITIONS ---
#define TFT_SCLK 14
#define TFT_MOSI 13
#define TFT_MISO 12
#define TFT_CS   15
#define TFT_DC   2
#define TFT_RST  -1
#define TFT_BL   21

// --- AUDIO PIN ---
#define AUDIO_OUT_PIN DAC_CHANNEL_2 

// --- RGB LED PINS ---
#define LED_RED_PIN    4
#define LED_GREEN_PIN  16
#define LED_BLUE_PIN   17

// --- MUSIC NOTES DEFINITION ---
#define REST      0
#define NOTE_E4  330
#define NOTE_G4  392
#define NOTE_A4  440
#define NOTE_AS4 466 // A#4
#define NOTE_B4  494
#define NOTE_C5  523
#define NOTE_D5  587
#define NOTE_E5  659
#define NOTE_F5  698
#define NOTE_G5  784
#define NOTE_A5  880

// --- SUPER MARIO MELODY AND TEMPO ---
int melody[] = {
  NOTE_E5, NOTE_E5, REST, NOTE_E5, REST, NOTE_C5, NOTE_E5, NOTE_G5, REST, NOTE_G4, REST,
  NOTE_C5, REST, NOTE_G4, REST, NOTE_E4, REST,
  NOTE_A4, REST, NOTE_B4, REST, NOTE_AS4, NOTE_A4, REST,
  NOTE_G4, NOTE_E5, NOTE_G5, NOTE_A5, REST, NOTE_F5, NOTE_G5,
  REST, NOTE_E5, REST, NOTE_C5, NOTE_D5, NOTE_B4
};

int tempo[] = {
  12, 12, 12, 12, 12, 12, 12, 6, 6, 6, 6,
  12, 12, 12, 12, 12, 12,
  12, 12, 12, 12, 12, 12, 12,
  9, 9, 9, 12, 12, 12,
  12, 12, 12, 12, 12, 12
};

// --- OBJECT INITIALIZATIONS ---
Adafruit_ST7789 tft = Adafruit_ST7789(&SPI, TFT_CS, TFT_DC, TFT_RST);
U8G2_FOR_ADAFRUIT_GFX u8g2_for_adafruit_gfx;
WebServer server(80);
DNSServer dnsServer;
Preferences preferences;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "time.google.com", 7 * 3600);

// --- GRAPH & HISTORY CONSTANTS ---
#define HISTORY_SIZE 60
int rssiHistory[HISTORY_SIZE];
const int GRAPH_X_START = 0;
const int GRAPH_Y_START = 40;
const int GRAPH_HEIGHT = 240 - GRAPH_Y_START;
const int GRAPH_WIDTH = 320 - GRAPH_X_START;
const int Y_GRAPH_PADDING_BOTTOM = 5;
const int Y_GRAPH_PADDING_TOP = 5;

// --- GLOBAL VARIABLES ---
int y_20_line_val, y_40_line_val, y_60_line_val, y_80_line_val, y_100_line_val;
bool wasConnected = false;
bool mainDisplayActive = false;
bool isDisconnectedState = false;
unsigned long lastUpdateTime = 0;
const long updateInterval = 1000;

String configuredSsid = "";
String configuredPassword = "";
bool wifiConfiguredSuccessfully = false;

// --- COLOR DEFINITIONS ---
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

// --- CONFIGURATION CONSTANTS ---
const int MAX_SSID_DISPLAY_LEN = 30;
const char *AP_SSID = "Rapptor IOT";
const char *AP_PASSWORD = "rapptoradmin";
bool configModeActive = false;
#define CONFIG_BUTTON_PIN 0
unsigned long lastButtonStateChangeTime = 0;
int lastButtonReading = HIGH;
int buttonState = HIGH;
bool firstPressDetected = false;
unsigned long firstPressTime = 0;
const unsigned long LONG_PRESS_DURATION = 1500;

// --- RESTART VARIABLES ---
bool restarting = false;
int restartCountdown = 0;
unsigned long lastCountdownUpdateTime = 0;
const long countdownUpdateInterval = 1000;


// =================================================================
// ==== UTILITY AND DISPLAY FUNCTIONS ==============================
// =================================================================

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
  int line_height = u8g2_for_adafruit_gfx.getFontAscent() - u8g2_for_adafruit_gfx.getFontDescent() + 4;
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
    u8g2_for_adafruit_gfx.drawUTF8(GRAPH_X_START + LABEL_LEFT_OFFSET, y_20_line_val - LABEL_Y_GAP_FROM_LINE - abs(fontDescent), "-20dBm");
    y_40_line_val = map(-40, -100, 0, y_drawing_area_bottom, y_drawing_area_top);
    tft.drawLine(GRAPH_X_START, y_40_line_val, x_line_end, y_40_line_val, GRAPH_REF_LINE_40_COLOR);
    u8g2_for_adafruit_gfx.setForegroundColor(GRAPH_REF_LINE_40_COLOR);
    u8g2_for_adafruit_gfx.drawUTF8(GRAPH_X_START + LABEL_LEFT_OFFSET, y_40_line_val - LABEL_Y_GAP_FROM_LINE - abs(fontDescent), "-40dBm");
    y_60_line_val = map(-60, -100, 0, y_drawing_area_bottom, y_drawing_area_top);
    tft.drawLine(GRAPH_X_START, y_60_line_val, x_line_end, y_60_line_val, GRAPH_REF_LINE_60_COLOR);
    u8g2_for_adafruit_gfx.setForegroundColor(GRAPH_REF_LINE_60_COLOR);
    u8g2_for_adafruit_gfx.drawUTF8(GRAPH_X_START + LABEL_LEFT_OFFSET, y_60_line_val - LABEL_Y_GAP_FROM_LINE - abs(fontDescent), "-60dBm");
    y_80_line_val = map(-80, -100, 0, y_drawing_area_bottom, y_drawing_area_top);
    tft.drawLine(GRAPH_X_START, y_80_line_val, x_line_end, y_80_line_val, GRAPH_REF_LINE_80_COLOR);
    u8g2_for_adafruit_gfx.setForegroundColor(GRAPH_REF_LINE_80_COLOR);
    u8g2_for_adafruit_gfx.drawUTF8(GRAPH_X_START + LABEL_LEFT_OFFSET, y_80_line_val - LABEL_Y_GAP_FROM_LINE - abs(fontDescent), "-80dBm");
    y_100_line_val = map(-100, -100, 0, y_drawing_area_bottom, y_drawing_area_top);
    tft.drawLine(GRAPH_X_START, y_100_line_val, x_line_end, y_100_line_val, GRAPH_REF_LINE_100_COLOR);
    u8g2_for_adafruit_gfx.setForegroundColor(GRAPH_REF_LINE_100_COLOR);
    u8g2_for_adafruit_gfx.drawUTF8(GRAPH_X_START + LABEL_LEFT_OFFSET, y_100_line_val - LABEL_Y_GAP_FROM_LINE - abs(fontDescent), "-100dBm");
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
    tft.drawLine(x1, y1, x2, y2, lineColor);
  }
}

void saveCredentials() {
  preferences.begin("wifi-creds", false);
  preferences.putString("ssid", configuredSsid);
  preferences.putString("pass", configuredPassword);
  preferences.end();
}

void loadCredentials() {
  preferences.begin("wifi-creds", true);
  configuredSsid = preferences.getString("ssid", "");
  configuredPassword = preferences.getString("pass", "");
  preferences.end();
}

// =================================================================
// ==== AUDIO AND LED FUNCTIONS (MATCHING THE SCHEMATIC) ===========
// =================================================================

void setLedColor(int red, int green, int blue) {
  analogWrite(LED_RED_PIN, red);
  analogWrite(LED_GREEN_PIN, green);
  analogWrite(LED_BLUE_PIN, blue);
}

void manageLed() {
  if (WiFi.status() == WL_CONNECTED) {
    int rssi = WiFi.RSSI();

   
    if (rssi >= -55) {
      setLedColor(255, 0, 0);
    } else if (rssi >= -65) {
      setLedColor(255, 100, 0);
    } else if (rssi >= -75) {
      setLedColor(255, 255, 0);
    } else {
      setLedColor(0, 255, 0);
    }

  } else {
    //
    setLedColor(0, 0, 255); 
  }
}


void playTone(int frequency, int duration) {
    if (frequency == 0) {
        delay(duration);
        return;
    }
    const int sampleRate = 20000;
    const int numSamples = duration * sampleRate / 1000;
    for (int i = 0; i < numSamples; i++) {
        int sample = 128 + 127 * sin(2 * PI * frequency * i / sampleRate);
        dac_output_voltage(AUDIO_OUT_PIN, sample);
        delayMicroseconds(1000000 / sampleRate);
    }
    dac_output_voltage(AUDIO_OUT_PIN, 0);
}

void playStartupSound() {
  int songLength = sizeof(melody) / sizeof(melody[0]);
  bool ledIsOn = false;
  int wholeNote = 1800;
  
  for (int i = 0; i < songLength; i++) {
    int noteDuration = wholeNote / tempo[i];
    playTone(melody[i], noteDuration * 0.9);
    
    ledIsOn = !ledIsOn;
    if (ledIsOn) {
      setLedColor(255, 255, 255);
    } else {
      setLedColor(0, 0, 0);
    }
    
    int pauseBetweenNotes = noteDuration * 0.1;
    delay(pauseBetweenNotes);
  }
  setLedColor(0, 0, 0);
}


// =================================================================
// ==== WEB SERVER HANDLER FUNCTIONS ===============================
// =================================================================

void handleRoot() {
  displayCenteredMessageU8g2("Đang quét Wi-Fi...", SCREEN_TEXT_COLOR);
  String availableNetworks = "";
  int n = WiFi.scanNetworks();
  displayCenteredMessageU8g2("Kết nối Wi-Fi\n" + String(AP_SSID) + "\nTruy cập: 192.168.4.1", ST77XX_WHITE);
  if (n > 0) {
      for (int i = 0; i < n; ++i) {
          availableNetworks += "<option value='" + WiFi.SSID(i) + "'>" + WiFi.SSID(i) + " (" + WiFi.RSSI(i) + "dBm)</option>\n";
      }
  }
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'><title>Cấu hình Rapptor IOT</title>";
  html += "<style>body{font-family:Arial,sans-serif;text-align:center;margin-top:20px;background-color:#f0f0f0}.main-container{display:flex;flex-direction:column;align-items:center;gap:20px}.card{background-color:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1);text-align:left;width:90%;max-width:400px}h2{text-align:center;color:#333}input,select,button{width:100%;padding:10px;margin:8px 0;display:inline-block;border:1px solid #ccc;border-radius:4px;box-sizing:border-box}button,input[type=submit]{cursor:pointer;font-weight:bold;color:#fff;border:none}input[type=submit]{background-color:#4CAF50}input[type=submit]:hover{background-color:#45a049}.status-message{color:blue;font-weight:bold;margin-top:15px;text-align:center}.device-info{text-align:center;margin-top:20px;color:#555}</style>";
  html += "</head><body><div class='main-container'>";
  html += "<div class='card'><h2>Cấu hình Wi-Fi</h2><form id='wifiForm' action='/submit' method='post'><label for='ssid_select'>Chọn SSID:</label><select id='ssid_select'><option value=''>--Chọn mạng--</option>" + availableNetworks + "</select><label for='ssid_input'>Hoặc nhập SSID:</label><input type='text' id='ssid_input' name='ssid'><label for='password_input'>Mật khẩu:</label><input id='password_input' type='password' name='password'><input type='submit' value='Lưu & Kết nối'></form><div id='status' class='status-message'></div></div>";
  html += "<div class='device-info'><p><strong>Tên:</strong> Rapptor | <strong>Phiên bản:</strong> v1.2 | <strong>Github:</strong> <a href='https://github.com/bvd0101/rapptor-iot' target='_blank'>Link</a></p></div>";
  html += "</div><script>document.getElementById('wifiForm').addEventListener('submit',function(e){e.preventDefault();var t=new FormData(this);fetch(this.action,{method:'POST',body:new URLSearchParams(t)}).then(e=>e.text()).then(e=>{document.getElementById('status').innerHTML=e})}),document.getElementById('ssid_select').addEventListener('change',function(){this.value&&(document.getElementById('ssid_input').value=this.value)});</script></body></html>";
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
    server.send(400, "text/plain", "Thiếu SSID hoặc mật khẩu.");
  }
}

void handleNotFound() {
  server.send(404, "text/plain", "Không tìm thấy trang.");
}

void enterConfigMode() {
  displayCenteredMessageU8g2("Kết nối Wi-Fi\n" + String(AP_SSID) + "\nTruy cập: 192.168.4.1", ST77XX_WHITE);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  dnsServer.start(53, "*", apIP);
  server.on("/", handleRoot);
  server.on("/submit", HTTP_POST, handleSubmit);
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
      if (buttonState == LOW && !firstPressDetected) {
        firstPressDetected = true;
        firstPressTime = millis();
      }
      if (buttonState == HIGH) {
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

// =================================================================
// ==== MAIN SETUP AND LOOP FUNCTIONS ==============================
// =================================================================

void setup() {
  Serial.begin(115200);

  pinMode(LED_RED_PIN, OUTPUT);
  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(LED_BLUE_PIN, OUTPUT);
  dac_output_enable(AUDIO_OUT_PIN); 

  SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, -1);
  tft.init(240, 320);
  tft.setRotation(3);
  tft.invertDisplay(false);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  u8g2_for_adafruit_gfx.begin(tft);
  u8g2_for_adafruit_gfx.setFontMode(1);
  u8g2_for_adafruit_gfx.setFontDirection(0);

  tft.fillScreen(SCREEN_BG_COLOR);
  displayCenteredMessageU8g2("Rapptor IOT v1.2\nStarting...", ST77XX_WHITE, u8g2_font_unifont_t_vietnamese2);

  playStartupSound();

  pinMode(CONFIG_BUTTON_PIN, INPUT_PULLUP);
  tft.fillScreen(SCREEN_BG_COLOR);
  displayCenteredMessageU8g2("GIÁM SÁT RSSI ESP32", ST77XX_WHITE, u8g2_font_unifont_t_vietnamese2);
  delay(1500);

  loadCredentials();

  if (configuredSsid.length() > 0 && configuredPassword.length() > 0) {
    displayCenteredMessageU8g2("Đang kết nối...", SCREEN_TEXT_COLOR);
    WiFi.mode(WIFI_STA);
    WiFi.begin(configuredSsid.c_str(), configuredPassword.c_str());
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 15) {
      delay(500); attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      displayCenteredMessageU8g2("Kết nối thành công!\nIP: " + WiFi.localIP().toString(), ST77XX_WHITE);
      delay(2000);
      wifiConfiguredSuccessfully = true;
      timeClient.begin();
      if (timeClient.update()) {
        displayCenteredMessageU8g2("Sẵn sàng theo dõi Wi-Fi.", SCREEN_TEXT_COLOR);
      } else {
        displayCenteredMessageU8g2("KHÔNG LẤY ĐƯỢC THỜI GIAN", RSSI_WEAK_COLOR);
      }
      delay(1500);
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
      delay(50);
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
  manageLed();
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
      u8g2_for_adafruit_gfx.drawUTF8(18, u8g2_for_adafruit_gfx.getFontAscent() + 2, ("SSID: " + getDisplaySsid(configuredSsid)).c_str());
      String currentRssiStr = "RSSI: " + String(currentRssi) + " dBm";
      int y_rssi_time_line = (u8g2_for_adafruit_gfx.getFontAscent() + 2) + u8g2_for_adafruit_gfx.getFontAscent() + 10;
      uint16_t rssiTextColor;
      if (currentRssi >= -30) rssiTextColor = RSSI_GOOD_COLOR;
      else if (currentRssi >= -70) rssiTextColor = RSSI_MEDIUM_COLOR;
      else rssiTextColor = RSSI_WEAK_COLOR;
      u8g2_for_adafruit_gfx.setForegroundColor(rssiTextColor);
      u8g2_for_adafruit_gfx.drawUTF8(18, y_rssi_time_line, currentRssiStr.c_str());
      u8g2_for_adafruit_gfx.setForegroundColor(TIME_COLOR);
      String timeLabel = "Thời gian: " + timeClient.getFormattedTime();
      uint16_t totalTimeInfoWidth = u8g2_for_adafruit_gfx.getUTF8Width(timeLabel.c_str());
      u8g2_for_adafruit_gfx.drawUTF8(tft.width() - totalTimeInfoWidth - 18, y_rssi_time_line, timeLabel.c_str());
      for (int i = 0; i < HISTORY_SIZE - 1; i++) {
        rssiHistory[i] = rssiHistory[i + 1];
      }
      rssiHistory[HISTORY_SIZE - 1] = currentRssi;
      drawRssiGraph();
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
