# Modular Smart Board Game V1 — Firmware Suite

โฟลเดอร์นี้รวบรวมซอร์สโค้ดเฟิร์มแวร์ทั้งหมดของโครงการ **Modular Smart Board Game V1** ที่ได้รับการอัปเดตตรงตามวงจรฮาร์ดแวร์ PCB และระบบสื่อสาร 2 ทาง สำหรับนำไปคอมไพล์และแฟลชลงฮาร์ดแวร์จริงได้ทันที

---

## 📁 โครงสร้างโฟลเดอร์

```text
firmware/
├── esp32_usb_bridge/
│   └── esp32_usb_bridge.ino          # [1] ตัวส่ง-รับสัญญาณ USB Bridge เสียบกับ Unity (ESP-NOW 2-Way)
├── esp32_master_gateway/
│   └── esp32_master_gateway.ino      # [2] ตัวรับ ESP-NOW บนบอร์ดแม่ (Master Gateway) ส่งต่อให้ ATtiny
├── attiny1604_master/
│   └── attiny1604_master.ino         # [3A] เฟิร์มแวร์เฉพาะบอร์ดแม่ (Master Tile - ID 0 คงที่, USART TX คุยกับ ESP32)
├── attiny1604_slave/
│   └── attiny1604_slave.ino          # [3B] เฟิร์มแวร์เฉพาะบอร์ดลูก (Slave Tiles ทุกแผ่น 100% - Auto-Addressing & Presence Detect)
├── esp32_rfid_addon/
│   └── esp32_rfid_addon.ino          # [4] แท่นอ่านการ์ดเกม RFID Addon (ESP32 + RC522)
└── README.md
```

---

## 📋 สรุป Pinout ฮาร์ดแวร์จริงที่ใช้ในเฟิร์มแวร์

| บอร์ด / โมดูล | อุปกรณ์ | ฟังก์ชัน | ขา MCU / พินวงจรจริง | หมายเหตุ |
|---|---|---|---|---|
| **บอร์ดแม่ (Master Board)** | ESP32 Wemos D1 Mini | `ESP_TX` (ไป ATtiny) | **GPIO 17** (Pin 29 / D3) | ผ่าน Logic Level Converter (3.3V $\rightarrow$ 5V) |
| | | `ESP_RX` (จาก ATtiny) | **GPIO 16** (Pin 31 / D4) | ผ่าน Logic Level Converter (5V $\rightarrow$ 3.3V) |
| | | Wi-Fi ESP-NOW | **Channel 1** | สื่อสารไร้สายกับ USB Bridge & Addons |
| | ATtiny1604-SS | `LED_DIN` (หลอดไฟ 64 ดวง) | **PA1** (Pin 11) | ผ่านตัวต้านทาน R_LED1 470 โอห์ม |
| | | `ATTINY_RX` (รับจาก ESP32) | **PB3** (Pin 6) | Hardware USART0 RX |
| | | `ATTINY_TX` (ส่งกลับ ESP32)| **PB2** (Pin 7) | Hardware USART0 TX |
| | | `E_D0` (ส่งต่อ Slave แผ่นถัดไป) | **PA4** (Pin 2) | Bit-Bang UART TX 115200 bps ไปขั้ว East |
| | | `UPDI` (แฟลชโปรแกรม) | **PA0** (Pin 10) | ต่อเข้า J_UPDI1 Header |
| **บอร์ดลูก (Slave Boards)** | ATtiny1604-SS | `LED_DIN` (หลอดไฟ 64 ดวง) | **PA1** (Pin 11) | ผ่านตัวต้านทาน R_LED1 470 โอห์ม |
| | | `W_D1` (รับจาก West / แผ่นก่อนหน้า)| **PB3** (Pin 6) | Hardware USART0 RX รับข้อมูลสี |
| | | `W_D0` (ส่งกลับ West / Upstream) | **PB2** (Pin 7) | Hardware USART0 TX ตอบกลับ ACK |
| | | `E_D0` (ส่งต่อแผ่นถัดไป) | **PA4** (Pin 2) | Bit-Bang UART TX 115200 bps ไปขั้ว East |
| | | `UPDI` (แฟลชโปรแกรม) | **PA0** (Pin 10) | ต่อเข้า J_UPDI1 Header |
| **แท่นอ่านการ์ด (RFID Addon)** | ESP32 Wemos D1 Mini | `RFID_SS` (SDA/CS) | **GPIO 5** | SPI Chip Select |
| | | `RFID_RST` (Reset) | **GPIO 4** | RC522 Reset Pin |
| | | `RFID_SCK` (Clock) | **GPIO 18** | VSPI Clock |
| | | `RFID_MISO` (Data In) | **GPIO 19** | VSPI MISO |
| | | `RFID_MOSI` (Data Out) | **GPIO 23** | VSPI MOSI |
| | | `Status LED` | **GPIO 2** | ไฟ LED บนบอร์ด แจ้งสถานะแตะการ์ด |

---

