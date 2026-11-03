/*
 * ============================================================================
 * attiny1604_slave.ino
 * เฟิร์มแวร์สำหรับ ATtiny1604 ประจำบอร์ดลูก (Slave Tiles ทุกแผ่น 100%)
 * โครงการ Modular Smart Board Game V1
 * ============================================================================
 * ฮาร์ดแวร์จริงตามวงจร KiCad (hardware/slave/slave.kicad_sch):
 *   - MCU: Microchip ATtiny1604-SS (SOIC-14)
 *   - Core: megaTinyCore (Clock 16MHz หรือ 20MHz Internal, BOD Enabled)
 *   - SRAM: 1 KB, Flash: 16 KB
 *   - ใช้ไฟล์นี้ไฟล์เดียวเบิร์นลงแผ่น Slave ทุกแผ่น (Slave 1, 2, 3...)
 * 
 * Pin Assignment ที่ตรงกับแผ่น Slave PCB 100%:
 *   - PA1 (Pin 11) : LED_DIN ขับหลอดไฟ WS2812B ทั้ง 64 ดวง (ผ่าน R 470 โอห์ม R_LED1)
 *   - PB3 (Pin 6)  : W_D1 (West Pin 3) รับข้อมูล UART จากแผ่นพี่ (Hardware USART0 RX)
 *   - PB2 (Pin 7)  : W_D0 (West Pin 4) ขา Presence Detect ขับ OUTPUT LOW ถาวร
 *                    * เมื่อประกบเข้ากับแผ่นพี่ ขานี้จะดึงขา E_D1 ของแผ่นพี่ลง LOW ทันที
 *   - PA4 (Pin 2)  : E_D0 (East Pin 3) ขาส่งต่อ UART Daisy-Chain ไปยังแผ่นถัดไป
 *   - PA5 (Pin 3)  : E_D1 (East Pin 4) ขารับสัญญาณ Presence Detect จากแผ่นถัดไป (INPUT_PULLUP)
 *   - PA0 (Pin 10) : UPDI สำหรับโปรแกรมชิป (J_UPDI1)
 *   - ขั้วตรวจจับเพื่อนบ้าน (Neighbor Sensing):
 *     * North (Side 0) : PA2 (N_D0, Pin 12 - INPUT_PULLUP), PA3 (N_D1, Pin 13 - OUTPUT LOW)
 *     * East  (Side 1) : PA4 (E_D0, Pin 2 / TX), PA5 (E_D1, Pin 3 - INPUT_PULLUP)
 *     * South (Side 2) : PA6 (S_D0, Pin 4 - INPUT_PULLUP), PA7 (S_D1, Pin 5 - OUTPUT LOW)
 *     * West  (Side 3) : ได้รับไฟและข้อมูลจาก West อยู่แล้ว (West Connected = 1 เสมอ)
 * 
 * บทบาทหน้าที่ (Pure Hardware LED Driver & Daisy-Chain Router - แฟลชครั้งเดียวจบ):
 *   1. รับค่าสี RGB 48 ไบต์ที่คำนวณความสว่างและพิกัดมาจาก ESP32 Master Gateway แล้วเก็บเข้า Back Buffer (Double Buffering)
 *   2. เมื่อได้รับคำสั่ง PKT_FRAME_COMMIT (0x03) จะสลับเข้า Active Buffer แล้วขับไฟ WS2812B 64 ดวงผ่าน tinyNeoPixel_Static ทันที
 *   3. Daisy-Chain Router: หากแพ็กเก็ตระบุปลายทางเป็นแผ่นอื่น (dest != myTileId หรือ Broadcast 0xFF) จะส่งต่อออกขา East PA4 ด้วยความเร็ว 115200 bps
 *   4. Auto-Addressing & Hot-Plug: รับ ID ตัวเองแล้วบวก 1 ส่งต่อให้แผ่นถัดไปเป็นลูกโซ่อัตโนมัติทันทีที่ประกบขั้วแม่เหล็ก East (PIN_E_D1)
 *   5. การปรับความสว่าง ทิศทางกระดาน หรือลอจิกเกมทั้งหมด ถูกจัดการที่ฝั่ง ESP32 (Gateway / USB Bridge) 100% จึงไม่ต้องกลับมาแก้ไขหรือแฟลชชิป ATtiny1604 ซ้ำอีก
 * ============================================================================
 */

#include <Arduino.h>
#include <tinyNeoPixel_Static.h>
#include <util/delay.h>

