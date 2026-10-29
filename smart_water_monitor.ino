#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <time.h>

#if __has_include(<Adafruit_SSD1306.h>)
  #include <Wire.h>
  #include <Adafruit_GFX.h>
  #include <Adafruit_SSD1306.h>
  #define USE_OLED 1
#else
  #define USE_OLED 0
#endif

// =====================
// Wi-Fi configuration
// =====================
// Replace these values before uploading. Do not hard-code secret values in production.
const char *WIFI_SSID = "YOUR_WIFI_SSID";
const char *WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char *SERVER_URL = "http://192.168.1.50:5000/api/data";

// =====================
// Sensor and board pins
// =====================
const int TRIG_PIN = 26;
const int ECHO_PIN = 27;
const int FLOW_SENSOR_PIN = 34;
const int TDS_SENSOR_PIN = 35;
const int RELAY_PIN = 25;
const int BUZZER_PIN = 15;
const int LED_WARN_PIN = 4;
const int LED_OK_PIN = 2;

// =====================
// Sensor calibration and thresholds
// =====================
const float TANK_EMPTY_DISTANCE_CM = 110.0;  // Distance when tank is empty
const float TANK_FULL_DISTANCE_CM = 10.0;    // Distance when tank is full
const float LOW_LEVEL_THRESHOLD = 20.0;        // Start pump below this level
const float HIGH_LEVEL_THRESHOLD = 80.0;       // Stop pump above this level
const float LEVEL_HYSTERESIS = 5.0;            // Prevent rapid switching
const float OVERFLOW_THRESHOLD = 95.0;         // Overflow warning
const float TDS_LIMIT = 300.0;                 // Simple educational threshold
const float PULSES_PER_LITER = 450.0;          // Typical flow sensor calibration factor
const float MAX_FLOW_LPM = 12.0;               // Abnormal flow rate limit
const float ADC_MAX = 4095.0;
const float VREF = 3.3;
const int SAMPLE_INTERVAL_MS = 5000;

volatile unsigned long pulseCount = 0;
unsigned long lastFlowSampleMs = 0;
unsigned long lastSendMs = 0;

float totalConsumptionLiters = 0.0;
float flowRateLpm = 0.0;
float lastLevelPercent = 0.0;
bool pumpOn = false;
bool sensorFailure = false;
bool pumpError = false;
String alertStatus = "NORMAL";

#if USE_OLED
Adafruit_SSD1306 display(128, 64, &Wire, -1);
#endif

// =====================
// Interrupt service routine for flow sensor pulses
// =====================
void IRAM_ATTR pulseCounterISR() {
  pulseCount++;
}

// =====================
// Utility functions
// =====================
String getFormattedTimestamp() {
  time_t now = time(nullptr);
  struct tm timeinfo;
  char timestampBuffer[30];

  if (localtime_r(&now, &timeinfo) == nullptr) {
    return "2026-01-01T00:00:00Z";
  }

  strftime(timestampBuffer, sizeof(timestampBuffer), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(timestampBuffer);
}

float readDistanceCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long duration = pulseIn(ECHO_PIN, HIGH, 25000);

  if (duration == 0) {
    return -1.0;
  }

  // Distance = (velocity * time) / 2
  float distanceCm = (duration * 0.0343) / 2.0;
  return distanceCm;
}

float computeWaterLevelPercent(float distanceCm) {
  if (distanceCm < 0) {
    return 0.0;
  }

  // Convert distance (cm) to level percentage.
  // A smaller distance means a fuller tank, so the formula is inverted.
  float range = TANK_EMPTY_DISTANCE_CM - TANK_FULL_DISTANCE_CM;
  float levelPercent = 100.0 * ((TANK_EMPTY_DISTANCE_CM - distanceCm) / range);

  if (levelPercent < 0.0) {
    levelPercent = 0.0;
  } else if (levelPercent > 100.0) {
    levelPercent = 100.0;
  }

  return levelPercent;
}

float readTdsValue() {
  int rawValue = analogRead(TDS_SENSOR_PIN);

  if (rawValue < 10) {
    return -1.0;
  }

  float voltage = (rawValue / ADC_MAX) * VREF;
  float tdsValue = voltage * 1000.0; // Approximate conversion for educational demo
  return tdsValue;
}

