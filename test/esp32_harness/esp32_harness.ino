#include <Arduino.h>
#include <Wire.h>

constexpr uint8_t NANO_I2C_ADDRESS = 0x11;
constexpr int I2C_SDA_PIN = 21;
constexpr int I2C_SCL_PIN = 22;

constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint32_t REQUEST_INTERVAL_MS = 500;
constexpr uint32_t CALIBRATION_INTERVAL_MS = 15000;

struct CalibrationPacket {
  float scale;
  int32_t offset;
};

static_assert(sizeof(CalibrationPacket) == 8, "CalibrationPacket must be 8 bytes");

unsigned long lastRequestAt = 0;
unsigned long lastCalibrationAt = 0;
unsigned long lastValidReadAt = 0;

float lastWeight = 0.0f;
float requestScale = 1.0f;
bool toggleScale = false;

uint32_t okReads = 0;
uint32_t invalidReads = 0;
uint32_t commErrors = 0;

bool sendCalibration(float scale, int32_t offset) {
  CalibrationPacket packet{scale, offset};

  Wire.beginTransmission(NANO_I2C_ADDRESS);
  const uint8_t *raw = reinterpret_cast<const uint8_t *>(&packet);
  Wire.write(raw, sizeof(packet));
  uint8_t txStatus = Wire.endTransmission();

  if (txStatus != 0) {
    commErrors++;
    Serial.print("CAL FAIL status=");
    Serial.println(txStatus);
    return false;
  }

  Serial.print("CAL OK scale=");
  Serial.print(scale, 6);
  Serial.print(" offset=");
  Serial.println(offset);
  return true;
}

bool requestWeight(float &weightOut) {
  uint8_t received = Wire.requestFrom(static_cast<int>(NANO_I2C_ADDRESS), static_cast<int>(sizeof(float)));
  if (received != sizeof(float)) {
    while (Wire.available()) {
      Wire.read();
    }
    commErrors++;
    Serial.print("REQ FAIL bytes=");
    Serial.println(received);
    return false;
  }

  uint8_t raw[sizeof(float)] = {0};
  for (size_t index = 0; index < sizeof(float); index++) {
    if (!Wire.available()) {
      commErrors++;
      Serial.println("REQ FAIL underrun");
      return false;
    }
    raw[index] = static_cast<uint8_t>(Wire.read());
  }

  memcpy(&weightOut, raw, sizeof(float));

  if (!isfinite(weightOut)) {
    invalidReads++;
    Serial.println("REQ FAIL non-finite");
    return false;
  }

  return true;
}

void printStatus(float weight, unsigned long nowMs) {
  Serial.print("RX OK t=");
  Serial.print(nowMs);
  Serial.print(" weight=");
  Serial.print(weight, 4);

  if (lastValidReadAt != 0) {
    Serial.print(" dt=");
    Serial.print(nowMs - lastValidReadAt);
    Serial.print("ms");
  }

  Serial.print(" ok=");
  Serial.print(okReads);
  Serial.print(" invalid=");
  Serial.print(invalidReads);
  Serial.print(" commErr=");
  Serial.println(commErrors);
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(300);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 100000);
  delay(100);

  sendCalibration(1.0f, 0);

  lastRequestAt = millis();
  lastCalibrationAt = millis();

  Serial.println("ESP32 HX711 harness started");
}

void loop() {
  unsigned long now = millis();

  if (now - lastCalibrationAt >= CALIBRATION_INTERVAL_MS) {
    lastCalibrationAt = now;

    toggleScale = !toggleScale;
    requestScale = toggleScale ? 2.0f : 1.0f;

    sendCalibration(requestScale, 0);
  }

  if (now - lastRequestAt >= REQUEST_INTERVAL_MS) {
    lastRequestAt = now;

    float weight = 0.0f;
    if (requestWeight(weight)) {
      okReads++;
      lastWeight = weight;
      printStatus(lastWeight, now);
      lastValidReadAt = now;
    }
  }
}
