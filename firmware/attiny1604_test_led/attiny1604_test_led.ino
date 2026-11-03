/*
 * ============================================================================
 * attiny1604_test_led.ino
 * โค้ดทดสอบฮาร์ดแวร์หลอดไฟ WS2812B (64 ดวง) บนบอร์ด ATtiny1604
 * โครงการ Modular Smart Board Game V1 (ใช้ได้ทั้งบอร์ด Master และบอร์ด Slave)
 * ============================================================================
 * ฮาร์ดแวร์:
 *   - MCU: Microchip ATtiny1604 (SOIC-14)
 *   - ขาขับหลอดไฟ: PA1 (Pin 11) ต่อผ่านตัวต้านทาน 470 โอห์ม (R_LED1)
 *   - หลอดไฟ: WS2812B (16 ช่อง x ช่องละ 4 ดวง = รวม 64 ดวง)
 * 
 * รูปแบบการทดสอบ (วนลูปอัตโนมัติ):
 *   1. Dot Chase : ไฟวิ่งทีละดวง (1 ถึง 64) ตรวจสอบว่าสัญญาณวิ่งผ่านครบทุกดวง
 *   2. Cell Chase: ไฟติดวิ่งทีละช่อง 4x4 (1 ถึง 16 ช่อง, ช่องละ 4 ดวง)
 *   3. Color Wipe: วิ่งกวาดสี แดง -> เขียว -> น้ำเงิน -> ขาวอ่อนๆ
 * ============================================================================
 */

#include <Arduino.h>
#include <tinyNeoPixel_Static.h>

#define LED_PIN         PIN_PA1   // ขา PA1 (Pin 11)
#define NUM_LEDS        64        // 64 ดวง (16 ช่อง x 4 ดวง)
#define BRIGHTNESS      50        // ความสว่างระดับปลอดภัย (50/255) ป้องกันความร้อนและกินไฟน้อย

// จองบัฟเฟอร์แบบ Static 192 ไบต์ ปลอดภัยบน RAM 1KB
uint8_t pixelBuffer[NUM_LEDS * 3];

tinyNeoPixel strip = tinyNeoPixel(NUM_LEDS, LED_PIN, NEO_GRB, pixelBuffer);

// ฟังก์ชันเซ็ตสีดวงเดียว
void setPixelRGB(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
    if (index >= NUM_LEDS) return;
    strip.setPixelColor(index, r, g, b);
}

// ฟังก์ชันเซ็ตสีเป็นช่อง (ช่องละ 4 ดวง: 0..15)
void setCellRGB(uint8_t cell, uint8_t r, uint8_t g, uint8_t b) {
    if (cell >= 16) return;
    for (uint8_t i = 0; i < 4; i++) {
        strip.setPixelColor(cell * 4 + i, r, g, b);
    }
}

void setup() {
    pinMode(LED_PIN, OUTPUT);
    strip.begin();
    strip.clear();
    strip.show();
    delay(500);
}

void loop() {
    // -------------------------------------------------------------
    // แบบที่ 1: ไฟวิ่งทีละดวง (Dot Chase) วิ่งวนเช็คครบ 64 ดวง
    // สีฟ้าอมเขียว (Cyan) วิ่งไล่ตามด้วยหางจางๆ
    // -------------------------------------------------------------
    for (uint8_t i = 0; i < NUM_LEDS; i++) {
        strip.clear();
        setPixelRGB(i, 0, BRIGHTNESS, BRIGHTNESS); // ดวงหลัก
        if (i > 0) setPixelRGB(i - 1, 0, BRIGHTNESS / 4, BRIGHTNESS / 4); // หางจาง
        strip.show();
        delay(35);
    }
    strip.clear();
    strip.show();
    delay(200);

    // -------------------------------------------------------------
    // แบบที่ 2: ไฟวิ่งทีละช่อง 4x4 (Cell Chase) 16 ช่อง
    // ช่องละ 4 ดวง วิ่งสลับสี แดง -> เขียว -> น้ำเงิน -> ส้ม
    // -------------------------------------------------------------
    for (uint8_t cell = 0; cell < 16; cell++) {
        strip.clear();
        uint8_t r = 0, g = 0, b = 0;
        switch (cell % 4) {
            case 0: r = BRIGHTNESS; break;                  // แดง
            case 1: g = BRIGHTNESS; break;                  // เขียว
            case 2: b = BRIGHTNESS; break;                  // น้ำเงิน
            case 3: r = BRIGHTNESS; g = BRIGHTNESS / 2; break; // ส้ม
        }
        setCellRGB(cell, r, g, b);
        strip.show();
        delay(100);
    }
    strip.clear();
    strip.show();
    delay(200);

    // -------------------------------------------------------------
    // แบบที่ 3: กวาดสีทีละดวงจนเต็มบอร์ด (Color Fill Wipe)
    // -------------------------------------------------------------
    // 3.1 กวาดสีแดง
    for (uint8_t i = 0; i < NUM_LEDS; i++) {
        setPixelRGB(i, BRIGHTNESS, 0, 0);
        strip.show();
        delay(15);
    }
    delay(250);

    // 3.2 กวาดสีเขียว
    for (uint8_t i = 0; i < NUM_LEDS; i++) {
        setPixelRGB(i, 0, BRIGHTNESS, 0);
        strip.show();
        delay(15);
    }
    delay(250);

    // 3.3 กวาดสีน้ำเงิน
    for (uint8_t i = 0; i < NUM_LEDS; i++) {
        setPixelRGB(i, 0, 0, BRIGHTNESS);
        strip.show();
        delay(15);
    }
    delay(250);

    // -------------------------------------------------------------
    // ดับไฟทั้งหมดแล้วเริ่มรอบใหม่
    // -------------------------------------------------------------
    strip.clear();
    strip.show();
    delay(600);
}

