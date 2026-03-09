#include <Arduino.h>
#include <EEPROM.h>
#include <Wire.h>
#include <HX711.h>

constexpr uint8_t HX711_DT_PIN = 9;
constexpr uint8_t HX711_SCK_PIN = 10;

constexpr uint8_t ARDUINO_I2C_ADDRESS = 0x11;
constexpr uint8_t ESP32_I2C_ADDRESS = 0x42;
constexpr uint8_t I2C_CMD_TARE_RESET = 0xA5;

constexpr unsigned long TX_INTERVAL_MS = 500;
constexpr unsigned long ERROR_LOG_INTERVAL_MS = 5000;
constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint32_t EEPROM_MAGIC = 0x57475331UL;
constexpr int EEPROM_ADDR = 0;

struct CalibrationData {
  uint32_t magic;
  float scale;
  long offset;
};

struct IncomingCalibrationPacket {
  float scale;
  long offset;
};

HX711 weightSensor;

CalibrationData calibration = {EEPROM_MAGIC, 1.0f, 0L};

volatile bool calibrationUpdatePending = false;
volatile bool tareResetPending = false;
volatile float pendingScale = 1.0f;
volatile long pendingOffset = 0L;

float latestWeight = 0.0f;
long latestRaw = 0L;
unsigned long lastTransmitAt = 0;
unsigned long lastReadErrorLogAt = 0;

void loadCalibrationFromEeprom() {
  CalibrationData stored{};
  EEPROM.get(EEPROM_ADDR, stored);

  if (stored.magic != EEPROM_MAGIC || !isfinite(stored.scale) || stored.scale == 0.0f) {
    calibration.magic = EEPROM_MAGIC;
    calibration.scale = 1.0f;
    calibration.offset = 0L;
    EEPROM.put(EEPROM_ADDR, calibration);
    Serial.println(F("[EEPROM] Invalid or empty calibration. Using defaults scale=1.000000 offset=0"));
    return;
  }

  calibration = stored;
  Serial.print(F("[EEPROM] Loaded calibration scale="));
  Serial.print(calibration.scale, 6);
  Serial.print(F(" offset="));
  Serial.println(calibration.offset);
}

void saveCalibrationToEeprom(float scale, long offset) {
  calibration.magic = EEPROM_MAGIC;
  calibration.scale = scale;
  calibration.offset = offset;
  EEPROM.put(EEPROM_ADDR, calibration);
  Serial.print(F("[EEPROM] Saved calibration scale="));
  Serial.print(scale, 6);
  Serial.print(F(" offset="));
  Serial.println(offset);
}

void applyCalibrationAndTare(float scale, long offset) {
  weightSensor.power_down();
  delay(20);
  weightSensor.power_up();
  delay(20);

  weightSensor.set_scale(scale);
  weightSensor.set_offset(offset);
  if (weightSensor.is_ready()) {
    weightSensor.tare();
    Serial.print(F("[HX711] Applied scale="));
    Serial.print(scale, 6);
    Serial.print(F(" offset="));
    Serial.print(offset);
    Serial.println(F(" and tare completed"));
  } else {
    Serial.println(F("[HX711] Applied calibration but sensor not ready for tare"));
  }
}

void onI2cReceive(int byteCount) {
  if (byteCount == 1 && Wire.available()) {
    uint8_t command = static_cast<uint8_t>(Wire.read());
    while (Wire.available()) {
      Wire.read();
    }

    if (command == I2C_CMD_TARE_RESET) {
      tareResetPending = true;
    }
    return;
  }

  if (byteCount < static_cast<int>(sizeof(IncomingCalibrationPacket))) {
    while (Wire.available()) {
      Wire.read();
    }
    return;
  }

  IncomingCalibrationPacket incoming{};
  uint8_t *raw = reinterpret_cast<uint8_t *>(&incoming);

  for (size_t index = 0; index < sizeof(IncomingCalibrationPacket) && Wire.available(); index++) {
    raw[index] = static_cast<uint8_t>(Wire.read());
  }

  while (Wire.available()) {
    Wire.read();
  }

  if (!isfinite(incoming.scale) || incoming.scale == 0.0f) {
    return;
  }

  pendingScale = incoming.scale;
  pendingOffset = incoming.offset;
  calibrationUpdatePending = true;
}

void onI2cRequest() {
  Wire.write(reinterpret_cast<const uint8_t *>(&latestWeight), sizeof(latestWeight));
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(150);

  Serial.print(F("[BOOT] Firmware Version: "));
  Serial.println(FIRMWARE_VERSION);
  Serial.println(F("[BOOT] Arduino WeightSensor starting"));
  Serial.print(F("[BOOT] HX711 pins DT="));
  Serial.print(HX711_DT_PIN);
  Serial.print(F(" SCK="));
  Serial.println(HX711_SCK_PIN);

  weightSensor.begin(HX711_DT_PIN, HX711_SCK_PIN);

  loadCalibrationFromEeprom();
  weightSensor.set_scale(calibration.scale);
  weightSensor.set_offset(calibration.offset);

  Wire.begin(ARDUINO_I2C_ADDRESS);
  Wire.onReceive(onI2cReceive);
  Wire.onRequest(onI2cRequest);

  Serial.print(F("[I2C] Local address=0x"));
  Serial.println(ARDUINO_I2C_ADDRESS, HEX);
  Serial.print(F("[I2C] Target ESP32 address=0x"));
  Serial.println(ESP32_I2C_ADDRESS, HEX);

  lastTransmitAt = millis();
}

void loop() {
  if (calibrationUpdatePending) {
    noInterrupts();
    float localScale = pendingScale;
    long localOffset = pendingOffset;
    calibrationUpdatePending = false;
    interrupts();

    saveCalibrationToEeprom(localScale, localOffset);
    applyCalibrationAndTare(localScale, localOffset);

    Serial.println(F("[I2C] Calibration update received and applied"));
  }

  if (tareResetPending) {
    noInterrupts();
    tareResetPending = false;
    interrupts();

    applyCalibrationAndTare(calibration.scale, calibration.offset);
    Serial.println(F("[I2C] Tare/reset command received and applied"));
  }

  if (weightSensor.is_ready()) {
    latestRaw = weightSensor.read();
    if (calibration.scale != 0.0f) {
      latestWeight = static_cast<float>(latestRaw - weightSensor.get_offset()) / calibration.scale;
    } else {
      latestWeight = 0.0f;
    }
    
    if (millis() - lastTransmitAt >= TX_INTERVAL_MS) {
      lastTransmitAt = millis();
      Wire.beginTransmission(ESP32_I2C_ADDRESS);
      Wire.write(reinterpret_cast<const uint8_t *>(&latestWeight), sizeof(latestWeight));
      uint8_t txStatus = Wire.endTransmission();

      Serial.print(F("[TX] weight="));
      Serial.print(latestWeight, 4);
      Serial.print(F(" raw="));
      Serial.print(latestRaw);
      Serial.print(F(" status="));
      Serial.println(txStatus);
    }
  }
  else {
    if (millis() - lastReadErrorLogAt >= ERROR_LOG_INTERVAL_MS) {
      lastReadErrorLogAt = millis();
      Serial.println(F("[ERROR] HX711 not ready for reading"));
    }
  }

}