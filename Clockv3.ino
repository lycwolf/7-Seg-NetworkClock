#include <Adafruit_NeoPixel.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <Wire.h>
#include <RTClib.h>         // RTClib supports DS3231

// ----- Configuration for NeoPixel display and WiFi -----
#define LED_PIN D4
#define NUM_LEDS 52
// Increase EEPROM size to store additional WiFi settings.
#define EEPROM_SIZE 128

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
ESP8266WebServer server(80);

// WiFi and NTP settings stored in EEPROM:
char ssid[32] = "FOXIOT";
char password[32] = "allthegooddoggos!";
// WiFi credentials will be stored at EEPROM addresses 49 and 81.
char ntpServer[32] = "pool.ntp.org";
int timeZone = -18000;
int brightness = 100;
uint32_t digitColor = 0x00FF00;
uint32_t colonColor = 0xFFFFFF;
bool use24Hour = true;  

// ----- NTP Client setup -----
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, ntpServer, timeZone, 60000);

// ----- RTC (DS3231) setup -----
// The RTC is connected via I2C on SDA=GPIO4, SCL=GPIO5.
RTC_DS3231 rtc;

// Arrays for time zone display and LED mapping.
const char* timeZones[] = {"UTC-8 (PST)", "UTC-7 (MST)", "UTC-6 (CST)", "UTC-5 (EST)"};
const int timeZoneOffsets[] = {-28800, -25200, -21600, -18000};

const uint8_t DIGIT_MAPPING[6][8] = {
    {50, 51, 48, 1, 0, 3, 2, 49},
    {46, 47, 44, 5, 4, 7, 6, 45},
    {41, 42, 39, 10, 9, 12, 11, 40},
    {37, 38, 35, 14, 13, 16, 15, 36},
    {32, 33, 30, 19, 18, 21, 20, 31},
    {28, 29, 26, 23, 22, 25, 24, 27}
};

const uint8_t COLON_LEDS[4] = {43, 34, 17, 8};

const uint8_t DIGIT_SEGMENTS[10][7] = {
    {1, 1, 1, 0, 1, 1, 1}, {0, 0, 1, 0, 0, 1, 0},
    {1, 0, 1, 1, 1, 0, 1}, {1, 0, 1, 1, 0, 1, 1},
    {0, 1, 1, 1, 0, 1, 0}, {1, 1, 0, 1, 0, 1, 1},
    {1, 1, 0, 1, 1, 1, 1}, {1, 0, 1, 0, 0, 1, 0},
    {1, 1, 1, 1, 1, 1, 1}, {1, 1, 1, 1, 0, 1, 1}
};

//
// Global variables for RTC sync logging.
DateTime lastSyncTime;      // Time of the last NTP sync update to the RTC
String rtcStatus = "OK";    // RTC status message

//
// --- Simple Circular Log Buffer ---
// Stores up to 20 log lines.
#define LOG_LINES 20
String logBuffer[LOG_LINES];
int logBufferIndex = 0;
bool logBufferFull = false;

void addLog(String line) {
  logBuffer[logBufferIndex] = line;
  logBufferIndex = (logBufferIndex + 1) % LOG_LINES;
  if (logBufferIndex == 0) logBufferFull = true;
}

String generateLogPage() {
  String page = "<html><body><h1>RTC Log</h1><pre>";
  if (logBufferFull) {
    for (int i = logBufferIndex; i < LOG_LINES; i++) {
      page += logBuffer[i] + "\n";
    }
  }
  for (int i = 0; i < logBufferIndex; i++) {
    page += logBuffer[i] + "\n";
  }
  page += "</pre></body></html>";
  return page;
}

//
// EEPROM storage: WiFi credentials are stored at addresses 49 and 81.
// Existing settings are stored as before.
//
void loadSettings() {
    EEPROM.get(0, brightness);
    EEPROM.get(4, digitColor);
    EEPROM.get(8, colonColor);
    EEPROM.get(12, timeZone);
    EEPROM.get(16, ntpServer);
    EEPROM.get(48, use24Hour);
    EEPROM.get(49, ssid);
    EEPROM.get(81, password);
}

void saveSettings() {
    EEPROM.put(0, brightness);
    EEPROM.put(4, digitColor);
    EEPROM.put(8, colonColor);
    EEPROM.put(12, timeZone);
    EEPROM.put(16, ntpServer);
    EEPROM.put(48, use24Hour);
    EEPROM.put(49, ssid);
    EEPROM.put(81, password);
    EEPROM.commit();
}

