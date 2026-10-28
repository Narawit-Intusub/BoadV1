# Modular LED Board V1

โครงการกระดานเกม LED แบบโมดูลาร์ จำนวนสูงสุด 4 บอร์ด:

- Master 1 บอร์ด
- Slave สูงสุด 3 บอร์ด
- พื้นที่เล่นบอร์ดละ 4×4 ช่อง
- LED บอร์ดละ 64 ดวง
- Unity ติดต่อ ESP32 ผ่าน Wi-Fi
- ESP32 ติดต่อ Master ATtiny1604 ผ่าน Logic Level Converter

อ่านรายละเอียดการออกแบบทั้งหมดได้ที่ [docs/V1_design_summary.md](docs/V1_design_summary.md)

---

## โครงสร้างโปรเจกต์ KiCad

โปรเจกต์นี้ได้รับการจัดโครงสร้างตามมาตรฐาน Multi-board Electronics Hardware:

```text
board game/
├── hardware/
│   ├── master/                 # บอร์ด Master (ESP32 + ATtiny1604 + LED Matrix 4x4)
│   │   ├── master.kicad_pro    # ไฟล์หลักสำหรับเปิดใน KiCad
│   │   ├── master.kicad_sch    # วงจร Schematic
│   │   ├── master.kicad_pcb    # เลย์เอาต์แผ่นวงจรพิมพ์ PCB
│   │   ├── sym-lib-table       # Symbol library mapping
│   │   └── fp-lib-table        # Footprint library mapping
│   ├── slave/                  # บอร์ด Slave (ATtiny1604 + LED Matrix 4x4)
│   │   ├── slave.kicad_pro     # ไฟล์หลักสำหรับเปิดใน KiCad
│   │   ├── slave.kicad_sch     # วงจร Schematic
│   │   ├── sym-lib-table       # Symbol library mapping
│   │   └── fp-lib-table        # Footprint library mapping
│   ├── rfid_addon/             # บอร์ดขยาย RFID Addon
│   │   ├── rfid_addon.kicad_pro
│   │   ├── rfid_addon.kicad_sch
│   │   ├── sym-lib-table
│   │   └── fp-lib-table
│   └── libraries/              # Custom Symbols & Footprints รวมของโครงการ
│       ├── esp32-wemos-d1-mini/
│       ├── magnetic-connector/
│       ├── ws2812-matrix-2x2/
│       └── board_game_v1-cache.lib
├── firmware/                   # ซอร์สโค้ดเฟิร์มแวร์ Arduino / megaTinyCore
│   ├── esp32_usb_bridge/       # ตัวส่งสัญญาณ USB Bridge (ESP-NOW)
│   ├── esp32_master_gateway/   # ตัวรับบนบอร์ดแม่ Master Gateway
│   └── attiny1604_tile_controller/ # ตัวขับหลอดไฟประจำแผ่น ATtiny1604
├── docs/                       # เอกสาร สรุปวงจร และไฟล์ Output
│   ├── pdf/                    # master.pdf, slave.pdf
│   ├── renders/                # ภาพ Render 3D และ SVG
│   ├── reports/                # ผลการตรวจ ERC / DRC
│   └── V1_design_summary.md    # รายละเอียดเชิงเทคนิคทั้งระบบ
├── README.md
└── .gitignore
```

### วิธีเปิดในโปรแกรม KiCad
เปิดโปรแกรม KiCad จากนั้นกด **File → Open Project** แล้วเลือกไฟล์โปรเจกต์ของแต่ละบอร์ดตามที่ต้องการ:
- **Master Board:** [hardware/master/master.kicad_pro](hardware/master/master.kicad_pro)
- **Slave Board:** [hardware/slave/slave.kicad_pro](hardware/slave/slave.kicad_pro)
- **RFID Addon Board:** [hardware/rfid_addon/rfid_addon.kicad_pro](hardware/rfid_addon/rfid_addon.kicad_pro)

---

## ข้อสรุปการออกแบบระบบ

- ใช้ Switching Power Supply 5V 30A แหล่งเดียว
- Master และ Slave ต่อ `5V/GND` แบบขนาน
- ไม่ใช้ Step-down บนแต่ละบอร์ด
- ESP32 รับไฟเข้าขา `5V/VCC` แต่ลอจิกยังเป็น 3.3V
- ระหว่าง ESP32 กับ ATtiny 5V ยังต้องใช้ Logic Level Converter 2 ช่อง
- ขั้วแม่เหล็กใช้ 6 พิน: `5V ×2`, `GND ×2`, `D0`, `D1`
- จำกัดความสว่างทุก Channel ไม่เกิน 50/255
- V1 ใช้ Unity เป็น Source of truth สำหรับตำแหน่งตัวละครและช่อง Action

## การต่อไฟระหว่างบอร์ด

ทุกบอร์ดต่อแบบขนาน:

```text
5V Power Supply
├─ Master
├─ Slave 1
├─ Slave 2
└─ Slave 3
```

ในทางกายภาพ Slave อาจรับไฟต่อผ่านขั้วแม่เหล็กของบอร์ดก่อนหน้า แต่เน็ต `+5V_BUS` และ `GND` ของทุกบอร์ดยังเป็นการต่อแบบขนาน ไม่ใช่อนุกรม

## การเปิด–ปิดและประกบบอร์ด

วิธีที่แนะนำคือปิด Main switch ก่อนเสียบหรือถอดขั้วแม่เหล็ก หากต้องทดลองขณะมีไฟ ให้ดับ LED หรือลดโหลดก่อน การมี TVS อย่างเดียวไม่ได้ทำให้ระบบรองรับ Hot-plug อย่างสมบูรณ์
