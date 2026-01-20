import tkinter as tk
from tkinter import ttk
import socket
import threading
import time
import math


class HapticGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("Haptic Rotary Control")
        # Etwas größeres Fenster für die Visualisierung
        self.root.geometry("350x500")

        # Konfiguration (aus original test_haptic.py)
        self.UDP_IP = "192.168.4.1"
        self.UDP_PORT = 4444
        self.running = True
        self.step_mode = True

        # Daten-Variablen (Thread-safe genug für einfache Floats in Python GIL)
        self.current_angle = 0.0
        self.current_velocity = 0.0

        # UI Setup
        self.setup_ui()

        # Key Bindings
        self.root.bind('s', lambda e: self.toggle_steps())
        self.root.bind('q', lambda e: self.on_closing())

        # Netzwerk Setup
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.sock.settimeout(0.1)

            # Handshake senden
            print("Sende Handshake...")
            self.sock.sendto(b"Hello", (self.UDP_IP, self.UDP_PORT))
            self.status_var.set(f"Verbunden mit {self.UDP_IP}:{self.UDP_PORT}")
        except Exception as e:
            self.status_var.set(f"Socket Fehler: {e}")

        # Start Empfangs-Thread
        self.thread = threading.Thread(target=self.receive_loop)
        self.thread.daemon = True
        self.thread.start()

        # Start UI Update Loop
        # (Tkinter Canvas updates müssen im Main-Thread sein)
        self.update_gui_loop()

    def setup_ui(self):
        # Hauptrahmen
        main_frame = ttk.Frame(self.root, padding="15")
        main_frame.pack(fill=tk.BOTH, expand=True)

        # 1. Visualisierung (Kreisdiagramm / Gauge)
        vis_frame = ttk.LabelFrame(main_frame, text="Visualisierung", padding="5")
        vis_frame.pack(fill=tk.BOTH, expand=True, pady=(0, 10))

        # Canvas für das Zeichnen
        self.canvas = tk.Canvas(vis_frame, width=220, height=220, bg="#f5f5f5")
        self.canvas.pack(pady=5)

        # 2. Werte Anzeige
        info_frame = ttk.LabelFrame(main_frame, text="Sensor Daten", padding="10")
        info_frame.pack(fill=tk.X, pady=(0, 10))

        # Angle
        ttk.Label(info_frame, text="Winkel:").grid(row=0, column=0, sticky=tk.W)
        self.angle_var = tk.StringVar(value="0.000")
        ttk.Label(info_frame, textvariable=self.angle_var, font=("Consolas", 12, "bold")).grid(row=0, column=1, padx=10, sticky=tk.W)

        # Velocity
        ttk.Label(info_frame, text="Geschw.:").grid(row=1, column=0, sticky=tk.W)
        self.vel_var = tk.StringVar(value="0.000")
        ttk.Label(info_frame, textvariable=self.vel_var, font=("Consolas", 12)).grid(row=1, column=1, padx=10, sticky=tk.W)

        # Steps Indicator Variable
        self.steps_var = tk.StringVar(value="Steps: AN")

        # 3. Controls
        cmd_frame = ttk.Frame(main_frame)
        cmd_frame.pack(fill=tk.X)
        self.btn_steps = ttk.Button(cmd_frame, textvariable=self.steps_var, command=self.toggle_steps)
        self.btn_steps.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(0, 2))

        ttk.Button(cmd_frame, text="Beenden (q)", command=self.on_closing).pack(side=tk.RIGHT, fill=tk.X, expand=True, padx=(2, 0))

        # Status Bar
        self.status_var = tk.StringVar(value="Starte...")
        status_label = ttk.Label(self.root, textvariable=self.status_var, relief=tk.SUNKEN, anchor=tk.W, font=("Arial", 8))
        status_label.pack(side=tk.BOTTOM, fill=tk.X)

    def draw_gauge(self):
        """Zeichnet das Kreisdiagramm für Winkel und Geschwindigkeit"""
        try:
            self.canvas.delete("all")
            w, h = 220, 220
            cx, cy = w/2, h/2
            r_outer = 90

            # --- Basis-Kreis (Rahmen) ---
            self.canvas.create_oval(cx-r_outer, cy-r_outer, cx+r_outer, cy+r_outer,
                                    outline="#888", width=2, fill="white")

            # Statische Ticks (optional, für Orientierung)
            for i in range(0, 360, 30):
                rad = math.radians(i)
                # kleiner Tick
                x1 = cx + (r_outer - 5) * math.cos(rad)
                y1 = cy + (r_outer - 5) * math.sin(rad)
                x2 = cx + r_outer * math.cos(rad)
                y2 = cy + r_outer * math.sin(rad)
                self.canvas.create_line(x1, y1, x2, y2, fill="#aaa")

            # --- Visualisierung Velocity (Farbiger Kern) ---
            # Ein Kreis im Zentrum, der je nach Geschwindigkeit wächst und die Farbe ändert.
            # Rot = CW (+), Blau = CCW (-)
            max_vel_display = 20.0  # Velocity Bereich für volle Größe
            vel_abs = abs(self.current_velocity)
            vel_clamped = min(vel_abs, max_vel_display)

            if vel_clamped > 0.1:
                # Radius skalieren (bis zu 80% des Gesamtradius)
                r_vel = (vel_clamped / max_vel_display) * (r_outer * 0.8)
                r_vel = max(r_vel, 8)  # Mindestgröße damit man was sieht

                if self.current_velocity > 0:
                    fill_col = "#ffcccc"  # Hellrot
                    out_col = "#ff4444"
                else:
                    fill_col = "#ccccff"  # Hellblau
                    out_col = "#4444ff"

                # Zeichne den 'Velocity Blob'
                self.canvas.create_oval(cx-r_vel, cy-r_vel, cx+r_vel, cy+r_vel,
                                        fill=fill_col, outline=out_col, width=2)

            # --- Visualisierung Winkel (Zeiger) ---
            # Wir nehmen an self.current_angle ist in Radians.
            # cos/sin erwarten Radians.
            # Invertierte Drehrichtung
            draw_angle = -self.current_angle
            x_needle = cx + (r_outer - 2) * math.cos(draw_angle)
            y_needle = cy + (r_outer - 2) * math.sin(draw_angle)

            # Zeigerlinie
            self.canvas.create_line(cx, cy, x_needle, y_needle, width=3, fill="#333", arrow=tk.LAST)
            # Mittelpunkt
            self.canvas.create_oval(cx-5, cy-5, cx+5, cy+5, fill="#333", outline="")

        except Exception:
            pass

    def update_gui_loop(self):
        if not self.running:
            return

        # 1. UI Text-Felder updaten
        self.angle_var.set(f"{self.current_angle:.3f}")
        self.vel_var.set(f"{self.current_velocity:.3f}")

        # 2. Canvas neu zeichnen
        self.draw_gauge()

        # Loop: 20 FPS (alle 50ms)
        self.root.after(50, self.update_gui_loop)

    def toggle_steps(self):
        self.step_mode = not self.step_mode
        state = 1 if self.step_mode else 0
        cmd = f"STEPS:{state}"

        try:
            self.sock.sendto(cmd.encode(), (self.UDP_IP, self.UDP_PORT))
            self.steps_var.set(f"Steps: {'AN' if self.step_mode else 'AUS'}")
            print(f"-> Sende: {cmd}")
        except Exception as e:
            self.status_var.set(f"Sende-Fehler: {e}")

    def receive_loop(self):
        while self.running:
            try:
                data, addr = self.sock.recvfrom(1024)
                text = data.decode('utf-8').strip()

                if "," in text:
                    parts = text.split(',')
                    if len(parts) >= 2:
                        try:
                            # Daten parsen und in Variablen speichern
                            # UI Updates werden separat im Main-Thread gemacht (thread-safety)
                            self.current_angle = float(parts[0])
                            self.current_velocity = float(parts[1])
                        except ValueError:
                            pass
            except socket.timeout:
                pass
            except Exception as e:
                # Fehler ignorieren um Console nicht zu spammen, oder loggen
                pass

            time.sleep(0.001)

    def on_closing(self):
        self.running = False
        try:
            self.sock.close()
        except:
            pass
        self.root.destroy()


if __name__ == "__main__":
    root = tk.Tk()
    app = HapticGUI(root)
    root.protocol("WM_DELETE_WINDOW", app.on_closing)
    root.mainloop()
