# Modular LED Board V1 — Design Handoff Summary

อัปเดตล่าสุด: 25 กันยายน 2026

เอกสารนี้เป็นข้อสรุปการออกแบบล่าสุดของ Modular LED Board V1 และใช้แทนข้อสรุประบบไฟ 24V ฉบับก่อนหน้า รวมถึงข้อสรุปเรื่องการต่อบอร์ด การจ่ายไฟจากแหล่งเดียว หน้าที่ของ ESP32 และการแสดงช่อง Action จาก Unity

## 1. เป้าหมายของ V1

- กระดานเกม LED แบบโมดูลาร์ ต่อได้สูงสุด 4 แผ่น: Master 1 แผ่น และ Slave 3 แผ่น
- รองรับรูปแบบ T, L, 1×4 และ 2×2 โดยทุกแผ่นวางในทิศเดียวกัน
- แต่ละแผ่นมีพื้นที่เล่น 4×4 และมี ATtiny1604 ควบคุม LED กับพอร์ตเชื่อมต่อของตัวเอง
- ESP32 บน Master ติดต่อ Unity ผ่าน Wi-Fi สร้าง Topology และแจกสถานะช่องให้แต่ละ Tile
- V1 เน้นตัวกระดานและการแสดงช่อง Action ยังไม่รวม RFID หรือตัวหมากอัจฉริยะ
- ตำแหน่งตัวละครใน V1 มาจาก Unity หรือการป้อนข้อมูลของผู้เล่น

## 2. ขนาดและจำนวน LED

| รายการ | ค่า |
|---|---:|
| ขนาดกระดาน | 192 × 192 มม. |
| จำนวนช่องต่อกระดาน | 4 × 4 = 16 ช่อง |
| ขนาดต่อช่อง | 48 × 48 มม. |
| LED ต่อช่อง | NeoPixel Matrix 2×2 จำนวน 4 LED |
| LED ต่อกระดาน | 64 LED |
| LED รวม 4 กระดาน | 256 LED |

โมดูล LED ต้นแบบ:

> NeoPixel Matrix 2×2, WS2812 5050 RGB

ไฟล์ KiCad:

- Symbol: `ws2812-matrix-2x2/ws2812-matrix-2x2.kicad_sym`
- Footprint: `ws2812-matrix-2x2:WS2812_Matrix_2x2_15x15mm`
- ขนาดโมดูลใน Footprint: 15 × 15 มม.
- Pad 1/4/5/8 = GND, Pad 2/6 = 5V, Pad 3 = DIN, Pad 7 = DOUT
- การเดินสายใน Schematic: เดินสายบัสขนาน 3 เส้นตรงยาว (VCC / DATA / GND) ตลอดแต่ละแถว (Row 1–4)
  - VCC: ต่อขนานตรง Pin 6 (VCC_OUT) เข้า Pin 2 (VCC_IN) ของโมดูลถัดไปในแถว โดยรับป้าย `+5V_BUS` เฉพาะที่ต้นแถว
  - DATA: ส่งต่อ Daisy-chain จาก Pin 7 (DOUT) ไป Pin 3 (DIN) ของโมดูลถัดไป และใช้ Global label เชื่อมข้ามระหว่างแถว
  - GND: ต่อขนานตรง Pin 8 (GND_OUT2) เข้า Pin 4 (GND_IN2) ของโมดูลถัดไปในแถว โดยรับป้าย `GND` เฉพาะที่ต้นแถว
- โมดูล NeoPixel Matrix 2×2 มี Ceramic capacitor 100nF บัดกรีในตัวอยู่แล้ว จึงตัด C_MOD1–C_MOD16 บนบอร์ดหลักออกเพื่อประหยัดพื้นที่และลดความซ้ำซ้อน
- ใส่ตัวต้านทานอนุกรม 470Ω (R_LED1) ตรงขา PA1 ของ ATtiny1604 ก่อนเข้า LED ตัวแรก (MOD1 DIN)
- ใส่ Bulk capacitor 100µF 25V (C_BULK1) หลัง Soft-Start บนราง 12V
- สัญญาณข้อมูลขั้วแม่เหล็ก (N_D0, N_D1, E_D0, E_D1, S_D0, S_D1) มีตัวต้านทาน 330Ω (R_ND0, R_ND1, R_ED0, R_ED1, R_SD0, R_SD1) อนุกรมคั่นจากขา ATtiny1604 เพื่อป้องกัน ESD/Surge ขณะประกบแม่เหล็ก