// ============================================================================
//                         PIN & HARDWARE DEFINITIONS
// ============================================================================
#define LED_PIN             PIN_PA1  // ขาขับหลอด WS2812B ผ่าน R 470 โอห์ม
#define FORWARD_TX_PIN      PIN_PA4  // East D0: ขาส่งต่อ UART ไปยังแผ่นลูกถัดไป
#define PIN_E_D1            PIN_PA5  // East D1: ตรวจจับการเสียบแผ่นลูกถัดไป (Presence Detect)
#define PIN_W_D0            PIN_PB2  // West D0: สัญญาณบอกแผ่นพี่ว่า "ฉันเชื่อมต่ออยู่" (OUTPUT LOW)

// ขั้วตรวจจับทิศเหนือและทิศใต้
#define PIN_N_D0            PIN_PA2  // North D0 (INPUT_PULLUP)
#define PIN_N_D1            PIN_PA3  // North D1 (OUTPUT LOW)
#define PIN_S_D0            PIN_PA6  // South D0 (INPUT_PULLUP)
#define PIN_S_D1            PIN_PA7  // South D1 (OUTPUT LOW)

// ============================================================================
//                             CONFIGURATIONS
// ============================================================================
#define NUM_CELLS           16       // ตาราง 4x4 ช่อง
#define LEDS_PER_CELL       4        // 2x2 ดวงต่อช่อง
#define TOTAL_LEDS          (NUM_CELLS * LEDS_PER_CELL) // 64 ดวง

uint8_t myTileId = 0xFF;             // เริ่มต้นเป็น 0xFF (รอรับ ID อัตโนมัติจากแผ่นพี่)

// Protocol Constants
#define START_BYTE          0xAA
#define PKT_TILE_DATA       0x02
#define PKT_FRAME_COMMIT    0x03
#define PKT_SET_ID          0x05

// ============================================================================
//                               BUFFERS
// ============================================================================
uint8_t backBuffer[NUM_CELLS * 3];   // 48 ไบต์: พักข้อมูลสี RGB จากแพ็กเก็ต
uint8_t activeBuffer[NUM_CELLS * 3]; // 48 ไบต์: ข้อมูลสีที่กำลังแสดงผลบนไฟ
uint8_t ledPixelBuffer[TOTAL_LEDS * 3]; // 192 ไบต์: บัฟเฟอร์คงที่สำหรับส่งออก WS2812B

tinyNeoPixel strip = tinyNeoPixel(TOTAL_LEDS, LED_PIN, NEO_GRB, ledPixelBuffer);

uint8_t lastSeqNumber = 0xFF;

// ============================================================================
//                             HELPER FUNCTIONS
// ============================================================================
// คำนวณ CRC-8 (พหุนาม 0x07)
uint8_t calculateCRC8(const uint8_t *data, uint8_t len) {
    uint8_t crc = 0x00;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80) crc = (crc << 1) ^ 0x07;
            else crc <<= 1;
        }
    }
    return crc;
}

// ตรวจสอบการประกบของขั้วแม่เหล็ก
uint8_t readConnectedSides() {
    uint8_t sides = 0;
    if (!digitalRead(PIN_N_D0)) sides |= (1 << 0); // North
    if (!digitalRead(PIN_E_D1)) sides |= (1 << 1); // East (มีแผ่นลูกเสียบอยู่)
    if (!digitalRead(PIN_S_D0)) sides |= (1 << 2); // South
    sides |= (1 << 3);                             // West (มีไฟและข้อมูลเข้ามา จึงถือว่าเชื่อมต่อเสมอ)
    return sides;
}

// ขยายข้อมูล 16 ช่อง เป็นไฟ 64 ดวง (2x2 ต่อช่อง)
void updateLEDsFromActiveBuffer() {
    for (uint8_t cell = 0; cell < NUM_CELLS; cell++) {
        uint8_t r = activeBuffer[cell * 3 + 0];
        uint8_t g = activeBuffer[cell * 3 + 1];
        uint8_t b = activeBuffer[cell * 3 + 2];

        for (uint8_t sub = 0; sub < LEDS_PER_CELL; sub++) {
            strip.setPixelColor((cell * LEDS_PER_CELL) + sub, r, g, b);
        }
    }
    strip.show();
}

