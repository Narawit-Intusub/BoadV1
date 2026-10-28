# Modular Smart Board Game V1 — Firmware Suite

โฟลเดอร์นี้รวบรวมซอร์สโค้ดเฟิร์มแวร์ทั้งหมดของโครงการ **Modular Smart Board Game V1** สำหรับนำไปคอมไพล์และแฟลชลงฮาร์ดแวร์จริงได้ทันที

---

## 📁 โครงสร้างโฟลเดอร์

```text
firmware/
├── esp32_usb_bridge/
│   └── esp32_usb_bridge.ino          # [1] ตัวส่งสัญญาณ USB Bridge (เสียบกับคอมพิวเตอร์ Unity)
├── esp32_master_gateway/
│   └── esp32_master_gateway.ino      # [2] ตัวรับ ESP-NOW บนบอร์ดแม่ (Master Gateway)
├── attiny1604_tile_controller/
│   └── attiny1604_tile_controller.ino # [3] ตัวควบคุมหลอดไฟประจำแผ่น (ATtiny1604)
└── README.md
```

---

## 1. `esp32_usb_bridge` (บอร์ดตัวส่ง USB)
* **ฮาร์ดแวร์:** ESP32 Wemos D1 Mini เสียบสาย USB เข้า Laptop / PC
* **หน้าที่:**
  * รับคำสั่งและเฟรมสีตาราง $4\times 4$ จากเกมบน Unity หรือจากคำสั่งข้อความ (Serial Monitor 115200 bps)
  * ส่งข้อมูลออกทางไร้สายด้วยโปรโตคอล **ESP-NOW (Channel 1)** ไปยังบอร์ดแม่
* **คำสั่งทดสอบผ่าน Serial Monitor (พิมพ์สั่งได้ทันที):**
  * `MAC` : ตรวจสอบ MAC Address และสถานะส่งข้อมูล
  * `CLEAR` : สั่งดับไฟทั้ง 16 ช่อง
  * `CORNER` : จำลองสถานการณ์หมากอยู่มุม (เดินได้=เขียว, ตีได้=แดง)
  * `TEST` : รันไฟทดสอบไล่ช่อง 0..15
  * `FILL r g b` : สั่งติดสีเดียวกันทั้งกระดาน เช่น `FILL 40 220 90`

---

## 2. `esp32_master_gateway` (บอร์ดแม่ Master)
* **ฮาร์ดแวร์:** ESP32 Wemos D1 Mini ติดตั้งบนบอร์ดแม่ (Master Board)
* **หน้าที่:**
  * รับแพ็กเก็ตไร้สาย ESP-NOW จากตัวส่ง USB Bridge
  * แปลงพิกัด และส่งต่อข้อมูลไปยัง ATtiny1604 ผ่านพอร์ต UART (ผ่าน Logic Level Shifter 3.3V $\leftrightarrow$ 5V)
* **โหมดการทำงาน (ปรับในโค้ดบรรทัดที่ 26):**
  * `#define MODE_FORWARD_TO_ATTINY true` : **โหมดมาตรฐาน** ส่งข้อมูลผ่าน UART ไปให้ ATtiny1604 ขับไฟ
  * `#define MODE_FORWARD_TO_ATTINY false` : **โหมดทดสอบเดี่ยว** ให้ ESP32 ขับหลอดไฟเองผ่านขา GPIO 18 (สำหรับเทสเร็วตอนยังไม่ได้ต่อ ATtiny)

---

## 3. `attiny1604_tile_controller` (ไมโครคอนโทรลเลอร์ประจำแผ่น)
* **ฮาร์ดแวร์:** Microchip ATtiny1604-SS (SOIC-14) บนบอร์ด Master และ Slave
* **หน้าที่:**
  * รับข้อมูลแพ็กเก็ตสี 48 ไบต์ผ่านสาย UART/ขั้วแม่เหล็ก
  * เก็บข้อมูลใน **Back Buffer** และสลับไปยัง **Active Buffer** เมื่อได้รับคำสั่ง `FRAME_COMMIT` (Double Buffering)
  * ขยายข้อมูล 16 ช่อง เป็นหลอดไฟ WS2812B จำนวน 64 ดวง (ช่องละ 4 ดวง)
  * ยิงสัญญาณพัลส์ความถี่ 800 kHz ออกทาง **ขา PA1 (Pin 11)** ผ่านตัวต้านทาน $470\,\Omega$
  * Clamp ความสว่างทุกสีไม่เกิน `50/255` ป้องกันไฟกระชากและความร้อน
* **การตั้งค่าใน Arduino IDE:**
  * Board: `megaTinyCore` $\rightarrow$ `ATtiny1604`
  * Clock: `16 MHz internal` หรือ `20 MHz internal`
  * Programmer: `SerialUPDI`

---

## 🔄 ลำดับการไหลของข้อมูลทั้งระบบ (System Pipeline)

```text
[ Unity (PC) ]
      │ (Serial USB 115200 bps)
      ▼
[ esp32_usb_bridge ]
      │ (Wireless ESP-NOW 2.4 GHz, Latency < 10 ms)
      ▼
[ esp32_master_gateway ]
      │ (UART TX/RX ผ่าน Level Shifter 3.3V <-> 5V)
      ▼
[ attiny1604_tile_controller ] ──(D0/D1 ขั้วแม่เหล็ก)──> [ Slave Tiles 1..3 ]
      │ (Signal 800 kHz ผ่านขา PA1)
      ▼
[ WS2812B Matrix 64 ดวง ]
```
