# 🛠️ สถาปัตยกรรมเฟิร์มแวร์และฮาร์ดแวร์ (Firmware & Hardware Architecture)
โครงการ Smart Modular Board Game (Unity IoT Smart Board)

เอกสารฉบับนี้รวบรวมรายละเอียดสถาปัตยกรรมเฟิร์มแวร์ โปรโตคอลการสื่อสาร แผนผังฮาร์ดแวร์ ตาราง Pinout จริง การจัดการพลังงาน และการทำงานร่วมกันระหว่าง Unity, ESP32 และ ATtiny1604

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
| | | `W_D0` (บอกแผ่นพี่ว่าเชื่อมต่ออยู่) | **PB2** (Pin 7) | ปิด Hardware TX, ขับ `OUTPUT LOW` ถาวร (สวิตช์ Presence Detect ดึง `E_D1` ของแผ่นพี่ลง LOW เมื่อประกบ) |
| | | `E_D0` (ส่งต่อแผ่นถัดไป) | **PA4** (Pin 2) | Bit-Bang UART TX 115200 bps ไปขั้ว East |
| | | `E_D1` (ตรวจจับการเสียบแผ่นถัดไป) | **PA5** (Pin 3) | `INPUT_PULLUP` เมื่อเสียบแผ่นลูกถัดไปจะถูกดึงเป็น LOW (Auto Hot-Plug) |
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
  * `PING` : สั่ง Ping บอร์ดแม่และ ATtiny1604 แบบไร้สายผ่าน ESP-NOW
  * `RESCAN` / `ADDR` / `SET_ID` : สั่งแจก Tile ID ใหม่อัตโนมัติแบบไร้สายผ่าน ESP-NOW
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

## 3. `attiny1604_master` และ `attiny1604_slave` (ไมโครคอนโทรลเลอร์ประจำแผ่น)
สถาปัตยกรรมแยกเฟิร์มแวร์ 2 ชุดตามวงจร KiCad จริง เพื่อขจัดปัญหาความขัดแย้งของพิน PB2:

### 3.1 `attiny1604_master` (บอร์ดแม่ Master Tile เท่านั้น)
* **ฮาร์ดแวร์:** Microchip ATtiny1604-SS บนบอร์ดแม่ (`hardware/master/master.kicad_sch`)
* **หน้าที่:**
  * กำหนด ID ประจำแผ่นตายตัวเป็น **`myTileId = 0`**
  * รับข้อมูลแพ็กเก็ตสีจาก ESP32 ผ่านพอร์ต Hardware USART0 (115200 bps) ที่ขา **PB3 (RX)**
  * ส่งข้อมูลตอบกลับหรือสถานะไปยัง ESP32 ผ่านขา **PB2 (TX)** (Hardware USART0 TX)
  * ส่งต่อแพ็กเก็ตสีไปยังแผ่นลูกแผ่นแรก (Slave 1) ทางขั้ว East ผ่านขา **PA4 (Bit-Bang UART TX 115200 bps)**
  * ขา **PA5 (`PIN_E_D1`)** ตรวจจับการเสียบแผ่นลูกแผ่นแรก (`INPUT_PULLUP`) หากตรวจพบจะส่งคำสั่งแจก `myTileId = 1` ให้ Slave 1 ทันทีแบบ Hot-Plug
  * ควบคุมไฟ WS2812B จำนวน 64 ดวง (2x2 ต่อช่อง 16 ช่อง) ผ่านขา **PA1** ด้วย **`tinyNeoPixel_Static`**

### 3.2 `attiny1604_slave` (บอร์ดลูก Slave Tiles ทุกแผ่น 100%)
* **ฮาร์ดแวร์:** Microchip ATtiny1604-SS บนบอร์ดลูก (`hardware/slave/slave.kicad_sch`)
* **หน้าที่:**
  * **ใช้ไฟล์เฟิร์มแวร์นี้ชุดเดียวเบิร์นลงแผ่น Slave ทุกแผ่น (Slave 1, 2, 3...) ไม่ต้องแก้โค้ด**
  * รับข้อมูลแพ็กเก็ตสีจากแผ่นพี่ผ่านพอร์ต Hardware USART0 (115200 bps) ที่ขา **PB3 (RX)**
  * ปิด Hardware TX บนขา **PB2** และขับเป็น **`OUTPUT LOW` ถาวร** ทำหน้าที่เป็นสวิตช์ Presence Detect ดึงขา `E_D1` ของแผ่นพี่ลง Ground ทันทีที่ประกบขั้วแม่เหล็ก
  * รับคำสั่ง **Auto-Addressing (`PKT_SET_ID`)**: เมื่อแผ่นนี้ได้รับ ID ของตัวเอง จะหน่วงเวลาสั้นๆ แล้วบวก 1 (`nextId = myTileId + 1`) ส่งต่อออกขา **PA4 (Bit-Bang TX)** ให้แผ่นลูกถัดไปอัตโนมัติ
  * ขา **PA5 (`PIN_E_D1`)** ตรวจจับการเสียบแผ่นลูกถัดไป (`INPUT_PULLUP`) รองรับ **Dynamic Hot-Plug** ขณะเปิดเครื่อง
  * รองรับ Double Buffering (Back Buffer 48B + Active Buffer 48B) และคำสั่ง Commit ทั้งแบบรายแผ่นและ Broadcast ทุกแผ่น

