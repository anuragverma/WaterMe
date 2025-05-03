#include <WiFi.h>
#include <WiFiManager.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <esp_event.h>
#include <esp_wifi.h>
#include <time.h>
#include <Update.h>
#include <ESPmDNS.h>

WebServer server(80);

const char* configFile = "/config.json";
const char* logFile = "/watering.log";

const int UPPER_LINE_PIN = 26;  // Relay 1
const int LOWER_LINE_PIN = 27;  // Relay 2
const int LIGHT_RELAY_PIN = 25;  // Relay 3 for Light
const int FAN_RELAY_PIN = 33;    // Relay 4 for Fan

bool watering = false;
unsigned long upperLineStartTime = 0;
unsigned long lowerLineStartTime = 0;
int upperLineDuration = 0;
int lowerLineDuration = 0;
bool skipNextSchedule = false;
unsigned long lastScheduleCheck = 0;

struct WateringConfig {
  bool light_on;
  bool fan_on;
  String morning_time;
  String evening_time;
  int morning_valve1;
  int morning_valve2;
  int evening_valve1;
  int evening_valve2;
  int manual_upper_duration;
  int manual_lower_duration;
} config;

void saveDefaultConfig();

void appendLog(const String& entry) {
  File log = LittleFS.open(logFile, "a");
  if (!log) return;
  log.println(entry);
  log.close();

  File logRead = LittleFS.open(logFile, "r");
  std::vector<String> lines;
  while (logRead.available()) lines.push_back(logRead.readStringUntil('\n'));
  logRead.close();

  if (lines.size() > 25) {
    File trimmed = LittleFS.open("/tmp.log", "w");
    for (int i = lines.size() - 25; i < lines.size(); ++i) trimmed.println(lines[i]);
    trimmed.close();
    LittleFS.remove(logFile);
    LittleFS.rename("/tmp.log", logFile);
  }
}

String getCurrentTimeStr() {
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  char buf[20];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min);
  return String(buf);
}

String getNextScheduleStr() {
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", t->tm_hour, t->tm_min);
  String nowStr = String(buf);

  String label, timeStr;
  if (nowStr < config.morning_time) {
    timeStr = config.morning_time; label = "Today";
  } else if (nowStr < config.evening_time) {
    timeStr = config.evening_time; label = "Today";
  } else {
    timeStr = config.morning_time; label = "Tomorrow";
  }
  int hour = atoi(timeStr.substring(0, 2).c_str());
  String period = (hour < 6) ? "Night" : (hour < 12) ? "Morning" : (hour < 17) ? "Afternoon" : "Evening";
  return label + " - " + timeStr + " (" + period + ")";
}

void startWatering(int upDur, int lowDur) {
  if (upDur > 0) {
    digitalWrite(UPPER_LINE_PIN, LOW);
    upperLineStartTime = millis();
    upperLineDuration = upDur;
  }
  if (lowDur > 0) {
    digitalWrite(LOWER_LINE_PIN, LOW);
    lowerLineStartTime = millis();
    lowerLineDuration = lowDur;
  }
  watering = true;
  appendLog(getCurrentTimeStr() + " - Started watering (Upper=" + upperLineDuration + "s, Lower=" + lowerLineDuration + "s)");
}

void stopWatering() {
  digitalWrite(UPPER_LINE_PIN, HIGH);
  digitalWrite(LOWER_LINE_PIN, HIGH);
  upperLineDuration = 0;
  lowerLineDuration = 0;
  watering = false;
  appendLog(getCurrentTimeStr() + " - Stopped watering");
}

bool loadConfig() {
  if (!LittleFS.exists(configFile)) return false;
  File file = LittleFS.open(configFile, "r");
  if (!file) return false;
  StaticJsonDocument<512> doc;
  doc["light_on"] = config.light_on;
  doc["fan_on"] = config.fan_on;
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) return false;

  config.morning_time = doc["morning_time"] | "07:00";
  config.evening_time = doc["evening_time"] | "18:00";
  config.morning_valve1 = doc["morning_durations"]["valve1"] | 60;
  config.morning_valve2 = doc["morning_durations"]["valve2"] | 60;
  config.evening_valve1 = doc["evening_durations"]["valve1"] | 60;
  config.evening_valve2 = doc["evening_durations"]["valve2"] | 60;
  config.manual_upper_duration = doc["manual_durations"]["upper"] | 60;
  config.manual_lower_duration = doc["manual_durations"]["lower"] | 60;
  config.light_on = doc["light_on"] | false;
  config.fan_on = doc["fan_on"] | false;
  return true;
  return true;
}

