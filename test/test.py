import socket
import cv2
import numpy as np
import threading
import time
from datetime import datetime

# === CONFIGURAÇÕES ===
BROADCAST_MSG = b"ESP_DISCOVERY"
RESPONSE_MSG = b"SERVER_OK"

CAMERAS = {
    8080: {"name": "Câmera Direita",  "side": "right"},
    8081: {"name": "Câmera Esquerda", "side": "left"}
}

stop_event = threading.Event()
frame_lock = threading.Lock()

latest_frames = {port: None for port in CAMERAS.keys()}
frame_timestamps = {port: 0.0 for port in CAMERAS.keys()}

# === FPS ===
fps_data = {
    port: {
        "frames": 0,
        "last_time": time.time(),
        "fps": 0.0
    } for port in CAMERAS.keys()
}


# === UTIL ===
def format_timestamp(ts):
    return datetime.fromtimestamp(ts).strftime("%Y%m%d_%H%M%S_%f")[:-3]


# === RECEPÇÃO UDP ===
def udp_receiver(port, camera_name, side, stop_event):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("", port))
    sock.settimeout(1.0)

    esp_ips = set()
    print(f"[SERVER] {camera_name} (porta {port}) aguardando conexão...")

    while not stop_event.is_set():
        try:
            data, addr = sock.recvfrom(65535)

            # DISCOVERY
            if data == BROADCAST_MSG:
                print(f"[DISCOVERY] {camera_name} conectada: {addr[0]}")
                sock.sendto(RESPONSE_MSG, addr)
                esp_ips.add(addr[0])
                continue

            # STREAM
            if addr[0] in esp_ips:
                frame = cv2.imdecode(
                    np.frombuffer(data, dtype=np.uint8),
                    cv2.IMREAD_COLOR
                )

                if frame is not None:
                    now = time.time()
                    with frame_lock:
                        latest_frames[port] = frame.copy()
                        frame_timestamps[port] = now

                        # FPS
                        fps_data[port]["frames"] += 1
                        dt = now - fps_data[port]["last_time"]
                        if dt >= 1.0:
                            fps_data[port]["fps"] = fps_data[port]["frames"] / dt
                            fps_data[port]["frames"] = 0
                            fps_data[port]["last_time"] = now

        except socket.timeout:
            continue
        except Exception as e:
            print(f"[ERRO] {camera_name}: {e}")

    sock.close()
    print(f"[SERVER] {camera_name} encerrada.")


# === DISPLAY ===
def display_thread(stop_event):
    window_name = "Visão Estéreo: Esquerda | Direita"
    cv2.namedWindow(window_name, cv2.WINDOW_NORMAL)
    cv2.resizeWindow(window_name, 1200, 480)

    print("\n[DISPLAY]")
    print("  q → sair")
    print("  s → salvar frames separados\n")

    while not stop_event.is_set():
        with frame_lock:
            frame_left = latest_frames[8081]
            frame_right = latest_frames[8080]
            fps_left = fps_data[8081]["fps"]
            fps_right = fps_data[8080]["fps"]

        if frame_left is None or frame_right is None:
            placeholder = np.zeros((240, 640, 3), np.uint8)
            cv2.putText(
                placeholder,
                "Aguardando ambas as câmeras...",
                (60, 120),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.9,
                (0, 0, 255),
                2
            )
            cv2.imshow(window_name, placeholder)
            cv2.waitKey(1)
            continue

        # Resize fixo
        frame_left_r = cv2.resize(frame_left, (320, 240))
        frame_right_r = cv2.resize(frame_right, (320, 240))

        canvas = np.zeros((240, 640, 3), dtype=np.uint8)
        canvas[:, :320] = frame_left_r
        canvas[:, 320:] = frame_right_r

        cv2.line(canvas, (320, 0), (320, 240), (255, 255, 255), 2)

        # Labels
        cv2.putText(canvas, "ESQUERDA", (10, 30),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.9, (0, 255, 0), 2)

        cv2.putText(canvas, "DIREITA", (330, 30),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.9, (0, 255, 0), 2)

        # FPS
        cv2.putText(canvas, f"{fps_left:.1f} FPS",
                    (200, 230),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 255), 2)

        cv2.putText(canvas, f"{fps_right:.1f} FPS",
                    (520, 230),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 255), 2)

        cv2.imshow(window_name, canvas)

        key = cv2.waitKey(1) & 0xFF

        if key == ord('q'):
            stop_event.set()

        elif key == ord('s'):
            with frame_lock:
                fl = latest_frames[8081]
                fr = latest_frames[8080]
                ts_l = frame_timestamps[8081]
                ts_r = frame_timestamps[8080]

            if fl is not None and fr is not None:
                name_l = f"{format_timestamp(ts_l)}_camera_esquerda.png"
                name_r = f"{format_timestamp(ts_r)}_camera_direita.png"

                cv2.imwrite(name_l, fl)
                cv2.imwrite(name_r, fr)

                print("[SALVO]")
                print(f"  → {name_l}")
                print(f"  → {name_r}")

    cv2.destroyAllWindows()
    print("[DISPLAY] Encerrado.")


# === THREADS ===
threads = []

for port, info in CAMERAS.items():
    t = threading.Thread(
        target=udp_receiver,
        args=(port, info["name"], info["side"], stop_event),
        daemon=True
    )
    t.start()
    threads.append(t)

display_t = threading.Thread(
    target=display_thread,
    args=(stop_event,),
    daemon=True
)
display_t.start()
threads.append(display_t)

try:
    while not stop_event.is_set():
        time.sleep(0.1)
except KeyboardInterrupt:
    print("\nInterrompido pelo usuário.")
finally:
    stop_event.set()
    for t in threads:
        t.join()

print("Servidor encerrado com sucesso.")