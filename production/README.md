# Modular Smart Board Game V1 — Production & Manufacturing Suite

โฟลเดอร์นี้รวบรวมไฟล์สำหรับงานสั่งผลิตแผ่นวงจรพิมพ์ (PCB Fabrication) และการทำต้นแบบด้วยเครื่องกัด CNC (CNC Isolation Milling) ทั้งหมดของโครงการ

---

## 📁 โครงสร้างโฟลเดอร์ (Directory Structure)

```text
production/
├── fab_packages/              # [1] ไฟล์ ZIP รวมชุดสำหรับอัปโหลดสั่งทำโรงงาน (JLCPCB, PCBWay)
│   ├── Master_Gerbers.zip     # บอร์ด Master (ESP32 + ATtiny1604 + LED 64 ดวง)
│   └── Slave_Gerbers.zip      # บอร์ด Slave (ATtiny1604 + LED 64 ดวง)
│
├── master_gerbers/            # [2] ไฟล์ Gerber & Drill แยกชิ้นครบ 12 ไฟล์ของบอร์ด Master
├── slave_gerbers/             # [3] ไฟล์ Gerber & Drill แยกชิ้นครบ 12 ไฟล์ของบอร์ด Slave
│
├── cnc_master/                # [4] สำหรับกัดบอร์ด Master ด้วยเครื่อง CNC
│   ├── gcode/                 # ไฟล์ G-code (.nc) พร้อมยิงเข้าเครื่องกัด (Candle/Mach3)
│   │   ├── master1-F_Cu.gtl_iso_combined_cnc.nc (อัปเดตล่าสุด)
│   │   └── master-F_Cu.gtl_iso_combined_cnc.nc
│   ├── flatcam/               # โปรเจกต์ FlatCAM (.FlatPrj)
│   │   └── master_F.FlatPrj
│   ├── README.txt             # คู่มือตั้งค่าเครื่องมือ ดอกกัด และ Feed rate
│   └── (Gerber เฉพาะเลเยอร์: F_Cu, B_Cu, PTH, NPTH, Edge_Cuts)
│
├── cnc_slave/                 # [5] สำหรับกัดบอร์ด Slave ด้วยเครื่อง CNC
│   ├── gcode/                 # ที่เก็บไฟล์ G-code
│   ├── flatcam/               # ที่เก็บไฟล์ FlatCAM Project
│   ├── README.txt             # คู่มือตั้งค่าเครื่องมือ
│   └── (Gerber เฉพาะเลเยอร์: F_Cu, B_Cu, PTH, NPTH, Edge_Cuts)
│
└── README.md                  # เอกสารฉบับนี้
```

---

## 🏭 1. ข้อกำหนดสำหรับการสั่งผลิตโรงงาน (JLCPCB / PCBWay)

ไฟล์ในโฟลเดอร์ `fab_packages/` สามารถลากไฟล์ `.zip` อัปโหลดขึ้นหน้าเว็บ JLCPCB หรือ PCBWay ได้ทันที โดยระบบจะตรวจจับพารามิเตอร์อัตโนมัติ:

| พารามิเตอร์ | ค่าที่แนะนำ | หมายเหตุ |
|:---|:---|:---|
| **ขนาดบอร์ด (Dimensions)** | **192 mm × 192 mm** | ตาราง 4×4 ช่อง (ช่องละ 48×48 mm) |
| **จำนวนเลเยอร์ (Layers)** | **2 Layers** | ด้านบน (Top) และด้านล่าง (Bottom) |
| **ความหนาแผ่น (Thickness)** | **1.6 mm** | มาตรฐานบอร์ดกระดานเกม |
| **ความหนาทองแดง (Copper)** | **1 oz (35 µm)** | รองรับกระแสไฟ 5V ได้เต็มประสิทธิภาพ |
| **วัสดุ (Material)** | **FR-4** Standard TG 130–140°C | |
| **สี Soldermask** | **Green** / **Black** / **White** | สีดำหรือสีเขียวเป็นที่นิยม |
| **สี Silkscreen** | **White** (หรือ Black กรณีใช้ Mask ขาว) | ลายสกรีนคมชัด สัญลักษณ์ชัดเจน |
| **การเคลือบผิว (Surface Finish)** | **Lead-Free HASL** หรือ **ENIG-RoHS** | แนะนำ Lead-free HASL สำหรับความคุ้มค่า |
| **Castellated Holes** | **No** | บอร์ดนี้ไม่มีรูขอบครึ่งวงกลม |
| **Flying Probe Test** | **Fully Tested** | ตรวจสอบวงจรขาด/ช็อต 100% |

---

## 📄 2. ตารางอ้างอิงนามสกุลไฟล์ Gerber (Gerber Layer Reference)

| นามสกุลไฟล์ | เลเยอร์ใน KiCad | รายละเอียดการใช้งาน |
|:---|:---|:---|
| `*-F_Cu.gtl` | Front Copper (Top Layer) | ลายเส้นทองแดงด้านหน้าบน |
| `*-B_Cu.gbl` | Back Copper (Bottom Layer) | ลายเส้นทองแดงด้านหลังล่าง |
| `*-F_Mask.gts` | Front Solder Mask | มาร์กเปิดช่องบัดกรีด้านหน้า |
| `*-B_Mask.gbs` | Back Solder Mask | มาร์กเปิดช่องบัดกรีด้านหลัง |
| `*-F_Silkscreen.gto` | Front Silk Screen | ตัวหนังสือ/สัญลักษณ์ด้านหน้า |
| `*-B_Silkscreen.gbo` | Back Silk Screen | ตัวหนังสือ/สัญลักษณ์ด้านหลัง |
| `*-F_Paste.gtp` | Front Solder Paste | มาร์กสำหรับยิง Stencil หน้าบน |
| `*-B_Paste.gbp` | Back Solder Paste | มาร์กสำหรับยิง Stencil หน้าล่าง |
| `*-Edge_Cuts.gm1` | Edge Cuts (Board Outline) | เส้นขอบตัดแผ่นวงจร |
| `*-PTH.drl` | Plated Through Hole | รูเจาะชุบทองแดง (ขาอุปกรณ์ Through-Hole, Via) |
| `*-NPTH.drl` | Non-Plated Through Hole | รูเจาะไม่ชุบทองแดง (รูยึดน็อต M3) |
| `*-job.gbrjob` | Gerber Job File | ไฟล์ระบุโครงสร้าง Stackup เลเยอร์ |