void updateFlowMetrics() {
  unsigned long now = millis();
  unsigned long elapsedMs = now - lastFlowSampleMs;

  if (elapsedMs < 2000) {
    return;
  }

  unsigned long pulseTotal = pulseCount;
  pulseCount = 0;

  if (pulseTotal > 0) {
    float elapsedSeconds = elapsedMs / 1000.0;
    float liters = pulseTotal / PULSES_PER_LITER;
    totalConsumptionLiters += liters;

    // flowRate = pulses per second * 60 / pulsesPerLiter
    flowRateLpm = (pulseTotal / elapsedSeconds) * 60.0 / PULSES_PER_LITER;
  } else {
    flowRateLpm = 0.0;
  }

  lastFlowSampleMs = now;
}

void setPumpState(bool on) {
  pumpOn = on;
  digitalWrite(RELAY_PIN, on ? LOW : HIGH);
}

void updateDisplay(float waterLevelPercent, float tdsValue, float flowRate, float totalLiters) {
#if USE_OLED
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("Level: ");
  display.print(waterLevelPercent, 1);
  display.println("%");

  display.print("TDS: ");
  display.print(tdsValue, 1);
  display.println(" ppm");

  display.print("Flow: ");
  display.print(flowRate, 2);
  display.println(" L/m");

  display.print("Pump: ");
  display.println(pumpOn ? "ON" : "OFF");

  display.display();
#endif
}

void setLocalAlertIndicators(bool hasAlert) {
  digitalWrite(BUZZER_PIN, hasAlert ? HIGH : LOW);
  digitalWrite(LED_WARN_PIN, hasAlert ? HIGH : LOW);
  digitalWrite(LED_OK_PIN, hasAlert ? LOW : HIGH);
}

void handlePumpControl(float waterLevelPercent) {
  bool desiredPumpState = pumpOn;

  if (waterLevelPercent <= LOW_LEVEL_THRESHOLD) {
    desiredPumpState = true;
  } else if (waterLevelPercent >= HIGH_LEVEL_THRESHOLD) {
    desiredPumpState = false;
  }

  // Hysteresis / dead-band to avoid rapid relay switching.
  if (pumpOn && waterLevelPercent >= HIGH_LEVEL_THRESHOLD + LEVEL_HYSTERESIS) {
    desiredPumpState = false;
  }

  if (!pumpOn && waterLevelPercent <= LOW_LEVEL_THRESHOLD - LEVEL_HYSTERESIS) {
    desiredPumpState = true;
  }

  if (desiredPumpState != pumpOn) {
    setPumpState(desiredPumpState);
  }
}

String evaluateAlertStatus(float waterLevelPercent, float tdsValue, bool flowAbnormal) {
  String alerts = "";

  if (sensorFailure) {
    alerts += "SENSOR_FAILURE;";
  }

  if (waterLevelPercent <= LOW_LEVEL_THRESHOLD) {
    alerts += "CRITICAL_LOW_LEVEL;";
  }

  if (waterLevelPercent >= OVERFLOW_THRESHOLD) {
    alerts += "OVERFLOW_HIGH_LEVEL;";
  }

  if (tdsValue > 0 && tdsValue >= TDS_LIMIT) {
    alerts += "POOR_WATER_QUALITY;";
  }

  if (flowAbnormal) {
    alerts += "ABNORMAL_WATER_CONSUMPTION;";
  }

  if (pumpError) {
    alerts += "PUMP_ABNORMAL_CONDITION;";
  }

  if (alerts.length() == 0) {
    return "NORMAL";
  }

  return alerts;
}