void wifiAnimation() {
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
      Serial.print(".");
      static int index = 0;
	    strip.clear();
	    strip.setPixelColor(index, strip.Color(0, 0, 255));
    	strip.show();
    	index = (index + 1) % NUM_LEDS;
	    delay(500);
    }
}

//
// Helper function: Convert color to 6-digit hex string (padded with zeros).
String toHex6(uint32_t color) {
  String hexString = String(color, HEX);
  while (hexString.length() < 6) {
    hexString = "0" + hexString;
  }
  return hexString;
}

//
// Build the web page including WiFi fields.
//
String getWebPage() {
    String page = "<html><body>";
    page += "<h1>ESP8266 Clock Settings</h1>";
    page += "<form action='/update' method='post'>";
    page += "Brightness: <input type='range' name='brightness' min='0' max='100' value='" + String(brightness) + "'><br>";
    page += "Digit Color: <input type='color' name='digitColor' value='#" + toHex6(digitColor) + "'><br>";
    page += "Colon Color: <input type='color' name='colonColor' value='#" + toHex6(colonColor) + "'><br>";
    page += "Time Zone: <select name='timeZone'>";
    for (int i = 0; i < 4; i++) {
        page += "<option value='" + String(timeZoneOffsets[i]) + "'";
        if (timeZone == timeZoneOffsets[i]) page += " selected";
        page += ">" + String(timeZones[i]) + "</option>";
    }
    page += "</select><br>";
    page += "NTP Server: <input type='text' name='ntpServer' value='" + String(ntpServer) + "'><br>";
    page += "24 Hour Format: <input type='checkbox' name='use24Hour' value='1'";
    if (use24Hour) page += " checked";
    page += "><br>";
    page += "<h2>WiFi Settings</h2>";
    page += "WiFi SSID: <input type='text' name='wifiSSID' value='" + String(ssid) + "'><br>";
    page += "WiFi Password: <input type='password' name='wifiPassword' value='" + String(password) + "'><br>";
    page += "<input type='submit' value='Update & Reboot'>";
    page += "</form>";
    page += "<p><a href='/logs'>View Log</a></p>";
    page += "</body></html>";
    return page;
}

//
// Update settings from the web form. Also updates WiFi credentials.
// After saving the settings, the device restarts.
void handleUpdate() {
    if (server.hasArg("brightness")) brightness = server.arg("brightness").toInt();
    if (server.hasArg("digitColor")) digitColor = strtoul(server.arg("digitColor").c_str() + 1, NULL, 16);
    if (server.hasArg("colonColor")) colonColor = strtoul(server.arg("colonColor").c_str() + 1, NULL, 16);
    if (server.hasArg("timeZone")) timeZone = server.arg("timeZone").toInt();
    if (server.hasArg("ntpServer")) server.arg("ntpServer").toCharArray(ntpServer, sizeof(ntpServer));
    use24Hour = server.hasArg("use24Hour");
    if (server.hasArg("wifiSSID")) {
        String tmp = server.arg("wifiSSID");
        tmp.toCharArray(ssid, sizeof(ssid));
    }
    if (server.hasArg("wifiPassword")) {
        String tmp = server.arg("wifiPassword");
        tmp.toCharArray(password, sizeof(password));
    }
    saveSettings();
    server.send(200, "text/html", "<html><body><h1>Settings Updated. Restarting...</h1></body></html>");
    delay(2000);
    ESP.restart();
}

//
// Use the same displayTime() function as before to render digits on the LED strip.
void displayTime(int hours, int minutes, int seconds) {
    strip.clear();
    strip.setBrightness(map(brightness, 0, 100, 0, 255));
    int digits[6] = {hours / 10, hours % 10, minutes / 10, minutes % 10, seconds / 10, seconds % 10};
    for (int i = 0; i < 6; i++) {
        for (int j = 0; j < 7; j++) {
            if (DIGIT_SEGMENTS[digits[i]][j]) {
                strip.setPixelColor(DIGIT_MAPPING[i][j], digitColor);
                if (j == 3) {
                    strip.setPixelColor(DIGIT_MAPPING[i][7], digitColor);
                }
            }
        }
    }
    for (int i = 0; i < 4; i++) {
        strip.setPixelColor(COLON_LEDS[i], colonColor);
    }
    strip.show();
}

