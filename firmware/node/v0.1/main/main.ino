/*
 * SAFE-NODE firmware v0.1 - Arduino prototype (ESP32 Dev Module)
 * Team NEXUS, M2026-133 - VConnecTech Engineering Marathon 2026
 *
 * RECONSTRUCTED from the behaviour documented in the Technical Documentation
 * (Section IV-A, Tables VIII-IX) and the 22 Nov 2026 simulation log. The
 * simulated build is src/main.ino in the LabWired project
 * "final_slider_monitor"
 * (https://app.labwired.com/p/963436e296210af24c3ec1ab57304f01).
 * If you export that file, replace this one with it so the repository holds
 * the exact code that was tested (see README.md in this folder).
 *
 * This prototype keeps the known limitations AR-2..AR-10 on purpose, so it
 * matches the SIM-1..SIM-6 records. It is NOT fail-safe; firmware v1.0
 * (../v1.0) closes them. Do not use on real hardware except for bring-up.
 *
 * Hardware note: on PCB rev 1.0 the MQ-2 output is divided by 0.455
 * (R1 = 12k, R2 = 10k). Recalibrate the 1800-count threshold before using
 * this sketch on the board.
 *
 * SPDX-License-Identifier: MIT
 */

// ---------------- Pin map (Technical Documentation Table IV) ----------------
const int PIN_GAS_ADC    = 32;   // MQ-2 AO (slider in simulation)
const int PIN_FLAME_ADC  = 33;   // flame AO (slider in simulation)
const int PIN_DHT        = 4;    // DHT22 data (not read in v0.1, AR-3)
const int PIN_BUZZER     = 13;   // PN2222A base via resistor
const int PIN_LED_RED    = 14;   // hazard
const int PIN_LED_GREEN  = 16;   // healthy
const int PIN_LED_YELLOW = 17;   // fault

// ---------------- Thresholds ----------------
const int   GAS_THRESHOLD   = 1800;   // ADC counts (slider connected directly)
const int   FLAME_THRESHOLD = 1800;   // ADC counts (AR-9: polarity unverified)
const float TEMP_THRESHOLD  = 50.0;   // deg C

// ---------------- Timing ----------------
const unsigned long CYCLE_MS     = 200;    // diagnostics + status cycle
const unsigned long HEARTBEAT_MS = 1000;   // heartbeat line

// ---------------- State ----------------
int   gasValue      = 0;
int   flameValue    = 0;
bool  flameDetected = false;
float temperature   = 30.0;   // fixed variable; DHT22 not read (AR-3)
float humidity      = 50.0;

bool gasSensorFault   = false;
bool flameSensorFault = false;
bool tempSensorFault  = false;
bool systemFault      = false;
bool hazardDetected   = false;
bool gasHazard        = false;
bool flameHazard      = false;
bool tempHazard       = false;

bool redLedState    = false;
bool buzzerState    = false;

unsigned long lastCycle     = 0;
unsigned long lastHeartbeat = 0;
String inputBuffer = "";

// ---------------- Forward declarations ----------------
void runDiagnostics();
void detectHazards();
void updateOutputs();
void printSystemStatus();
void processCommand(String cmd);
void printHelp();
void runDemoSequence();

// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(PIN_GAS_ADC, INPUT);
  pinMode(PIN_FLAME_ADC, INPUT);
  pinMode(PIN_DHT, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_YELLOW, OUTPUT);

  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_LED_RED, LOW);
  digitalWrite(PIN_LED_YELLOW, LOW);

  Serial.println();
  Serial.println("==============================================");
  Serial.println("   SAFE-NODE  Fail-Safe Gas and Fire Monitor");
  Serial.println("   Team NEXUS  |  Firmware v0.1 (prototype)");
  Serial.println("==============================================");
  Serial.println("[BOOT] ESP32 initialized");

  // AR-2: these lines are printed without testing anything.
  Serial.println("[CHECK] Gas sensor ........ OK");
  Serial.println("[CHECK] Flame sensor ...... OK");
  Serial.println("[CHECK] Temperature sensor  OK");
  Serial.println("[CHECK] Outputs ........... OK");

  digitalWrite(PIN_LED_GREEN, HIGH);
  Serial.println("[SYSTEM] SAFE MODE ACTIVE");
  Serial.println("Type ? for test commands.");
}

