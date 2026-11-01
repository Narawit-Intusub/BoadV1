/*
 * ============================================================================
 * esp32_rfid_addon.ino
 * เฟิร์มแวร์สำหรับแท่นอ่านการ์ดเกม (RFID Card Reader Addon)
 * โครงการ Modular Smart Board Game V1
 * ============================================================================
 * ฮาร์ดแวร์:
 *   - บอร์ดประมวลผล: ESP32 Wemos D1 Mini (U1)
 *   - โมดูลอ่านการ์ด: MFRC522 / RC522 RFID Reader Module (J_RFID1) ผ่านบัส SPI
 *   - แหล่งจ่ายไฟ: รับไฟจากขั้วแม่เหล็ก 6-Pin (J_MAG1) ผ่านสเต็ปดาวน์ MP2315 จ่าย +5V/+3.3V
 * 
 * ขาเชื่อมต่อตามวงจร Schematic (hardware/rfid_addon/rfid_addon.kicad_sch):
 *   - RFID_SS   (SDA/CS) -> GPIO 5   (IO_05 / D8 บน Wemos D1 Mini)
 *   - RFID_RST  (Reset)  -> GPIO 4   (IO_04 บน Wemos D1 Mini)
 *   - RFID_SCK  (Clock)  -> GPIO 18  (IO_18 / D5 บน Wemos D1 Mini)
 *   - RFID_MISO (DataIn) -> GPIO 19  (IO_19 / D6 บน Wemos D1 Mini)
 *   - RFID_MOSI (DataOut)-> GPIO 23  (IO_23 / D7 บน Wemos D1 Mini)
 *   - Status LED         -> GPIO 2   (ไฟ LED แสดงสถานะบนบอร์ด ESP32)
 * 
 * ขอบเขตหน้าที่ของบอร์ดนี้ (Card Reader Only):
 *   - ทำหน้าที่เป็น "แท่นอ่านการ์ดเกม" (Action Card / Item Card / Spell Card) เท่านั้น
 *   - ไม่ใช่ตัวตรวจจับตำแหน่งหมากตัวละคร (การตรวจจับหมาก/ชิปบนกระดานเป็นอีกระบบหนึ่ง)
 *   - ตรวจจับเมื่อผู้เล่นนำการ์ดมาแตะ (Card Tap):
 *     1. อ่านรหัส Card UID (Mifare Classic, Ultralight, NTAG 13.56 MHz)
 *     2. ส่งสัญญาณ Card Event (รหัสการ์ด) ทันที 1 ครั้งต่อการแตะ
 *     3. ป้องกันการอ่านซ้ำ (Debounce) ขณะวางการ์ดแช่ไว้บนแท่น
 *     4. ส่งข้อมูลออกผ่าน ESP-NOW (Channel 1) เข้า USB Bridge / Master
 *     5. ส่งข้อมูลออกทาง USB Serial ในฟอร์แมต "RFID:CARD,UID=...,LEN=...,ADDON=..."
 * ============================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <SPI.h>
#include <MFRC522.h>

// ============================================================================
//                         HARDWARE PIN CONFIGURATION
// ============================================================================
// พินตรงตามวงจร Schematic: hardware/rfid_addon/rfid_addon.kicad_sch
#define PIN_RFID_SS         5       // GPIO 5 (RFID_SS / Chip Select)
#define PIN_RFID_RST        4       // GPIO 4 (RFID_RST)
#define PIN_RFID_SCK        18      // GPIO 18 (RFID_SCK / VSPI SCK)
#define PIN_RFID_MISO       19      // GPIO 19 (RFID_MISO / VSPI MISO)
#define PIN_RFID_MOSI       23      // GPIO 23 (RFID_MOSI / VSPI MOSI)
#define PIN_STATUS_LED      2       // GPIO 2 (ESP32 On-board LED)

#define SERIAL_BAUD         115200  // Serial Monitor Baudrate
#define ESPNOW_CHANNEL      1       // WiFi Channel เดียวกับ Master และ USB Bridge
#define ADDON_ID            1       // หมายเลขประจำบอร์ด Addon (1..4)
#define CARD_COOLDOWN_MS    1200    // หน่วงเวลาไม่ให้อ่านการ์ดใบเดิมซ้ำขณะวางทับค้างไว้ (ms)

// ============================================================================
//                         PACKET STRUCTURE (16 BYTES)
// ============================================================================
#define HEADER_BYTE_1       0xAA
#define HEADER_BYTE_2       0x55
#define PKT_TYPE_CARD       0x10    // 0x10 = RFID Card Tap Event

#pragma pack(push, 1)
struct CardPacket {
    uint8_t header1;        // 0xAA
    uint8_t header2;        // 0x55
    uint8_t pktType;        // 0x10
    uint8_t addonId;        // ID โมดูล Addon (1..4)
    uint8_t uidLength;      // ขนาด UID ของการ์ด (4, 7 หรือ 10 ไบต์)
    uint8_t uid[10];        // ข้อมูล UID การ์ด
    uint8_t crc;            // CRC-8 ครอบคลุม pktType..uid (13 ไบต์)
};
#pragma pack(pop)

// MAC ปลายทาง: Broadcast ส่งถึงทุกตัวในแชนแนล (USB Bridge และ Master Gateway)
uint8_t TARGET_MAC[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

// ============================================================================
//                           GLOBAL OBJECTS & STATE
// ============================================================================
MFRC522 mfrc522(PIN_RFID_SS, PIN_RFID_RST);

volatile uint32_t txSuccessCount = 0;
volatile uint32_t txFailCount = 0;
unsigned long lastCardTapTime = 0;
uint8_t lastCardUid[10] = {0};
uint8_t lastCardUidLen = 0;

// ============================================================================
//                            CRC-8 CALCULATION
// ============================================================================
// พหุนาม x^8 + x^2 + x + 1 (0x07) มาตรฐานเดียวกับ esp32_master_gateway และ bridge
uint8_t calcCRC8(const uint8_t* data, size_t len) {
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++) {
            if (crc & 0x80)
                crc = (uint8_t)((crc << 1) ^ 0x07);
            else
                crc <<= 1;
        }
    }
    return crc;
}

// ============================================================================
//                       ESP-NOW CALLBACKS
// ============================================================================
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
void onDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
#else
void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
#endif
    if (status == ESP_NOW_SEND_SUCCESS) {
        txSuccessCount++;
    } else {
        txFailCount++;
    }
}

// ============================================================================
//                      SEND CARD PACKET VIA ESP-NOW
// ============================================================================
void sendCardPacket(const uint8_t* uid, uint8_t uidLen) {
    CardPacket pkt;
    memset(&pkt, 0, sizeof(CardPacket));

    pkt.header1 = HEADER_BYTE_1;
    pkt.header2 = HEADER_BYTE_2;
    pkt.pktType = PKT_TYPE_CARD;
    pkt.addonId = ADDON_ID;
    pkt.uidLength = uidLen;

    memcpy(pkt.uid, uid, (uidLen > 10) ? 10 : uidLen);
    pkt.crc = calcCRC8(&pkt.pktType, 13); // pktType(1) + addonId(1) + uidLength(1) + uid(10) = 13 bytes

    esp_err_t res = esp_now_send(TARGET_MAC, (const uint8_t*)&pkt, sizeof(CardPacket));
    if (res == ESP_OK) {
        Serial.printf("[ESP-NOW OK] Sent Card Packet (Addon=%d, Len=%d)\n", pkt.addonId, pkt.uidLength);
    } else {
        Serial.printf("[ESP-NOW ERR] Failed to send, err=%d\n", res);
    }
}

// ============================================================================
//                         CHECK RC522 HARDWARE
// ============================================================================
bool verifyMFRC522Connection() {
    byte v = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
    if (v == 0x00 || v == 0xFF) {
        return false;
    }
    return true;
}

// ============================================================================
//                                SETUP
// ============================================================================
void setup() {
    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, LOW);

    Serial.begin(SERIAL_BAUD);
    delay(200);

    Serial.println();
    Serial.println("======================================================");
    Serial.println(" RFID Game Card Reader Addon (Card Reader Only)");
    Serial.println(" Board: ESP32 Wemos D1 Mini + MFRC522");
    Serial.println("======================================================");

    // 1. เริ่มต้น SPI Bus ตามพินวงจร
    SPI.begin(PIN_RFID_SCK, PIN_RFID_MISO, PIN_RFID_MOSI, PIN_RFID_SS);

    // 2. เริ่มต้นโมดูล MFRC522
    mfrc522.PCD_Init();
    delay(50);

    if (verifyMFRC522Connection()) {
        Serial.print("[RC522] Hardware detected! Firmware version: 0x");
        byte ver = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
        Serial.println(ver, HEX);
        // ตั้งค่า Gain เสาอากาศสูงสุดเพื่อความไวในการสแกนการ์ด
        mfrc522.PCD_SetAntennaGain(mfrc522.RxGain_max);
    } else {
        Serial.println("[RC522 WARNING] MFRC522 not responding! Check SPI wiring:");
        Serial.printf("  SS=%d, RST=%d, SCK=%d, MISO=%d, MOSI=%d\n",
                      PIN_RFID_SS, PIN_RFID_RST, PIN_RFID_SCK, PIN_RFID_MISO, PIN_RFID_MOSI);
    }

    // 3. เริ่มต้น WiFi & ESP-NOW
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    if (esp_now_init() != ESP_OK) {
        Serial.println("[FATAL] ESP-NOW Init Failed!");
        return;
    }

    esp_now_register_send_cb(onDataSent);

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, TARGET_MAC, 6);
    peerInfo.channel = ESPNOW_CHANNEL;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("[ERR] Failed to add broadcast peer");
    } else {
        Serial.println("[ESP-NOW] Broadcast peer ready.");
    }

    Serial.print("[INFO] MAC Address: ");
    Serial.println(WiFi.macAddress());
    Serial.printf("[INFO] Addon ID: %d, WiFi Channel: %d\n", ADDON_ID, ESPNOW_CHANNEL);
    Serial.println("======================================================");
    Serial.println("Ready to scan Game Cards!");

    // กะพริบไฟ LED 2 ครั้งยืนยันความพร้อม
    for (int i = 0; i < 2; i++) {
        digitalWrite(PIN_STATUS_LED, HIGH);
        delay(80);
        digitalWrite(PIN_STATUS_LED, LOW);
        delay(80);
    }
}

// ============================================================================
//                                LOOP
// ============================================================================
void loop() {
    // ------------------------------------------------------------------------
    // 1. รับคำสั่งผ่าน Serial Monitor สำหรับทดสอบ
    // ------------------------------------------------------------------------
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        if (cmd.equalsIgnoreCase("MAC")) {
            Serial.printf("MAC: %s (Channel: %d)\n", WiFi.macAddress().c_str(), ESPNOW_CHANNEL);
        } else if (cmd.equalsIgnoreCase("STATUS")) {
            bool ok = verifyMFRC522Connection();
            Serial.printf("MFRC522 Status: %s (TX ok=%u, fail=%u)\n",
                          ok ? "ONLINE" : "OFFLINE", txSuccessCount, txFailCount);
        } else if (cmd.equalsIgnoreCase("TEST")) {
            // จำลองการแตะการ์ดทดสอบ UID: DE AD BE EF
            Serial.println("[TEST] Simulating Game Card Tap (DEADBEEF)...");
            uint8_t testUid[4] = {0xDE, 0xAD, 0xBE, 0xEF};
            sendCardPacket(testUid, 4);
        }
    }

    // ------------------------------------------------------------------------
    // 2. สแกนหาการแตะการ์ดเกม (Card Tap)
    // ------------------------------------------------------------------------
    if (!mfrc522.PICC_IsNewCardPresent()) {
        return;
    }

    if (!mfrc522.PICC_ReadCardSerial()) {
        return;
    }

    // ตรวจพบการ์ดแตะเข้ามาที่แท่นอ่าน!
    uint8_t uidLen = mfrc522.uid.size;
    unsigned long now = millis();

    // Debounce: ป้องกันการอ่านการ์ดใบเดิมซ้ำขณะผู้เล่นวางการ์ดแช่ไว้
    bool isSameCard = (uidLen == lastCardUidLen) && (memcmp(mfrc522.uid.uidByte, lastCardUid, uidLen) == 0);
    if (isSameCard && (now - lastCardTapTime < CARD_COOLDOWN_MS)) {
        mfrc522.PICC_HaltA();
        mfrc522.PCD_StopCrypto1();
        return;
    }

    // บันทึกเวลาและการ์ดล่าสุด
    lastCardTapTime = now;
    lastCardUidLen = uidLen;
    memcpy(lastCardUid, mfrc522.uid.uidByte, uidLen);

    // เปิดไฟ LED แสดงสถานะรับการ์ดสำเร็จ
    digitalWrite(PIN_STATUS_LED, HIGH);

    // แปลง UID ของการ์ดเป็นสตริง HEX
    String uidStr = "";
    for (byte i = 0; i < uidLen; i++) {
        if (mfrc522.uid.uidByte[i] < 0x10) uidStr += "0";
        uidStr += String(mfrc522.uid.uidByte[i], HEX);
        if (i < uidLen - 1) uidStr += ":";
    }
    uidStr.toUpperCase();

    // ดึงประเภทการ์ด
    MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
    const char* typeName = mfrc522.PICC_GetTypeName(piccType);

    // พิมพ์ผลลัพธ์ออก Serial
    Serial.println();
    Serial.println("------------------------------------------------------");
    Serial.printf("[CARD TAPPED] UID: %s (Bytes: %d)\n", uidStr.c_str(), uidLen);
    Serial.printf("              Card Type: %s (SAK: 0x%02X)\n", typeName, mfrc522.uid.sak);
    Serial.printf("RFID:CARD,UID=%s,LEN=%d,ADDON=%d\n", uidStr.c_str(), uidLen, ADDON_ID);
    Serial.println("------------------------------------------------------");

    // 3. ส่งข้อมูลการแตะการ์ดออกผ่าน ESP-NOW
    sendCardPacket(mfrc522.uid.uidByte, uidLen);

    // สั่งปิดการสื่อสารกับการ์ดปัจจุบันให้เรียบร้อย (เพื่อให้พร้อมแตะใบต่อไป)
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();

    // ดับไฟแสดงสถานะหลังอ่านเสร็จ
    delay(100);
    digitalWrite(PIN_STATUS_LED, LOW);
}
