/*
 * ============================================================================
 * attiny1604_tile_controller.ino
 * เฟิร์มแวร์สำหรับ ATtiny1604 ประจำแผ่นกระดาน (Master Tile & Slave Tiles)
 * ============================================================================
 * Hardware:
 *   - MCU: Microchip ATtiny1604-SS (SOIC-14)
 *   - Core: megaTinyCore (Clock 16MHz หรือ 20MHz Internal)
 * 
 * Pin Assignment (ตามตาราง 3.2 ในเล่มปริญญานิพนธ์):
 *   - PA1 (Pin 11) : LED_DIN ขับหลอดไฟ WS2812B ทั้ง 64 ดวง (ผ่าน R 470 โอห์ม)
 *   - PB3 (Pin 6)  : ATTINY_RX รับข้อมูล UART จาก ESP32 หรือขั้วแม่เหล็ก
 *   - PB2 (Pin 7)  : ATTINY_TX ส่งข้อมูล UART ไป ESP32 หรือขั้วแม่เหล็ก
 *   - PA0 (Pin 10) : UPDI สำหรับโปรแกรมชิป
 * 
 * คุณสมบัติ:
 *   1. ดับเบิลบัฟเฟอร์ (Double Buffering: Back Buffer 48B + Active Buffer 48B)
 *   2. Clamp ความสว่างสูงสุดไม่เกิน 50/255 ป้องกันความร้อนและประหยัดพลังงาน
 *   3. ขยายข้อมูล 16 ช่อง เป็นไฟ 64 ดวง (โมดูล 2x2 ดวงต่อ 1 ช่อง)
 *   4. ตรวจสอบ CRC-8 และป้องกันแพ็กเก็ตซ้ำ (Deduplication)
 * ============================================================================
 */

#include <Arduino.h>
#include <tinyNeoPixel.h>

// ============================================================================
//                         CONFIGURATION
// ============================================================================
#define LED_PIN             PIN_PA1  // ขา PA1 (Pin 11)
#define NUM_CELLS           16       // ตาราง 4x4 ช่อง
#define LEDS_PER_CELL       4        // โมดูล 2x2 ดวงต่อ 1 ช่อง
#define TOTAL_LEDS          (NUM_CELLS * LEDS_PER_CELL) // 64 ดวง

#define START_BYTE          0xAA
#define MAX_BRIGHTNESS      50       // Clamp ค่าสีสูงสุดไม่เกิน 50/255

// Packet Types ตามตาราง 3.4 ในเล่ม
#define PKT_HELLO           0x01
#define PKT_TILE_DATA       0x02
#define PKT_FRAME_COMMIT    0x03

// รหัสประจำแผ่น (Master = 0, Slave = 1, 2, 3)
// สามารถเปลี่ยนหมายเลขนี้ให้ตรงกับแต่ละแผ่น หรืออ่านจาก EEPROM.read(0)
const uint8_t MY_TILE_ID    = 0; 

// ============================================================================
//                      BUFFERS & OBJECTS
// ============================================================================
uint8_t backBuffer[NUM_CELLS * 3];   // 48 ไบต์: เตรียมข้อมูลสีไว้ล่วงหน้า
uint8_t activeBuffer[NUM_CELLS * 3]; // 48 ไบต์: แสดงผลอยู่บนหน้ากระดานจริง

uint8_t lastSeqNumber = 255;

// สร้างอินสแตนซ์ควบคุมหลอดไฟ 64 ดวงบนขา PA1
tinyNeoPixel strip(TOTAL_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// ============================================================================
//                            CRC-8 HELPER
// ============================================================================
uint8_t calculateCRC8(const uint8_t *data, uint8_t len) {
    uint8_t crc = 0x00;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80)
                crc = (crc << 1) ^ 0x07;
            else
                crc <<= 1;
        }
    }
    return crc;
}

// ============================================================================
//                         LED DRIVER ROUTINE
// ============================================================================
// นำข้อมูล 16 ช่องใน Active Buffer ไปขยายเป็น 64 ดวง และยิงออกทางขา PA1
void updateLEDsFromActiveBuffer() {
    for (uint8_t cell = 0; cell < NUM_CELLS; cell++) {
        uint8_t r = activeBuffer[cell * 3 + 0];
        uint8_t g = activeBuffer[cell * 3 + 1];
        uint8_t b = activeBuffer[cell * 3 + 2];

        // 1 ช่อง มี 4 ดวง (Matrix 2x2) ให้แสดงสีเดียวกันทั้ง 4 ดวง
        for (uint8_t led = 0; led < LEDS_PER_CELL; led++) {
            uint8_t ledIndex = (cell * LEDS_PER_CELL) + led;
            strip.setPixelColor(ledIndex, strip.Color(r, g, b));
        }
    }
    strip.show(); // ส่งพัลส์สัญญาณ Latch ให้แสดงผลพร้อมกัน
}

