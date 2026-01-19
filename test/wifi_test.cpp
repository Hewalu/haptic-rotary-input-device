#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>

// Konfiguration muss identisch zur main.cpp sein
const char *ssid = "Haptic Rotary Device";
const char *password = "Password123456";
WiFiUDP udp;
const int localPort = 4444;

IPAddress controllerIP;
int controllerPort = 0;
bool controllerConnected = false;

// Dummy Variablen für Simulation
float simAngle = 0.0;
float simVelocity = 0.0;
int simClick = 0;

void setup()
{
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n--- WiFi Communication Test (No Motor) ---");

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
}

void loop()
{
    // 1. Verbindungs-Check
    if (WiFi.softAPgetStationNum() == 0)
    {
        if (controllerConnected)
        {
            Serial.println("[WIFI] Client hat Verbindung getrennt.");
            controllerConnected = false;
        }
    }

    // 2. Empfangen (mit striktem Filter gegen Error 12)
    int packetSize = udp.parsePacket();
    if (packetSize)
    {
        char packetBuffer[64];
        int len = udp.read(packetBuffer, 63);
        if (len > 0)
        {
            packetBuffer[len] = 0;

            // STRENGE FILTERUNG (exakt wie in main.cpp)
            bool validPacket = false;

            // Handshake "HELLO"
            if (strcmp(packetBuffer, "HELLO") == 0)
                validPacket = true;

            // Befehle (L:.., S:.., H:..) - müssen ':' an Stelle 1 haben
            else if (len >= 3 && packetBuffer[1] == ':' && (packetBuffer[0] == 'L' || packetBuffer[0] == 'S' || packetBuffer[0] == 'H'))
                validPacket = true;

            // Reset "R"
            else if (packetBuffer[0] == 'R')
                validPacket = true;

            if (validPacket)
            {
                if (!controllerConnected)
                {
                    Serial.printf("[UDP] Neuer Controller akzeptiert: %s:%d\n", udp.remoteIP().toString().c_str(), udp.remotePort());
                }

                controllerIP = udp.remoteIP();
                controllerPort = udp.remotePort();
                controllerConnected = true;

                // Debug Output
                Serial.printf("[RX] Befehl empfangen: %s\n", packetBuffer);
            }
            else
            {
                // Garbage ignorieren
            }
        }
    }

    // 3. Senden (Simulation)
    static unsigned long lastTelemetry = 0;
    static unsigned long lastErrorTime = 0;
    const int TELEMETRY_INTERVAL = 50;

    if (controllerConnected &&
        (millis() - lastTelemetry > TELEMETRY_INTERVAL) &&
        (millis() - lastErrorTime > 1000) &&
        WiFi.softAPgetStationNum() > 0)
    {
        lastTelemetry = millis();

        // Simuliere Bewegung (Sinusschlange)
        simAngle = sin(millis() / 1000.0) * 3.14;
        simVelocity = cos(millis() / 1000.0);

        // Simuliere Klick alle 2 Sekunden
        simClick = ((millis() / 2000) % 2) == 0 ? 1 : 0;

        char telemBuf[64];
        // T:Winkel:Geschwindigkeit:KlickStatus
        snprintf(telemBuf, sizeof(telemBuf), "T:%.3f:%.3f:%d", simAngle, simVelocity, simClick);

        // Senden versuchen mit Fehlerbehandlung
        if (udp.beginPacket(controllerIP, controllerPort))
        {
            udp.print(telemBuf);
            if (!udp.endPacket())
            {
                Serial.println("[TX Error] Fehler 12 - Warte 1s...");
                lastErrorTime = millis();
            }
        }
        else
        {
            lastErrorTime = millis();
        }
    }
}