/*
 * ============================================================================
 * esp32_master_gateway.ino
 * เฟิร์มแวร์สำหรับ ESP32 บนบอร์ดแม่ (Master Gateway / 4x4 Prototype Board)
 * โครงการ Modular Smart Board Game V1
 * ============================================================================
 * ฮาร์ดแวร์จริงตามวงจร KiCad (hardware/master/master.kicad_sch):
 *   - MCU: ESP32 Wemos D1 Mini (U2)
 *   - แหล่งจ่ายไฟ: 12V Type-C PD ผ่าน Buck Converter (D-SUN) แปลงเป็น +5V 3A
 *   - พอร์ต UART ต่อผ่าน Logic Level Converter (U3):
 *     * ESP32 TX = GPIO 17 (Pin 29 / D3) -> Level Converter -> ATtiny1604 PB3 (RX)
 *     * ESP32 RX = GPIO 16 (Pin 31 / D4) <- Level Converter <- ATtiny1604 PB2 (TX)
 * 
 * หน้าที่หลัก:
 *   1. รับแพ็กเก็ตเฟรมสี 4x4 (53 ไบต์) ผ่านโปรโตคอลไร้สาย ESP-NOW จากบอร์ด USB Bridge
 *      (รองรับทั้งเฟรมเดี่ยว BoardFramePacket 0x01 และระบบหลายแผ่น TileChunk 0x04 + CommitAll 0x05)
 *   2. แปลงพิกัดช่อง (16 ช่อง) ตามแนววงจร Left-to-Right + Flip Y
 *   3. ส่งคำสั่ง UART Packet (PKT_TILE_DATA 0x02 + PKT_FRAME_COMMIT 0x03)
 *      ไปยัง ATtiny1604 ประจำแผ่นกระดานผ่าน Serial2 (GPIO17 TX / GPIO16 RX)
 *   4. Auto-Addressing: แจก ID เริ่มจาก Master = 0 แล้วกระจายไปยังบอร์ดลูกแบบลูกโซ่
 *   5. ส่งอีเวนต์เกมกลับไปยัง USB Bridge ผ่าน ESP-NOW (BoardEventPacket 53 ไบต์, PKT_TYPE_EVENT 0x03)
 * ============================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// ============================================================================
//                         HARDWARE CONFIGURATION
// ============================================================================
#define LED_PIN             18      // ขา GPIO 18 (เฉพาะรุ่น experimental/master_esp32)
#define NUM_CELLS           16      // ตาราง 4x4 = 16 ช่อง (MOD1 ถึง MOD16)
#define LEDS_PER_CELL       4       // โมดูล WS2812B 2x2 = 4 ดวงต่อช่อง
#define TOTAL_LEDS          (NUM_CELLS * LEDS_PER_CELL) // 64 ดวง
#define MAX_CHANNEL_VAL     120     // จำกัดค่าสีสูงสุด 120/255 ตามข้อกำหนดโครงงาน
#define ESPNOW_CHANNEL      1       // ช่องสัญญาณ Wi-Fi สำหรับ ESP-NOW (ตรงกับ esp32_usb_bridge)
#define SERIAL_BAUD         115200

// ============================================================================
//                         LED DRIVER MODE SELECTION
// ============================================================================
// เลือกว่าจะให้ฮาร์ดแวร์ฝั่งไหนเป็นผู้ขับหลอดไฟ WS2812B:
// true  = ส่งข้อมูลผ่าน UART ไปให้ ATtiny1604 ขับไฟ (สำหรับบอร์ดจริง hardware/master)
// false = ขับไฟ WS2812B ตรงจาก ESP32 ขา GPIO 18 (สำหรับบอร์ดทดลอง hardware/experimental/master_esp32)
#define USE_ATTINY_UART_DRIVER   true

#if (!USE_ATTINY_UART_DRIVER)
#include <Adafruit_NeoPixel.h>
#endif

// ขา UART สำหรับสื่อสารกับ ATtiny1604 ประจำแผ่นกระดาน (Serial2 ตามวงจรจริง)
#define TILE_UART_BAUD      115200
#define TILE_UART_RX_PIN    16      // GPIO 16 (D4) ต่อเข้า Level Shifter ด้านต่ำ
#define TILE_UART_TX_PIN    17      // GPIO 17 (D3) ต่อเข้า Level Shifter ด้านต่ำ

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
#define PKT_TYPE_FRAME        0x01
#define PKT_TYPE_TEST         0x02
#define PKT_TYPE_EVENT        0x03
#define PKT_TYPE_TILE_CHUNK   0x04
#define PKT_TYPE_COMMIT_ALL   0x05
#define PKT_TYPE_CARD         0x10

// ============================================================================
//               TILE UART PROTOCOL CONSTANTS (TO ATTINY1604)
// ============================================================================
#define TILE_START_BYTE     0xAA
#define TILE_PKT_HELLO      0x01
#define TILE_PKT_DATA       0x02
#define TILE_PKT_COMMIT     0x03
#define TILE_PKT_SET_ID     0x05
#define MASTER_SRC_ID       0x00
#define BROADCAST_DEST_ID   0xFF
#define DEFAULT_TTL         16      // ขยายรองรับสูงสุด 16 แผ่นตามแนวทาง N-Tiles Scalability

#if (!USE_ATTINY_UART_DRIVER)
Adafruit_NeoPixel strip(TOTAL_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
#endif

#pragma pack(push, 1)
struct BoardFramePacket {
    uint8_t header1;        // 0xAA
    uint8_t header2;        // 0x55
    uint8_t pktType;        // 0x01 = Frame Data, 0x02 = Test Pattern
    uint8_t frameId;        // Sequence / Frame ID
    uint8_t rgb[16][3];     // 16 cells x (R, G, B) = 48 bytes
    uint8_t crc;            // CRC-8 over pktType + frameId + rgb (50 bytes)
};

struct TileChunkPacket {
    uint8_t header1;        // 0xAA
    uint8_t header2;        // 0x55
    uint8_t pktType;        // 0x04 = Scalable Tile Chunk
    uint8_t tileId;         // Tile ID: 0 (Master), 1..N (Slaves)
    uint8_t rgb[16][3];     // 16 cells x (R, G, B) = 48 bytes
    uint8_t crc;            // CRC-8 over pktType + tileId + rgb (50 bytes)
};

struct TileCommitPacket {
    uint8_t header1;        // 0xAA
    uint8_t header2;        // 0x55
    uint8_t pktType;        // 0x05 = Synchronous Commit
    uint8_t frameId;        // Frame Sequence ID (0..255)
    uint8_t totalTiles;     // Total Tiles in this frame (e.g. 4, 6, 8)
    uint8_t padding[46];    // 0x00 padding to 53 bytes
    uint8_t crc;            // CRC-8 over pktType + frameId + totalTiles + padding (50 bytes)
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

struct CardPacket {
    uint8_t header1;        // 0xAA
    uint8_t header2;        // 0x55
    uint8_t pktType;        // 0x10 = RFID Card Tap Event (esp32_rfid_addon)
    uint8_t addonId;        // ID โมดูล Addon (1..4)
    uint8_t uidLength;      // ขนาด UID ของการ์ด (4, 7 หรือ 10 ไบต์)
    uint8_t uid[10];        // ข้อมูล UID การ์ด
    uint8_t crc;            // CRC-8 ครอบคลุม pktType..uid (13 ไบต์)
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
uint8_t currentMaxBrightness = MAX_CHANNEL_VAL; // ปรับค่าได้แบบ Real-time ผ่านคำสั่ง /bright โดยไม่ต้องแฟลช ATtiny1604 ใหม่

// Lock-free Ring Buffer สำหรับพักแพ็กเก็ตจาก ESP-NOW Callback (ป้องกัน Wi-Fi Task ค้างหรือชนกับ UART)
#define ESPNOW_RX_QUEUE_SIZE 32
BoardFramePacket espNowRxQueue[ESPNOW_RX_QUEUE_SIZE];
volatile uint8_t espNowRxHead = 0;
volatile uint8_t espNowRxTail = 0;

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
//       BRIGHTNESS SCALING & COORDINATE MAPPING (LOGICAL -> MOD1..16)
// ============================================================================
// ย่อสัดส่วนความสว่างสูงสุดไม่ให้เกิน currentMaxBrightness (ค่าเริ่มต้น 120/255) โดยคงอัตราส่วนแม่สี R:G:B เดิมไว้
inline void scaleColorToMaxBrightness(uint8_t &r, uint8_t &g, uint8_t &b) {
    uint8_t maxComp = max(r, max(g, b));
    if (maxComp > currentMaxBrightness && maxComp > 0) {
        r = (uint8_t)((uint16_t)r * currentMaxBrightness / maxComp);
        g = (uint8_t)((uint16_t)g * currentMaxBrightness / maxComp);
        b = (uint8_t)((uint16_t)b * currentMaxBrightness / maxComp);
    }
}

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

// ฟังก์ชันกลางสำหรับแปลงพิกัด Logical 16 ช่อง เป็นตำแหน่งหลอดบน PCB จริง พร้อมจำกัดความสว่างในจุดเดียว
void mapAndScaleLogicalGrid(const uint8_t srcLogical[16][3], uint8_t dstHardware[16][3]) {
    for (int i = 0; i < NUM_CELLS; i++) {
        uint8_t r = srcLogical[i][0];
        uint8_t g = srcLogical[i][1];
        uint8_t b = srcLogical[i][2];
        scaleColorToMaxBrightness(r, g, b);

        int hwIdx = mapLogicalCellToHardwareMod(i);
        dstHardware[hwIdx][0] = r;
        dstHardware[hwIdx][1] = g;
        dstHardware[hwIdx][2] = b;
    }
}

// ============================================================================
//          SEND GAME EVENT TO UNITY (VIA ESP-NOW & DIRECT SERIAL)
// ============================================================================
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

void announceMasterConnectedOnce() {
    static bool hasAnnouncedMaster = false;
    if (!hasAnnouncedMaster) {
        hasAnnouncedMaster = true;
        sendEventToUnity("/master", "connected");
    }
}

// ============================================================================
//              SEND UART PACKETS TO ATTINY1604 TILE CONTROLLER
// ============================================================================
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

    // พิมพ์ Debug Log เพื่อช่วยเช็คการทำงานของสาย UART และ Level Converter
    Serial.printf("[UART_TX -> ATTINY] PktType=0x%02X Dest=%u Seq=%u Len=%u CRC=0x%02X\n",
                  pktType, destId, header[4], payloadLen, crc);
}

// ฟังก์ชันสั่งแจก ID ใหม่อัตโนมัติแบบลูกโซ่ (0..N)
void triggerAutoAddressing() {
    uint8_t startId = 0;
    Serial.println("[AUTO_ADDR] Dispatched initial Tile ID assignment (0..N)");
    sendTilePacketUART(TILE_PKT_SET_ID, 0xFF, 0, &startId, 1);
}

// ฟังก์ชันยิง Ping ไปทักทาย ATtiny1604 (PKT_HELLO 0x01) เพื่อเช็คการเชื่อมต่อ
void pingTileMaster() {
    Serial.println("[PING] Sending PKT_HELLO (0x01) to Master ATtiny1604...");
    sendTilePacketUART(TILE_PKT_HELLO, 0x00, 0, nullptr, 0);
}

// ส่งข้อมูลสี 48 ไบต์เข้า Back Buffer ของ ATtiny1604 แล้วสั่ง Latch ด้วย PKT_FRAME_COMMIT
void forwardFrameToTiles(uint8_t frameNum, const uint8_t hwScaledRgb[16][3]) {
    sendTilePacketUART(TILE_PKT_DATA, 0x00, frameNum, (const uint8_t*)hwScaledRgb, 48);
    sendTilePacketUART(TILE_PKT_COMMIT, BROADCAST_DEST_ID, frameNum, nullptr, 0);
}

// ============================================================================
//                      ON-BOARD WS2812B LED DRIVER
// ============================================================================
void showLEDs() {
#if (USE_ATTINY_UART_DRIVER)
    // [โหมดบอร์ดจริง V1]: ส่งข้อมูล UART (Serial2) ให้ ATtiny1604 ขับไฟ (activeBuffer ถูกสเกลสีแล้ว)
    forwardFrameToTiles(lastFrameId, activeBuffer);
#else
    // [โหมดบอร์ดทดลอง]: ยิงพัลส์ Latch ขับหลอดไฟ 64 ดวงตรงจาก ESP32 GPIO 18
    for (int cell = 0; cell < NUM_CELLS; cell++) {
        uint8_t r = activeBuffer[cell][0];
        uint8_t g = activeBuffer[cell][1];
        uint8_t b = activeBuffer[cell][2];
        for (int led = 0; led < LEDS_PER_CELL; led++) {
            strip.setPixelColor(cell * LEDS_PER_CELL + led, strip.Color(r, g, b));
        }
    }
    strip.show();
#endif
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

    if (strcmp(safeTopic, "/card") == 0) {
        // ส่งต่ออีเวนต์แตะการ์ดออกทาง USB Serial (รองรับกรณีต่อสาย USB เข้าบอร์ดแม่โดยตรง)
        Serial.printf("EVT /card %s\n", safePayload);
        return;
    }

    Serial.printf("[CMD_RECV] topic=%s payload=%s\n", safeTopic, safePayload);

    if (strcmp(safeTopic, "/bright") == 0) {
        int val = atoi(safePayload);
        if (val >= 1 && val <= 220) {
            currentMaxBrightness = (uint8_t)val;
            Serial.printf("[BRIGHTNESS] Updated currentMaxBrightness = %u/255\n", currentMaxBrightness);
        }
        return;
    }

    #if (USE_ATTINY_UART_DRIVER)
    // รองรับคำสั่งสั่งแจก ID ใหม่อัตโนมัติทางวิทยุ ESP-NOW จาก USB Bridge
    if (strcmp(safeTopic, "/rescan") == 0 || strcmp(safeTopic, "/addr") == 0 || strcmp(safeTopic, "/set_id") == 0) {
        triggerAutoAddressing();
    } else if (strcmp(safeTopic, "/ping") == 0) {
        pingTileMaster();
    }
    #endif
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

    // 1. แพ็กเก็ตเฟรมเดี่ยว (4x4) แบบดั้งเดิม
    if (pkt.pktType == PKT_TYPE_FRAME) {
        announceMasterConnectedOnce();
        mapAndScaleLogicalGrid(pkt.rgb, backBuffer);
        memcpy(activeBuffer, backBuffer, sizeof(activeBuffer));
        lastFrameId = pkt.frameId;
        rxFrameCount++;
        frameReadyToCommit = true;
        return;
    }

    // 2. แพ็กเก็ตชังก์สีสำหรับระบบหลายแผ่น (Scalable N-Tiles)
    if (pkt.pktType == PKT_TYPE_TILE_CHUNK) {
        announceMasterConnectedOnce();
        TileChunkPacket chunk;
        memcpy(&chunk, &pkt, sizeof(TileChunkPacket));

        uint8_t mappedChunk[NUM_CELLS][3];
        mapAndScaleLogicalGrid(chunk.rgb, mappedChunk);

        if (chunk.tileId == 0) {
            // Master Tile: บันทึกข้อมูลลง backBuffer ของบอร์ดแม่
            memcpy(backBuffer, mappedChunk, sizeof(backBuffer));
            #if (USE_ATTINY_UART_DRIVER)
            sendTilePacketUART(TILE_PKT_DATA, 0x00, lastFrameId, (const uint8_t*)backBuffer, 48);
            #endif
        } else {
            // Slave Tiles: ส่งต่อ UART ข้ามแผ่นไปยัง ATtiny1604 ของ Slave นั้นๆ (แปลงตำแหน่งแล้วเพื่อให้ทิศทางตรงกับ PCB)
            #if (USE_ATTINY_UART_DRIVER)
            sendTilePacketUART(TILE_PKT_DATA, chunk.tileId, lastFrameId, (const uint8_t*)mappedChunk, 48);
            #endif
        }
        Serial.printf("[CHUNK] Processed tileId=%u (ok)\n", chunk.tileId);
        return;
    }

    // 3. แพ็กเก็ตยืนยันเฟรมแบบซิงโครนัสพร้อมกันทุกแผ่น (Commit All)
    if (pkt.pktType == PKT_TYPE_COMMIT_ALL) {
        announceMasterConnectedOnce();
        TileCommitPacket cmt;
        memcpy(&cmt, &pkt, sizeof(TileCommitPacket));
        lastFrameId = cmt.frameId;
        rxFrameCount++;

        // บันทึกเฟรมของบอร์ดแม่
        memcpy(activeBuffer, backBuffer, sizeof(activeBuffer));
        #if (USE_ATTINY_UART_DRIVER)
        // สั่ง Broadcast commit (0xFF) ให้บอร์ดลูกทุกแผ่น Latch สีพร้อมกัน
        sendTilePacketUART(TILE_PKT_COMMIT, BROADCAST_DEST_ID, lastFrameId, nullptr, 0);
        #else
        showLEDs();
        #endif
        Serial.printf("[COMMIT_ALL] Frame %u committed across %u tiles\n", cmt.frameId, cmt.totalTiles);
        return;
    }
}

// ============================================================================
//               ESP-NOW RECEIVE CALLBACK (NON-BLOCKING RING BUFFER)
// ============================================================================
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
#else
void onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
#endif
    if (len == sizeof(BoardFramePacket)) {
        uint8_t nextHead = (uint8_t)((espNowRxHead + 1) % ESPNOW_RX_QUEUE_SIZE);
        if (nextHead != espNowRxTail) {
            memcpy((void*)&espNowRxQueue[espNowRxHead], incomingData, sizeof(BoardFramePacket));
            espNowRxHead = nextHead;
        }
        return;
    }

    // รองรับการรับแพ็กเก็ตแตะการ์ด (16 ไบต์) จาก esp32_rfid_addon โดยตรง
    if (len == sizeof(CardPacket) &&
        incomingData[0] == HEADER_BYTE_1 &&
        incomingData[1] == HEADER_BYTE_2 &&
        incomingData[2] == PKT_TYPE_CARD) {
        CardPacket cardPkt;
        memcpy(&cardPkt, incomingData, sizeof(CardPacket));
        uint8_t expectedCrc = calcCRC8(&cardPkt.pktType, 13);
        if (cardPkt.crc == expectedCrc || cardPkt.crc == 0xFF) {
            BoardEventPacket evPkt;
            memset(&evPkt, 0, sizeof(evPkt));
            evPkt.header1 = HEADER_BYTE_1;
            evPkt.header2 = HEADER_BYTE_2;
            evPkt.pktType = PKT_TYPE_EVENT;
            evPkt.seqId   = cardPkt.addonId;
            strcpy(evPkt.topic, "/card");

            char hexBuf[32];
            int pos = snprintf(hexBuf, sizeof(hexBuf), "%u_", cardPkt.addonId);
            uint8_t ulen = (cardPkt.uidLength > 10) ? 10 : cardPkt.uidLength;
            for (uint8_t i = 0; i < ulen && (pos + 2) < (int)sizeof(hexBuf); i++) {
                pos += snprintf(hexBuf + pos, sizeof(hexBuf) - pos, "%02X", cardPkt.uid[i]);
            }
            strncpy(evPkt.payload, hexBuf, sizeof(evPkt.payload) - 1);
            evPkt.crc = 0xFF;

            uint8_t nextHead = (uint8_t)((espNowRxHead + 1) % ESPNOW_RX_QUEUE_SIZE);
            if (nextHead != espNowRxTail) {
                memcpy((void*)&espNowRxQueue[espNowRxHead], &evPkt, sizeof(BoardFramePacket));
                espNowRxHead = nextHead;
            }
        }
    }
}

// ============================================================================
//                     SELF-TEST & CORNER DEMO PATTERNS
// ============================================================================
void setCellColorLogical(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (x < 0 || x >= 4 || y < 0 || y >= 4) return;
    scaleColorToMaxBrightness(r, g, b);
    int logicalIdx = y * 4 + x;
    int hwIdx = mapLogicalCellToHardwareMod(logicalIdx);
    activeBuffer[hwIdx][0] = r;
    activeBuffer[hwIdx][1] = g;
    activeBuffer[hwIdx][2] = b;
}

void clearAllCells() {
    memset(backBuffer, 0, sizeof(backBuffer));
    memset(activeBuffer, 0, sizeof(activeBuffer));
#if (USE_ATTINY_UART_DRIVER)
    // สั่งล้างสีทุกแผ่นพร้อมกัน (Broadcast 0xFF ทั้ง Master และ Slave ทุกแผ่น)
    sendTilePacketUART(TILE_PKT_DATA, BROADCAST_DEST_ID, lastFrameId, (const uint8_t*)activeBuffer, 48);
    sendTilePacketUART(TILE_PKT_COMMIT, BROADCAST_DEST_ID, lastFrameId, nullptr, 0);
#else
    showLEDs();
#endif
}

void runHardwareTestSequence() {
    Serial.println("[TEST] Running 4x4 Hardware LED Test Sequence...");

    uint8_t r = 40, g = 220, b = 90;
    scaleColorToMaxBrightness(r, g, b);

    for (int mod = 0; mod < NUM_CELLS; mod++) {
        memset(activeBuffer, 0, sizeof(activeBuffer));
        activeBuffer[mod][0] = r;
        activeBuffer[mod][1] = g;
        activeBuffer[mod][2] = b;
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

#if (USE_ATTINY_UART_DRIVER)
    // โหมดบอร์ดจริง V1: เปิดพอร์ต Serial2 เชื่อมต่อ ATtiny1604 ผ่าน Level Shifter
    Serial2.begin(TILE_UART_BAUD, SERIAL_8N1, TILE_UART_RX_PIN, TILE_UART_TX_PIN);

    // หน่วงเวลาเล็กน้อยให้ ATtiny1604 ทุกแผ่นบูตเสร็จ
    delay(200);

    // เริ่มแจก ID แบบลูกโซ่ โดยเริ่มจากบอร์ดแม่ = 0
    triggerAutoAddressing();
#else
    // โหมดบอร์ดทดลอง: เริ่มต้นแถบหลอดไฟ NeoPixel บนขา GPIO 18
    strip.begin();
    strip.show();
#endif
    delay(200);

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

    // ส่งสัญญาณแจ้งเตือนไปยัง Unity ว่าบอร์ดแม่ (Master Board) ออนไลน์และเชื่อมต่อแล้ว
    delay(100);
    sendEventToUnity("/master", "online");

    Serial.println("======================================================");
    Serial.println(" ESP32 Master Gateway 4x4 Board Ready (ESP-NOW 2-Way)");
    Serial.print  (" Master MAC Address : ");
    Serial.println(WiFi.macAddress());
#if (USE_ATTINY_UART_DRIVER)
    Serial.printf (" Mode: ATtiny1604 UART Driver (Hardware V1 Real Board) | Max Bright: %d/255\n", MAX_CHANNEL_VAL);
    Serial.printf (" UART Serial2 Config: TX=GPIO%d, RX=GPIO%d @ %d baud\n", TILE_UART_TX_PIN, TILE_UART_RX_PIN, TILE_UART_BAUD);
#else
    Serial.printf (" Mode: Direct ESP32 GPIO %d Driver (Experimental Board) | Total LEDs: %d | Max Bright: %d/255\n",
                   LED_PIN, TOTAL_LEDS, MAX_CHANNEL_VAL);
#endif
    Serial.println(" Commands available via Serial Monitor: PING, TEST, CORNER, CLEAR, ADDR");
    Serial.println("======================================================");
}

// ============================================================================
//                                LOOP
// ============================================================================
void loop() {
    // 0. ดึงแพ็กเก็ตที่พักไว้ใน Ring Buffer จาก ESP-NOW มาประมวลผลบน Main Thread อย่างปลอดภัย
    while (espNowRxTail != espNowRxHead) {
        BoardFramePacket pkt;
        memcpy(&pkt, (const void*)&espNowRxQueue[espNowRxTail], sizeof(BoardFramePacket));
        espNowRxTail = (uint8_t)((espNowRxTail + 1) % ESPNOW_RX_QUEUE_SIZE);
        processPacket(pkt);
    }

    // 1. เมื่อได้รับเฟรมใหม่จาก ESP-NOW ให้อัปเดตไฟ LED และส่งต่อให้ ATtiny1604
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

    #if (USE_ATTINY_UART_DRIVER)
    // 4. รับข้อมูลตอบกลับจาก ATtiny1604 บอร์ดแม่ผ่าน Serial2 (ACK / รายงานขั้วต่อ)
    while (Serial2.available() > 0) {
        uint8_t b = (uint8_t)Serial2.read();
        unsigned long nowS2 = millis();
        static enum { S2_WAIT_HEADER, S2_READ_INFO, S2_READ_PAYLOAD, S2_READ_CRC } s2State = S2_WAIT_HEADER;
        static uint8_t s2Type, s2Src, s2Dest, s2Seq, s2Frame, s2Ttl, s2Len;
        static uint8_t s2Payload[16];
        static uint8_t s2PayloadIdx = 0;
        static unsigned long lastS2ByteAt = 0;

        if (s2State != S2_WAIT_HEADER && (nowS2 - lastS2ByteAt > 50)) {
            s2State = S2_WAIT_HEADER;
        }
        lastS2ByteAt = nowS2;

        switch (s2State) {
            case S2_WAIT_HEADER:
                if (b == 0xAA) {
                    s2State = S2_READ_INFO;
                    s2PayloadIdx = 0;
                }
                break;
            case S2_READ_INFO:
                s2Payload[s2PayloadIdx++] = b;
                if (s2PayloadIdx >= 7) {
                    s2Type = s2Payload[0];
                    s2Src  = s2Payload[1];
                    s2Dest = s2Payload[2];
                    s2Seq  = s2Payload[3];
                    s2Frame = s2Payload[4];
                    s2Ttl  = s2Payload[5];
                    s2Len  = s2Payload[6];
                    s2PayloadIdx = 0;
                    if (s2Len > 0 && s2Len <= sizeof(s2Payload)) {
                        s2State = S2_READ_PAYLOAD;
                    } else if (s2Len == 0) {
                        s2State = S2_READ_CRC;
                    } else {
                        Serial.printf("[UART_RX <- ATTINY] Invalid payload length: %u\n", s2Len);
                        s2State = S2_WAIT_HEADER;
                    }
                }
                break;
            case S2_READ_PAYLOAD:
                s2Payload[s2PayloadIdx++] = b;
                if (s2PayloadIdx >= s2Len) {
                    s2State = S2_READ_CRC;
                }
                break;
            case S2_READ_CRC: {
                uint8_t receivedCRC = b;
                uint8_t expectedCRC = (s2Len > 0) ? calcCRC8(s2Payload, s2Len) : 0x00;
                if (receivedCRC == expectedCRC || receivedCRC == 0xFF) {
                    Serial.printf("[UART_RX <- ATTINY] Valid packet! Type=0x%02X from Tile %u (len=%u)\n", s2Type, s2Src, s2Len);
                    if (s2Type == 0x81 && s2Len >= 2) { // PKT_HELLO_ACK
                        uint8_t ackTileId = s2Payload[0];
                        uint8_t ackSides  = s2Payload[1];
                        Serial.printf("[TILE_ACK] Tile %u online! Connected sides: 0x%02X\n", ackTileId, ackSides);
                        char payloadStr[32];
                        snprintf(payloadStr, sizeof(payloadStr), "%u_%u", ackTileId, ackSides);
                        sendEventToUnity("board/sides", payloadStr);
                    }
                } else {
                    Serial.printf("[UART_RX <- ATTINY] CRC Error! Type=0x%02X got CRC=0x%02X (expected 0x%02X)\n", s2Type, receivedCRC, expectedCRC);
                }
                s2State = S2_WAIT_HEADER;
                break;
            }
        }
    }
    #endif

    // 3. รองรับการรับคำสั่งทาง USB Serial โดยตรง (กรณีต่อสาย USB เทสกับ PC)
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
                    } else if (serialCmd.equalsIgnoreCase("PING")) {
                        pingTileMaster();
                    } else if (serialCmd.equalsIgnoreCase("TEST")) {
                        runHardwareTestSequence();
                    } else if (serialCmd.equalsIgnoreCase("CORNER")) {
                        showCornerPattern();
                    } else if (serialCmd.equalsIgnoreCase("CLEAR")) {
                        clearAllCells();
                    } else if (serialCmd.equalsIgnoreCase("ADDR") || serialCmd.equalsIgnoreCase("SET_ID") || serialCmd.equalsIgnoreCase("RESCAN")) {
                        triggerAutoAddressing();
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
