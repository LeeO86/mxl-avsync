"""AVsync calibrator: one place for parts, nets and placement.

gen_sch.py (schematic) and gen_pcb.py (board) both read this file, so the
schematic and the board always have the same parts and the same nets.
Units: mm. Board origin is the top-left corner of the board outline.
"""

# Board outline (mm). End A (x = 0) carries the LED and the two sensors.
# End B (x = BOARD_W) carries the USB-C receptacle.
BOARD_W = 66.0
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
    "U1": ("calibrator:CH32X035G8R6", "CH32X035G8R6", "Package_SO:QSOP-28_3.9x9.9mm_P0.635mm", "C49118551",
           {"6": "VDD", "7": "GND", "3": "DP", "2": "DM", "5": "SENS_A", "9": "SENS_A", "11": "SENS_B",
            "15": "LED_DRV", "16": "BUZ_DRV",
            "1": None, "4": None, "8": None, "10": None, "12": None, "13": None, "14": None, "17": None,
            "18": None, "19": None, "20": None, "21": None, "22": None, "23": None, "24": None, "25": None,
            "26": None, "27": None, "28": None},
           (29.0, 11.0, 180, "F")),
    "J1": ("Connector:USB_C_Receptacle_USB2.0_16P", "TYPE-C 16P",
           "Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12", "C393939",
           {"A4": "VBUS", "A9": "VBUS", "B4": "VBUS", "B9": "VBUS", "A1": "GND", "A12": "GND", "B1": "GND",
            "B12": "GND", "S1": "GND", "A5": "CC1", "B5": "CC2", "A6": "DP", "B6": "DP", "A7": "DM", "B7": "DM",
            "A8": None, "B8": None},
           (BOARD_W - 3.35, SPLIT_Y, 90, "F")),
    "SW1": ("Switch:SW_Push", "TS-1187A (BOOT)", "Button_Switch_SMD:SW_Push_1P1T_XKB_TS-1187A", "C318884",
            {"1": "BOOT", "2": "VDD"}, (40.5, 6.0, 0, "F")),
    "R9": ("Device:R", "4.7k", FP_R0402, "C25900", {"1": "DP", "2": "BOOT"}, (40.5, 16.6, 90, "F")),
    "R1": ("Device:R", "5.1k", FP_R0402, "C25905", {"1": "CC1", "2": "GND"}, (56.6, 7.0, 90, "F")),
    "R2": ("Device:R", "5.1k", FP_R0402, "C25905", {"1": "CC2", "2": "GND"}, (56.6, 15.0, 90, "F")),
    "C1": ("Device:C", "10u", FP_C0805, "C15850", {"1": "VBUS", "2": "GND"}, (56.6, 2.8, 0, "F")),
    "R3": ("Device:R", "10", FP_R0402, "C25077", {"1": "VBUS", "2": "VDD"}, (34.3, 2.4, 0, "F")),
    "C2": ("Device:C", "1u", FP_C0402, "C52923", {"1": "VDD", "2": "GND"}, (33.6, 12.0, 90, "F")),
    "C3": ("Device:C", "100n", FP_C0402, "C1525", {"1": "VDD", "2": "GND"}, (35.0, 12.0, 90, "F")),
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
    "R7": ("Device:R", "22k", FP_R0402, "C25768", {"1": "SENS_A", "2": "GND"}, (33.6, 8.4, 90, "F")),
    "C4": ("Device:C", "1n", FP_C0402, "C1523", {"1": "SENS_A", "2": "GND"}, (35.0, 8.4, 90, "F")),
    "Q3": ("Device:Q_Photo_NPN", "VEMT2500X01", "calibrator:Vishay_VEMT2500X01_ReverseGullwing", "C3210911",
           {"1": "VDD", "2": "SENS_B"}, (5.0, SPLIT_Y + 4.5, 0, "F")),
    "R8": ("Device:R", "22k", FP_R0402, "C25768", {"1": "SENS_B", "2": "GND"}, (33.6, 5.4, 90, "F")),
    "C5": ("Device:C", "1n", FP_C0402, "C1523", {"1": "SENS_B", "2": "GND"}, (35.0, 5.4, 90, "F")),
}


NETS = sorted({n for p in PARTS.values() for n in p[4].values() if n})
POWER_NETS = ["VBUS", "GND", "VDD", "BUZ_N"]