void syncNTP() {
  configTime(19800, 0, "pool.ntp.org", "time.nist.gov");
  while (time(nullptr) < 8 * 3600 * 2) delay(500);
}

void checkSchedule() {
  if (watering || skipNextSchedule) return;
  String nowStr = getCurrentTimeStr().substring(11);
  if (nowStr == config.morning_time) startWatering(config.morning_valve1, config.morning_valve2);
  else if (nowStr == config.evening_time) startWatering(config.evening_valve1, config.evening_valve2);
  skipNextSchedule = false;
}

void setup() {
  Serial.begin(115200);
  pinMode(UPPER_LINE_PIN, OUTPUT);
  pinMode(LOWER_LINE_PIN, OUTPUT);
  pinMode(LIGHT_RELAY_PIN, OUTPUT);
  pinMode(FAN_RELAY_PIN, OUTPUT);
  digitalWrite(UPPER_LINE_PIN, HIGH);  // Relay OFF (active low)
  digitalWrite(LOWER_LINE_PIN, HIGH);  // Relay OFF (active low)
  digitalWrite(LIGHT_RELAY_PIN, HIGH); // Light OFF
  digitalWrite(FAN_RELAY_PIN, HIGH);   // Fan OFF

  if (config.light_on) digitalWrite(LIGHT_RELAY_PIN, LOW);
  if (config.fan_on) digitalWrite(FAN_RELAY_PIN, LOW);

  WiFiManager wm;
  if (!wm.autoConnect("AutoWaterConfig")) {
    Serial.println("WiFi failed"); return;
  }

  if (!LittleFS.begin()) {
    Serial.println("LittleFS failed"); return;
  }
  if (!LittleFS.exists(logFile)) {
    File log = LittleFS.open(logFile, "w");
    if (log) log.close();
  }
  if (!loadConfig()) {
    Serial.println("Loading config failed. Writing defaults.");
    saveDefaultConfig();
  }

  syncNTP();

  server.on("/", HTTP_GET, []() {
    File file = LittleFS.open("/index.html", "r");
    String mdnsNotice = "<!-- Access via http://watering.local -->";
    if (file) {
      server.send(200, "text/html", mdnsNotice + file.readString());
      file.close();
      return;
    }
    if (!file) {
      server.send(500, "text/plain", "File not found"); return;
    }
    server.streamFile(file, "text/html"); file.close();
  });

  server.on("/config", HTTP_GET, []() {
    File file = LittleFS.open("/config.html", "r");
    if (!file) {
      server.send(500, "text/plain", "File not found"); return;
    }
    server.streamFile(file, "text/html"); file.close();
  });

  server.on("/api/config", HTTP_GET, []() {
    StaticJsonDocument<512> doc;
    doc["morning_time"] = config.morning_time;
    doc["evening_time"] = config.evening_time;
    doc["morning_durations"]["valve1"] = config.morning_valve1;
    doc["morning_durations"]["valve2"] = config.morning_valve2;
    doc["evening_durations"]["valve1"] = config.evening_valve1;
    doc["evening_durations"]["valve2"] = config.evening_valve2;
    doc["manual_durations"]["upper"] = config.manual_upper_duration;
    doc["manual_durations"]["lower"] = config.manual_lower_duration;
    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
  });

  server.on("/api/config/reset", HTTP_POST, []() {
    saveDefaultConfig();
    server.send(200, "application/json", "{\"status\":\"Configuration reset to defaults\"}");
  });

  server.on("/api/config", HTTP_POST, []() {
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    if (error) {
      server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }
    config.morning_time = doc["morning_time"].as<String>();
    config.evening_time = doc["evening_time"].as<String>();
    config.morning_valve1 = doc["morning_durations"]["valve1"];
    config.morning_valve2 = doc["morning_durations"]["valve2"];
    config.evening_valve1 = doc["evening_durations"]["valve1"];
    config.evening_valve2 = doc["evening_durations"]["valve2"];
    config.manual_upper_duration = doc["manual_durations"]["upper"];
    config.manual_lower_duration = doc["manual_durations"]["lower"];
    config.light_on = digitalRead(LIGHT_RELAY_PIN) == LOW;
    config.fan_on = digitalRead(FAN_RELAY_PIN) == LOW;
    File file = LittleFS.open(configFile, "w");
    if (file) {
      serializeJsonPretty(doc, file);
      file.close();
    }
    server.send(200, "application/json", "{\"status\":\"Configuration updated\"}");
  });

  server.on("/update", HTTP_GET, []() {
    File file = LittleFS.open("/ota.html", "r");
    if (!file) { server.send(500, "text/plain", "File not found"); return; }
    server.streamFile(file, "text/html"); file.close();
  });

  server.on("/upload", HTTP_POST, []() {
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    appendLog(getCurrentTimeStr() + " - Firmware update " + ((Update.hasError()) ? "Failed" : "Successful") + ". Restarting...");
    delay(500); // <-- allow log to flush
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    appendLog(getCurrentTimeStr() + " - Starting firmware update.");

    if (upload.status == UPLOAD_FILE_START) Update.begin(UPDATE_SIZE_UNKNOWN);
    else if (upload.status == UPLOAD_FILE_WRITE) Update.write(upload.buf, upload.currentSize);
    else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {
        appendLog(getCurrentTimeStr() + " - OTA Update: Firmware write complete.");
      } else {
        appendLog(getCurrentTimeStr() + " - OTA Update: Firmware write failed.");
      }
    }
  });

  server.on("/api/trigger", HTTP_POST, []() {
    if (watering) {
      server.send(400, "application/json", "{\"error\":\"Watering already in progress\"}");
      return;
    }
    startWatering(config.manual_upper_duration, config.manual_lower_duration);
    server.send(200, "application/json", "{\"status\":\"Watering started manually\"}");
  });

  server.on("/api/trigger/upper", HTTP_POST, []() {
    if (watering) {
      server.send(400, "application/json", "{\"error\":\"Watering already in progress\"}");
      return;
    }
    startWatering(config.manual_upper_duration, 0);
    server.send(200, "application/json", "{\"status\":\"Upper line triggered\"}");
  });

  server.on("/api/trigger/lower", HTTP_POST, []() {
    if (watering) {
      server.send(400, "application/json", "{\"error\":\"Watering already in progress\"}");
      return;
    }
    startWatering(0, config.manual_lower_duration);
    server.send(200, "application/json", "{\"status\":\"Lower line triggered\"}");
  });

  server.on("/api/stop", HTTP_POST, []() {
    if (watering) {
      stopWatering();
      server.send(200, "application/json", "{\"status\":\"Watering stopped manually\"}");
    } else {
      server.send(400, "application/json", "{\"error\":\"Not currently watering\"}");
    }
  });

  server.on("/api/stop", HTTP_POST, []() {
    if (watering) {
      stopWatering();
      server.send(200, "application/json", "{\"status\":\"Watering stopped manually\"}");
    } else {
      server.send(400, "application/json", "{\"error\":\"Not currently watering\"}");
    }
  });

  server.on("/api/skip", HTTP_POST, []() {
    skipNextSchedule = true;
    server.send(200, "application/json", "{\"status\":\"Next watering schedule skipped\"}");
  });

  server.on("/api/skip/cancel", HTTP_POST, []() {
    skipNextSchedule = false;
    server.send(200, "application/json", "{\"status\":\"Skip cancelled\"}");
  });

  server.on("/api/schedule", HTTP_GET, []() {
    String json = "{\"next_schedule\":\"" + getNextScheduleStr() + "\",\"skip_next\":" + (skipNextSchedule ? "true" : "false") + "}";
    server.send(200, "application/json", json);
  });

  server.on("/api/status", HTTP_GET, []() {
    String json = "{";
    json += "\"watering\":" + String(watering ? "true" : "false");
    json += ",\"next\":\"" + getNextScheduleStr() + "\"";
    json += ",\"skip\":" + String(skipNextSchedule ? "true" : "false");
    json += ",\"light\":" + String(digitalRead(LIGHT_RELAY_PIN) == LOW ? "true" : "false");
    json += ",\"fan\":" + String(digitalRead(FAN_RELAY_PIN) == LOW ? "true" : "false");
    json += "}";
    server.send(200, "application/json", json);
  });

  server.on("/api/light/on", HTTP_POST, []() {
    digitalWrite(LIGHT_RELAY_PIN, LOW);
    config.light_on = true;
    File file = LittleFS.open(configFile, "w");
    if (file) {
      StaticJsonDocument<512> doc;
      doc["light_on"] = true;
      serializeJsonPretty(doc, file);
      file.close();
    }
    appendLog(getCurrentTimeStr() + " - Light turned ON");
    server.send(200, "application/json", "{\"status\":\"Light turned on\"}");
  });
  server.on("/api/light/off", HTTP_POST, []() {
    digitalWrite(LIGHT_RELAY_PIN, HIGH);
    config.light_on = false;
    File file = LittleFS.open(configFile, "w");
    if (file) {
      StaticJsonDocument<512> doc;
      doc["light_on"] = false;
      serializeJsonPretty(doc, file);
      file.close();
    }
    appendLog(getCurrentTimeStr() + " - Light turned OFF");
    server.send(200, "application/json", "{\"status\":\"Light turned off\"}");
  });
  server.on("/api/fan/on", HTTP_POST, []() {
    digitalWrite(FAN_RELAY_PIN, LOW);
    config.fan_on = true;
    File file = LittleFS.open(configFile, "w");
    if (file) {
      StaticJsonDocument<512> doc;
      doc["fan_on"] = true;
      serializeJsonPretty(doc, file);
      file.close();
    }
    appendLog(getCurrentTimeStr() + " - Fan turned ON");
    server.send(200, "application/json", "{\"status\":\"Fan turned on\"}");
  });
  server.on("/api/fan/off", HTTP_POST, []() {
    digitalWrite(FAN_RELAY_PIN, HIGH);
    config.fan_on = false;
    File file = LittleFS.open(configFile, "w");
    if (file) {
      StaticJsonDocument<512> doc;
      doc["fan_on"] = false;
      serializeJsonPretty(doc, file);
      file.close();
    }
    appendLog(getCurrentTimeStr() + " - Fan turned OFF");
    server.send(200, "application/json", "{\"status\":\"Fan turned off\"}");
  });

  server.on("/watering.log", HTTP_GET, []() {
    File file = LittleFS.open(logFile, "r");
    if (!file) {
      server.send(404, "text/plain", "Not found: /watering.log");
      return;
    }
    server.streamFile(file, "text/plain");
    file.close();
  });

  server.begin();
  Serial.println("HTTP server started");

  if (MDNS.begin("watering")) {
    Serial.println("mDNS responder started at http://watering.local");
  } else {
    Serial.println("Error setting up mDNS responder!");
  }
}

