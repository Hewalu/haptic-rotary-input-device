#include <SimpleFOC.h>
#include <WiFi.h>
#include <WiFiUdp.h>

// Konfiguration muss identisch zur main.cpp sein
const char *ssid = "Haptic Rotary Device";
const char *password = "Password123456";
WiFiUDP udp;
const int localPort = 4444;
const int TELEMETRY_INTERVAL_MS = 20; // Sende-Intervall in ms (50hz)

IPAddress controllerIP;
int controllerPort = 0;
bool controllerConnected = false;
const int LED_PIN = 2; // Onboard LED des ESP32

// --- Hardware Konfiguration ---
BLDCMotor motor = BLDCMotor(7);
// Deine Pins: 25, 26, 27 (PWM), 14 (Enable)
BLDCDriver3PWM driver = BLDCDriver3PWM(25, 26, 27, 14);

// Sensor an Standard VSPI Pins: CS=5, CLK=18, MISO=19, MOSI=23
MagneticSensorSPI sensor = MagneticSensorSPI(AS5048_SPI, 5);

const int FSR_PIN = 33;
const int FSR_THRESHOLD = 500;     // Schwellwert für Druckerkennung
const int HAPTIC_DURATION_MS = 20; // Dauer des haptischen Feedbacks
const int HAPTIC_VELOCITY = 10;    // Dauer des haptischen Feedbacks

// --- Limit Konfiguration ---
bool limitMode = true;               // Limits aktivieren/deaktivieren
float upperBound = 4.0;              // Oberes Limit (in Radian)
float lowerBound = -4.0;             // Unteres Limit (in Radian)
const float WALL_STIFFNESS = 30;     // Stärke der "Wand" (Federkraft)
const float WALL_MIN_VELOCITY = 2.0; // Mindestgeschwindigkeit für Rückstellung
float startAngle = 0;                // Start-Winkel offset

// --- Step Konfiguration ---
bool stepMode = true;
float stepAngle = 0.52;           // ~30 Grad (PI / 6)
const float STEP_STIFFNESS = 8.0; // Rasterung Härte

void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("--- Starte Setup ---");

  pinMode(FSR_PIN, INPUT);

  // 1. Sensor initialisieren
  sensor.init();

  // Verbindung Motor <-> Sensor
  motor.linkSensor(&sensor);

  // 2. Treiber initialisieren
  driver.voltage_power_supply = 12;
  driver.init();
  motor.linkDriver(&driver);

  // 3. Motor Parameter
  // Reduziertes Limit damit der Motor im Stillstand nicht heiß wird (Normalbetrieb)
  motor.voltage_limit = 1.0;
  motor.phase_resistance = 5.57;

  // Regelung: Velocity
  motor.controller = MotionControlType::velocity;

  // PID Werte (müssen evtl. angepasst werden, aber 0.2/2.0 ist okay für den Start)
  motor.PID_velocity.P = 0.2;
  motor.PID_velocity.I = 2.0;
  motor.LPF_velocity.Tf = 0.01;

  // 4. FOC Initialisierung
  Serial.println("Initialisiere FOC...");
  motor.init();

  // WICHTIG: Das hier führt die Kalibrierung durch
  // Achte im Serial Monitor auf "MOT: Success!" oder "MOT: Failed!"
  motor.initFOC();
  motor.disable();

  // Aktuelle Position als Nullpunkt setzen
  delay(100);
  startAngle = sensor.getAngle();

  // WiFi Setup
  WiFi.softAP(ssid, password);
  WiFi.setSleep(false); // Wichtig für Latenz

  if (udp.begin(localPort))
  {
    Serial.printf("UDP Server gestartet auf Port %d\n", localPort);
    Serial.print("PC bitte verbinden mit WLAN: ");
    Serial.println(ssid);
  }
  else
  {
    Serial.println("UDP Fehler beim Starten!");
  }

  Serial.println("Setup fertig.");
}