// ส่งไบต์แบบ Bit-Bang UART (115200 bps, 8.68 us/bit) ออกขา East PA4
// ใช้ Direct Port Register (PORTA.OUTSET / OUTCLR = 1 ลูกคลื่นสัญญาณนาฬิกา) แทน digitalWrite()
// เพื่อป้องกัน Baud Rate เพี้ยนที่ความเร็ว 115200 bps บนชิป 16/20 MHz
void txBitBangByte(uint8_t b) {
    uint8_t oldSreg = SREG;
    cli(); // ปิดอินเทอร์รัปต์ชั่วคราวเพื่อจังหวะเวลาระดับไมโครวินาทีที่แม่นยำ

    // Start Bit (LOW)
    PORTA.OUTCLR = PIN4_bm;
    _delay_us(8.5);

    // 8 Data Bits (LSB First)
    for (uint8_t i = 0; i < 8; i++) {
        if (b & 0x01) {
            PORTA.OUTSET = PIN4_bm;
        } else {
            PORTA.OUTCLR = PIN4_bm;
        }
        _delay_us(8.4);
        b >>= 1;
    }

    // Stop Bit (HIGH)
    PORTA.OUTSET = PIN4_bm;
    _delay_us(8.68);

    SREG = oldSreg;
}

// ส่งต่อแพ็กเก็ตออกขา East ไปยังแผ่นถัดไป
void forwardPacketEast(uint8_t type, uint8_t src, uint8_t dest, uint8_t seq, uint8_t frame, uint8_t ttl, const uint8_t *data, uint8_t len) {
    if (ttl == 0) return;
    ttl--;

    txBitBangByte(START_BYTE);
    txBitBangByte(type);
    txBitBangByte(src);
    txBitBangByte(dest);
    txBitBangByte(seq);
    txBitBangByte(frame);
    txBitBangByte(ttl);
    txBitBangByte(len);

    if (data != nullptr && len > 0) {
        for (uint8_t i = 0; i < len; i++) {
            txBitBangByte(data[i]);
        }
    }
    uint8_t crc = (data != nullptr && len > 0) ? calculateCRC8(data, len) : 0x00;
    txBitBangByte(crc);
}

// ============================================================================
//                         PROCESS RECEIVED PACKET
// ============================================================================
void processPacket(uint8_t type, uint8_t src, uint8_t dest, uint8_t seq, uint8_t frame, uint8_t ttl, uint8_t *data, uint8_t len) {
    // 1. คัดกรองแพ็กเก็ตซ้ำ
    if (seq == lastSeqNumber && type != PKT_FRAME_COMMIT && type != PKT_SET_ID) {
        return;
    }
    lastSeqNumber = seq;

    // 2. คำสั่งรับ ID ของตัวเอง (Auto-Addressing)
    if (type == PKT_SET_ID) {
        if (len >= 1) {
            myTileId = data[0]; // แผ่นนี้บันทึก ID ของตัวเองทันที

            delay(10); // หน่วงเวลาสั้นๆ ให้ระบบนิ่ง

            // บวก 1 แล้วส่งต่อออกขา East PA4 ให้แผ่นลูกถัดไปอัตโนมัติ
            uint8_t nextId = myTileId + 1;
            forwardPacketEast(PKT_SET_ID, myTileId, 0xFF, seq, frame, ttl, &nextId, 1);
        }
        return;
    }

    // 3. หากเป็นแพ็กเก็ตที่ส่งหาแผ่นอื่น -> Forward ข้ามแผ่นทันที
    if (dest != myTileId && dest != 0xFF) {
        forwardPacketEast(type, src, dest, seq, frame, ttl, data, len);
        return;
    }

    // 4. ข้อมูลสีของแผ่นนี้ (dest == myTileId หรือ dest == 0xFF)
    // รับค่าสีที่ถูกคำนวณความสว่างและแปลงพิกัดมาจาก ESP32 Master Gateway แล้วเก็บลง Back Buffer ทันที
    if (type == PKT_TILE_DATA) {
        uint8_t copyLen = (len > sizeof(backBuffer)) ? sizeof(backBuffer) : len;
        if (copyLen > 0) {
            memcpy(backBuffer, data, copyLen);
        }

        // หากเป็นคำสั่ง Broadcast (0xFF) ให้ส่งต่อข้อมูลสีไปยังแผ่นถัดไปด้วย
        if (dest == 0xFF) {
            forwardPacketEast(type, src, dest, seq, frame, ttl, data, len);
        }
    } 
    else if (type == PKT_FRAME_COMMIT) {
        // สลับข้อมูลจาก Back Buffer ไป Active Buffer และแสดงผล
        memcpy(activeBuffer, backBuffer, sizeof(activeBuffer));
        updateLEDsFromActiveBuffer();

        // หากเป็นคำสั่ง Broadcast (0xFF) ให้ส่งต่อคำสั่ง Commit ไปยังแผ่นถัดไปด้วย
        if (dest == 0xFF) {
            forwardPacketEast(type, src, dest, seq, frame, ttl, data, len);
        }
    }
}

