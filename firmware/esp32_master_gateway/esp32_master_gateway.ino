/*
 * ============================================================================
 * esp32_master_gateway.ino
 * เฟิร์มแวร์สำหรับ ESP32 บนบอร์ดแม่ (Master Gateway / 4x4 Prototype Board)
 * ============================================================================
 * หน้าที่:
 *   1. รับแพ็กเก็ตเฟรมสี 4x4 (53 ไบต์) ผ่านโปรโตคอลไร้สาย ESP-NOW จากบอร์ด USB Bridge
 *      (และรองรับการเสียบสาย USB ตรงเข้าบอร์ดแม่เพื่อเทสผ่าน Serial ได้ด้วย)
 *   2. แปลงพิกัดช่อง (16 ช่อง) และส่งคำสั่ง UART Packet (PKT_TILE_DATA 0x02 +
 *      PKT_FRAME_COMMIT 0x03) ไปยังไมโครคอนโทรลเลอร์ประจำแผ่นกระดาน (ATtiny1604)
 *   3. รองรับการขับไฟ WS2812B 64 ดวงโดยตรงผ่านขา GPIO 18 (D5 บน Wemos D1 Mini ESP32)
 *      และจำกัดความสว่างสูงสุดไม่เกิน MAX_CHANNEL_VAL = 120/255 เพื่อความปลอดภัย
 *   4. รับ-ส่งอีเวนต์และคำสั่งเกมผ่าน ESP-NOW (BoardEventPacket 53 ไบต์, PKT_TYPE_EVENT 0x03)
 *      แทนระบบ MQTT เดิม (/rfid, /status, /dice, /fight, /character, /team, /led)
 * ============================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Adafruit_NeoPixel.h>

// ============================================================================
//                         HARDWARE CONFIGURATION
// ============================================================================
#define LED_PIN             18      // ขา GPIO 18 (D5 บน Wemos D1 Mini ESP32 ตามลายวงจร PCB)
#define NUM_CELLS           16      // ตาราง 4x4 = 16 ช่อง (MOD1 ถึง MOD16)
#define LEDS_PER_CELL       4       // โมดูล WS2812B 2x2 = 4 ดวงต่อช่อง
#define TOTAL_LEDS          (NUM_CELLS * LEDS_PER_CELL) // 64 ดวง
#define MAX_CHANNEL_VAL     120     // จำกัดค่าสีสูงสุด 120/255 ตามข้อกำหนดโครงงาน
#define ESPNOW_CHANNEL      1       // ช่องสัญญาณ Wi-Fi สำหรับ ESP-NOW (ตรงกับ esp32_usb_bridge)
#define SERIAL_BAUD         115200

// ============================================================================
//                         LED DRIVER MODE SELECTION
// ============================================================================
// เลือกว่าจะให้ฮาร์ดแวร์ฝั่งไหนเป็นผู้ขับหลอดไฟ WS2812B (เลือกอย่างใดอย่างหนึ่ง ไม่ทำงานซ้ำซ้อน):
// true  = ส่งข้อมูลผ่าน UART ไปให้ ATtiny1604 ขับไฟ (โหมดตามสถาปัตยกรรมบอร์ดจริง - ESP32 ไม่โดนบล็อกเวลา)
// false = ขับไฟ WS2812B ตรงจาก ESP32 ขา GPIO 18 (สำหรับเทสเดี่ยวแบบเร็ว โดยไม่ต้องต่อ ATtiny)
#define USE_ATTINY_UART_DRIVER   true

// ขา UART สำหรับสื่อสารกับ ATtiny1604 ประจำแผ่นกระดาน (Serial2)
#define TILE_UART_BAUD      115200
#define TILE_UART_RX_PIN    16
#define TILE_UART_TX_PIN    17

// ตั้งค่าทิศทางการเรียงลำดับช่องบนบอร์ด:
// false = เรียงแบบ Left-to-Right (MOD1..MOD4, MOD5..MOD8, MOD9..MOD12, MOD13..MOD16) ตาม PCB
// true  = กรณีบัดกรีสายสลับฟันปลา (Serpentine / Zig-Zag)
#define SERPENTINE_WIRING   false

// สลับแกน Y หรือไม่: 
// ใน Unity: y=0 คือแถวล่างสุด (Row1/D), y=3 คือแถวบนสุด (Row4/A)
// บน PCB: MOD1..MOD4 อยู่แถวบน (North), MOD13..MOD16 อยู่แถวล่าง (South)
#define FLIP_Y_AXIS         true

// MAC Address สำหรับส่งอีเวนต์กลับไปยัง esp32_usb_bridge (Broadcast ใน Channel 1)
uint8_t BRIDGE_MAC[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

// ============================================================================
//               ESP-NOW PACKET STRUCTURES (53 BYTES)
// ============================================================================
#define PACKET_SIZE         53
#define HEADER_BYTE_1       0xAA
#define HEADER_BYTE_2       0x55
#define PKT_TYPE_FRAME      0x01
#define PKT_TYPE_TEST       0x02
#define PKT_TYPE_EVENT      0x03

// ============================================================================
//               TILE UART PROTOCOL CONSTANTS (TO ATTINY1604)
// ============================================================================
#define TILE_START_BYTE     0xAA
#define TILE_PKT_HELLO      0x01
#define TILE_PKT_DATA       0x02
#define TILE_PKT_COMMIT     0x03
#define MASTER_SRC_ID       0x00
#define BROADCAST_DEST_ID   0xFF
#define DEFAULT_TTL         4

Adafruit_NeoPixel strip(TOTAL_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

#pragma pack(push, 1)
struct BoardFramePacket {
    uint8_t header1;        // 0xAA
    uint8_t header2;        // 0x55
    uint8_t pktType;        // 0x01 = Frame Data, 0x02 = Test Pattern
    uint8_t frameId;        // Sequence / Frame ID
    uint8_t rgb[16][3];     // 16 cells x (R, G, B) = 48 bytes
    uint8_t crc;            // CRC-8
};

struct BoardEventPacket {
    uint8_t header1;        // 0xAA
    uint8_t header2;        // 0x55
    uint8_t pktType;        // 0x03 = Game Event / Command (replaces MQTT)
    uint8_t seqId;          // Sequence ID
    char    topic[16];      // e.g. "/rfid", "/status", "/dice", "/fight", "/character", "/team", "/led"
    char    payload[32];    // e.g. "Dummy1_A1", "Dummy1_InTag", "6", "4,2,6", "Dummy1_Red"
    uint8_t crc;            // CRC-8 over pktType + seqId + topic + payload (50 bytes)
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
uint8_t tileSeqCounter = 0;
uint8_t eventSeqCounter = 0;

// สำหรับรับข้อมูลทาง USB Serial โดยตรง (กรณีต่อสาย USB เข้าบอร์ดแม่โดยตรง)
uint8_t serialRxBuf[PACKET_SIZE];
int serialRxIdx = 0;
unsigned long lastSerialByteAt = 0;
String serialCmd = "";

// ============================================================================
//                            CRC-8 HELPER
// ============================================================================
// Polynomial x^8 + x^2 + x + 1 (0x07)
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
//          SEND GAME EVENT TO UNITY (VIA ESP-NOW & DIRECT SERIAL)
// ============================================================================
// ใช้แทนการ Publish MQTT เดิม เช่น:
//   sendEventToUnity("/rfid", "Dummy1_A1");
//   sendEventToUnity("/status", "Dummy1_InTag");
//   sendEventToUnity("/dice", "6");
//   sendEventToUnity("/fight", "4,2,6");
//   sendEventToUnity("/character", "Dummy1_SW");
void sendEventToUnity(const char* topic, const char* payload) {
    BoardEventPacket evPkt;
    memset(&evPkt, 0, sizeof(evPkt));
    evPkt.header1 = HEADER_BYTE_1;
    evPkt.header2 = HEADER_BYTE_2;
    evPkt.pktType = PKT_TYPE_EVENT;
    evPkt.seqId   = ++eventSeqCounter;

    strncpy(evPkt.topic, topic, sizeof(evPkt.topic) - 1);
    strncpy(evPkt.payload, payload, sizeof(evPkt.payload) - 1);
    evPkt.crc = calcCRC8(&evPkt.pktType, 50);

    // 1. ส่งออกไร้สายผ่าน ESP-NOW ไปยัง esp32_usb_bridge
    esp_now_send(BRIDGE_MAC, (const uint8_t*)&evPkt, sizeof(BoardEventPacket));

    // 2. พิมพ์ออกทาง Serial ด้วย (กรณีต่อสาย USB เข้าบอร์ดแม่โดยตรงกับ Unity)
    Serial.printf("EVT %s %s\n", evPkt.topic, evPkt.payload);
}

// ============================================================================
//              SEND UART PACKETS TO ATTINY1604 TILE CONTROLLER
// ============================================================================
// โครงสร้างแพ็กเก็ต UART ตามตาราง 3.4:
// [0]     : 0xAA (START_BYTE)
// [1..7]  : pktType, srcId, destId, seqNum, frameNum, ttl, payloadLen (7 ไบต์)
// [8..N]  : Payload (0..48 ไบต์)
// [Last]  : CRC-8 ของ Payload
void sendTilePacketUART(uint8_t pktType, uint8_t destId, uint8_t frameNum, const uint8_t* payload, uint8_t payloadLen) {
    uint8_t header[8];
    header[0] = TILE_START_BYTE;
    header[1] = pktType;
    header[2] = MASTER_SRC_ID;
    header[3] = destId;
    header[4] = ++tileSeqCounter;
    header[5] = frameNum;
    header[6] = DEFAULT_TTL;
    header[7] = payloadLen;

    uint8_t crc = (payload != nullptr && payloadLen > 0) ? calcCRC8(payload, payloadLen) : 0x00;

    Serial2.write(header, sizeof(header));
    if (payload != nullptr && payloadLen > 0) {
        Serial2.write(payload, payloadLen);
    }
    Serial2.write(crc);
}

// ส่งข้อมูลสี 48 ไบต์ (ที่ย่อสัดส่วนความสว่างแล้ว) เข้า Back Buffer ของ ATtiny1604 แล้วสั่ง Latch ด้วย PKT_FRAME_COMMIT
void forwardFrameToTiles(uint8_t frameNum, const uint8_t scaledRgb[16][3]) {
    sendTilePacketUART(TILE_PKT_DATA, 0x00, frameNum, (const uint8_t*)scaledRgb, 48);
    sendTilePacketUART(TILE_PKT_COMMIT, BROADCAST_DEST_ID, frameNum, nullptr, 0);
}

// ============================================================================
//                      ON-BOARD WS2812B LED DRIVER
// ============================================================================
void showLEDs() {
    uint8_t scaledBuffer[16][3];

    for (int cell = 0; cell < NUM_CELLS; cell++) {
        uint8_t r = activeBuffer[cell][0];
        uint8_t g = activeBuffer[cell][1];
        uint8_t b = activeBuffer[cell][2];

        // ย่อสัดส่วนความสว่างสูงสุดไม่ให้เกิน MAX_CHANNEL_VAL (120/255) โดยคงอัตราส่วนแม่สี R:G:B เดิมไว้
        uint8_t maxComp = max(r, max(g, b));
        if (maxComp > MAX_CHANNEL_VAL) {
            r = (uint8_t)((uint16_t)r * MAX_CHANNEL_VAL / maxComp);
            g = (uint8_t)((uint16_t)g * MAX_CHANNEL_VAL / maxComp);
            b = (uint8_t)((uint16_t)b * MAX_CHANNEL_VAL / maxComp);
        }

        scaledBuffer[cell][0] = r;
        scaledBuffer[cell][1] = g;
        scaledBuffer[cell][2] = b;

#if (!USE_ATTINY_UART_DRIVER)
        // [โหมดเทสเดี่ยว]: ESP32 ขับไฟตรงออก GPIO 18
        for (int led = 0; led < LEDS_PER_CELL; led++) {
            strip.setPixelColor(cell * LEDS_PER_CELL + led, strip.Color(r, g, b));
        }
#endif
    }

#if (USE_ATTINY_UART_DRIVER)
    // [โหมดกระดานจริง]: ส่งข้อมูล UART (Serial2) ให้ ATtiny1604 ขับไฟ (ไม่บล็อกเวลา CPU ของ ESP32)
    forwardFrameToTiles(lastFrameId, scaledBuffer);
#else
    // [โหมดเทสเดี่ยว]: ยิงพัลส์ Latch ขับหลอดไฟ 64 ดวงตรงจาก ESP32
    strip.show();
#endif
}

// ============================================================================
//                   COORDINATE MAPPING (LOGICAL -> MOD1..16)
// ============================================================================
// แปลงพิกัดช่องจาก Unity (cellIndex = y * 4 + x โดย x=0..3, y=0..3)
// ให้ตรงกับตำแหน่งโมดูล MOD1..MOD16 บนแผ่น PCB
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
//                          PROCESS PACKETS
// ============================================================================
void processEventPacket(const BoardEventPacket &ev) {
    if (ev.header1 != HEADER_BYTE_1 || ev.header2 != HEADER_BYTE_2 || ev.pktType != PKT_TYPE_EVENT)
        return;

    uint8_t expectedCrc = calcCRC8(&ev.pktType, 50);
    if (ev.crc != expectedCrc && ev.crc != 0xFF)
        return;

    char safeTopic[16];
    char safePayload[32];
    memcpy(safeTopic, ev.topic, sizeof(safeTopic));
    memcpy(safePayload, ev.payload, sizeof(safePayload));
    safeTopic[sizeof(safeTopic) - 1] = '\0';
    safePayload[sizeof(safePayload) - 1] = '\0';

    Serial.printf("[CMD_RECV] topic=%s payload=%s\n", safeTopic, safePayload);
}

void processPacket(const BoardFramePacket &pkt) {
    if (pkt.header1 != HEADER_BYTE_1 || pkt.header2 != HEADER_BYTE_2)
        return;

    if (pkt.pktType == PKT_TYPE_EVENT) {
        BoardEventPacket ev;
        memcpy(&ev, &pkt, sizeof(BoardEventPacket));
        processEventPacket(ev);
        return;
    }

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
//                     SELF-TEST & CORNER DEMO PATTERNS
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

// ทดสอบไล่ไฟทีละช่องตั้งแต่ MOD1 ถึง MOD16
void runHardwareTestSequence() {
    Serial.println("[TEST] Running 4x4 Hardware LED Test Sequence...");

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

// จำลองสถานการณ์: ตัวหมากอยู่หัวมุม (0,0)
//   - ช่อง (0,0) = สีฟ้าสว่าง (ตำแหน่งหมากที่กำลังเล่น)
//   - ช่องเดินได้ (ระยะ 1-2) = สีเขียว
//   - ช่องโจมตีได้ (ระยะ 3) = สีแดง
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

#if (USE_ATTINY_UART_DRIVER)
    // โหมดกระดานจริง: เปิดพอร์ต Serial2 เชื่อมต่อไปยัง ATtiny1604
    Serial2.begin(TILE_UART_BAUD, SERIAL_8N1, TILE_UART_RX_PIN, TILE_UART_TX_PIN);
#else
    // โหมดเทสเดี่ยว: เริ่มต้นแถบหลอดไฟ NeoPixel บนขา GPIO 18
    strip.begin();
    strip.show();
#endif
    delay(200);

    // ไฟกระพริบสั้นๆ ตอนเปิดเครื่องเพื่อเช็กว่าบอร์ดพร้อมทำงาน
    clearAllCells();
    setCellColorLogical(0, 0, 0, 180, 255);
    setCellColorLogical(3, 0, 0, 180, 255);
    setCellColorLogical(0, 3, 0, 180, 255);
    setCellColorLogical(3, 3, 0, 180, 255);
    showLEDs();
    delay(400);
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

    // ลงทะเบียน Broadcast Peer สำหรับส่ง Event กลับไปยัง esp32_usb_bridge
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, BRIDGE_MAC, 6);
    peerInfo.channel = ESPNOW_CHANNEL;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);

    Serial.println("======================================================");
    Serial.println(" ESP32 Master Gateway 4x4 Board Ready (ESP-NOW 2-Way)");
    Serial.print  (" Master MAC Address : ");
    Serial.println(WiFi.macAddress());
#if (USE_ATTINY_UART_DRIVER)
    Serial.printf (" Mode: ATtiny1604 UART Driver (Modular Real Board) | Max Bright: %d/255\n", MAX_CHANNEL_VAL);
#else
    Serial.printf (" Mode: Direct ESP32 GPIO %d Driver (Standalone Test) | Total LEDs: %d | Max Bright: %d/255\n",
                   LED_PIN, TOTAL_LEDS, MAX_CHANNEL_VAL);
#endif
    Serial.println("======================================================");
}

// ============================================================================
//                                LOOP
// ============================================================================
void loop() {
    // 1. เมื่อได้รับเฟรมใหม่จาก ESP-NOW ให้อัปเดตไฟ LED และส่งต่อให้ ATtiny1604 ทันที
    if (frameReadyToCommit) {
        frameReadyToCommit = false;
        showLEDs();
        Serial.printf("[FRAME] Committed frameId=%u (total=%lu)\n",
                      lastFrameId, (unsigned long)rxFrameCount);
    }

    // 2. หากได้รับคำสั่งรันโหมดทดสอบ
    if (runTestPatternFlag) {
        runTestPatternFlag = false;
        runHardwareTestSequence();
    }

    // 3. รองรับการรับคำสั่งทาง USB Serial โดยตรงด้วย (เผื่อต่อสาย USB เทสบอร์ดแม่โดยตรง)
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
                    } else if (serialCmd.startsWith("EVT ") || serialCmd.startsWith("evt ")) {
                        String rest = serialCmd.substring(4);
                        rest.trim();
                        int sp = rest.indexOf(' ');
                        if (sp > 0) {
                            String topic = rest.substring(0, sp);
                            String payload = rest.substring(sp + 1);
                            topic.trim();
                            payload.trim();
                            sendEventToUnity(topic.c_str(), payload.c_str());
                        }
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
