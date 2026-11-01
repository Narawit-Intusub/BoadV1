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
├── esp32_rfid_addon/
│   └── esp32_rfid_addon.ino          # [4] โมดูลอ่านการ์ด RFID Addon (ESP32 + RC522)
└── README.md
```

---

## 1. `esp32_usb_bridge` (บอร์ดตัวส่งและรับสัญญาณ USB Bridge)
* **ฮาร์ดแวร์:** ESP32 Wemos D1 Mini เสียบสาย USB เข้า Laptop / PC (Unity)
* **หน้าที่ (สื่อสารแบบ 2 ทิศทาง):**
  * **ขาออก (Unity $\rightarrow$ ฮาร์ดแวร์):**
    * รับเฟรมสีตาราง $4\times 4$ แบบ Binary (53 ไบต์) ส่งต่อผ่าน ESP-NOW ไปยังบอร์ดแม่
    * รับคำสั่งเกมข้อความ เช่น `CMD /team Dummy1_Red` หรือ `CMD /led Dummy1_Turn` แล้วแพ็กเป็น `BoardEventPacket` (53 ไบต์) ส่งออกทาง ESP-NOW
  * **ขาเข้า (ฮาร์ดแวร์ $\rightarrow$ Unity):**
    * รับแพ็กเก็ตอีเวนต์จากกระดานหรือลูกเต๋า ส่งต่อเข้า Serial: `EVT <topic> <payload>` (เช่น `EVT /rfid Dummy1_A1`, `EVT /dice 6`)
    * รับแพ็กเก็ตแตะการ์ดจากแท่นอ่าน RFID ส่งต่อเข้า Serial: `EVT /card <addonId>_<UID>` (เช่น `EVT /card 1_DEADBEEF`)
* **คำสั่งทดสอบผ่าน Serial Monitor:**
  * `MAC` / `STATUS` : ตรวจสอบสถานะและจำนวนแพ็กเก็ต TX/RX
  * `CLEAR` / `FILL r g b` / `CORNER` / `TEST` : ทดสอบแสดงผลสีบนกระดาน
  * `TEST_CARD` : จำลองการแตะการ์ดเข้า Unity (`EVT /card 1_DEADBEEF`)
  * `CMD <topic> <payload>` : ส่งคำสั่งเข้าสู่ระบบ ESP-NOW
  * `EVT <topic> <payload>` : จำลองอีเวนต์เข้า Unity

---

## 2. `esp32_master_gateway` (บอร์ดแม่ Master Gateway)
* **ฮาร์ดแวร์:** ESP32 Wemos D1 Mini บนบอร์ดแม่ (Master Board)
* **หน้าที่:**
  * รับแพ็กเก็ตไร้สาย ESP-NOW จากตัวส่ง USB Bridge (ทั้งเฟรมสีและคำสั่งอีเวนต์)
  * ปรับความสว่างสูงสุดได้ถึง **MAX_CHANNEL_VAL = 120/255**
  * มีสวิตช์เลือกโหมดการขับไฟ **`USE_ATTINY_UART_DRIVER`**:
    * `true` : **โหมดกระดานจริง** ส่งข้อมูลผ่าน UART (`Serial2` TX=GPIO17, RX=GPIO16) ไปให้ ATtiny1604 ขับไฟ 64 ดวง (ESP32 ไม่โดนบล็อกเวลา CPU)
    * `false` : **โหมดเทสเดี่ยว** ให้ ESP32 ขับไฟ WS2812B 64 ดวงตรงผ่านขา **GPIO 18** โดยไม่ต้องต่อ ATtiny
  * ส่งอีเวนต์ของเกมกลับไปยัง USB Bridge ผ่าน ESP-NOW (และพิมพ์ออกทาง USB Serial กรณีต่อสาย USB ตรงเข้าบอร์ดแม่) ด้วยฟังก์ชัน `sendEventToUnity()`

---

## 3. `attiny1604_tile_controller` (ไมโครคอนโทรลเลอร์ประจำแผ่น)
* **ฮาร์ดแวร์:** Microchip ATtiny1604-SS (SOIC-14) บนบอร์ด Master และ Slave
* **หน้าที่:**
  * รับข้อมูลแพ็กเก็ตสีผ่านพอร์ต UART (115200 bps) ที่ขา **PB3 (RX)** และ **PB2 (TX)**
  * เก็บข้อมูลใน **Back Buffer** และสลับไปยัง **Active Buffer** เมื่อได้รับคำสั่ง `PKT_FRAME_COMMIT` (Double Buffering)
  * ย่อสัดส่วนความสว่างไม่ให้เกิน `50/255` โดยรักษาสัดส่วนแม่สี R:G:B เดิมไว้
  * ขยายข้อมูล 16 ช่อง เป็นหลอดไฟ WS2812B จำนวน 64 ดวง (ช่องละ 4 ดวง) ยิงสัญญาณพัลส์ 800 kHz ออกทาง **ขา PA1 (Pin 11)**
  * มีระบบคัดกรองแพ็กเก็ตซ้ำด้วย `lastSeqNumber` (Deduplication) และตรวจ Checksum CRC-8
* **การตั้งค่าใน Arduino IDE:**
  * Board: `megaTinyCore` $\rightarrow$ `ATtiny1604`
  * Clock: `16 MHz internal` หรือ `20 MHz internal`
  * Programmer: `SerialUPDI`

---

## 4. `esp32_rfid_addon` (แท่นอ่านการ์ดเกม RFID Addon)
* **ฮาร์ดแวร์:** ESP32 Wemos D1 Mini + โมดูล RC522 (MFRC522) ต่อผ่าน SPI + ขั้วแม่เหล็ก 6-Pin รับไฟ 12V/5V
* **ขอบเขตการทำงาน:**
  * ทำหน้าที่เป็น **แท่นอ่านการ์ด (Card Reader)** สำหรับการ์ดแอคชั่น / การ์ดไอเทมของผู้เล่นเท่านั้น (ไม่ใช่ตัวตรวจจับหมากตัวละครบนกระดาน)
* **การต่อขาตามวงจร Schematic (`hardware/rfid_addon/rfid_addon.kicad_sch`):**
  * `RFID_SS` (SDA/CS) $\rightarrow$ **GPIO 5**
  * `RFID_RST` (Reset) $\rightarrow$ **GPIO 4**
  * `RFID_SCK` (Clock) $\rightarrow$ **GPIO 18**
  * `RFID_MISO` (Data In) $\rightarrow$ **GPIO 19**
  * `RFID_MOSI` (Data Out) $\rightarrow$ **GPIO 23**
  * `Status LED` $\rightarrow$ **GPIO 2** (กะพริบแจ้งสถานะเมื่ออ่านการ์ดสำเร็จ)
* **หน้าที่:**
  * สแกนการแตะการ์ดเกม (Card Tap) 13.56 MHz (Mifare / Ultralight / NTAG)
  * ส่งข้อมูลรหัสการ์ด 1 ครั้งต่อการแตะ พร้อมระบบ Debounce ป้องกันการอ่านซ้ำขณะวางการ์ดแช่ไว้
  * แพ็กข้อมูลรหัส UID พร้อม CRC-8 และ Addon ID ยิงผ่าน **ESP-NOW (Channel 1)**
  * เมื่อ USB Bridge ได้รับจะแปลงเป็นฟอร์แมต `EVT /card <addonId>_<UID>` ส่งเข้า Unity ทันที
* **ไลบรารีที่ต้องติดตั้งใน Arduino IDE:**
  * `MFRC522` by GithubCommunity (miguelbalboa)

---

## 🔄 ลำดับการไหลของข้อมูลทั้งระบบ (System Pipeline)

```text
[ Unity (PC) ]
      ▲                                         │
      │ (Serial USB: EVT <topic> <payload>)     │ (Serial USB: Binary Frame & CMD)
      │                                         ▼
┌─────┴─────────────────────────────────────────┴─────┐
│                 esp32_usb_bridge                    │
└──────▲──────────────────────▲─────────────────┬─────┘
       │                      │ (ESP-NOW Event) │
       │ (ESP-NOW CardPacket) │                 ▼ (ESP-NOW Frame & CMD)
       │                      │ ┌─────────────────────────────┐
       │                      └─┤    esp32_master_gateway     │
       │                        └──────────────┬──────────────┘
       │                                       │ (UART TX/RX Level Shifter)
       │                                       ▼
       │                        ┌─────────────────────────────┐
       │                        │ attiny1604_tile_controller  │
       │                        └──────────────┬──────────────┘
       │                                       │ (800 kHz PA1)
       │                                       ▼
       │                        ┌─────────────────────────────┐
       │                        │   WS2812B Matrix 64 ดวง     │
       │                        └─────────────────────────────┘
       │
┌──────┴──────────────────────┐
│       esp32_rfid_addon      │ (แท่นอ่านการ์ด RC522)
└─────────────────────────────┘
```