ต้องมีโมดูลตัวอย่างเพื่อยืนยันขนาด Pinout และทิศทางข้อมูลก่อนล็อกงานทางกล

## 3. สถาปัตยกรรมระบบ

```text
Unity
  ↕ Wi-Fi
ESP32 บน Master
  ↕ UART ผ่าน Logic Level Converter
ATtiny1604 บน Master
  ↕ D0/D1 ผ่านขั้วแม่เหล็ก
ATtiny1604 บน Slave
  ↕
LED ของแต่ละกระดาน
```

การแบ่งหน้าที่:

- Unity เป็นผู้คำนวณกติกา ตำแหน่งตัวละคร และช่อง Action
- ESP32 เป็นตัวควบคุมระดับระบบ สร้าง Topology และแปลงพิกัดรวมเป็น Tile/Cell
- Master ATtiny เป็น Gateway ระหว่าง ESP32 กับเครือข่ายกระดาน และควบคุม LED ของ Master
- Slave ATtiny ตรวจเพื่อนบ้าน รับ/ส่งต่อ Packet และควบคุม LED ของตัวเอง

### ไฟล์ออกแบบที่ใช้อ้างอิง

| ไฟล์ | สถานะ |
|---|---|
| `master_design_clean/master_design_clean.kicad_sch` | แบบ Master ที่แนะนำให้เปิด ใช้ Global Label แบ่งบล็อกและเหลือเฉพาะสายสั้น |
| `master/master.kicad_sch` | แบบ Master ต้นฉบับ เก็บไว้เป็น Baseline |
| `master_wiring_v2/master_wiring_v2.kicad_sch` | แบบทดลองเดินสายเต็ม ใช้สำหรับย้อนดู ไม่ใช่แบบแนะนำสำหรับอ่านวงจร |
| `ex/magnetMatrix6x6-main/PCB/miniwall1b6x6/miniwall1b6x6.kicad_sch` | วงจรตัวอย่างสำหรับศึกษาแนวคิดเท่านั้น |

ใน `master_design_clean` การเชื่อมต่อข้ามบล็อกใช้แท็กชื่อเน็ต เช่น `+5V_BUS`, `GND`, `ESP_TX`, `ESP_RX` และ `LED_R1_TO_R2` แทนสายยาว ชื่อแท็กที่เหมือนกันหมายถึงจุดเดียวกันทางไฟฟ้า

## 4. ระบบจ่ายไฟ 5V

ยืนยันใช้แหล่งจ่ายเดียว:

> Switching Power Supply 5V 30A

ระบบปัจจุบันไม่ใช้ 24V และไม่ใช้ Step-down บนแต่ละกระดาน

```text
Switching Power Supply 5V 30A
└─ SW_MAIN (สวิตช์กลไก) → Fuse หลัก 5A → Q_SW1 (AO4407A Soft-Start) → ราง +5V_BUS / GND ร่วม
                                                                      ├─ ESP32 ขา 5V/VCC
                                                                      ├─ Master ATtiny + LED
                                                                      ├─ Slave 1
                                                                      ├─ Slave 2
                                                                      └─ Slave 3
```

บอร์ดทั้งสี่ต่อไฟแบบขนานทางไฟฟ้า: ทุกบอร์ดรับ `+5V_BUS` เดียวกันและใช้ `GND` ร่วมกัน ห้ามต่อไฟเลี้ยงบอร์ดแบบอนุกรม

แม้บอร์ดจะประกบต่อกันเป็นสาย 1×4 กระแสของ Slave ด้านท้ายจะไหลผ่านขั้วและลายทองแดงของบอร์ดด้านหน้า จึงต้องทำราง 5V/GND ให้กว้าง ใช้ Copper pour และเลือกขั้วต่อให้รองรับกระแสจริง

ข้อกำหนด:

