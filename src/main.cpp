#include <SimpleFOC.h>

// --- Hardware Konfiguration ---
BLDCMotor motor = BLDCMotor(7);
// Deine Pins: 25, 26, 27 (PWM), 14 (Enable)
BLDCDriver3PWM driver = BLDCDriver3PWM(25, 26, 27, 14);

// Sensor an Standard VSPI Pins: CS=5, CLK=18, MISO=19, MOSI=23
MagneticSensorSPI sensor = MagneticSensorSPI(AS5048_SPI, 5);

const int FSR_PIN = 33;
const int FSR_THRESHOLD = 500;     // Schwellwert für Druckerkennung
const int HAPTIC_DURATION_MS = 20; // Dauer des haptischen Feedbacks
const int HAPTIC_VELOCITY = 10;     // Dauer des haptischen Feedbacks

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
  Serial.println("Initialisiere FOC... (Motor zuckt kurz)");
  motor.init();

  // WICHTIG: Das hier führt die Kalibrierung durch
  // Achte im Serial Monitor auf "MOT: Success!" oder "MOT: Failed!"
  motor.initFOC();
  motor.disable();

  Serial.println("Setup fertig. Motor sollte drehen.");
}

void loop()
{
  // 1. WICHTIGSTE ZEILE: Muss so schnell wie möglich laufen!
  motor.loopFOC();

  // FSR dauerhaft lesen für schnelle Reaktion
  int fsrValue = analogRead(FSR_PIN);

  // Statische Variablen für den Zustand
  static int hapticState = 0; // 0=Idle, 1=Impuls Hin, 2=Impuls Zurück, 3=Warten auf Reset
  static unsigned long stateTimer = 0;
  const int FSR_RESET_LEVEL = 100;

  float targetVelocity = 0; // Standard: 0

  switch (hapticState)
  {
  case 0: // IDLE - Warten auf Auslöser
    if (fsrValue > FSR_THRESHOLD)
    {
      hapticState = 1;
      stateTimer = millis();
      motor.enable();          // Motor einschalten
      motor.voltage_limit = 5; // Volle Kraft für Klick
      targetVelocity = HAPTIC_VELOCITY;
    }
    break;

  case 1: // CLICK PHASE 1: Ausschlagen
    targetVelocity = HAPTIC_VELOCITY;
    if (millis() - stateTimer > HAPTIC_DURATION_MS)
    {
      hapticState = 2; // Weiter zu Phase 2
      stateTimer = millis();
    }
    break;

  case 2: // CLICK PHASE 2: Zurückschlagen
    targetVelocity = -HAPTIC_VELOCITY;
    if (millis() - stateTimer > HAPTIC_DURATION_MS)
    {
      hapticState = 3; // Fertig -> Cooldown
      motor.disable(); // Motor wieder aus
      motor.voltage_limit = 1.0;
    }
    break;

  case 3: // COOLDOWN / RESET: Warten bis Finger weg
    targetVelocity = 0;
    // Erst wieder scharfschalten, wenn Druck unter 100 fällt
    if (fsrValue < FSR_RESET_LEVEL)
    {
      hapticState = 0; // Wieder bereit
    }
    break;
  }

  // 2. Bewegung setzen
  motor.move(targetVelocity);

  // 3. Sensoren auslesen und Drucken (ABER GEDROSSELT!)
  // Wir nutzen "static", damit sich die Variable den Wert merkt
  static unsigned long letzteDruckZeit = 0;

  // Nur alle 100ms drucken (10 mal pro Sekunde), nicht öfter!
  if (millis() - letzteDruckZeit > 100)
  {
    // fsrValue ist schon aktuell
    float aktuellerWinkel = sensor.getAngle(); // Nur zur Info

    Serial.print("FSR: ");
    Serial.print(fsrValue);
    Serial.print("\t Winkel: ");
    Serial.println(aktuellerWinkel);

    letzteDruckZeit = millis();
  }
}