"""Single source of truth for the SAFE-NODE carrier board: parts, values, footprints and pin-to-net map.
Both the schematic generator and the PCB generator read this file."""

FP_R = "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal"
FP_C = "Capacitor_THT:C_Disc_D3.0mm_W1.6mm_P2.50mm"
FP_HDR = "Connector_PinHeader_2.54mm:PinHeader_1x0{}_P2.54mm_Vertical"

NC = None  # explicitly not connected

devkit_left = ["3V3", "EN", "VP", "VN", "IO34", "IO35", "IO32", "IO33", "IO25", "IO26", "IO27",
               "IO14", "IO12", "GND", "IO13", "IO9", "IO10", "IO11", "5V"]
devkit_right = ["GND", "IO23", "IO22", "TX", "RX", "IO21", "GND", "IO19", "IO18", "IO5", "IO17",
                "IO16", "IO4", "IO0", "IO2", "IO15", "IO8", "IO7", "IO6"]
devkit_net = {"3V3": "+3V3", "IO35": "VBAT_ADC", "IO32": "GAS_ADC", "IO33": "FLAME_AO", "IO26": "LORA_DIO0",
              "IO27": "LORA_RST", "IO14": "LED_RED", "GND": "GND", "IO13": "BUZ_DRV", "5V": "DEVKIT_5V",
              "IO23": "LORA_MOSI", "IO19": "LORA_MISO", "IO18": "LORA_SCK", "IO5": "LORA_NSS",
              "IO17": "LED_YELLOW", "IO16": "LED_GREEN", "IO4": "DHT_DATA"}
u1_pins = {}
for i, n in enumerate(devkit_left):
    u1_pins[str(i + 1)] = devkit_net.get(n, NC)
for i, n in enumerate(devkit_right):
    u1_pins[str(i + 20)] = devkit_net.get(n, NC)