- GND ของ ESP32, ATtiny, LED และ Logic Level Converter ต้องร่วมกัน
- ESP32 รับไฟเข้าขา 5V/VCC แล้วใช้ Regulator บนบอร์ดสร้าง 3.3V
- ห้ามป้อนไฟ 5V เข้าขา 3V3 ของ ESP32
- ESP32 รับไฟจากราง 5V ร่วม โดยมี Capacitor ใกล้โมดูล
- ใช้ Terminal block หรือขั้วกำลังรับไฟจาก Power Supply ไม่ใช้ DC Jack 5.5×2.5 มม.
- Power Supply 30A มีกระแสลัดวงจรสูง จึงต้องมีฟิวส์หลักประมาณ 5A ใกล้จุดรับไฟ
- รุ่นต้นแบบใช้ราง 5V ตรงร่วมกันเหมือนบอร์ดตัวอย่าง ไม่มี eFuse หรือฟิวส์แยก ESP/LED
- TVS และฟิวส์แยกถูกเก็บเป็น Footprint แบบ DNP สำหรับเพิ่มภายหลัง
- วิธีใช้งานที่ปลอดภัยและง่ายที่สุดคือปิด Main switch ก่อนประกบหรือถอดบอร์ด
- อนุญาตให้ทดลองประกบขณะเปิดไฟได้เฉพาะตอน LED ดับหรือโหลดต่ำ แต่ยังไม่ถือว่าเป็น Hot-plug ที่รับประกัน
- TVS ช่วยลดแรงดันกระชาก แต่ไม่สามารถแทนวงจร Hot-swap, Current limit หรือการจัดลำดับขั้วต่อได้

อุปกรณ์จากแบบ 24V เดิมที่ตัดออกจาก Baseline ปัจจุบัน:

- Adapter 24V 5A
- Buck 24V→5V ทั้งหมด
- SMBJ26A
- DMP6023LE-13 สำหรับระบบ 24V
- DC Jack 5.5×2.5 มม.
- INA226 และชันต์ 0.01Ω 2W แบบเดิม ต้องออกแบบใหม่หากต้องการวัดกระแสระบบ 5V
- KCD1-101A แบบเดิมไม่ใช้ตัดกระแส 5V หลักโดยตรง

## 5. ขีดจำกัดความสว่างและงบกระแส

V1 จำกัดค่าสีสูงสุดต่อ Channel เป็น:

```text
R ≤ 50
G ≤ 50
B ≤ 50
ช่วงข้อมูลเต็ม = 0–255
```

กรณีหนักสุดที่อนุญาตคือทุก LED แสดง `RGB(50,50,50)`:

```text
กระแสต่อ LED ≈ 60mA × 50/255 ≈ 11.8mA
64 LED ต่อกระดาน ≈ 0.75A
256 LED รวม ≈ 3.0A
รวม ESP32, ATtiny และ Margin ≈ 4A โดยประมาณ
```

การจำกัดต้องทำสองชั้น:

1. ESP32 ห้ามส่งค่า RGB เกิน 50
2. ATtiny Clamp ทุก Channel ด้วย `min(value, 50)` ก่อนส่งให้ WS2812

ต้องมี Failsafe เพราะหาก Software เปิด `RGB(255,255,255)` กระแสอาจเพิ่มเป็นประมาณ 3.84A ต่อกระดาน

## 6. ขั้วต่อแม่เหล็กระหว่างกระดาน

ใช้ขั้วต่อแม่เหล็ก 6 ขาต่อด้าน:

```text
5V  × 2
GND × 2
D0
D1
```

ทิศและเพศ:

| ด้าน | รูปแบบ |
|---|---|
| เหนือ | ตัวเมีย |
| ตะวันออก | ตัวเมีย |
| ใต้ | ตัวผู้ |
| ตะวันตก | ตัวผู้ |

ข้อกำหนดทางไฟฟ้าและกลไก:

- เมื่อประกบต้องได้ `5V ↔ 5V`, `GND ↔ GND` และ D0/D1 ไขว้ตามวงจรที่กำหนด
- D0/D1 ของแต่ละด้านต้องแยกเข้าขา ATtiny ห้ามรวมสี่ด้านเป็นบัสเดียว
- ต้องใช้ขั้วที่รองรับอย่างน้อย 2A ต่อ Contact และมี Contact resistance ต่ำ
- ต้องตรวจสอบการประกบเหลื่อมหนึ่งขา การกลับด้าน และการลัดวงจรระหว่าง 5V/GND/Data
- ลาย 5V/GND ต้องเป็น Copper pour ขนาดใหญ่ และพิจารณาทองแดง 2 oz
- รุ่นขั้วต่อและลำดับขาจริงยังต้องยืนยันจาก Datasheet และตัวอย่างจริง