void loop()
{
  // 1. WICHTIGSTE ZEILE: Muss so schnell wie möglich laufen!
  motor.loopFOC();

  // Sensorwerte lesen (Relativ zum Start!)
  float currentAngle = sensor.getAngle() - startAngle;
  int fsrValue = analogRead(FSR_PIN);

  // Variablen für Action-Logic
  bool motorActive = false;
  float targetVelocity = 0;
  float currentVoltageLimit = 1.0;

  // --- STATEMACHINE: Haptischer Klick ---
  static int hapticState = 0; // 0=Idle, 1=Impuls Hin, 2=Impuls Zurück, 3=Cooldown
  static unsigned long stateTimer = 0;
  static float preClickAngle = 0; // Winkel vor dem Klick speichern
  const int FSR_RESET_LEVEL = 100;

  // Zustandsübergänge prüfen
  switch (hapticState)
  {
  case 0: // IDLE
    if (fsrValue > FSR_THRESHOLD)
    {
      hapticState = 1;
      stateTimer = millis();
      preClickAngle = currentAngle; // Position speichern
    }
    break;
  case 1: // CLICK PHASE 1
    if (millis() - stateTimer > HAPTIC_DURATION_MS)
    {
      hapticState = 2;
      stateTimer = millis();
    }
    break;
  case 2: // CLICK PHASE 2
    if (millis() - stateTimer > HAPTIC_DURATION_MS)
    {
      hapticState = 3;
    }
    break;
  case 3: // COOLDOWN
    if (fsrValue < FSR_RESET_LEVEL)
    {
      hapticState = 0;
      // Offset korrigieren: Der Code-Winkel soll identisch zum preClickAngle sein
      // currentAngle = sensor.getAngle() - startAngle  ==>  wir wollen currentAngle = preClickAngle
      // ==> preClickAngle = sensor.getAngle() - newStartAngle
      // ==> newStartAngle = sensor.getAngle() - preClickAngle
      startAngle = sensor.getAngle() - preClickAngle;
    }
    break;
  }

  // --- ARBITRATION: Wer darf den Motor steuern? ---

  // 1. Priorität: Haptischer Klick (Überschreibt alles)
  if (hapticState == 1)
  {
    motorActive = true;
    currentVoltageLimit = 5.0; // Volle Power
    targetVelocity = HAPTIC_VELOCITY;
  }
  else if (hapticState == 2)
  {
    motorActive = true;
    currentVoltageLimit = 5.0; // Volle Power
    targetVelocity = -HAPTIC_VELOCITY;
  }
  // 2. Priorität: Virtual Walls (Nur wenn kein Klick aktiv)
  else if (limitMode)
  {
    static bool recoveringUpper = false;
    static bool recoveringLower = false;
    const float WALL_HYSTERESIS = 0.01; // Pufferzone: 0.5 Rad (~28 Grad)

    // --- State-Erkennung mit Hysterese ---

    // oberes Limit
    if (currentAngle > upperBound)
      recoveringUpper = true;
    if (recoveringUpper && currentAngle < (upperBound - WALL_HYSTERESIS))
      recoveringUpper = false;

    // unteres Limit
    if (currentAngle < lowerBound)
      recoveringLower = true;
    if (recoveringLower && currentAngle > (lowerBound + WALL_HYSTERESIS))
      recoveringLower = false;

    // --- Aktionen ---
    if (recoveringUpper)
    {
      motorActive = true;
      currentVoltageLimit = 4.0;

      // Ziel: Nicht nur zum Limit, sondern tief in die sichere Zone
      float targetPos = upperBound - WALL_HYSTERESIS;

      // Fehler = Target - Current. Motor invertiert -> Positives Vorzeichen für negativen Fehler
      // Wenn wir bei 5 sind, Target 3.5. Diff = -1.5. -> Velocity muss + sein um Winkel zu verringern
      float velocity = -(targetPos - currentAngle) * WALL_STIFFNESS;

      // Mindest-Geschwindigkeit
      if (velocity < 0 && velocity > -WALL_MIN_VELOCITY)
        velocity = -WALL_MIN_VELOCITY;
      else if (velocity > 0 && velocity < WALL_MIN_VELOCITY)
        velocity = WALL_MIN_VELOCITY;

      targetVelocity = velocity;
    }
    else if (recoveringLower)
    {
      motorActive = true;
      currentVoltageLimit = 4.0;

      // Ziel: Tief in die sichere Zone
      float targetPos = lowerBound + WALL_HYSTERESIS;

      float velocity = -(targetPos - currentAngle) * WALL_STIFFNESS;

      // Mindest-Geschwindigkeit
      if (velocity < 0 && velocity > -WALL_MIN_VELOCITY)
        velocity = -WALL_MIN_VELOCITY;
      else if (velocity > 0 && velocity < WALL_MIN_VELOCITY)
        velocity = WALL_MIN_VELOCITY;

      targetVelocity = velocity;
    }

    // --- 3. Step Logic (Nur wenn NICHT an der Wand) ---
    if (!motorActive && stepMode)
    {
      static bool inScrollMove = false;
      float stepTarget = round(currentAngle / stepAngle) * stepAngle;

      // SICHERHEIT: Verhindere, dass ein Step genau auf oder hinter der Wand liegt (Konflikt!)
      // Wir nehmen den letzten gültigen Step VOR der Wand.
      float safeUpper = upperBound - 0.1; // 0.1 Rad Abstand zur Wand halten
      float safeLower = lowerBound + 0.1;

      if (stepTarget > safeUpper)
        stepTarget = floor(safeUpper / stepAngle) * stepAngle;
      if (stepTarget < safeLower)
        stepTarget = ceil(safeLower / stepAngle) * stepAngle;

      float diff = stepTarget - currentAngle;

      // Hysterese damit der Motor nicht zittert
      const float STEP_TRIGGER_ON = 0.05;  // Ab wann korrigieren? (~3 Grad)
      const float STEP_TRIGGER_OFF = 0.01; // Wann fertig? (~0.5 Grad)

      if (abs(diff) > STEP_TRIGGER_ON)
        inScrollMove = true;
      if (abs(diff) < STEP_TRIGGER_OFF)
        inScrollMove = false;

      if (inScrollMove)
      {
        motorActive = true;
        currentVoltageLimit = 2.0;

        // P-Regler hin zum Step (invertiert wie oben)
        targetVelocity = -diff * STEP_STIFFNESS;
      }
    }
  }

  // --- MOTOR ANSTEUERUNG ---
  motor.voltage_limit = currentVoltageLimit;

  if (motorActive)
  {
    motor.enable();
    motor.move(targetVelocity);
  }
  else
  {
    // Wenn nichts zu tun ist: Strom sparen / Freilauf
    motor.disable();
  }

  // 3. Debug Output (Gedrosselt)
  static unsigned long letzteDruckZeit = 0;
  if (millis() - letzteDruckZeit > 100)
  {
    Serial.printf("Ang: %.2f\t FSR: %d\n", currentAngle, fsrValue);
    letzteDruckZeit = millis();
  }

  // --- UDP Empfang & Senden ---
  int packetSize = udp.parsePacket();
  if (packetSize)
  {
    char packetBuff[255];
    int len = udp.read(packetBuff, 255);
    // Absender registrieren
    controllerIP = udp.remoteIP();
    controllerPort = udp.remotePort();
    controllerConnected = true;
  }

  static unsigned long lastTelemetry = 0;
  if (controllerConnected && (millis() - lastTelemetry > TELEMETRY_INTERVAL_MS))
  {
    lastTelemetry = millis();
    udp.beginPacket(controllerIP, controllerPort);
    // Sende: Winkel,Geschwindigkeit (Kommagetrennt)
    udp.printf("%.4f,%.4f", currentAngle, sensor.getVelocity());
    udp.endPacket();
  }
}