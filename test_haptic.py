import socket
import time
import msvcrt
import sys

# Konfiguration
UDP_IP = "192.168.4.1"
UDP_PORT = 4444

print(f"--- Haptic Rotary Control Client ---")
print(f"Ziel: {UDP_IP}:{UDP_PORT}")
print("Steuerung:")
print(" [s] Steps an/aus toggeln")
print(" [q] Beenden")
print("------------------------------------")

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setblocking(False)  # Non-blocking für flüssige Loop

# Handshake
print("Sende Handshake...")
sock.sendto(b"Hello", (UDP_IP, UDP_PORT))

step_mode = True

try:
    while True:
        # 1. Empfangen (Non-blocking)
        try:
            data, addr = sock.recvfrom(1024)
            text = data.decode('utf-8').strip()

            # Format "Winkel,Velocity" parsen
            if "," in text:
                parts = text.split(',')
                if len(parts) >= 2:
                    angle = float(parts[0])
                    velocity = float(parts[1])
                    print(f"\rAng: {angle:6.3f} | Vel: {velocity:6.3f} | Steps: {'ON ' if step_mode else 'OFF'}  ", end="")
        except BlockingIOError:
            pass  # Keine Daten da, einfach weiter machen
        except Exception as e:
            # Bei Windows kann error 10035 (WSAEWOULDBLOCK) kommen, das ist normal bei non-blocking
            if hasattr(e, 'errno') and e.errno == 10035:
                pass
            else:
                pass
                # print(f"\nNetzwerkfehler: {e}")

        # 2. Senden (Tastaturabfrage)
        if msvcrt.kbhit():
            key = msvcrt.getch()

            if key == b'q':
                break

            elif key == b's':
                step_mode = not step_mode
                cmd = f"STEPS:{1 if step_mode else 0}"
                sock.sendto(cmd.encode(), (UDP_IP, UDP_PORT))
                # Kleines Feedback, damit man sieht dass gedrückt wurde (überschreibt Zeile kurz)
                print(f"\n-> Sende: {cmd}")

        time.sleep(0.001)  # CPU schonen

except KeyboardInterrupt:
    pass

print("\nBeendet.")
sock.close()