กรณี 1×4 ที่ `RGB(50,50,50)`:

```text
โหลดสาม Slave ≈ 0.75A × 3 ≈ 2.26A
แบ่งผ่านขา 5V สองขา ≈ 1.13A ต่อขา
ขา GND รับกระแสกลับใกล้เคียงกัน
```

ต้องทดสอบแรงดันตกและอุณหภูมิที่ขั้วแรกจริง โดยเผื่อการแบ่งกระแสระหว่าง Contact ไม่เท่ากัน

## 7. การตรวจจับเพื่อนบ้านและการสื่อสาร D0/D1

นำแนวคิดจาก `magnetMatrix6x6` มาใช้เฉพาะส่วนตรวจจับการประกบ:

- ตอนเริ่มต้น ขาหนึ่งตั้งเป็น `OUTPUT LOW`
- อีกขาตั้งเป็น `INPUT_PULLUP`
- เมื่อประกบกระดาน สาย D0/D1 ที่ไขว้กันทำให้ Input อ่าน LOW
- ATtiny จึงรู้ว่ามีเพื่อนบ้านอยู่ด้านใด

หลังตรวจพบเพื่อนบ้าน D0/D1 เปลี่ยนเป็นโหมดสื่อสารสองสายแบบ Half-duplex หรือ Synchronous GPIO และสื่อสารทีละด้าน

เหตุผลที่ไม่ใช้ UART แยกสี่ด้าน:

- ATtiny1604 มี GPIO 12 ขา แต่มี Hardware USART เพียงหนึ่งชุด
- การใช้ D0/D1 เป็น GPIO แยกแต่ละด้านทำให้ยังระบุทิศทางของ Packet ได้
- Hardware USART บน Master สงวนไว้สำหรับการเชื่อมกับ ESP32

รายละเอียดที่ยังต้องออกแบบและทดสอบ:

- บทบาท Clock/Data หรือรูปแบบ Half-duplex ของ D0/D1
- Pull-up ภายนอกและตัวต้านทานอนุกรมป้องกันการขับชนกัน
- Data rate เป้าหมาย
- Timeout, Retry และการกู้คืนเมื่อถอดกระดาน
- การหลีกเลี่ยงช่วง `leds.show()` ซึ่ง WS2812 ต้องการ Timing เฉพาะ

## 8. รูปแบบ Packet

Packet ระหว่างกระดานควรมีอย่างน้อย:

```text
Source ID
Destination ID
Ingress/Direction
Sequence Number
Packet Type
TTL
Payload Length
Payload
CRC
```

หลักการ:

- ESP32 สร้าง Spanning tree จากข้อมูลเพื่อนบ้าน
- Link ที่ทำให้เกิด Loop ในรูป 2×2 ถูกเก็บเป็น Secondary link หรือไม่ใช้ส่ง Packet ปกติ
- Sequence Number ใช้ตรวจ Packet ซ้ำ
- TTL ป้องกัน Packet วนไม่สิ้นสุด
- การส่งข้อมูลต้องถูกจัดคิวเพื่อไม่ให้หลายด้านขับสัญญาณชนกัน

## 9. ATtiny1604 ประจำกระดาน

ยืนยันใช้:

> ATtiny1604-SS, SOIC-14 (`MCU_Microchip_ATtiny:ATtiny1604-SS`)

หน้าที่:

- ตรวจเพื่อนบ้านตามทิศเหนือ ตะวันออก ใต้ และตะวันตก
- รายงานสถานะ Link และ Tile ID
- รับและส่งต่อ Packet
- รับสถานะหรือสีของ 16 ช่อง
- ขับ LED 64 ดวง โดยทำสีซ้ำ 4 LED ต่อหนึ่งช่อง
- Clamp ค่า RGB ไม่เกิน 50
- โปรแกรมผ่าน UPDI

Pin mapping ที่ใช้ใน schematic:

