/*
  ESP32-S3: BLE Server + PN532 RFID Reader (UART / HSU Mode)
  รองรับการเชื่อมต่อ BLE หลายอุปกรณ์ (Multi-Device)
  + อ่านข้อความยาว และส่งแบบ Chunking
  + ปรับปรุงความปลอดภัยด้าน Memory เพื่อป้องกัน StoreProhibited Crash
*/
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Wire.h>

// ไลบรารีสำหรับเชื่อมต่อกับ PN532 ผ่าน UART (Hardware Serial)
#include <PN532_HSU.h>
#include <PN532.h>

// ไลบรารีสำหรับอ่านข้อความ (ต้องติดตั้ง "NDEF by Don Coleman" เพิ่มใน Library Manager)
#include <NfcAdapter.h>

// กำหนดขา RX, TX สำหรับ ESP32-S3 (คุณสามารถเปลี่ยนตัวเลขให้ตรงกับขาที่ต่อจริงได้)
#define PN532_RX_PIN 17
#define PN532_TX_PIN 18

HardwareSerial MySerial(1);
PN532_HSU pn532hsu(MySerial);
// ใช้ NfcAdapter ครอบการทำงานของ PN532 เพื่อให้อ่าน NDEF ได้ง่ายขึ้น
NfcAdapter nfc = NfcAdapter(pn532hsu);

BLEServer *pServer = nullptr;
BLECharacteristic *pCharacteristic = nullptr;
uint32_t connectedClients = 0;

#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

class MyServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *pServer)
  {
    connectedClients++;
    Serial.print("Device connected! Total clients: ");
    Serial.println(connectedClients);

    // *** สำคัญ *** สั่งให้เริ่มปล่อยสัญญาณใหม่ทันที เพื่อให้เครื่องอื่นๆ เข้ามาเชื่อมต่อเพิ่มได้
    // BLEDevice::startAdvertising();
    delay(100);
    pServer->startAdvertising();
  };

  void onDisconnect(BLEServer *pServer)
  {
    connectedClients--;
    Serial.print("Device disconnected! Total clients: ");
    Serial.println(connectedClients);

    // กลับมาปล่อยสัญญาณกรณีมีคนหลุด
    delay(100);
    pServer->startAdvertising();
  }
};

// ==========================================
// ฟังก์ชันสำหรับหั่นข้อความและส่งผ่าน BLE
// ==========================================
void sendLongDataViaBLE(String longMessage)
{
  if (connectedClients == 0)
  {
    Serial.println("ยังไม่มี Client เชื่อมต่อ ยกเลิกการส่ง");
    return;
  }

  int chunkSize = 20; // จำกัดการส่งทีละ 20 ตัวอักษร
  int messageLength = longMessage.length();

  Serial.println("\n--- เริ่มส่งข้อมูล ---");

  for (int i = 0; i < messageLength; i += chunkSize)
  {
    // หั่นข้อความออกมา
    String chunk = longMessage.substring(i, min(i + chunkSize, messageLength));

    // ตั้งค่าแล้วกดส่ง Notify
    pCharacteristic->setValue(chunk.c_str());
    pCharacteristic->notify();

    Serial.print("Sent chunk: ");
    Serial.println(chunk);

    delay(50); // ดีเลย์ป้องกันคอขวด
  }

  // ส่ง Marker ว่าจบการส่ง
  pCharacteristic->setValue("[END]");
  pCharacteristic->notify();
  Serial.println("--- ส่ง [END] จบการทำงาน ---\n");
}

void setup(void)
{
  Serial.begin(115200);
  delay(1000); // รอให้ Serial Monitor พร้อมใช้งาน

  MySerial.setRxBufferSize(500);
  // เริ่มต้น Serial 1 สำหรับ HSU
  MySerial.begin(115200, SERIAL_8N1, PN532_RX_PIN, PN532_TX_PIN);

  // เริ่มต้น NFC
  nfc.begin();
  Serial.println("NDEF Reader is ready!");

  // --- ตั้งค่า BLE ---
  BLEDevice::init("ESP32_PN532_Gateway");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_READ |
          BLECharacteristic::PROPERTY_NOTIFY);
  pCharacteristic->addDescriptor(new BLE2902());
  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x0);
  BLEDevice::startAdvertising();

  Serial.println("BLE Server is ready! Waiting for clients...");
}

void loop(void)
{
  // ตรวจสอบว่ามีบัตรมาแตะหรือไม่
  if (nfc.tagPresent())
  {
    Serial.println("พบการแตะบัตร NFC!");

    // ตรวจสอบ RAM เหลืออยู่ก่อนอ่านบัตร ป้องกัน StoreProhibited crash จาก Heap หมด
    uint32_t freeHeap = ESP.getFreeHeap();
    Serial.printf("Free Heap: %u bytes\n", freeHeap);

    if (freeHeap < 15000)
    {
      Serial.println("⚠️ Warning: Heap memory low! Skipping read to prevent crash.");
      delay(2000);
      return;
    }

    NfcTag tag = nfc.read();

    // ตรวจสอบว่าบัตรนี้มีการเขียนข้อความแบบ NDEF ไว้หรือไม่
    if (tag.hasNdefMessage())
    {
      NdefMessage message = tag.getNdefMessage();
      int recordCount = message.getRecordCount();

      String extractedText = "";

      // วนลูปอ่านข้อมูลในบัตร (เผื่อมีหลายบล็อค)
      for (int i = 0; i < recordCount; i++)
      {
        NdefRecord record = message.getRecord(i);

        int payloadLength = record.getPayloadLength();

        // 1. ตรวจสอบความยาวของ Payload ป้องกัน Stack Overflow / Corruption
        if (payloadLength <= 0 || payloadLength > 1024)
        {
          Serial.printf("⚠️ Record %d has invalid or empty payload length (%d). Skipping...\n", i, payloadLength);
          continue;
        }

        // 2. เปลี่ยนจาก VLA (Variable-length array) บน Stack เป็น Heap Allocation ที่ปลอดภัย
        byte *payload = new (std::nothrow) byte[payloadLength];
        if (payload == nullptr)
        {
          Serial.println("❌ Allocation failed: Not enough heap for payload buffer!");
          continue;
        }

        record.getPayload(payload);

        // 3. ตรวจสอบ offset ของข้อความ NDEF Text Record อย่างปลอดภัย
        if (payloadLength > 1)
        {
          byte statusByte = payload[0];
          int languageCodeLength = statusByte & 0x3F;
          int textOffset = languageCodeLength + 1;

          if (textOffset < payloadLength)
          {
            for (int c = textOffset; c < payloadLength; c++)
            {
              extractedText += (char)payload[c];
            }
          }
        }

        // คืนคืน Memory ทุกครั้งหลังอ่าน Record เสร็จ
        delete[] payload;
      }

      // ถ้าดึงข้อความออกมาได้สำเร็จ
      if (extractedText.length() > 0)
      {
        Serial.println("\n✅ อ่านข้อความได้: " + extractedText);

        // สั่งหั่นและกระจายสัญญาณไปให้ทุก Client ที่เชื่อมต่ออยู่
        sendLongDataViaBLE(extractedText);
      }
      else
      {
        Serial.println("❌ ไม่พบข้อมูล Text ในบัตรใบนี้");
      }
    }
    else
    {
      Serial.println("❌ บัตรใบนี้ว่างเปล่า (ไม่มีข้อความ NDEF)");
    }

    // ดีเลย์ 3 วินาที เพื่อไม่ให้อ่านซ้ำรัวๆ ตอนที่ยังไม่ยกบัตรออก
    delay(3000);
  }
}