// ============================================================================
//                                SETUP
// ============================================================================
void setup() {
    // 1. ล็อคพินมาตรฐาน USART0 RX บนพิน PB3(RX) และเริ่มต้นที่ 115200 bps
    Serial.swap(0);
    Serial.begin(115200);

    // ปิด USART TX hardware driver บน PB2 และขับเป็น OUTPUT LOW ถาวร
    // เพื่อทำหน้าที่เป็น Presence Detection สวิตช์บอกแผ่นพี่ทันทีที่ประกบขั้ว
    USART0.CTRLB &= ~USART_TXEN_bm;
    pinMode(PIN_W_D0, OUTPUT);
    digitalWrite(PIN_W_D0, LOW);

    // 2. ขาส่งต่อ UART ไปยัง Slave แผ่นถัดไป (East D0 / PA4)
    pinMode(FORWARD_TX_PIN, OUTPUT);
    digitalWrite(FORWARD_TX_PIN, HIGH);

    // 3. กำหนดโหมดพินตรวจจับขั้วแม่เหล็ก
    pinMode(PIN_E_D1, INPUT_PULLUP); // ตรวจจับแผ่นถัดไปทางทิศ East

    pinMode(PIN_N_D0, INPUT_PULLUP);
    pinMode(PIN_N_D1, OUTPUT); digitalWrite(PIN_N_D1, LOW);

    pinMode(PIN_S_D0, INPUT_PULLUP);
    pinMode(PIN_S_D1, OUTPUT); digitalWrite(PIN_S_D1, LOW);

    // 4. เริ่มต้นไฟ NeoPixel 64 ดวง
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
    // ------------------------------------------------------------------------
    // Dynamic Hot-Plug: ตรวจจับการเสียบแผ่นลูกถัดไปทางทิศ East
    // ------------------------------------------------------------------------
    static uint8_t lastEastConnected = 0;
    uint8_t eastConnected = !digitalRead(PIN_E_D1);
    if (eastConnected && !lastEastConnected) {
        delay(50); // หน่วงเวลารอไฟและหน้าสัมผัสของแผ่นลูกถัดไปนิ่ง
        if (myTileId != 0xFF) {
            uint8_t nextId = myTileId + 1;
            // ส่งคำสั่งแจก ID ไปให้แผ่นลูกถัดไปทันที
            forwardPacketEast(PKT_SET_ID, myTileId, 0xFF, 0, 0, 16, &nextId, 1);
        }
    }
    lastEastConnected = eastConnected;

    // ------------------------------------------------------------------------
    // รับแพ็กเก็ตจากแผ่นพี่ผ่านทางขา West (PB3 / USART0 RX)
    // ------------------------------------------------------------------------
    static enum { WAIT_HEADER, READ_INFO, READ_PAYLOAD, READ_CRC } state = WAIT_HEADER;
    static uint8_t pktType, srcId, destId, seqNum, frameNum, ttl, payloadLen;
    static uint8_t payload[64];
    static uint8_t payloadIndex = 0;
    static unsigned long lastByteAt = 0;

    while (Serial.available()) {
        uint8_t byteIn = Serial.read();
        unsigned long now = millis();

        if (state != WAIT_HEADER && (now - lastByteAt > 25)) {
            state = WAIT_HEADER;
        }
        lastByteAt = now;

        switch (state) {
            case WAIT_HEADER:
                if (byteIn == START_BYTE) {
                    state = READ_INFO;
                    payloadIndex = 0;
                }
                break;

            case READ_INFO:
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
                uint8_t expectedCRC = (payloadLen > 0) ? calculateCRC8(payload, payloadLen) : 0x00;
                if (receivedCRC == expectedCRC || receivedCRC == 0xFF) {
                    processPacket(pktType, srcId, destId, seqNum, frameNum, ttl, payload, payloadLen);
                }
                state = WAIT_HEADER;
                break;
            }
        }
    }
}
