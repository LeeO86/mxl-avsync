"""AVsync calibrator: one place for parts, nets and placement.

gen_sch.py (schematic) and gen_pcb.py (board) both read this file, so the
schematic and the board always have the same parts and the same nets.
Units: mm. Board origin is the top-left corner of the board outline.
"""

# Board outline (mm). End A (x = 0) carries the LED and the two sensors.
# End B (x = BOARD_W) carries the USB-C receptacle.
BOARD_W = 56.0
BOARD_H = 22.0
CORNER_R = 2.0
SPLIT_Y = BOARD_H / 2  # sensor split line runs along the board at this y
NOTCH_R = 1.0          # half-round notch in end A, on the split line
PCB_THICKNESS = 1.2    # makes the reverse-gullwing sensor lenses flush with the bottom

# KiCad shows the board at this offset on the page.
ORIGIN = (100.0, 100.0)

FP_R0402 = "Resistor_SMD:R_0402_1005Metric"
FP_R0603 = "Resistor_SMD:R_0603_1608Metric"
FP_C0402 = "Capacitor_SMD:C_0402_1005Metric"
FP_C0805 = "Capacitor_SMD:C_0805_2012Metric"

# ref: (lib_id, value, footprint, LCSC, {pin: net}, sch (x, y, rot), pcb (x, y, rot, side))
PARTS = {
    "U1": ("MCU_Puya:PY32F002AF15P", "PY32F002AF15P6TU", "Package_SO:TSSOP-20_4.4x6.5mm_P0.65mm", "C5292059",
           {"1": "GND", "20": "VMCU", "2": "SWDIO", "3": "SWCLK", "10": "NRST", "5": "GND",
            "13": "SENS_B", "14": "SENS_A", "7": "LED_DRV", "6": "BUZ_DRV",
            "4": None, "8": None, "9": None, "11": None, "12": None, "15": None,
            "16": None, "17": None, "18": None, "19": None},
           (152.4, 99.06, 0), (29.0, 11.0, 0, "F")),
    "J1": ("Connector:USB_C_Receptacle_PowerOnly_6P", "TYPE-C 6P (power only)",
           "calibrator:USB_C_Receptacle_ShouHan_TYPE-C-6P", "C456012",
           {"A9": "VBUS", "B9": "VBUS", "A12": "GND", "B12": "GND", "S1": "GND", "A5": "CC1", "B5": "CC2"},
           (33.02, 63.5, 0), (BOARD_W - 3.1, SPLIT_Y, 90, "F")),
    "R1": ("Device:R", "5.1k", FP_R0402, "C25905", {"1": "CC1", "2": "GND"}, (63.5, 76.2, 0), (46.8, 7.2, 90, "F")),
    "R2": ("Device:R", "5.1k", FP_R0402, "C25905", {"1": "CC2", "2": "GND"}, (73.66, 76.2, 0), (46.8, 14.8, 90, "F")),
    "C1": ("Device:C", "10u", FP_C0805, "C15850", {"1": "VBUS", "2": "GND"}, (63.5, 48.26, 0), (46.5, 18.4, 0, "F")),
    "R3": ("Device:R", "10", FP_R0402, "C25077", {"1": "VBUS", "2": "VMCU"}, (83.82, 48.26, 0), (35.2, 4.4, 0, "F")),
    "C2": ("Device:C", "1u", FP_C0402, "C52923", {"1": "VMCU", "2": "GND"}, (99.06, 50.8, 0), (34.4, 8.2, 90, "F")),
    "C3": ("Device:C", "100n", FP_C0402, "C1525", {"1": "VMCU", "2": "GND"}, (109.22, 50.8, 0), (35.8, 8.2, 90, "F")),
    "BZ1": ("Device:Buzzer", "MLT-8530", "calibrator:Buzzer_MLT-8530", "C94599",
            {"1": "VBUS", "2": "BUZ_N"}, (48.26, 124.46, 0), (14.6, SPLIT_Y, 180, "F")),
    "D2": ("Device:D", "1N4148WS", "Diode_SMD:D_SOD-323", "C2128",
           {"1": "VBUS", "2": "BUZ_N"}, (30.48, 114.3, 0), (21.6, 4.3, 90, "F")),
    "Q1": ("Transistor_BJT:Q_NPN_BEC", "S8050", "Package_TO_SOT_SMD:SOT-23", "C2146",
           {"1": "BUZ_B", "2": "GND", "3": "BUZ_N"}, (55.88, 144.78, 0), (21.6, 17.6, 0, "F")),
    "R5": ("Device:R", "1k", FP_R0402, "C11702", {"1": "BUZ_DRV", "2": "BUZ_B"}, (33.02, 154.94, 0), (24.4, 15.4, 90, "F")),
    "R6": ("Device:R", "100k", FP_R0402, "C25741", {"1": "BUZ_B", "2": "GND"}, (45.72, 165.1, 0), (24.4, 19.6, 0, "F")),
    "R4": ("Device:R", "150", FP_R0603, "C22808", {"1": "LED_DRV", "2": "LED_A"}, (205.74, 50.8, 0), (23.2, 9.6, 90, "F")),
    "D1": ("Device:LED", "XL-3216UWC-FB (white, reverse mount)", "calibrator:LED_1206_ReverseMount_XL-3216UWC-FB",
           "C3646935", {"1": "GND", "2": "LED_A"}, (205.74, 71.12, 0), (4.4, SPLIT_Y, 0, "F")),
    "Q2": ("Device:Q_Photo_NPN", "VEMT2500X01", "calibrator:Vishay_VEMT2500X01_ReverseGullwing", "C3210911",
           {"1": "VMCU", "2": "SENS_A"}, (193.04, 116.84, 0), (5.0, SPLIT_Y - 4.5, 0, "F")),
    "R7": ("Device:R", "22k", FP_R0402, "C25768", {"1": "SENS_A", "2": "GND"}, (203.2, 134.62, 0), (34.4, 12.4, 90, "F")),
    "C4": ("Device:C", "10n", FP_C0402, "C15195", {"1": "SENS_A", "2": "GND"}, (213.36, 134.62, 0), (35.8, 12.4, 90, "F")),
    "Q3": ("Device:Q_Photo_NPN", "VEMT2500X01", "calibrator:Vishay_VEMT2500X01_ReverseGullwing", "C3210911",
           {"1": "VMCU", "2": "SENS_B"}, (236.22, 116.84, 0), (5.0, SPLIT_Y + 4.5, 0, "F")),
    "R8": ("Device:R", "22k", FP_R0402, "C25768", {"1": "SENS_B", "2": "GND"}, (246.38, 134.62, 0), (34.4, 15.6, 90, "F")),
    "C5": ("Device:C", "10n", FP_C0402, "C15195", {"1": "SENS_B", "2": "GND"}, (256.54, 134.62, 0), (35.8, 15.6, 90, "F")),
    # Programming pads on the flat bottom side (pogo jig). Not assembled.
    "TP1": ("Connector:TestPoint", "VMCU", "TestPoint:TestPoint_Pad_D1.5mm", "", {"1": "VMCU"}, (101.6, 160.02, 0), (24.0, 18.8, 0, "B")),
    "TP2": ("Connector:TestPoint", "GND", "TestPoint:TestPoint_Pad_D1.5mm", "", {"1": "GND"}, (111.76, 160.02, 0), (26.54, 18.8, 0, "B")),
    "TP3": ("Connector:TestPoint", "SWDIO", "TestPoint:TestPoint_Pad_D1.5mm", "", {"1": "SWDIO"}, (121.92, 160.02, 0), (29.08, 18.8, 0, "B")),
    "TP4": ("Connector:TestPoint", "SWCLK", "TestPoint:TestPoint_Pad_D1.5mm", "", {"1": "SWCLK"}, (132.08, 160.02, 0), (31.62, 18.8, 0, "B")),
    "TP5": ("Connector:TestPoint", "NRST", "TestPoint:TestPoint_Pad_D1.5mm", "", {"1": "NRST"}, (142.24, 160.02, 0), (34.16, 18.8, 0, "B")),
}

# Power flags tell ERC that these nets are driven (USB VBUS from the phone).
PWR_FLAGS = {"VBUS": (25.4, 40.64), "GND": (25.4, 182.88), "VMCU": (91.44, 40.64)}

NETS = sorted({n for p in PARTS.values() for n in p[4].values() if n})
POWER_NETS = ["VBUS", "GND", "VMCU", "BUZ_N"]
