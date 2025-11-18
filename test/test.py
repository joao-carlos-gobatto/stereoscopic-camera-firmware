import socket
import cv2
import numpy as np
import threading
import time

# === CONFIGURAÇÕES ===
BROADCAST_MSG = b"ESP_DISCOVERY"
RESPONSE_MSG = b"SERVER_OK"

# Portas e nomes + rotação
CAMERAS = {
    8080: {"name": "Câmera Direita",  "side": "right", "rotate": cv2.ROTATE_90_COUNTERCLOCKWISE}, # 270° direita = 90° anti-horário
    8081: {"name": "Câmera Esquerda", "side": "left",  "rotate": cv2.ROTATE_90_CLOCKWISE}      # 270° esquerda = 90° horário
}

stop_event = threading.Event()
frame_lock = threading.Lock()
latest_frames = {port: None for port in CAMERAS.keys()}
frame_timestamps = {port: 0.0 for port in CAMERAS.keys()}


def udp_receiver(port, camera_name, side, rotate_code, stop_event):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("", port))
    sock.settimeout(1.0)

    esp_ips = set()

    print(f"[SERVER] {camera_name} (porta {port}) — aguardando conexão...")

    while not stop_event.is_set():
        try:
            data, addr = sock.recvfrom(65535)

            # --- DISCOVERY ---
            if data == BROADCAST_MSG:
                print(f"[DISCOVERY] {camera_name} conectada: {addr[0]}")
                sock.sendto(RESPONSE_MSG, addr)
                esp_ips.add(addr[0])
                continue

            # --- STREAMING ---
            if addr[0] in esp_ips:
                frame_data = bytearray(data)
                frame = cv2.imdecode(np.frombuffer(frame_data, dtype=np.uint8), cv2.IMREAD_COLOR)

                if frame is not None:
                    # === APLICA ROTAÇÃO ===
                    frame = cv2.rotate(frame, rotate_code)

                    with frame_lock:
                        latest_frames[port] = frame.copy()
                        frame_timestamps[port] = time.time()
                else:
                    print(f"[WARN] Frame inválido ({camera_name}) de {addr[0]}")

        except socket.timeout:
            continue
        except Exception as e:
            print(f"[ERRO] {camera_name}: {e}")

    sock.close()
    print(f"[SERVER] {camera_name} encerrada.")


def display_thread(stop_event):
    window_name = "Visão Estéreo: Esquerda | Direita"
    cv2.namedWindow(window_name, cv2.WINDOW_NORMAL)
    cv2.resizeWindow(window_name, 1200, 480)

    print(f"\n[DISPLAY] Exibindo em uma única janela: '{window_name}'")
    print("   Pressione 'q' para sair.\n")

    while not stop_event.is_set():
        with frame_lock:
            frame_left = latest_frames[8081]
            frame_right = latest_frames[8080]

        # Só exibe se ambas as câmeras enviaram pelo menos um frame
        if frame_left is None or frame_right is None:
            placeholder = np.zeros((480, 1200, 3), np.uint8)
            cv2.putText(placeholder, "Aguardando ambas as câmeras...", (200, 240),
                        cv2.FONT_HERSHEY_SIMPLEX, 1.2, (0, 0, 255), 3)
            cv2.imshow(window_name, placeholder)
            if cv2.waitKey(1) & 0xFF == ord('q'):
                stop_event.set()
            time.sleep(0.1)
            continue

        # Redimensiona para mesma altura
        h_l, w_l = frame_left.shape[:2]
        h_r, w_r = frame_right.shape[:2]
        target_h = 480
        scale_l = target_h / h_l
        scale_r = target_h / h_r
        scale = min(scale_l, scale_r)

        new_w_l = int(w_l * scale)
        new_w_r = int(w_r * scale)

        frame_left = cv2.resize(frame_left, (new_w_l, target_h))
        frame_right = cv2.resize(frame_right, (new_w_r, target_h))

        # Cria canvas
        canvas_w = 720
        canvas = np.zeros((target_h, canvas_w, 3), dtype=np.uint8)

        # Coloca câmera esquerda na esquerda
        start_x_l = 0
        canvas[:target_h, start_x_l:start_x_l + new_w_l] = frame_left

        # Coloca câmera direita na direita
        start_x_r = canvas_w - new_w_r
        canvas[:target_h, start_x_r:start_x_r + new_w_r] = frame_right

        # Linha divisória
        cv2.line(canvas, (canvas_w // 2, 0), (canvas_w // 2, target_h), (255, 255, 255), 2)

        # Legendas
        cv2.putText(canvas, "ESQUERDA", (20, 30), cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 0), 2)
        cv2.putText(canvas, "DIREITA", (canvas_w - 220, 30), cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 0), 2)

        cv2.imshow(window_name, canvas)

        if cv2.waitKey(1) & 0xFF == ord('q'):
            stop_event.set()

    cv2.destroyAllWindows()
    print("[DISPLAY] Janela fechada.")


# === INICIAR THREADS ===
threads = []

# Threads de recepção com rotação
for port, info in CAMERAS.items():
    t = threading.Thread(
        target=udp_receiver,
        args=(port, info["name"], info["side"], info["rotate"], stop_event),
        daemon=True
    )
    t.start()
    threads.append(t)

# Thread de exibição
display_t = threading.Thread(target=display_thread, args=(stop_event,), daemon=True)
display_t.start()
threads.append(display_t)

# Aguarda 'q' ou interrupção
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