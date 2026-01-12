import socket
import time

# Konfiguration
UDP_IP = "192.168.4.1"  # Standard IP wenn du mit dem ESP32-WLAN verbunden bist
UDP_PORT = 4444

print(f"--- Haptic Rotary Test Client ---")
print(f"Ziel IP: {UDP_IP}")
print(f"Port:    {UDP_PORT}")

# Socket erstellen
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(1.0)  # 1 Sekunde Timeout

# Initiales Paket senden um beim ESP32 als Empfänger registriert zu werden
print("Sende Handshake...")
sock.sendto(b"Hello ESP32", (UDP_IP, UDP_PORT))

print("Warte auf Daten (Strg+C zum Beenden)...")

try:
    while True:
        try:
            # Daten empfangen
            data, addr = sock.recvfrom(1024)
            text = data.decode('utf-8').strip()

            # Erwartetes Format: "Winkel,Velocity"
            if "," in text:
                parts = text.split(',')
                if len(parts) >= 2:
                    angle = float(parts[0])
                    velocity = float(parts[1])

                    # Live-Ausgabe in einer Zeile
                    print(f"\rEmpfangen -> Winkel: {angle:6.3f} rad  |  Velocity: {velocity:6.3f} rad/s   ", end="")
            else:
                print(f"\rRaw: {text}")

        except socket.timeout:
            print("\nKeine Daten... sende erneuten Handshake...")
            sock.sendto(b"Ping", (UDP_IP, UDP_PORT))

except KeyboardInterrupt:
    print("\nTest beendet.")
finally:
    sock.close()