| ขา ATtiny1604 | Master | Slave |
|---|---|---|
| PA0 | UPDI | UPDI |
| PA1 | LED Data | LED Data |
| PA2 / PA3 | North D0 / D1 | North D0 / D1 |
| PA4 / PA5 | East D0 / D1 | East D0 / D1 |
| PA6 / PA7 | South D0 / D1 | South D0 / D1 |
| PB2 / PB3 | UART TX / RX ไป ESP32 | West D0 / D1 |
| PB0 / PB1 | Reserved | Reserved |

Master ใช้ USART0 ที่ PB2/PB3 ผ่าน Logic Level Converter ส่วน Slave ไม่มี ESP32 จึงนำ PB2/PB3 ไปใช้กับขั้วด้านตะวันตกตามแนวทางของบอร์ดตัวอย่าง

ข้อมูลสีแบบ Raw ต่อกระดาน:

```text
16 ช่อง × RGB 3 ไบต์ = 48 ไบต์
Active buffer 48 ไบต์
Back buffer 48 ไบต์
รวม 96 ไบต์
```

## 10. ESP32 บน Master

โมดูลต้นแบบ:

> MH-ET LIVE ESP32 Mini D1, CP2104

การใช้งานจริงของ V1:

- ไม่ใช้ USB เป็นแหล่งจ่ายไฟหรือช่องสื่อสารกับ Unity
- รับไฟ 5V จากรางร่วมหลังฟิวส์หลัก พร้อม Capacitor ใกล้โมดูล
- ติดต่อ Unity ผ่าน Wi-Fi
- USB ใช้เฉพาะการโปรแกรมระหว่างพัฒนา โดยต้องถอดไฟภายนอกหรือจัดการ VBUS ไม่ให้ชนกัน

หน้าที่:

- ติดต่อ Unity
- รวบรวม Tile ID และข้อมูลเพื่อนบ้านจาก Master ATtiny
- สร้าง Topology graph และ Spanning tree
- กำหนด Master เป็นพิกัด `(0,0)`
- คำนวณพิกัดของ Slave
- แปลงพิกัด Global เป็น Tile coordinate และ Local cell 0–15
- แจกสถานะช่อง สี และ Animation
- จัดลำดับ Packet และตรวจ Sequence/TTL/CRC

## 11. Logic Level Converter ระหว่าง ESP32 และ Master ATtiny

ESP32 ใช้ลอจิก 3.3V แม้รับไฟเข้าบอร์ด 5V ขณะที่ ATtiny ทำงานที่ 5V จึงยังต้องแปลงระดับแรงดัน

โมดูลต้นแบบ:

> Logic Level Converter Module 2 channel 5V–3.3V IIC UART SPI

การต่อเบื้องต้น:

```text
HV  → 5V
LV  → 3.3V จาก ESP32
GND → GND ร่วม

ESP32 TX  → TXI
TXO       → ATtiny RX

ATtiny TX → RXI
RXO       → ESP32 RX
```

ข้อกำหนด:

- ทดสอบที่ Baud rate จริง เริ่มต้นที่ 115200 baud
- เดินสายให้สั้น
- ใส่ 100nF ระหว่าง HV–GND และ LV–GND ใกล้โมดูล
- โมดูลสำเร็จรูปใช้สำหรับต้นแบบ
- PCB รุ่นผลิตควรพิจารณา TXU0202 หรือ TXS0102 ที่มี Datasheet และคุณสมบัติ Ioff/VCC isolation ชัดเจน

## 12. การแสดงช่อง Action จาก Unity

Unity เป็น Source of truth สำหรับตำแหน่งตัวละครและกติกาเกม เนื่องจาก V1 ยังไม่มี RFID

สถานะช่องเบื้องต้น:

| Code | ความหมาย | สีเสนอแนะ |
|---:|---|---|
| 0 | ปิด | ดับ |
| 1 | ตัวละครที่เลือก | เขียว |
| 2 | เดินได้ | น้ำเงิน |
| 3 | โจมตีได้ | แดง |
| 4 | โต้ตอบได้ | เหลือง |
| 5 | เป้าหมาย/ช่องพิเศษ | ม่วง |

ลำดับข้อมูล:

