# ไฟล์สำหรับเครื่องกัด CNC (PCB CNC Milling) - บอร์ด Master

โฟลเดอร์นี้รวบรวมไฟล์สำหรับการผลิตบอร์ด Master ด้วยเครื่องกัด CNC:

## 📁 โครงสร้างโฟลเดอร์
1. ไฟล์ Gerber คัดแยกเฉพาะเลเยอร์สำหรับกัดลายวงจร:
   - master-F_Cu.gtl      -> ลายทองแดงหน้าบน (Top Layer) : ใช้ทำ Isolation Routing
   - master-B_Cu.gbl      -> ลายทองแดงหน้าล่าง (Bottom Layer) : ใช้ทำ Isolation Routing (ต้อง Mirror แกน X หรือ Y ก่อนกัด)
   - master-PTH.drl       -> รูเจาะขาอุปกรณ์และ Via (Plated Holes) : ใช้ทำ Drilling
   - master-NPTH.drl      -> รูเจาะยึดน็อต/ไม่มีทองแดง (Non-Plated Holes) : ใช้ทำ Drilling
   - master-Edge_Cuts.gm1 -> เส้นขอบบอร์ด (Board Outline) : ใช้ทำ Cutout ตัดแผ่น PCB ออกมา

2. โฟลเดอร์ `flatcam/` :
   - master_F.FlatPrj : โปรเจกต์ FlatCAM บันทึกพารามิเตอร์การตั้งค่าเครื่องมือทั้งหมด

3. โฟลเดอร์ `gcode/` :
   - master1-F_Cu.gtl_iso_combined_cnc.nc : ไฟล์ G-code พร้อมกัด (เวอร์ชันอัปเดตล่าสุด 31 ต.ค. 2026: Tool 0.1mm, 64 steps/circle, Z_Move 2.0mm)
   - master-F_Cu.gtl_iso_combined_cnc.nc  : ไฟล์ G-code เวอร์ชันเดิม (29 ต.ค. 2026)

---
## การตั้งค่าในโปรแกรม CAM (FlatCAM / Candle / Mach3)
- กัดลายวงจร: V-bit 20-30 deg หรือ Endmill 0.1-0.2mm, Cut Z = -0.04 to -0.06mm, Travel Z = 1.5-2.0mm, Feed = 120-180 mm/min
- เจาะรู: ดอกสว่าน PCB 0.8-1.0mm, Cut Z = -1.8mm (สำหรับบอร์ดหนา 1.6mm)
- ตัดขอบบอร์ด: ดอกกัด Endmill 1.0-1.5mm, Cut Z = -1.8mm (แบ่งกินรอบละ 0.4-0.5mm)