## 1. `esp32_usb_bridge` (บอร์ดตัวส่งและรับสัญญาณ USB Bridge)
* **ฮาร์ดแวร์:** ESP32 Wemos D1 Mini เสียบสาย USB เข้า Laptop / PC (Unity)
* **หน้าที่ (สื่อสารแบบ 2 ทิศทาง):**
  * **ขาออก (Unity $\rightarrow$ ฮาร์ดแวร์):**
    * รับเฟรมสีตาราง $4\times 4$ แบบ Binary (53 ไบต์: `0xAA 0x55 0x01` + FrameId + 48B RGB + CRC8)
    * รองรับแพ็กเก็ตชังก์หลายแผ่น `PKT_TYPE_TILE_CHUNK` (`0x04`) และ `PKT_TYPE_COMMIT_ALL` (`0x05`) สำหรับระบบกระดานขนาดใหญ่
    * รับคำสั่งเกมข้อความ เช่น `CMD /team Dummy1_Red` หรือ `CMD /led Dummy1_Turn` แล้วแพ็กเป็น `BoardEventPacket` (53 ไบต์) ยิงออกทาง ESP-NOW
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
  * รับแพ็กเก็ตไร้สาย ESP-NOW จากตัวส่ง USB Bridge (ทั้งเฟรมสีเดี่ยว, ชังก์หลายแผ่น และคำสั่งอีเวนต์)
  * ปรับความสว่างสูงสุดได้ถึง **MAX_CHANNEL_VAL = 120/255**
  * มีสวิตช์เลือกโหมดการขับไฟ **`USE_ATTINY_UART_DRIVER`**:
    * `true` : **โหมดบอร์ดจริง V1 (`hardware/master`)** ส่งข้อมูลผ่าน UART (`Serial2` TX=GPIO17, RX=GPIO16) ไปให้ ATtiny1604 ขับไฟ 64 ดวง (ESP32 ไม่โดนบล็อกเวลา CPU)
    * `false` : **โหมดบอร์ดทดลอง (`hardware/experimental/master_esp32`)** ให้ ESP32 ขับไฟ WS2812B 64 ดวงตรงผ่านขา **GPIO 18**
  * ส่งอีเวนต์ของเกมกลับไปยัง USB Bridge ผ่าน ESP-NOW ด้วยฟังก์ชัน `sendEventToUnity()`

---

## 3. `attiny1604_master` & `attiny1604_slave` (ไมโครคอนโทรลเลอร์ประจำแผ่น)
* **ฮาร์ดแวร์:** Microchip ATtiny1604-SS (SOIC-14) แยกโฟลเดอร์สำหรับแผ่นแม่และแผ่นลูกอย่างชัดเจน:
  * **`attiny1604_master` (บอร์ดแม่ 1 แผ่น):**
    * กำหนด ID ประจำแผ่นเป็น `0` คงที่ตั้งแต่บูต
    * พิน **PB2** ทำหน้าที่เป็น Hardware USART TX ส่งข้อมูลกลับ ESP32 ทางสายตรง
    * พิน **PA5 (East D1)** ทำหน้าที่เป็น `INPUT_PULLUP` Presence Detect ตรวจจับการเสียบแผ่นลูก
    * เมื่อตรวจพบแผ่นลูกแผ่นแรกเสียบเข้ามา จะส่งคำสั่งแจก ID 1 ออกทางขา **PA4 (Bit-Bang TX 115200 bps)** ทันที
  * **`attiny1604_slave` (บอร์ดลูกทุกแผ่น 100%):**
    * พิน **PB2 (West D0)** ขับ `OUTPUT LOW` ถาวร เพื่อเป็นสวิตช์ Presence Detect ดึงขาแผ่นพี่ลงกราวด์ทันทีที่แม่เหล็กประกบ
    * พิน **PB3 (West D1)** รับแพ็กเก็ตจากแผ่นพี่
    * เมื่อได้รับ ID ตัวเอง จะบวก 1 ทันทีแล้วส่งต่อคำสั่ง `PKT_SET_ID` ออกทาง **PA4** ให้แผ่นถัดไป
    * เมื่อมีแผ่นลูกใหม่มาต่อทางทิศ East จะตรวจจับผ่านพิน **PA5** แล้วแจก ID ให้แผ่นถัดไปแบบอัตโนมัติ
* **คุณสมบัติร่วมของทั้งสองฝั่ง:**
  * เก็บข้อมูลใน **Back Buffer** และสลับไปยัง **Active Buffer** เมื่อได้รับคำสั่ง `PKT_FRAME_COMMIT`
  * ใช้ **`tinyNeoPixel_Static`** จัดสรรหน่วยความจำแบบคงที่ ป้องกันปัญหา RAM 1KB เต็ม
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
       │ (ESP-NOW CardPacket) │                 ▼ (ESP-NOW Frame / TileChunk / CommitAll)
       │                      │ ┌─────────────────────────────┐
       │                      └─┤    esp32_master_gateway     │
       │                        └──────────────┬──────────────┘
       │                                       │ (UART TX=GPIO17, RX=GPIO16 Level Shifter)
       │                                       ▼
       │                        ┌─────────────────────────────┐
       │                        │ Master ATtiny1604 (Tile 0)  │
       │                        └──────┬──────────────────────┘
       │                               │ (Bit-Bang East D0 PA4 -> West D1 PB3)
       │                               ▼
       │                        ┌─────────────────────────────┐
       │                        │  Slave ATtiny1604 (Tile 1)  │
       │                        └──────┬──────────────────────┘
       │                               │ (Bit-Bang East D0 PA4 -> West D1 PB3)
       │                               ▼
       │                        ┌─────────────────────────────┐
       │                        │  Slave ATtiny1604 (Tile 2..)│
       │                        └─────────────────────────────┘
       │
┌──────┴──────────────────────┐
│       esp32_rfid_addon      │ (แท่นอ่านการ์ด RC522)
└─────────────────────────────┘
```
