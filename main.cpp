#include <Arduino.h>
#include <Adafruit_PN532.h>

// กำหนดพิน GPIO ปลอดภัยสำหรับ ESP32-S3 (โหมด HSU)
#define RX_PIN 18     
#define TX_PIN 17    
#define RESET_PIN 8   

// เรียกใช้คอนสตรักเตอร์ Hardware Serial1 สำหรับ ESP32-S3
Adafruit_PN532 nfc(RESET_PIN, &Serial1);

void setup() {
  Serial.begin(115200);
  delay(2000); // ให้เวลาพอร์ต Native USB CDC ของ ESP32-S3 เชื่อมต่อ (แทนขณะรอ while(!Serial))

  Serial.println("PN532 NFC reader starting...");

  // เริ่มต้น Hardware Serial1
  Serial1.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);

  nfc.begin();

  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("Didn't find PN532 board");
    while (1) {
      delay(10); // ป้องกัน Watchdog Timer รีเซ็ตระบบ
    }
  }

  Serial.print("Found PN532 chip, firmware version: ");
  Serial.print((versiondata >> 24) & 0xFF, HEX);
  Serial.print('.');
  Serial.println((versiondata >> 16) & 0xFF, HEX);

  nfc.SAMConfig();
  Serial.println("Waiting for an ISO14443A card/tag...");
}

void loop() {
  uint8_t uid[7];
  uint8_t uidLength;

  bool success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 1000);
  if (success) {
    Serial.print("Found tag UID: ");
    for (uint8_t i = 0; i < uidLength; i++) {
      if (uid[i] < 0x10) Serial.print('0');
      Serial.print(uid[i], HEX);
      if (i < uidLength - 1) Serial.print(':');
    }
    Serial.println();
    delay(1000);
  }
}