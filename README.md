# Modular LED Board V1

โครงการกระดานเกม LED แบบโมดูลาร์ จำนวนสูงสุด 4 บอร์ด:

- Master 1 บอร์ด
- Slave สูงสุด 3 บอร์ด
- พื้นที่เล่นบอร์ดละ 4×4 ช่อง
- LED บอร์ดละ 64 ดวง
- Unity ติดต่อ ESP32 ผ่าน Wi-Fi
- ESP32 ติดต่อ Master ATtiny1604 ผ่าน Logic Level Converter

อ่านรายละเอียดการออกแบบทั้งหมดได้ที่ [V1_design_summary.md](V1_design_summary.md)

## ข้อสรุปปัจจุบัน

- ใช้ Switching Power Supply 5V 30A แหล่งเดียว
- Master และ Slave ต่อ `5V/GND` แบบขนาน
- ไม่ใช้ Step-down บนแต่ละบอร์ด
- ESP32 รับไฟเข้าขา `5V/VCC` แต่ลอจิกยังเป็น 3.3V
- ระหว่าง ESP32 กับ ATtiny 5V ยังต้องใช้ Logic Level Converter 2 ช่อง
- ขั้วแม่เหล็กใช้ 6 พิน: `5V ×2`, `GND ×2`, `D0`, `D1`
- จำกัดความสว่างทุก Channel ไม่เกิน 50/255
- V1 ใช้ Unity เป็น Source of truth สำหรับตำแหน่งตัวละครและช่อง Action

## ไฟล์ KiCad

- แบบ Master ที่แนะนำ: [master_design_clean/master_design_clean.kicad_sch](master_design_clean/master_design_clean.kicad_sch)
- ไฟล์โครงการ: [master_design_clean/master_design_clean.kicad_pro](master_design_clean/master_design_clean.kicad_pro)
- แบบ Master ต้นฉบับ: [master/master.kicad_sch](master/master.kicad_sch)
- แบบทดลองเดินสายเต็ม: [master_wiring_v2/master_wiring_v2.kicad_sch](master_wiring_v2/master_wiring_v2.kicad_sch)

`master_design_clean` แบ่งวงจรเป็นบล็อกและใช้ Global Label แบบหัวลูกศรสำหรับการเชื่อมต่อข้ามบล็อก สายที่มองเห็นจึงเหลือเฉพาะสายสั้นระหว่างโมดูล LED ที่อยู่ติดกัน

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