---

## 🛠️ 3. การทำต้นแบบด้วยเครื่องกัด CNC (CNC Isolation Milling)

สำหรับผู้ที่ต้องการกัดบอร์ดต้นแบบด้วยตนเองโดยใช้โปรแกรม **FlatCAM** และส่ง G-code เข้าโปรแกรม **Candle** หรือ **Mach3**:

1. **กัดลายทองแดง (Isolation Routing):**
   - ใช้ไฟล์ `*-F_Cu.gtl` หรือ `*-B_Cu.gbl` (สำหรับ B_Cu ต้องสั่ง Mirror แกนก่อน)
   - ดอกกัด: V-bit 20°–30° (Tip 0.1 mm) หรือ Endmill 0.1–0.2 mm
   - พารามิเตอร์: Cut Z = `-0.04` ถึง `-0.06 mm`, Travel Z = `1.5` ถึง `2.0 mm`, Feedrate = `120 mm/min`
2. **เจาะรู (Drilling):**
   - รูขาอุปกรณ์และ Via: ใช้ `*-PTH.drl` (ดอกสว่าน 0.8 mm / 1.0 mm)
   - รูยึดน็อตมุม: ใช้ `*-NPTH.drl` (ดอกสว่าน 3.2 mm สำหรับน็อต M3)
   - Cut Z = `-1.8 mm` (ทะลุแผ่น PCB 1.6 mm เข้าแผ่นรอง Spoilboard)
3. **ตัดขอบแผ่น PCB (Board Cutout):**
   - ใช้ไฟล์ `*-Edge_Cuts.gm1`
   - ดอกกัด: Flat Endmill 1.0 mm – 1.5 mm
   - Cut Z = `-1.8 mm` (แบ่งกินรอบละ Multi-depth `0.4 - 0.5 mm` พร้อมทำ Holding Tabs กันแผ่นหลุด)

---

## ⚡ 4. ระบบสร้าง G-code อัตโนมัติด้วย FlatCAM (1-Click Automation)

ทางโครงการได้สร้างระบบ **FlatCAM Script Automation (TCL Scripts)** ไว้ให้เรียบร้อยแล้ว ไม่จำเป็นต้องคลิกตั้งค่าด้วยตนเองทีละขั้นตอน

### ไฟล์สคริปต์ที่มีให้:
- **Master Board:**
  - ตัวสั่งรัน: `production/cnc_master/run_flatcam_master.bat`
  - สคริปต์ FlatCAM: `production/cnc_master/generate_master_gcode.FlatScript`
- **Slave Board:**
  - ตัวสั่งรัน: `production/cnc_slave/run_flatcam_slave.bat`
  - สคริปต์ FlatCAM: `production/cnc_slave/generate_slave_gcode.FlatScript`

### วิธีใช้งาน 2 รูปแบบ:

#### วิธีที่ 1: คลิกเดียวผ่าน Batch File (แนะนำ รวดเร็วที่สุด)
1. ดับเบิลคลิกไฟล์ `run_flatcam_master.bat` (สำหรับบอร์ด Master) หรือ `run_flatcam_slave.bat` (สำหรับบอร์ด Slave)
2. สคริปต์จะเรียก `FlatCAM.exe` ขึ้นมารันสคริปต์ประมวลผลไฟล์ Gerber ทั้งหมด (F_Cu, B_Cu, PTH Drills, Edge Cuts)
3. ไฟล์ G-code (`.nc`) จะถูกสร้างและบันทึกลงโฟลเดอร์ `gcode/` ให้อัตโนมัติทันที

#### วิธีที่ 2: รันจากในโปรแกรม FlatCAM GUI
1. เปิดโปรแกรม FlatCAM
2. ไปที่เมนู **File** $\rightarrow$ **Run Script...**
3. เลือกไฟล์ `generate_master_gcode.FlatScript` หรือ `generate_slave_gcode.FlatScript`
4. โปรแกรมจะประมวลผลทุกเลเยอร์และบันทึกไฟล์ G-code ลงโฟลเดอร์ `gcode/` ให้ทันที

### ผลลัพธ์ไฟล์ G-code ที่ได้ในโฟลเดอร์ `gcode/`:
- `master_top_iso.nc` / `slave_top_iso.nc` : กัดลายทองแดงหน้าบน (F_Cu)
- `master_bot_iso.nc` / `slave_bot_iso.nc` : กัดลายทองแดงหน้าล่าง (B_Cu สลับ Mirror แกน X ให้เรียบร้อย)
- `master_drills.nc` / `slave_drills.nc` : เจาะรูขาทะลุและ Via (PTH)
- `master_cutout.nc` / `slave_cutout.nc` : ตัดขอบบอร์ด 192×192 mm แบบ Multi-depth พร้อมทำ Holding Tabs 4 จุด