//
// Log current settings including RTC info; add log line to the circular log buffer.
void logCurrentSettings() {
    DateTime now = rtc.now();
    char logLine[128];
    sprintf(logLine, "RTC: %02d:%02d:%02d | Last Sync: %04d/%02d/%02d %02d:%02d:%02d | Status: %s | TZ=%d | Bright=%d",
            now.hour(), now.minute(), now.second(),
            lastSyncTime.year(), lastSyncTime.month(), lastSyncTime.day(),
            lastSyncTime.hour(), lastSyncTime.minute(), lastSyncTime.second(),
            rtcStatus.c_str(), timeZone, brightness);
    Serial.println(logLine);
    addLog(String(logLine));
}

//
// Setup: Initialize EEPROM, RTC, WiFi and web server endpoints.
// If WiFi fails to connect within 10 seconds, start AP mode.
void setup() {
    Serial.begin(115200);
    EEPROM.begin(EEPROM_SIZE);
    loadSettings();
    strip.begin();
    strip.show();

    // Initialize I2C with SDA=GPIO4, SCL=GPIO5 for the RTC.
    Wire.begin(4, 5);
    
    if (!rtc.begin()) {
        Serial.println("Couldn't find DS3231 RTC");
        while (1);  // Halt if RTC not found.
    }
    
    // Attempt to connect to WiFi with a 10-second timeout.
    unsigned long wifiStart = millis();
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.println("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED && (millis() - wifiStart) < 10000) {
      Serial.print(".");
      static int index = 0;
	    strip.clear();
	    strip.setPixelColor(index, strip.Color(0, 0, 255));
    	strip.show();
    	index = (index + 1) % NUM_LEDS;
	    delay(500);
    }
    bool wifiConnected = (WiFi.status() == WL_CONNECTED);
    if (wifiConnected) {
        Serial.println("WiFi connected.");
    } else {
        Serial.println("WiFi not connected within 10 seconds. Starting AP mode.");
        // Start AP mode so the device can be configured.
        Serial.println(WiFi.softAP("ESPClock_Config", "configureme") ? "Ready" : "Config Failed");
        Serial.print("AP IP address: ");
        Serial.println(WiFi.softAPIP());
    }

    // If RTC lost power, update it immediately from NTP if WiFi is available.
    if (rtc.lostPower()) {
        rtcStatus = "RTC lost power; ";
        if (WiFi.status() == WL_CONNECTED) {
            timeClient.begin();
            timeClient.setTimeOffset(timeZone);
            timeClient.update();
            rtc.adjust(DateTime(timeClient.getEpochTime()));
            lastSyncTime = DateTime(timeClient.getEpochTime());
            rtcStatus += "synced from NTP";
        } else {
            rtcStatus += "could not sync from NTP";
        }
    } else {
        rtcStatus = "RTC OK";
        lastSyncTime = rtc.now();
    }

    // Start NTP client regardless (for hourly sync if WiFi connects later).
    timeClient.begin();
    timeClient.setTimeOffset(timeZone);

    // Set up web server endpoints.
    server.on("/", HTTP_GET, []() {
        server.send(200, "text/html", getWebPage());
    });
    server.on("/logs", HTTP_GET, []() {
        server.send(200, "text/html", generateLogPage());
    });
    server.on("/update", HTTP_POST, handleUpdate);
    server.begin();
}

//
// Main loop: use the RTC for timekeeping and update from NTP at the top of each hour if WiFi is connected.
void loop() {
    server.handleClient();

    DateTime now = rtc.now();

    // Every hour (minute 0 and within the first 2 seconds) update RTC from NTP if WiFi is connected.
    static int lastSyncHour = -1;
    if (now.minute() == 0 && now.second() < 2 && now.hour() != lastSyncHour) {
        if (WiFi.status() == WL_CONNECTED) {
            timeClient.update();
            rtc.adjust(DateTime(timeClient.getEpochTime()));
            lastSyncTime = DateTime(timeClient.getEpochTime());
            lastSyncHour = now.hour();
            rtcStatus = "RTC updated from NTP";
            Serial.println("RTC updated from NTP.");
        } else {
            Serial.println("Hourly sync skipped: WiFi not connected.");
        }
    }

    // Choose hour to display (convert to 12-hour format if needed).
    int currentHour = now.hour();
    if (!use24Hour) {
        currentHour = currentHour % 12;
        if (currentHour == 0) currentHour = 12;
    }
    displayTime(currentHour, now.minute(), now.second());
    logCurrentSettings();
    delay(1000);
}