// ============================================================================
void loop() {
  // Serial test commands (Table IX)
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (inputBuffer.length() > 0) {
        processCommand(inputBuffer);
        inputBuffer = "";
      }
    } else {
      inputBuffer += c;
    }
  }

  // Sample both analog inputs on every pass (AR-10: floods the log)
  gasValue   = analogRead(PIN_GAS_ADC);
  flameValue = analogRead(PIN_FLAME_ADC);
  flameDetected = flameValue >= FLAME_THRESHOLD;
  Serial.print("Gas ADC: ");
  Serial.print(gasValue);
  Serial.print(" | Flame ADC: ");
  Serial.print(flameValue);
  Serial.print(" | Flame detected: ");
  Serial.println(flameDetected ? "YES" : "NO");

  unsigned long now = millis();
  if (now - lastCycle >= CYCLE_MS) {
    lastCycle = now;
    runDiagnostics();
    detectHazards();
    updateOutputs();
    printSystemStatus();
  }

  // AR-6: fixed text, even during a hazard or fault
  if (now - lastHeartbeat >= HEARTBEAT_MS) {
    lastHeartbeat = now;
    Serial.println("[HEARTBEAT] System operating normally");
  }
}

// ============================================================================
// Sets systemFault if a fault flag is set or the gas value is outside 0-4095.
// AR-4: analogRead() can never leave 0-4095, so only UART flags raise faults.
void runDiagnostics() {
  bool gasRangeFault = (gasValue < 0 || gasValue > 4095);
  systemFault = gasSensorFault || flameSensorFault || tempSensorFault || gasRangeFault;

  Serial.println("---------- DIAGNOSTICS ----------");
  Serial.print("Gas sensor:   ");
  Serial.println(gasSensorFault ? "FAULT" : "OK");
  Serial.print("Flame sensor: ");
  Serial.println(flameSensorFault ? "FAULT" : "OK");
  Serial.print("Temp sensor:  ");
  Serial.println(tempSensorFault ? "FAULT" : "OK");
  Serial.print("System fault: ");
  Serial.println(systemFault ? "YES" : "NO");
}

// ============================================================================
// Hazard if gas >= 1800, flame >= 1800 or temperature >= 50 C; each channel
// is skipped while its fault flag is set. AR-5: single sample, no latch.
void detectHazards() {
  gasHazard   = !gasSensorFault   && gasValue >= GAS_THRESHOLD;
  flameHazard = !flameSensorFault && flameValue >= FLAME_THRESHOLD;
  tempHazard  = !tempSensorFault  && temperature >= TEMP_THRESHOLD;

  hazardDetected = gasHazard || flameHazard || tempHazard;

  if (gasHazard) {
    Serial.println("[ALERT] GAS LEAKAGE DETECTED");
  }
  if (flameHazard) {
    Serial.println("[ALERT] FLAME DETECTED");
  }
  if (tempHazard) {
    Serial.println("[ALERT] HIGH TEMPERATURE DETECTED");
  }
}

// ============================================================================
// Hazard: red LED + buzzer. Else fault: yellow LED. Else: green LED.
// Hazard takes priority over fault.
void updateOutputs() {
  if (hazardDetected) {
    digitalWrite(PIN_LED_GREEN, LOW);
    digitalWrite(PIN_LED_YELLOW, LOW);
    digitalWrite(PIN_LED_RED, HIGH);
    digitalWrite(PIN_BUZZER, HIGH);
    if (!redLedState) {
      Serial.println("[OUTPUT] RED LED ON");
    }
    if (!buzzerState) {
      Serial.println("[OUTPUT] BUZZER ON");
    }
    redLedState = true;
    buzzerState = true;
  } else {
    if (redLedState) {
      Serial.println("[OUTPUT] RED LED OFF");
    }
    if (buzzerState) {
      Serial.println("[OUTPUT] BUZZER OFF");
    }
    digitalWrite(PIN_LED_RED, LOW);
    digitalWrite(PIN_BUZZER, LOW);
    redLedState = false;
    buzzerState = false;

    if (systemFault) {
      digitalWrite(PIN_LED_GREEN, LOW);
      digitalWrite(PIN_LED_YELLOW, HIGH);
    } else {
      digitalWrite(PIN_LED_YELLOW, LOW);
      digitalWrite(PIN_LED_GREEN, HIGH);
    }
  }
}

