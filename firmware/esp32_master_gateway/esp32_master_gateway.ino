/*
 * ============================================================================
 * esp32_master_gateway.ino
 * เฟิร์มแวร์สำหรับ ESP32 บนบอร์ดแม่ (Master Board)
 * ============================================================================
 * หน้าที่:
 *   1. รับแพ็กเก็ตเฟรมสี 4x4 ผ่านโปรโตคอลไร้สาย ESP-NOW จากบอร์ดตัวส่ง (USB Bridge)
 *   2. โหมดการทำงาน (เลือกได้ผ่าน #define ด้านล่าง):
 *      - MODE_FORWARD_TO_ATTINY (ค่าเริ่มต้น: true): 
 *        แปลงพิกัดและส่งต่อแพ็กเก็ตผ่านพอร์ต UART (ผ่าน Logic Level Shifter 3.3V <-> 5V)
 *        ไปยัง ATtiny1604 เพื่อขับหลอดไฟ 64 ดวง และกระจายต่อให้ Slave ผ่านขั้วแม่เหล็ก
 *      - MODE_STANDALONE_LED_TEST (false):
 *        โหมดทดสอบขับหลอดไฟ WS2812B ตรงจาก ESP32 ผ่าน GPIO 18 (ไม่ต้องมี ATtiny)
 * ============================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// ============================================================================
//                         MODE SELECTION
// ============================================================================
// true  = ส่งข้อมูลผ่าน UART ไปให้ ATtiny1604 ขับไฟ (โหมดตามสถาปัตยกรรมบอร์ดจริง)
// false = ขับไฟ WS2812B ตรงจาก ESP32 ขา GPIO 18 (สำหรับเทสเดี่ยวแบบเร็ว)
#define MODE_FORWARD_TO_ATTINY  true

#if !MODE_FORWARD_TO_ATTINY
#include <Adafruit_NeoPixel.h>
#define LED_PIN             18      // GPIO 18 (D5 บน Wemos D1 Mini)
#endif

// ============================================================================
//                         HARDWARE CONFIGURATION
// ============================================================================
#define SERIAL_BAUD         115200  // USB Serial Monitor
#define ATTINY_UART_BAUD    115200  // UART ที่คุยกับ ATtiny1604

// ขา UART คุยกับ ATtiny1604 (ผ่าน Logic Level Converter)
// บน ESP32 Wemos D1 Mini: ใช้ Serial2 (RX2=GPIO16, TX2=GPIO17) หรือ GPIO ตามที่ต่อจริง
#define ATTINY_RX_PIN       16      // ESP32 รับจาก ATtiny TX
#define ATTINY_TX_PIN       17      // ESP32 ส่งไป ATtiny RX

#define NUM_CELLS           16      // ตาราง 4x4 ช่อง
#define LEDS_PER_CELL       4       // โมดูล 2x2 ดวงต่อ 1 ช่อง
#define TOTAL_LEDS          (NUM_CELLS * LEDS_PER_CELL) // 64 ดวง
#define MAX_CHANNEL_VAL     50      // Clamp ค่าสีสูงสุด 50/255 ตามข้อกำหนดโครงงาน
#define ESPNOW_CHANNEL      1

#define SERPENTINE_WIRING   false
#define FLIP_Y_AXIS         true

// ============================================================================
//                      PACKET STRUCTURE (53 BYTES)
// ============================================================================
#define PACKET_SIZE         53
#define HEADER_BYTE_1       0xAA
#define HEADER_BYTE_2       0x55
#define PKT_TYPE_FRAME      0x01
#define PKT_TYPE_TEST       0x02

// Packet Types สำหรับคุยกับ ATtiny1604
#define ATTINY_PKT_TILE_DATA     0x02
#define ATTINY_PKT_FRAME_COMMIT  0x03

#pragma pack(push, 1)
struct BoardFramePacket {
    uint8_t header1;        // 0xAA
    uint8_t header2;        // 0x55
    uint8_t pktType;        // 0x01 = Frame Data, 0x02 = Test Pattern
    uint8_t frameId;        // Sequence / Frame ID
    uint8_t rgb[16][3];     // 16 cells x (R, G, B) = 48 bytes
    uint8_t crc;            // CRC-8 over pktType + frameId + rgb
};
#pragma pack(pop)

// ============================================================================
//                           BUFFERS & STATE
// ============================================================================
uint8_t activeBuffer[16][3];
uint8_t backBuffer[16][3];
volatile bool frameReadyToCommit = false;
volatile bool runTestPatternFlag = false;
volatile uint8_t lastFrameId = 0;
volatile uint32_t rxFrameCount = 0;

uint8_t serialRxBuf[PACKET_SIZE];
int serialRxIdx = 0;
unsigned long lastSerialByteAt = 0;
String serialCmd = "";

#if !MODE_FORWARD_TO_ATTINY
Adafruit_NeoPixel strip(TOTAL_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
#endif

// ============================================================================
//                            CRC-8 HELPER
// ============================================================================
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
//                   COORDINATE MAPPING (LOGICAL -> MOD1..16)
// ============================================================================
int mapLogicalCellToHardwareMod(int logicalIndex) {
    int x = logicalIndex % 4;
    int y = logicalIndex / 4;

    if (FLIP_Y_AXIS) {
        y = 3 - y;
    }

    if (SERPENTINE_WIRING && (y % 2 == 1)) {
        x = 3 - x;
    }

    return y * 4 + x;
}

// ============================================================================
//               FORWARD DATA TO ATTINY1604 VIA UART
// ============================================================================
void forwardToATtiny(uint8_t destTileId, uint8_t frameId, uint8_t rgbData[16][3]) {
    // โครงสร้างแพ็กเก็ตส่งหา ATtiny1604 (ตาราง 3.4 ในเล่ม):
    // Header (0xAA) | Type | Src | Dest | Seq | Frame | TTL | Len | Payload(48B) | CRC8
    uint8_t header[8];
    header[0] = 0xAA;                       // Start Byte
    header[1] = ATTINY_PKT_TILE_DATA;       // Type = 0x02
    header[2] = 0x00;                       // Src = Master ESP32
    header[3] = destTileId;                 // Dest (0 = Master ATtiny, 1..3 = Slave)
    header[4] = frameId;                    // Sequence Number
    header[5] = frameId;                    // Frame Number
    header[6] = 0x04;                       // TTL = 4
    header[7] = 48;                         // Payload Length = 48 bytes

    uint8_t payload[48];
    for (int i = 0; i < 16; i++) {
        // Clamp ค่าสีทุก Channel ไม่เกิน MAX_CHANNEL_VAL (50)
        payload[i * 3 + 0] = min((uint8_t)rgbData[i][0], (uint8_t)MAX_CHANNEL_VAL);
        payload[i * 3 + 1] = min((uint8_t)rgbData[i][1], (uint8_t)MAX_CHANNEL_VAL);
        payload[i * 3 + 2] = min((uint8_t)rgbData[i][2], (uint8_t)MAX_CHANNEL_VAL);
    }
    uint8_t crc = calcCRC8(payload, 48);

    // ส่งออกทาง Serial2 ไปยัง ATtiny1604
    Serial2.write(header, 8);
    Serial2.write(payload, 48);
    Serial2.write(crc);

    // ส่งคำสั่งยืนยันให้แสดงผลพร้อมกัน (FRAME_COMMIT)
    uint8_t commitPkt[9];
    commitPkt[0] = 0xAA;
    commitPkt[1] = ATTINY_PKT_FRAME_COMMIT;
    commitPkt[2] = 0x00;
    commitPkt[3] = 0xFF; // Broadcast ทุกแผ่น
    commitPkt[4] = frameId;
    commitPkt[5] = frameId;
    commitPkt[6] = 0x04;
    commitPkt[7] = 0x00; // Payload Len = 0
    commitPkt[8] = 0x00; // CRC
    Serial2.write(commitPkt, 9);
}

// ============================================================================
//                  SHOW LEDS (FOR STANDALONE TEST MODE)
// ============================================================================
void showLEDs() {
#if !MODE_FORWARD_TO_ATTINY
    for (int cell = 0; cell < NUM_CELLS; cell++) {
        uint8_t r = activeBuffer[cell][0];
        uint8_t g = activeBuffer[cell][1];
        uint8_t b = activeBuffer[cell][2];

        uint8_t maxComp = max(r, max(g, b));
        if (maxComp > MAX_CHANNEL_VAL) {
            r = (uint8_t)((uint16_t)r * MAX_CHANNEL_VAL / maxComp);
            g = (uint8_t)((uint16_t)g * MAX_CHANNEL_VAL / maxComp);
            b = (uint8_t)((uint16_t)b * MAX_CHANNEL_VAL / maxComp);
        }

        for (int led = 0; led < LEDS_PER_CELL; led++) {
            strip.setPixelColor(cell * LEDS_PER_CELL + led, strip.Color(r, g, b));
        }
    }
    strip.show();
#else
    // โหมดบอร์ดจริง: ส่ง UART ให้ ATtiny1604 รับหน้าที่ขับไฟ
    forwardToATtiny(0, lastFrameId, activeBuffer);
#endif
}

// ============================================================================
//                          PROCESS PACKET
// ============================================================================
void processPacket(const BoardFramePacket &pkt) {
    if (pkt.header1 != HEADER_BYTE_1 || pkt.header2 != HEADER_BYTE_2)
        return;

    uint8_t expectedCrc = calcCRC8(&pkt.pktType, 50);
    if (pkt.crc != expectedCrc && pkt.crc != 0xFF)
        return;

    if (pkt.pktType == PKT_TYPE_TEST) {
        runTestPatternFlag = true;
        return;
    }

    if (pkt.pktType == PKT_TYPE_FRAME) {
        for (int i = 0; i < NUM_CELLS; i++) {
            int hwIdx = mapLogicalCellToHardwareMod(i);
            backBuffer[hwIdx][0] = pkt.rgb[i][0];
            backBuffer[hwIdx][1] = pkt.rgb[i][1];
            backBuffer[hwIdx][2] = pkt.rgb[i][2];
        }
        memcpy(activeBuffer, backBuffer, sizeof(activeBuffer));
        lastFrameId = pkt.frameId;
        rxFrameCount++;
        frameReadyToCommit = true;
    }
}

// ============================================================================
//                       ESP-NOW RECEIVE CALLBACK
// ============================================================================
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
#else
void onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
#endif
    if (len == sizeof(BoardFramePacket)) {
        BoardFramePacket pkt;
        memcpy(&pkt, incomingData, sizeof(BoardFramePacket));
        processPacket(pkt);
    }
}

// ============================================================================
//                     TEST PATTERNS
// ============================================================================
void setCellColorLogical(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (x < 0 || x >= 4 || y < 0 || y >= 4) return;
    int logicalIdx = y * 4 + x;
    int hwIdx = mapLogicalCellToHardwareMod(logicalIdx);
    activeBuffer[hwIdx][0] = r;
    activeBuffer[hwIdx][1] = g;
    activeBuffer[hwIdx][2] = b;
}

void clearAllCells() {
    memset(activeBuffer, 0, sizeof(activeBuffer));
    showLEDs();
}

void runHardwareTestSequence() {
    Serial.println("[TEST] Running 4x4 LED Test Sequence...");
    for (int mod = 0; mod < NUM_CELLS; mod++) {
        memset(activeBuffer, 0, sizeof(activeBuffer));
        activeBuffer[mod][0] = 40;
        activeBuffer[mod][1] = 220;
        activeBuffer[mod][2] = 90;
        showLEDs();
        delay(120);
    }
    clearAllCells();
    Serial.println("[TEST] Done.");
}

void showCornerPattern() {
    memset(activeBuffer, 0, sizeof(activeBuffer));
    setCellColorLogical(0, 0, 40, 120, 255);
    setCellColorLogical(1, 0, 40, 220, 90);
    setCellColorLogical(0, 1, 40, 220, 90);
    setCellColorLogical(2, 0, 40, 220, 90);
    setCellColorLogical(1, 1, 40, 220, 90);
    setCellColorLogical(0, 2, 40, 220, 90);
    setCellColorLogical(3, 0, 255, 40, 40);
    setCellColorLogical(2, 1, 255, 40, 40);
    setCellColorLogical(1, 2, 255, 40, 40);
    setCellColorLogical(0, 3, 255, 40, 40);
    setCellColorLogical(3, 3, 255, 75, 75);
    showLEDs();
    Serial.println("[CORNER] Displayed.");
}

// ============================================================================
//                                SETUP
// ============================================================================
void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(200);

    // เริ่มต้น Serial2 สำหรับคุยกับ ATtiny1604 ผ่าน Level Shifter
    Serial2.begin(ATTINY_UART_BAUD, SERIAL_8N1, ATTINY_RX_PIN, ATTINY_TX_PIN);

#if !MODE_FORWARD_TO_ATTINY
    strip.begin();
    strip.show();
#endif

    clearAllCells();

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    if (esp_now_init() != ESP_OK) {
        Serial.println("[FATAL] ESP-NOW Init Failed!");
        return;
    }

    esp_now_register_recv_cb(onDataRecv);

    Serial.println("======================================================");
    Serial.println(" ESP32 Master Gateway Ready!");
    Serial.print  (" Master MAC Address : ");
    Serial.println(WiFi.macAddress());
    Serial.printf (" Mode: %s\n", MODE_FORWARD_TO_ATTINY ? "FORWARD_TO_ATTINY (UART)" : "STANDALONE_LED_TEST (GPIO18)");
    Serial.println("======================================================");
}

// ============================================================================
//                                LOOP
// ============================================================================
void loop() {
    if (frameReadyToCommit) {
        frameReadyToCommit = false;
        showLEDs();
        Serial.printf("[FRAME] Committed frameId=%u (total=%lu)\n",
                      lastFrameId, (unsigned long)rxFrameCount);
    }

    if (runTestPatternFlag) {
        runTestPatternFlag = false;
        runHardwareTestSequence();
    }

    // รับคำสั่งผ่าน USB Serial
    while (Serial.available() > 0) {
        uint8_t b = (uint8_t)Serial.read();
        unsigned long now = millis();

        if (serialRxIdx > 0 && (now - lastSerialByteAt > 50)) {
            serialRxIdx = 0;
        }
        lastSerialByteAt = now;

        if (serialRxIdx == 0) {
            if (b == HEADER_BYTE_1) {
                serialRxBuf[serialRxIdx++] = b;
            } else {
                if (b == '\n' || b == '\r') {
                    serialCmd.trim();
                    if (serialCmd.equalsIgnoreCase("MAC")) {
                        Serial.print("[MASTER_4x4] MAC: ");
                        Serial.println(WiFi.macAddress());
                    } else if (serialCmd.equalsIgnoreCase("TEST")) {
                        runHardwareTestSequence();
                    } else if (serialCmd.equalsIgnoreCase("CORNER")) {
                        showCornerPattern();
                    } else if (serialCmd.equalsIgnoreCase("CLEAR")) {
                        clearAllCells();
                    }
                    serialCmd = "";
                } else if (serialCmd.length() < 256) {
                    serialCmd += (char)b;
                }
            }
        } else if (serialRxIdx == 1) {
            if (b == HEADER_BYTE_2) {
                serialRxBuf[serialRxIdx++] = b;
            } else {
                serialRxIdx = 0;
            }
        } else {
            serialRxBuf[serialRxIdx++] = b;
            if (serialRxIdx == PACKET_SIZE) {
                BoardFramePacket pkt;
                memcpy(&pkt, serialRxBuf, PACKET_SIZE);
                serialRxIdx = 0;
                processPacket(pkt);
            }
        }
    }
}