void sendSensorData(float distanceCm, float waterLevelPercent, float tdsValue, float totalLiters) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi disconnected. Skipping upload.");
    return;
  }

  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/json");

  String payload = "{";
  payload += "\"timestamp\":\"" + getFormattedTimestamp() + "\",";
  payload += "\"water_level_cm\":" + String(distanceCm, 2) + ",";
  payload += "\"water_level_percent\":" + String(waterLevelPercent, 2) + ",";
  payload += "\"tds\":" + String(tdsValue, 2) + ",";
  payload += "\"flow_rate_lpm\":" + String(flowRateLpm, 2) + ",";
  payload += "\"total_consumption_liters\":" + String(totalLiters, 2) + ",";
  payload += "\"pump_status\":\"" + String(pumpOn ? "ON" : "OFF") + "\",";
  payload += "\"system_status\":\"" + String(alertStatus == "NORMAL" ? "OK" : "ALERT") + "\",";
  payload += "\"alert_status\":\"" + alertStatus + "\",";
  payload += "\"sensor_status\":\"" + String(sensorFailure ? "ERROR" : "OK") + "\",";
  payload += "\"pump_error\":\"" + String(pumpError ? "ERROR" : "OK") + "\",";
  payload += "\"raw_data\":\"esp32_demo\"";
  payload += "}";

  int httpResponseCode = http.POST(payload);

  if (httpResponseCode > 0) {
    String responseText = http.getString();
    Serial.print("HTTP Response: ");
    Serial.println(httpResponseCode);
    Serial.println(responseText);
  } else {
    Serial.print("HTTP error code: ");
    Serial.println(httpResponseCode);
  }

  http.end();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(FLOW_SENSOR_PIN, INPUT_PULLUP);
  pinMode(TDS_SENSOR_PIN, INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_WARN_PIN, OUTPUT);
  pinMode(LED_OK_PIN, OUTPUT);

  digitalWrite(RELAY_PIN, HIGH);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LED_WARN_PIN, LOW);
  digitalWrite(LED_OK_PIN, HIGH);

#if USE_OLED
  Wire.begin();
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED not found");
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Booting...");
    display.display();
  }
#endif

  Serial.println("Starting Smart Water Monitor...");
  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("Wi-Fi connected: ");
    Serial.println(WiFi.localIP());
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  } else {
    Serial.println();
    Serial.println("Wi-Fi connection failed. Running offline mode.");
  }

  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), pulseCounterISR, FALLING);
  lastFlowSampleMs = millis();
  setPumpState(false);
}

void loop() {
  static unsigned long lastMeasurementMs = 0;

  if (millis() - lastMeasurementMs >= 2000) {
    lastMeasurementMs = millis();

    float distanceCm = readDistanceCm();
    float waterLevelPercent = 0.0;
    float tdsValue = readTdsValue();

    if (distanceCm < 0) {
      sensorFailure = true;
      Serial.println("Sensor read invalid: distance measurement failed.");
      waterLevelPercent = 0.0;
    } else {
      sensorFailure = false;
      waterLevelPercent = computeWaterLevelPercent(distanceCm);
      lastLevelPercent = waterLevelPercent;
    }

    if (tdsValue < 0) {
      sensorFailure = true;
      Serial.println("TDS sensor invalid or disconnected.");
    }

    updateFlowMetrics();
    handlePumpControl(waterLevelPercent);

    bool flowAbnormal = (flowRateLpm > MAX_FLOW_LPM) || (flowRateLpm < 0);
    alertStatus = evaluateAlertStatus(waterLevelPercent, tdsValue, flowAbnormal);
    setLocalAlertIndicators(alertStatus != "NORMAL");

    updateDisplay(waterLevelPercent, tdsValue, flowRateLpm, totalConsumptionLiters);

    Serial.print("Distance: ");
    Serial.print(distanceCm);
    Serial.print(" cm | Level: ");
    Serial.print(waterLevelPercent);
    Serial.print(" % | TDS: ");
    Serial.print(tdsValue);
    Serial.print(" ppm | Flow: ");
    Serial.print(flowRateLpm);
    Serial.print(" L/min | Total: ");
    Serial.print(totalConsumptionLiters);
    Serial.print(" L | Pump: ");
    Serial.print(pumpOn ? "ON" : "OFF");
    Serial.print(" | Alerts: ");
    Serial.println(alertStatus);
  }

  if (millis() - lastSendMs >= SAMPLE_INTERVAL_MS) {
    float distanceCm = readDistanceCm();
    float waterLevelPercent = 0.0;
    float tdsValue = readTdsValue();

    if (distanceCm >= 0) {
      waterLevelPercent = computeWaterLevelPercent(distanceCm);
    }

    if (tdsValue < 0) {
      tdsValue = 0.0;
    }

    sendSensorData(distanceCm, waterLevelPercent, tdsValue, totalConsumptionLiters);
    lastSendMs = millis();
  }

  delay(50);
}