```text
Unity คำนวณช่อง Action
        ↓ Wi-Fi
ESP32 รับพิกัด Global
        ↓
ESP32 แปลงเป็น Tile ID + Local cell 0–15
        ↓
Master ATtiny ส่ง Packet ผ่าน D0/D1
        ↓
ATtiny ปลายทางอัปเดต LED 4 ดวงต่อช่อง
```

Payload สองรูปแบบ:

- `CELL_STATE`: ส่งรหัสสถานะ 16 ช่อง เหมาะกับ Action และใช้ข้อมูลน้อย
- `RAW_RGB`: ส่ง RGB 48 ไบต์ต่อกระดาน เหมาะกับ Effect หรือ Animation พิเศษ

สีทั้งหมดต้องผ่าน Brightness clamp สูงสุด 50/255

## 13. Master และ Slave

### Master

- มีพื้นที่เล่น 4×4 และ LED 64 ดวงเหมือน Slave
- มี ATtiny1604 ควบคุม LED และ Gateway ของเครือข่ายกระดาน
- มี ESP32 ติดต่อ Unity ผ่าน Wi-Fi
- รับไฟ 5V จาก Switching Power Supply
- ต่อกระดานได้สามด้าน: เหนือ ตะวันออก และใต้
- ด้านตะวันตกใช้สำหรับวงจร ESP32 และไฟเข้า

อุปกรณ์หลักที่ต้องมีบน Master:

- Terminal block (J_PWR1) สำหรับรับ 5V/GND จาก Power Supply
- Main mechanical switch (SW_MAIN: คอนเนคเตอร์ JST-XH 2-Pin B2B-XH-A) ตัดไฟ 5V ทั้งระบบก่อนเข้าบอร์ด รองรับเสียบสายไปสวิตช์ติดเคส
- Main fuse 5A (F_MAIN1) ป้องกันกระแสเกิน
- วงจร Soft-Start P-MOSFET (Q_SW1 AO4407A SO-8 พร้อม R_GATE1 100k, R_GATE2 47k, C_SS1 100nF) หน่วงเวลาเปิดไฟ ~10ms ป้องกันไฟกระชาก
- Bulk capacitor ประมาณ 1000µF (C_BULK1) บนราง +5V_BUS
- Footprint TVS สำหรับราง 5V แบบ DNP ในต้นแบบ (D_TVS1)
- ESP32 Mini D1 รับไฟที่ขา 5V/VCC
- ATtiny1604-SS พร้อมหัวโปรแกรม UPDI
- Logic Level Converter 2 ช่อง ระหว่าง ESP32 3.3V กับ ATtiny 5V
- Capacitor 100nF ใกล้ขาไฟของ ESP32, ATtiny และทั้งสองฝั่งของ Level Converter
- ตัวต้านทาน 330–470Ω (R_LED1: 470R) ต่ออนุกรมตรงจากขา PA1 (Pin 11) ของ ATtiny1604 ก่อนข้อมูลเข้า LED ตัวแรก (LED_DIN)
- โมดูล LED 2×2 จำนวน 16 โมดูล
- ขั้วแม่เหล็ก 6 พินด้านเหนือ ตะวันออก และใต้

Master ไม่ต้องมี Step-down เพราะทั้งระบบใช้แหล่งจ่าย 5V เดียว และ ESP32 รับ 5V ผ่าน Regulator ที่อยู่บนโมดูล

### Slave

- มี ATtiny1604 และ LED 64 ดวง
- รับ 5V ผ่านขั้วแม่เหล็ก
- มีขั้วต่อครบสี่ด้าน
- ตรวจจับเพื่อนบ้านและส่งต่อ Packet ตามคำสั่งของ Master

## 14. สถานะที่ยืนยันแล้ว