### 3.3 `attiny1604_test_led` (ชุดทดสอบหลอดไฟฮาร์ดแวร์ Standalone)
* **ฮาร์ดแวร์:** Microchip ATtiny1604-SS (ใช้ได้ทั้งแผ่น Master และแผ่น Slave)
* **หน้าที่:**
  * ใช้ทดสอบการบัดกรีและสายสัญญาณหลอดไฟ WS2812B ทั้ง 64 ดวงผ่านขา **PA1** โดยไม่ต้องเชื่อมต่อ ESP32
  * รันลูปทดสอบอัตโนมัติ 3 รูปแบบที่ความสว่างปลอดภัย (`50/255`):
    1. **Dot Chase**: ไฟวิ่งไล่ทีละดวงครบ 1-64 ดวง
    2. **Cell Chase**: ไฟติดทีละช่องตาราง $4\times 4$ (ช่องละ 4 ดวง) สลับสี แดง, เขียว, น้ำเงิน, ส้ม
    3. **Color Fill Wipe**: กวาดสีแดง, เขียว, น้ำเงิน จนเต็มบอร์ด

* **การตั้งค่าใน Arduino IDE (สำหรับทั้ง Master, Slave และ Test LED):**
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

---

## ⚡ 5. การจัดการพลังงานและข้อจำกัดไฟ LED (Power Management)

* **แหล่งจ่ายไฟประจำแผ่น**: ใช้ชิปสเต็ปดาวน์ **MP2315** แปลงไฟลงมาที่ 5V จ่ายกระแสต่อเนื่องได้สูงสุด **3.0A (Peak 4.0A)**
* **เพดานความปลอดภัยของหลอดไฟ (Hardware Safety Clamp)**:
  - ใน `esp32_master_gateway.ino`: `#define MAX_CHANNEL_VAL 120`
  - ใน `attiny1604_master.ino` และ `attiny1604_slave.ino`: `#define MAX_BRIGHTNESS 120`
  - สัดส่วนความสว่างอยู่ที่ **`120/255` (~47%)** ทำให้แสงสว่างชัดเจนทะลุฝาครอบอะคริลิก และประหยัดพลังงาน

### การกินกระแสจริงบนแผ่นกระดาน 4x4 (64 หลอด WS2812B):
| สีที่เปิดใช้งาน | กระแสต่อหลอด (ที่ค่า 120) | กระแสรวม 64 หลอด | ความปลอดภัยต่อ MP2315 (3.0A) |
| :--- | :--- | :--- | :--- |
| **สีเดี่ยว** (แดง, เขียว, น้ำเงินล้วน) | ~9.4 mA | **~0.60 A** | ปลอดภัยมาก (ใช้เพียง 20% ของพิกัด) |
| **สีผสม 2 สี** (เหลือง, ฟ้า Cyan, ม่วง) | ~18.8 mA | **~1.20 A** | ปลอดภัยมาก (ใช้เพียง 40% ของพิกัด) |
| **สีขาว** (กรณีเลวร้ายที่สุด 64 หลอดพร้อมกัน) | ~28.2 mA | **~1.81 A** | ปลอดภัย (มี Safety Margin เหลือกว่า 1.2A) |

---

## 💻 6. ข้อมูลการคอมไพล์และแฟลชชิป (Flashing Instructions)

### A. สำหรับ ESP32 (Bridge, Master Gateway, RFID Addon)
* **ซอฟต์แวร์**: Arduino IDE 2.x หรือ PlatformIO
* **บอร์ด**: `ESP32 Dev Module` หรือ `LOLIN D1 mini ESP32`
* **ตั้งค่าคอมไพล์**:
  - Flash Frequency: 80MHz
  - Upload Speed: 921600 หรือ 115200
  - Core Debug Level: None / Info

### B. สำหรับ ATtiny1604 (Tile Controller)
* **บอร์ด Core**: [megaTinyCore](https://github.com/SpenceKonde/megaTinyCore) โดย Spence Konde
* **บอร์ด**: `ATtiny1604`
* **ตั้งค่าคอมไพล์**:
  - Clock: `20 MHz internal` หรือ `16 MHz internal`
  - Programmer: `SerialUPDI` (ผ่านขั้ว UPDI บนพิน PA0)
  - Pinout: Standard
* **ไลบรารีที่ต้องใช้**: `tinyNeoPixel_Static` (มีมาพร้อมกับ megaTinyCore)
