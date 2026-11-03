/*
 * ============================================================================
 * esp32_usb_bridge.ino
 * บอร์ดตัวส่งและรับสัญญาณสองทิศทาง (Bi-Directional USB-to-ESP-NOW Bridge)
 * เสียบสาย USB เข้ากับ Laptop / PC (Unity)
 * โครงการ Modular Smart Board Game V1
 * ============================================================================
 * หน้าที่:
 *   1. ขาออก (Unity -> USB Serial -> ESP-NOW -> Master Board / Addons):
 *      - รับเฟรมสีตาราง 4x4 แบบ Binary (53 ไบต์: 0xAA 0x55 0x01 + FrameId + 48B RGB + CRC8)
 *      - รับเฟรมสีแบบหลายแผ่น (Scalable N-Tiles):
 *        * PKT_TYPE_TILE_CHUNK (0x04, 53 ไบต์): ข้อมูลสี 16 ช่องของ Tile ID นั้นๆ
 *        * PKT_TYPE_COMMIT_ALL (0x05, 53 ไบต์): คำสั่ง Latch สีพร้อมกันทุกแผ่น
 *      - รับคำสั่งเกมจาก Unity เช่น "CMD /team Dummy1_Red", "CMD /led Dummy1_Turn"
 *        แล้วแพ็กเป็น BoardEventPacket (0xAA 0x55 0x03, 53 ไบต์) ส่งออกทาง ESP-NOW
 *      - รองรับ Text Command ทดสอบไฟ (CLEAR, FILL, CORNER, TEST, FRAME, MAC)
 *   2. ขาเข้า (Master Board / RFID / Dice / Card Addon -> ESP-NOW -> USB Serial -> Unity):
 *      - รับแพ็กเก็ต BoardEventPacket (0xAA 0x55 0x03, 53 ไบต์) จากบอร์ดกระดานหรือลูกเต๋า
 *        ส่งออก Serial: "EVT <topic> <payload>\n" (เช่น "EVT /rfid Dummy1_A1", "EVT /dice 6")
 *      - รับแพ็กเก็ต CardPacket (0xAA 0x55 0x10, 16 ไบต์) จากแท่นอ่านการ์ด esp32_rfid_addon
 *        ส่งออก Serial: "EVT /card <addonId>_<UID>\n" (เช่น "EVT /card 1_DEADBEEF")
 * ============================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// ============================================================================
//                         CONFIGURATION
// ============================================================================
#define SERIAL_BAUD     115200
#define ESPNOW_CHANNEL  1

// MAC Address ปลายทาง (บอร์ดแม่ Master / ตัวหมาก)
// ค่าเริ่มต้น: FF:FF:FF:FF:FF:FF (Broadcast ส่งหาทุกบอร์ดในช่องสัญญาณเดียวกันทันที)
uint8_t TARGET_MAC[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

// ============================================================================
//                      PACKET STRUCTURES
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
//                           STATE VARIABLES
// ============================================================================
uint8_t rxBuffer[PACKET_SIZE];
int rxIndex = 0;
unsigned long lastByteTime = 0;

String cmdLine = "";
uint8_t localFrameCounter = 0;
uint8_t localEventCounter = 0;
uint32_t txSuccessCount = 0;
uint32_t txFailCount = 0;

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
//                         ESP-NOW CALLBACKS
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

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
    const uint8_t *mac = info->src_addr;
#else
void onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
#endif
    // 1. ตรวจสอบว่าเป็นแพ็กเก็ต Event (PKT_TYPE_EVENT = 0x03) ขนาด 53 ไบต์จากบอร์ดกระดาน/ลูกเต๋าหรือไม่
    if (len == sizeof(BoardEventPacket) &&
        incomingData[0] == HEADER_BYTE_1 &&
        incomingData[1] == HEADER_BYTE_2 &&
        incomingData[2] == PKT_TYPE_EVENT) {

        BoardEventPacket evPkt;
        memcpy(&evPkt, incomingData, sizeof(BoardEventPacket));

        uint8_t expectedCrc = calcCRC8(&evPkt.pktType, 50);
        if (evPkt.crc == expectedCrc || evPkt.crc == 0xFF) {
            evPkt.topic[sizeof(evPkt.topic) - 1] = '\0';
            evPkt.payload[sizeof(evPkt.payload) - 1] = '\0';
            // ส่งต่อเข้า Unity ทาง USB Serial ทันที
            Serial.printf("EVT %s %s\n", evPkt.topic, evPkt.payload);
        }
        return;
    }

    // 2. ตรวจสอบว่าเป็นแพ็กเก็ตแตะการ์ด (PKT_TYPE_CARD = 0x10) ขนาด 16 ไบต์จาก esp32_rfid_addon หรือไม่
    if (len == sizeof(CardPacket) &&
        incomingData[0] == HEADER_BYTE_1 &&
        incomingData[1] == HEADER_BYTE_2 &&
        incomingData[2] == PKT_TYPE_CARD) {

        CardPacket cardPkt;
        memcpy(&cardPkt, incomingData, sizeof(CardPacket));

        uint8_t expectedCrc = calcCRC8(&cardPkt.pktType, 13);
        if (cardPkt.crc == expectedCrc || cardPkt.crc == 0xFF) {
            String uidStr = "";
            uint8_t ulen = (cardPkt.uidLength > 10) ? 10 : cardPkt.uidLength;
            for (uint8_t i = 0; i < ulen; i++) {
                if (cardPkt.uid[i] < 0x10) uidStr += "0";
                uidStr += String(cardPkt.uid[i], HEX);
            }
            uidStr.toUpperCase();

            // ส่งต่อเข้า Unity ทาง USB Serial: "EVT /card <addonId>_<UID>" เช่น "EVT /card 1_DEADBEEF"
            Serial.printf("EVT /card %u_%s\n", cardPkt.addonId, uidStr.c_str());
        }
        return;
    }

    Serial.print("[MASTER_ACK] MAC=");
    for (int i = 0; i < 6; i++) {
        if (i > 0) Serial.print(":");
        if (mac[i] < 16) Serial.print("0");
        Serial.print(mac[i], HEX);
    }
    Serial.print(" LEN=");
    Serial.println(len);
}

// ============================================================================
//                      SEND PACKETS OVER ESP-NOW
// ============================================================================
void sendFrameEspNow(BoardFramePacket &pkt) {
    pkt.header1 = HEADER_BYTE_1;
    pkt.header2 = HEADER_BYTE_2;
    pkt.crc = calcCRC8(&pkt.pktType, 50); // pktType (1) + frameId (1) + rgb (48) = 50 bytes

    esp_err_t res = esp_now_send(TARGET_MAC, (const uint8_t*)&pkt, sizeof(BoardFramePacket));
    if (res == ESP_OK) {
        Serial.printf("[OK] Sent frameId=%u via ESP-NOW (ok=%lu, fail=%lu)\n",
                      pkt.frameId, (unsigned long)txSuccessCount, (unsigned long)txFailCount);
    } else {
        Serial.printf("[ERR] esp_now_send error=%d\n", (int)res);
    }
}

void sendEventEspNow(const char* topic, const char* payload) {
    BoardEventPacket evPkt;
    memset(&evPkt, 0, sizeof(evPkt));
    evPkt.header1 = HEADER_BYTE_1;
    evPkt.header2 = HEADER_BYTE_2;
    evPkt.pktType = PKT_TYPE_EVENT;
    evPkt.seqId   = ++localEventCounter;

    strncpy(evPkt.topic, topic, sizeof(evPkt.topic) - 1);
    strncpy(evPkt.payload, payload, sizeof(evPkt.payload) - 1);
    evPkt.crc = calcCRC8(&evPkt.pktType, 50);

    esp_now_send(TARGET_MAC, (const uint8_t*)&evPkt, sizeof(BoardEventPacket));
}

// ============================================================================
//              PARSE TEXT COMMANDS (FROM UNITY OR SERIAL MONITOR)
// ============================================================================
void handleTextCommand(String line) {
    line.trim();
    if (line.length() == 0) return;

    if (line.equalsIgnoreCase("MAC") || line.equalsIgnoreCase("STATUS")) {
        Serial.print("[USB_SENDER] My MAC: ");
        Serial.print(WiFi.macAddress());
        Serial.printf(" | Channel: %d | TX_OK: %lu | TX_FAIL: %lu\n",
                      ESPNOW_CHANNEL, (unsigned long)txSuccessCount, (unsigned long)txFailCount);
        return;
    }

    // 1. คำสั่งเกมจาก Unity ส่งต่อไปยังบอร์ด/หมากผ่าน ESP-NOW: "CMD <topic> <payload>"
    //    เช่น "CMD /team Dummy1_Red" หรือ "CMD /led Dummy1_Turn"
    if (line.startsWith("CMD ") || line.startsWith("cmd ")) {
        String rest = line.substring(4);
        rest.trim();
        int sp = rest.indexOf(' ');
        if (sp > 0) {
            String topic = rest.substring(0, sp);
            String payload = rest.substring(sp + 1);
            topic.trim();
            payload.trim();
            sendEventEspNow(topic.c_str(), payload.c_str());
        } else if (rest.length() > 0) {
            sendEventEspNow(rest.c_str(), "");
        }
        return;
    }

    // สั่งแจก ID ใหม่ไปยังบอร์ดแม่ผ่านคลื่นวิทยุ ESP-NOW (สั่งจากหน้าจอคอมได้ทันที)
    if (line.equalsIgnoreCase("RESCAN") || line.equalsIgnoreCase("ADDR") || line.equalsIgnoreCase("SET_ID")) {
        sendEventEspNow("/rescan", "1");
        Serial.println("[BRIDGE] Dispatched wireless RESCAN command to Master Board via ESP-NOW");
        return;
    }

    // สั่ง Ping บอร์ดแม่และ ATtiny1604 ผ่านคลื่นวิทยุ ESP-NOW
    if (line.equalsIgnoreCase("PING")) {
        sendEventEspNow("/ping", "1");
        Serial.println("[BRIDGE] Dispatched wireless PING command to Master Board via ESP-NOW");
        return;
    }

    // สั่งปรับเพดานความสว่างสูงสุด (1..220) ไปยังบอร์ดแม่แบบไร้สาย เช่น "BRIGHT 120"
    if (line.startsWith("BRIGHT ") || line.startsWith("bright ")) {
        String valStr = line.substring(7);
        valStr.trim();
        sendEventEspNow("/bright", valStr.c_str());
        Serial.printf("[BRIDGE] Dispatched wireless BRIGHTNESS=%s to Master Board via ESP-NOW\n", valStr.c_str());
        return;
    }

    // 2. คำสั่งจำลอง Event ขาเข้า Unity (สำหรับพิมพ์ทดสอบใน Serial): "EVT <topic> <payload>"
    if (line.startsWith("EVT ") || line.startsWith("evt ")) {
        String rest = line.substring(4);
        rest.trim();
        Serial.print("EVT ");
        Serial.println(rest);
        return;
    }

    // 3. จำลองการแตะการ์ดจาก Serial Monitor: "TEST_CARD"
    if (line.equalsIgnoreCase("TEST_CARD")) {
        Serial.println("EVT /card 1_DEADBEEF");
        return;
    }

    BoardFramePacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.pktType = PKT_TYPE_FRAME;
    pkt.frameId = ++localFrameCounter;

    if (line.equalsIgnoreCase("CLEAR")) {
        sendFrameEspNow(pkt);
        return;
    }

    if (line.equalsIgnoreCase("CORNER")) {
        // Cell 0 (0,0): ตำแหน่งหมากปัจจุบัน -> สีฟ้าสว่าง (40, 120, 255)
        pkt.rgb[0][0] = 40;  pkt.rgb[0][1] = 120; pkt.rgb[0][2] = 255;
        // ช่องเดินได้ (ระยะ 1-2 ช่อง) -> สีเขียว (40, 220, 90)
        int moveCells[] = {1, 4, 2, 5, 8};
        for (int c : moveCells) {
            pkt.rgb[c][0] = 40; pkt.rgb[c][1] = 220; pkt.rgb[c][2] = 90;
        }
        // ช่องระยะตี (ระยะ 3 ช่อง) -> สีแดงสด (255, 40, 40)
        int atkCells[] = {3, 6, 9, 12};
        for (int c : atkCells) {
            pkt.rgb[c][0] = 255; pkt.rgb[c][1] = 40; pkt.rgb[c][2] = 40;
        }
        // ช่องศัตรูยืนอยู่ที่มุมตรงข้าม (3,3)=Cell 15 -> สีแดงอ่อน
        pkt.rgb[15][0] = 255; pkt.rgb[15][1] = 75; pkt.rgb[15][2] = 75;

        sendFrameEspNow(pkt);
        return;
    }

    if (line.equalsIgnoreCase("TEST")) {
        pkt.pktType = PKT_TYPE_TEST;
        sendFrameEspNow(pkt);
        return;
    }

    if (line.startsWith("FILL ") || line.startsWith("fill ")) {
        int r = 0, g = 0, b = 0;
        if (sscanf(line.c_str() + 5, "%d %d %d", &r, &g, &b) == 3) {
            for (int i = 0; i < 16; i++) {
                pkt.rgb[i][0] = (uint8_t)constrain(r, 0, 255);
                pkt.rgb[i][1] = (uint8_t)constrain(g, 0, 255);
                pkt.rgb[i][2] = (uint8_t)constrain(b, 0, 255);
            }
            sendFrameEspNow(pkt);
        }
        return;
    }

    // รูปแบบ: FRAME <frameId> <r0,g0,b0;r1,g1,b1;...;r15,g15,b15>
    if (line.startsWith("FRAME ")) {
        int firstSpace = line.indexOf(' ', 6);
        if (firstSpace > 0) {
            int fid = line.substring(6, firstSpace).toInt();
            pkt.frameId = (uint8_t)(fid & 0xFF);
            String payload = line.substring(firstSpace + 1);

            int cellIdx = 0;
            int startPos = 0;
            while (cellIdx < 16 && startPos < (int)payload.length()) {
                int semi = payload.indexOf(';', startPos);
                if (semi == -1) semi = payload.length();
                String token = payload.substring(startPos, semi);
                int r = 0, g = 0, b = 0;
                if (sscanf(token.c_str(), "%d,%d,%d", &r, &g, &b) == 3) {
                    pkt.rgb[cellIdx][0] = (uint8_t)constrain(r, 0, 255);
                    pkt.rgb[cellIdx][1] = (uint8_t)constrain(g, 0, 255);
                    pkt.rgb[cellIdx][2] = (uint8_t)constrain(b, 0, 255);
                }
                cellIdx++;
                startPos = semi + 1;
            }
            sendFrameEspNow(pkt);
        }
        return;
    }
}

// ============================================================================
//                                SETUP
// ============================================================================
void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(200);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    // ล็อค Wi-Fi Channel ให้ตรงกับบอร์ดแม่
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    if (esp_now_init() != ESP_OK) {
        Serial.println("[FATAL] ESP-NOW Init Failed!");
        return;
    }

    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataRecv);

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, TARGET_MAC, 6);
    peerInfo.channel = ESPNOW_CHANNEL;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("[ERR] Failed to add ESP-NOW peer");
    }

    Serial.println("======================================================");
    Serial.println(" ESP32 Bi-Directional USB-to-ESP-NOW Bridge Ready");
    Serial.print  (" Bridge MAC : ");
    Serial.println(WiFi.macAddress());
    Serial.println(" Commands   : MAC, CLEAR, CORNER, TEST, TEST_CARD, FILL r g b");
    Serial.println(" Game Cmds  : CMD <topic> <payload> | EVT <topic> <payload>");
    Serial.println("======================================================");
}

// ============================================================================
//                                LOOP
// ============================================================================
void loop() {
    while (Serial.available() > 0) {
        uint8_t b = (uint8_t)Serial.read();
        unsigned long now = millis();

        // รีเซ็ต Binary Buffer หากข้อมูลขาดช่วงเกิน 50 ms
        if (rxIndex > 0 && (now - lastByteTime > 50)) {
            rxIndex = 0;
        }
        lastByteTime = now;

        // ตรวจจับ Binary Frame ที่ขึ้นต้นด้วย 0xAA 0x55
        if (rxIndex == 0) {
            if (b == HEADER_BYTE_1) {
                rxBuffer[rxIndex++] = b;
            } else {
                if (b == '\n' || b == '\r') {
                    if (cmdLine.length() > 0) {
                        handleTextCommand(cmdLine);
                        cmdLine = "";
                    }
                } else if (cmdLine.length() < 512) {
                    cmdLine += (char)b;
                }
            }
        } else if (rxIndex == 1) {
            if (b == HEADER_BYTE_2) {
                rxBuffer[rxIndex++] = b;
            } else {
                rxIndex = 0;
            }
        } else {
            rxBuffer[rxIndex++] = b;
            if (rxIndex == PACKET_SIZE) {
                BoardFramePacket pkt;
                memcpy(&pkt, rxBuffer, PACKET_SIZE);
                rxIndex = 0;

                uint8_t expectedCrc = calcCRC8(&pkt.pktType, 50);
                if (pkt.crc == expectedCrc || pkt.crc == 0xFF) {
                    esp_now_send(TARGET_MAC, (const uint8_t*)&pkt, sizeof(BoardFramePacket));
                    if (pkt.pktType == PKT_TYPE_TILE_CHUNK) {
                        Serial.printf("[CHUNK] Forwarded tileId=%u via ESP-NOW\n", pkt.frameId);
                    } else if (pkt.pktType == PKT_TYPE_COMMIT_ALL) {
                        Serial.printf("[COMMIT] Forwarded commit frameId=%u via ESP-NOW\n", pkt.frameId);
                    }
                } else {
                    Serial.printf("[CRC_ERR] recv=0x%02X expected=0x%02X\n", pkt.crc, expectedCrc);
                }
            }
        }
    }
}