| หัวข้อ | ข้อสรุป |
|---|---|
| Board count | Master 1 + Slave 3 |
| Board grid | 4×4 ช่องต่อกระดาน |
| LED | 64 LED ต่อกระดาน, 256 LED รวม |
| LED module | NeoPixel Matrix 2×2, WS2812 5050 |
| Tile MCU | ATtiny1604-SS, SOIC-14 |
| Master controller | MH-ET LIVE ESP32 Mini D1 |
| Unity link | Wi-Fi ไม่ใช้ USB ระหว่างเล่น |
| Power source | Switching Power Supply 5V 30A |
| Buck converter | ไม่ใช้ |
| Power distribution | Fuse หลัก 5A → ราง +5V_BUS ร่วม; TVS/ฟิวส์แยกเป็น DNP |
| Board power connection | Master และ Slave ทั้งหมดต่อ 5V/GND แบบขนาน |
| Powered attachment | ทดลองได้เมื่อ LED ดับหรือโหลดต่ำ; ยังไม่รับรอง Hot-plug เต็มรูปแบบ |
| Brightness limit | RGB แต่ละ Channel ไม่เกิน 50/255 |
| Magnetic connector signals | Female: VCC, GND, D0, D1, GND, VCC / Male: VCC, GND, D1, D0, GND, VCC |
| Link detection | GPIO Output LOW + Input Pull-up ต่อด้าน |
| ESP/ATtiny level shift | โมดูล Logic Level Converter 2 channel สำหรับต้นแบบ |
| Recommended Master schematic | `master_design_clean/master_design_clean.kicad_sch` |
| V1 scope | ยังไม่รวม RFID และตัวหมากอัจฉริยะ |

## 15. รายการที่ยังต้องตัดสินใจหรือยืนยัน

1. รุ่นขั้วต่อแม่เหล็ก 6 ขาและ Datasheet กระแสต่อ Contact
2. ลำดับขาขั้วแม่เหล็ก 6 พิน: แยกตัวเมีย Female (North, East: 5V, GND, D0, D1, GND, 5V) และตัวผู้ Male (South: 5V, GND, D1, D0, GND, 5V) ทำให้เมื่อประกบหน้าชนกัน D0↔D0, D1↔D1, 5V↔5V, GND↔GND ตรงกัน 100%
3. วงจร D0/D1 หลัง Detection: Half-duplex หรือ Synchronous Clock/Data
4. Data rate และ Timing ร่วมกับการขับ WS2812
5. ตัวต้านทานอนุกรม, Pull-up และ ESD protection ของ D0/D1
6. ยืนยันค่าและ Footprint ของ Fuse หลัก 5A จากกระแสจริงและขนาดสาย/ลายทองแดง
7. รุ่นสวิตช์หรือ MOSFET ที่รองรับกระแส DC ของระบบ 5V
8. ขนาด Terminal block, สายไฟ, Copper weight และ Thermal design
9. ผลทดสอบแรงดันตก/อุณหภูมิที่ขั้วแรกในรูป 1×4
10. ผลทดสอบรูป 2×2 ที่มีเส้นทางไฟและ Data เป็น Loop
11. ตรวจความถูกต้องของ D0/D1 หลังกลับด้านตัวผู้/ตัวเมียตาม Pin mapping ที่กำหนด
12. รุ่น/ขนาด/Pinout จริงของ NeoPixel 2×2
13. วิธีตั้ง Tile ID และการเก็บค่าใน EEPROM
14. Packet format, CRC, Retry และ Timeout ฉบับสมบูรณ์
15. วิธีโปรแกรม ESP32 โดยไม่ให้ USB VBUS ชนกับไฟภายนอก
16. วงจรวัดกระแสใหม่ หากยังต้องการ Current monitoring
17. ตรวจ ERC และปรับตำแหน่งแท็กใน `master_design_clean` หลังเปิดด้วย KiCad จริง

## 16. ลำดับงานถัดไป

1. เลือกขั้วต่อแม่เหล็ก 6 ขาและตรวจ Datasheet
2. วาดตาราง Pin mapping ตัวผู้/ตัวเมียทั้งสี่ด้าน
3. สรุปวงจร D0/D1 สำหรับ Detect + Communication
4. ทดสอบ Pin mapping ATtiny1604 ใน Firmware ทั้ง Master และ Slave
5. ทำต้นแบบ Link สองกระดานและทดสอบ Packet พร้อม WS2812
6. ทำต้นแบบ Power chain สี่กระดานที่ `RGB(50,50,50)`
7. วัดแรงดันตก อุณหภูมิ และกระแสกระชากในรูป 1×4 และ 2×2
8. ทดสอบ Unity → Wi-Fi → ESP32 → ATtiny → LED แบบครบเส้นทาง
9. เริ่ม KiCad หลังวงจรขั้วต่อ Power protection และ D0/D1 ผ่านการทดสอบ