// ============================================================================
void printSystemStatus() {
  Serial.println("------------ STATUS -------------");
  Serial.print("Gas: ");
  Serial.print(gasValue);
  Serial.print(" | Flame: ");
  Serial.print(flameValue);
  Serial.print(" | Temperature: ");
  Serial.print(temperature, 0);
  Serial.print(" C | Humidity: ");
  Serial.print(humidity, 0);
  Serial.println(" %");
  Serial.print("Hazard: ");
  Serial.print(hazardDetected ? "YES" : "NO");
  Serial.print(" | System Fault: ");
  Serial.println(systemFault ? "YES" : "NO");
  Serial.println("---------------------------------");
}

// ============================================================================
// G<n> F<n> T<n> H<n> set values; X/Y/Z toggle fault flags; R resets; ? help.
void processCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) {
    return;
  }
  char c = toupper(cmd.charAt(0));
  String arg = cmd.substring(1);

  switch (c) {
    case 'G':
      gasValue = arg.toInt();   // overwritten at the next sample
      Serial.print("[TEST] Gas value set to ");
      Serial.println(gasValue);
      break;
    case 'F':
      flameDetected = arg.toInt() != 0;
      Serial.print("[TEST] Flame detected flag set to ");
      Serial.println(flameDetected ? 1 : 0);
      break;
    case 'T':
      temperature = arg.toFloat();
      Serial.print("[TEST] Temperature set to ");
      Serial.println(temperature);
      break;
    case 'H':
      humidity = arg.toFloat();
      Serial.print("[TEST] Humidity set to ");
      Serial.println(humidity);
      break;
    case 'X':
      gasSensorFault = !gasSensorFault;
      Serial.print("[TEST] Gas sensor fault ");
      Serial.println(gasSensorFault ? "SET" : "CLEARED");
      break;
    case 'Y':
      flameSensorFault = !flameSensorFault;
      Serial.print("[TEST] Flame sensor fault ");
      Serial.println(flameSensorFault ? "SET" : "CLEARED");
      break;
    case 'Z':
      tempSensorFault = !tempSensorFault;
      Serial.print("[TEST] Temperature sensor fault ");
      Serial.println(tempSensorFault ? "SET" : "CLEARED");
      break;
    case 'R':
      gasSensorFault = flameSensorFault = tempSensorFault = false;
      systemFault = hazardDetected = false;
      temperature = 30.0;
      humidity = 50.0;
      Serial.println("[RECOVERY] All values and fault flags reset");
      break;
    case '?':
      printHelp();
      break;
    default:
      Serial.print("[TEST] Unknown command: ");
      Serial.println(cmd);
      printHelp();
      break;
  }
}

void printHelp() {
  Serial.println("Commands:");
  Serial.println("  G<n>  set gas value (counts)      F<n>  set flame flag (0/1)");
  Serial.println("  T<n>  set temperature (C)         H<n>  set humidity (%)");
  Serial.println("  X / Y / Z  toggle gas / flame / temperature fault");
  Serial.println("  R  reset all values and flags     ?  this help");
}

// ============================================================================
// Five-step demo cycle. Defined but not called (Table VIII).
void runDemoSequence() {
  Serial.println("[DEMO] 1/5 normal");
  gasValue = 500;  flameValue = 300;  temperature = 28.0;
  detectHazards(); updateOutputs(); delay(2000);

  Serial.println("[DEMO] 2/5 gas leak");
  gasValue = 2500;
  detectHazards(); updateOutputs(); delay(2000);

  Serial.println("[DEMO] 3/5 flame");
  gasValue = 500;  flameValue = 3000;
  detectHazards(); updateOutputs(); delay(2000);

  Serial.println("[DEMO] 4/5 sensor fault");
  flameValue = 300; gasSensorFault = true;
  runDiagnostics(); detectHazards(); updateOutputs(); delay(2000);

  Serial.println("[DEMO] 5/5 recovery");
  gasSensorFault = false;
  runDiagnostics(); detectHazards(); updateOutputs();
}
