import socket
import time
import sys

# Konfiguration
UDP_IP = "192.168.4.1"  # ESP32 SoftAP IP
UDP_PORT = 4444

print(f"--- ESP32 Haptic Connection Test ---")
print(f"Target: {UDP_IP}:{UDP_PORT}")

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(2.0)  # 2 Sekunden Timeout

try:
    # 1. Handshake senden
    print("\n[1] Sending Handshake 'HELLO'...")
    sock.sendto(b"HELLO", (UDP_IP, UDP_PORT))

    # 2. Auf Antwort warten
    try:
        data, addr = sock.recvfrom(1024)
        print(f"✅ Received Reply: {data.decode('utf-8').strip()}")
        print("   -> Connection established!")
    except socket.timeout:
        print("❌ Timeout! No response. Are you connected to 'Haptic Rotary Device' WiFi?")
        sys.exit(1)

    # 3. Frequenz-Test (Stabilität)
    print("\n[2] Testing Stability (Sampling for 3 seconds)...")
    start_time = time.time()
    packets = 0
    last_print = 0

    while time.time() - start_time < 3.0:
        try:
            data, addr = sock.recvfrom(1024)
            packets += 1
            # Nur ab und zu drucken
            if time.time() - last_print > 0.5:
                print(f"   RX: {data.decode('utf-8').strip()}")
                last_print = time.time()
        except socket.timeout:
            print("   ⚠️ Packet Drop / Timeout")

    duration = time.time() - start_time
    rate = packets / duration
    print(f"Result: {packets} packets in {duration:.2f}s")
    print(f"Rate:   {rate:.2f} Hz (Target: ~20 Hz)")

    if rate > 15 and rate < 25:
        print("✅ RATE OK")
    else:
        print("⚠️ RATE UNUSUAL (Check interference or loop delay)")

    # 4. Filter Test (Müll senden)
    print("\n[3] Testing Protocol Filter (Sending Garbage Data)...")
    print("   Sending 'INVALID_JUNK_DATA' -> ESP32 should ignore it and stay alive.")
    sock.sendto(b"INVALID_JUNK_DATA", (UDP_IP, UDP_PORT))
    time.sleep(0.5)

    # Prüfen ob er noch da ist
    try:
        data, addr = sock.recvfrom(1024)
        print(f"✅ ESP32 is still alive! Last msg: {data.decode('utf-8').strip()}")
    except socket.timeout:
        print("❌ ESP32 died/stopped sending after receiving garbage!")

except KeyboardInterrupt:
    print("\nTest cancelled.")
finally:
    sock.close()
    print("\n--- Test Finished ---")
