"""AVsync calibrator: one place for parts, nets and placement.

gen_sch.py (schematic) and gen_pcb.py (board) both read this file, so the
schematic and the board always have the same parts and the same nets.
Units: mm. Board origin is the top-left corner of the board outline.
"""

# Board outline (mm). End A (x = 0) carries the LED and the two sensors.
# End B (x = BOARD_W) carries the USB-C receptacle.
BOARD_W = 60.0
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

# ref: (lib_id, value, footprint, LCSC, {pin: net}, pcb (x, y, rot, side))
# The schematic drawing (positions and wires) is in gen_sch.py.
PARTS = {
    "U1": ("calibrator:PY32F002AF15P", "PY32F002AF15P6TU", "Package_SO:TSSOP-20_4.4x6.5mm_P0.65mm", "C5292059",
           {"1": "GND", "20": "VDD", "2": "SWDIO", "3": "SWCLK", "10": "NRST", "5": "GND",
            "13": "SENS_B", "14": "SENS_A", "7": "LED_DRV", "6": "BUZ_DRV",
            "4": None, "8": None, "9": None, "11": None, "12": None, "15": None,
            "16": None, "17": None, "18": None, "19": None},
           (29.0, 11.0, 0, "F")),
    "J1": ("Connector:USB_C_Receptacle_PowerOnly_6P", "TYPE-C 6P (power only)",
           "calibrator:USB_C_Receptacle_ShouHan_TYPE-C-6P", "C456012",
           {"A9": "VBUS", "B9": "VBUS", "A12": "GND", "B12": "GND", "S1": "GND", "A5": "CC1", "B5": "CC2"},
           (BOARD_W - 3.1, SPLIT_Y, 90, "F")),
    "R1": ("Device:R", "5.1k", FP_R0402, "C25905", {"1": "CC1", "2": "GND"}, (50.8, 7.2, 90, "F")),
    "R2": ("Device:R", "5.1k", FP_R0402, "C25905", {"1": "CC2", "2": "GND"}, (50.8, 14.8, 90, "F")),
    "C1": ("Device:C", "10u", FP_C0805, "C15850", {"1": "VBUS", "2": "GND"}, (50.6, 3.2, 0, "F")),
    "R3": ("Device:R", "10", FP_R0402, "C25077", {"1": "VBUS", "2": "VDD"}, (35.2, 4.4, 0, "F")),
    "C2": ("Device:C", "1u", FP_C0402, "C52923", {"1": "VDD", "2": "GND"}, (34.4, 8.2, 90, "F")),
    "C3": ("Device:C", "100n", FP_C0402, "C1525", {"1": "VDD", "2": "GND"}, (35.8, 8.2, 90, "F")),
    "BZ1": ("Device:Buzzer", "MLT-8530", "calibrator:Buzzer_MLT-8530", "C94599",
            {"1": "VBUS", "2": "BUZ_N"}, (14.6, SPLIT_Y, 180, "F")),
    "D2": ("Device:D", "1N4148WS", "Diode_SMD:D_SOD-323", "C2128",
           {"1": "VBUS", "2": "BUZ_N"}, (21.6, 4.3, 90, "F")),
    "Q1": ("Transistor_BJT:Q_NPN_BEC", "S8050", "Package_TO_SOT_SMD:SOT-23", "C2146",
           {"1": "BUZ_B", "2": "GND", "3": "BUZ_N"}, (21.6, 17.6, 0, "F")),
    "R5": ("Device:R", "1k", FP_R0402, "C11702", {"1": "BUZ_DRV", "2": "BUZ_B"}, (24.4, 15.4, 90, "F")),
    "R6": ("Device:R", "100k", FP_R0402, "C25741", {"1": "BUZ_B", "2": "GND"}, (24.4, 19.6, 0, "F")),
    "R4": ("Device:R", "150", FP_R0603, "C22808", {"1": "LED_DRV", "2": "LED_A"}, (23.2, 9.6, 90, "F")),
    "D1": ("Device:LED", "XL-3216UWC-FB white", "calibrator:LED_1206_ReverseMount_XL-3216UWC-FB",
           "C3646935", {"1": "GND", "2": "LED_A"}, (4.4, SPLIT_Y, 0, "F")),
    "Q2": ("Device:Q_Photo_NPN", "VEMT2500X01", "calibrator:Vishay_VEMT2500X01_ReverseGullwing", "C3210911",
           {"1": "VDD", "2": "SENS_A"}, (5.0, SPLIT_Y - 4.5, 0, "F")),
    "R7": ("Device:R", "22k", FP_R0402, "C25768", {"1": "SENS_A", "2": "GND"}, (34.4, 12.4, 90, "F")),
    "C4": ("Device:C", "10n", FP_C0402, "C15195", {"1": "SENS_A", "2": "GND"}, (35.8, 12.4, 90, "F")),
    "Q3": ("Device:Q_Photo_NPN", "VEMT2500X01", "calibrator:Vishay_VEMT2500X01_ReverseGullwing", "C3210911",
           {"1": "VDD", "2": "SENS_B"}, (5.0, SPLIT_Y + 4.5, 0, "F")),
    "R8": ("Device:R", "22k", FP_R0402, "C25768", {"1": "SENS_B", "2": "GND"}, (34.4, 15.6, 90, "F")),
    "C5": ("Device:C", "10n", FP_C0402, "C15195", {"1": "SENS_B", "2": "GND"}, (35.8, 15.6, 90, "F")),
    # Programming pads on the flat bottom side (pogo jig). Not assembled.
    "TP1": ("Connector:TestPoint", "VDD", "TestPoint:TestPoint_Pad_D1.5mm", "", {"1": "VDD"}, (24.0, 18.8, 0, "B")),
    "TP2": ("Connector:TestPoint", "GND", "TestPoint:TestPoint_Pad_D1.5mm", "", {"1": "GND"}, (26.54, 18.8, 0, "B")),
    "TP3": ("Connector:TestPoint", "SWDIO", "TestPoint:TestPoint_Pad_D1.5mm", "", {"1": "SWDIO"}, (29.08, 18.8, 0, "B")),
    "TP4": ("Connector:TestPoint", "SWCLK", "TestPoint:TestPoint_Pad_D1.5mm", "", {"1": "SWCLK"}, (31.62, 18.8, 0, "B")),
    "TP5": ("Connector:TestPoint", "NRST", "TestPoint:TestPoint_Pad_D1.5mm", "", {"1": "NRST"}, (34.16, 18.8, 0, "B")),
}


NETS = sorted({n for p in PARTS.values() for n in p[4].values() if n})
POWER_NETS = ["VBUS", "GND", "VDD", "BUZ_N"]