void saveDefaultConfig() {
  config.morning_time = "07:00";
  config.evening_time = "18:00";
  config.morning_valve1 = 60;
  config.morning_valve2 = 60;
  config.evening_valve1 = 60;
  config.evening_valve2 = 60;
  config.manual_upper_duration = 60;
  config.manual_lower_duration = 60;

  StaticJsonDocument<512> doc;
  doc["morning_time"] = config.morning_time;
  doc["evening_time"] = config.evening_time;
  doc["morning_durations"]["valve1"] = config.morning_valve1;
  doc["morning_durations"]["valve2"] = config.morning_valve2;
  doc["evening_durations"]["valve1"] = config.evening_valve1;
  doc["evening_durations"]["valve2"] = config.evening_valve2;
  doc["manual_durations"]["upper"] = config.manual_upper_duration;
  doc["manual_durations"]["lower"] = config.manual_lower_duration;

  File file = LittleFS.open(configFile, "w");
  if (file) {
    serializeJsonPretty(doc, file);
    file.close();
    Serial.println("Default config saved.");
  } else {
    Serial.println("Failed to save default config.");
  }
}

unsigned long lastNTPSync = 0;

void loop() {
  server.handleClient();
  if (watering) {
    unsigned long now = millis();
    if (upperLineDuration > 0 && now - upperLineStartTime >= upperLineDuration * 1000) {
      digitalWrite(UPPER_LINE_PIN, HIGH);
      upperLineDuration = 0;
      appendLog(getCurrentTimeStr() + " - Upper line stopped");
    }
    if (lowerLineDuration > 0 && now - lowerLineStartTime >= lowerLineDuration * 1000) {
      digitalWrite(LOWER_LINE_PIN, HIGH);
      lowerLineDuration = 0;
      appendLog(getCurrentTimeStr() + " - Lower line stopped");
    }
    if (upperLineDuration == 0 && lowerLineDuration == 0) {
      watering = false;
    }
    delay(500);
  }
  if (millis() - lastNTPSync > 6UL * 60UL * 60UL * 1000UL) {
    syncNTP();
    lastNTPSync = millis();
  }
  if (millis() - lastScheduleCheck > 60000) {
    checkSchedule(); lastScheduleCheck = millis(); delay(500);
  }
}