# ref: (lib_id, value, footprint, {pin: net}, description)
PARTS = {
    "U1": ("SAFE-NODE:ESP32-DevKitC-V4", "ESP32-DevKitC-V4", "SAFE-NODE:ESP32-DevKitC-V4_Socket", u1_pins,
           "Espressif ESP32-DevKitC V4 (WROOM-32) on 2x19 female sockets, rows 25.4 mm apart"),
    "U2": ("RF_Module:RFM95W-868S2", "RFM95W-868S2", "RF_Module:HOPERF_RFM9XW_SMD",
           {"1": "GND", "2": "LORA_MISO", "3": "LORA_MOSI", "4": "LORA_SCK", "5": "LORA_NSS", "6": "LORA_RST",
            "7": NC, "8": "GND", "9": "ANT", "10": "GND", "11": NC, "12": NC, "13": "+3V3", "14": "LORA_DIO0",
            "15": NC, "16": NC}, "HopeRF SX1276 LoRa module, operated at 865-867 MHz"),
    "J1": ("Connector:Conn_Coaxial", "SMA 865MHz", "Connector_Coaxial:SMA_Amphenol_132289_EdgeMount",
           {"1": "ANT", "2": "GND"}, "SMA edge-mount antenna connector"),
    "J2": ("Connector_Generic:Conn_01x04", "MQ-2 module", FP_HDR.format(4),
           {"1": "+5V", "2": "GND", "3": NC, "4": "GAS_AO"}, "MQ-2 module: VCC, GND, DO (unused), AO"),
    "J3": ("Connector_Generic:Conn_01x04", "Flame module", FP_HDR.format(4),
           {"1": "+3V3", "2": "GND", "3": NC, "4": "FLAME_AO"}, "IR flame module: VCC, GND, DO (unused), AO"),
    "J4": ("Connector_Generic:Conn_01x03", "DHT22 module", FP_HDR.format(3),
           {"1": "+3V3", "2": "DHT_DATA", "3": "GND"}, "DHT22/AM2302: VCC, DATA, GND"),
    "J5": ("Connector_Generic:Conn_01x02", "5V IN", "TerminalBlock:TerminalBlock_bornier-2_P5.08mm",
           {"1": "VIN", "2": "GND"}, "External 5 V input (adapter or 18650 + TP4056 + MT3608 module)"),
    "J6": ("Connector_Generic:Conn_01x02", "VBAT sense", FP_HDR.format(2),
           {"1": "VBAT", "2": "GND"}, "Battery cell voltage for monitoring"),
    "JP1": ("Jumper:Jumper_2_Open", "5V_LINK", FP_HDR.format(2),
            {"1": "+5V", "2": "DEVKIT_5V"}, "Fit only when the DevKit USB cable is NOT connected"),
    "D1": ("Device:LED", "RED hazard", "LED_THT:LED_D5.0mm", {"1": "GND", "2": "LED_R_A"}, "5 mm red LED"),
    "D2": ("Device:LED", "GREEN healthy", "LED_THT:LED_D5.0mm", {"1": "GND", "2": "LED_G_A"}, "5 mm green LED"),
    "D3": ("Device:LED", "YELLOW fault", "LED_THT:LED_D5.0mm", {"1": "GND", "2": "LED_Y_A"}, "5 mm yellow LED"),
    "D4": ("Device:D", "1N4148", "Diode_THT:D_DO-35_SOD27_P7.62mm_Horizontal", {"1": "+5V", "2": "BUZ_N"},
           "Buzzer flyback diode"),
    "D5": ("Device:D_Schottky", "1N5819", "Diode_THT:D_DO-41_SOD81_P10.16mm_Horizontal", {"1": "+5V", "2": "VIN"},
           "Reverse-polarity protection"),
    "Q1": ("Transistor_BJT:PN2222A", "PN2222A", "Package_TO_SOT_THT:TO-92_Inline",
           {"1": "GND", "2": "Q1_B", "3": "BUZ_N"}, "NPN buzzer driver, TO-92 E-B-C"),
    "BZ1": ("Device:Buzzer", "Buzzer 5V", "Buzzer_Beeper:Buzzer_12x9.5RM7.6", {"1": "+5V", "2": "BUZ_N"},
            "Active 5 V buzzer, 12 mm"),
    "R1": ("Device:R", "12k", FP_R, {"1": "GAS_AO", "2": "GAS_ADC"}, "MQ-2 divider, top"),
    "R2": ("Device:R", "10k", FP_R, {"1": "GAS_ADC", "2": "GND"}, "MQ-2 divider, bottom; open-wire pull-down"),
    "R3": ("Device:R", "220", FP_R, {"1": "LED_RED", "2": "LED_R_A"}, "LED current limit"),
    "R4": ("Device:R", "220", FP_R, {"1": "LED_GREEN", "2": "LED_G_A"}, "LED current limit"),
    "R5": ("Device:R", "220", FP_R, {"1": "LED_YELLOW", "2": "LED_Y_A"}, "LED current limit"),
    "R6": ("Device:R", "1k", FP_R, {"1": "BUZ_DRV", "2": "Q1_B"}, "Q1 base resistor"),
    "R7": ("Device:R", "10k", FP_R, {"1": "+3V3", "2": "DHT_DATA"}, "DHT22 pull-up (omit if module has one)"),
    "R8": ("Device:R", "100k", FP_R, {"1": "VBAT", "2": "VBAT_ADC"}, "Battery divider, top"),
    "R9": ("Device:R", "100k", FP_R, {"1": "VBAT_ADC", "2": "GND"}, "Battery divider, bottom"),
    "C1": ("Device:C_Polarized", "100uF 10V", "Capacitor_THT:CP_Radial_D6.3mm_P2.50mm", {"1": "+5V", "2": "GND"},
           "5 V bulk"),
    "C2": ("Device:C_Polarized", "10uF 10V", "Capacitor_THT:CP_Radial_D5.0mm_P2.00mm", {"1": "+3V3", "2": "GND"},
           "RFM95W bulk"),
    "C3": ("Device:C", "100nF", FP_C, {"1": "+3V3", "2": "GND"}, "RFM95W decoupling"),
    "C4": ("Device:C", "100nF", FP_C, {"1": "GAS_ADC", "2": "GND"}, "ADC input filter"),
    "C5": ("Device:C", "100nF", FP_C, {"1": "FLAME_AO", "2": "GND"}, "ADC input filter"),
    "C6": ("Device:C", "100nF", FP_C, {"1": "+3V3", "2": "GND"}, "DHT22 decoupling"),
    "C7": ("Device:C", "100nF", FP_C, {"1": "VBAT_ADC", "2": "GND"}, "ADC input filter"),
    "H1": ("Mechanical:MountingHole", "M3", "MountingHole:MountingHole_3.2mm_M3", {}, "Mounting hole"),
    "H2": ("Mechanical:MountingHole", "M3", "MountingHole:MountingHole_3.2mm_M3", {}, "Mounting hole"),
    "H3": ("Mechanical:MountingHole", "M3", "MountingHole:MountingHole_3.2mm_M3", {}, "Mounting hole"),
    "H4": ("Mechanical:MountingHole", "M3", "MountingHole:MountingHole_3.2mm_M3", {}, "Mounting hole"),
}
POWER_NETS = {"+5V": "power:+5V", "+3V3": "power:+3V3", "GND": "power:GND"}
FLAG_NETS = ["+5V", "GND"]  # PWR_FLAG for ERC (VIN feeds +5V through D5)


def nets():
    out = {}
    for ref, (_, _, _, pins, _) in PARTS.items():
        for p, n in pins.items():
            if n:
                out.setdefault(n, []).append((ref, p))
    return out