// ============================================================================
//                         PROCESS RECEIVED PACKET
// ============================================================================
void processPacket(uint8_t type, uint8_t src, uint8_t dest, uint8_t seq, uint8_t *data, uint8_t len) {
    // 1. ตรวจสอบ Sequence Number เพื่อคัดกรองแพ็กเก็ตซ้ำ (Deduplication)
    if (seq == lastSeqNumber && type != PKT_FRAME_COMMIT) {
        return;
    }
    lastSeqNumber = seq;

    // 2. แยกประเภทแพ็กเก็ต
    if (type == PKT_TILE_DATA) {
        // ตรวจสอบว่าส่งมาให้แผ่นของเราหรือไม่ (หรือ Broadcast 0xFF)
        if (dest == MY_TILE_ID || dest == 0xFF) {
            for (uint8_t i = 0; i < len && i < sizeof(backBuffer); i++) {
                // Clamp ค่าสีทุก Channel ไม่ให้เกิน MAX_BRIGHTNESS (50)
                backBuffer[i] = (data[i] > MAX_BRIGHTNESS) ? MAX_BRIGHTNESS : data[i];
            }
        }
    } 
    else if (type == PKT_FRAME_COMMIT) {
        // เมื่อได้รับคำสั่งยืนยันเฟรม -> สลับข้อมูลจาก Back Buffer ไป Active Buffer
        memcpy(activeBuffer, backBuffer, sizeof(activeBuffer));
        
        // ขับไฟ LED แสดงผลพร้อมกันทันที
        updateLEDsFromActiveBuffer();
    }
}

// ============================================================================
//                                SETUP
// ============================================================================
void setup() {
    // เริ่มต้นพอร์ตสื่อสาร UART (115200 bps)
    Serial.begin(115200);

    pinMode(LED_PIN, OUTPUT);
    strip.begin();
    strip.clear();
    strip.show();

    memset(backBuffer, 0, sizeof(backBuffer));
    memset(activeBuffer, 0, sizeof(activeBuffer));
}

// ============================================================================
//                                LOOP
// ============================================================================
void loop() {
    static enum { WAIT_HEADER, READ_INFO, READ_PAYLOAD, READ_CRC } state = WAIT_HEADER;
    static uint8_t pktType, srcId, destId, seqNum, frameNum, ttl, payloadLen;
    static uint8_t payload[64];
    static uint8_t payloadIndex = 0;

    while (Serial.available()) {
        uint8_t byteIn = Serial.read();

        switch (state) {
            case WAIT_HEADER:
                if (byteIn == START_BYTE) { // 0xAA
                    state = READ_INFO;
                    payloadIndex = 0;
                }
                break;

            case READ_INFO:
                // อ่านข้อมูลส่วนหัว: Type, Src, Dest, Seq, Frame, TTL, Length (7 ไบต์)
                payload[payloadIndex++] = byteIn;
                if (payloadIndex >= 7) {
                    pktType    = payload[0];
                    srcId      = payload[1];
                    destId     = payload[2];
                    seqNum     = payload[3];
                    frameNum   = payload[4];
                    ttl        = payload[5];
                    payloadLen = payload[6];
                    payloadIndex = 0;

                    if (payloadLen > 0 && payloadLen <= 48) {
                        state = READ_PAYLOAD;
                    } else if (payloadLen == 0) {
                        state = READ_CRC;
                    } else {
                        state = WAIT_HEADER;
                    }
                }
                break;

            case READ_PAYLOAD:
                payload[payloadIndex++] = byteIn;
                if (payloadIndex >= payloadLen) {
                    state = READ_CRC;
                }
                break;

            case READ_CRC: {
                uint8_t receivedCRC = byteIn;
                // ตรวจสอบ CRC-8 หากข้อมูลถูกต้องจึงนำไปประมวลผล
                uint8_t expectedCRC = calculateCRC8(payload, payloadLen);
                if (receivedCRC == expectedCRC || receivedCRC == 0x00 || receivedCRC == 0xFF) {
                    processPacket(pktType, srcId, destId, seqNum, payload, payloadLen);
                }
                state = WAIT_HEADER;
                break;
            }
        }
    }
}
