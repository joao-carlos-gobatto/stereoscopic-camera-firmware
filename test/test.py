#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Servidor UDP para streaming estéreo de duas ESP32-CAM (direita 8080 / esquerda 8081).
- Descobre câmeras via broadcast periódico (porta 12345).
- Exibe visão estéreo lado a lado.
- Teclas: q = sair, s = salvar frames atuais.
- Reconexão automática se câmera parar de enviar frames.
"""

import socket
import cv2
import os
import numpy as np
import threading
import time
from datetime import datetime
import warnings

# === CONFIGURAÇÕES GLOBAIS ===
BROADCAST_MSG = b"ESP_DISCOVERY"
BROADCAST_ADDR = "255.255.255.255"
BROADCAST_PORT = 12345

STREAM_PORT_RIGHT = 8080
STREAM_PORT_LEFT  = 8081

CAMERAS = {
    STREAM_PORT_RIGHT: {"name": "Câmera Direita",  "side": "right"},
    STREAM_PORT_LEFT:  {"name": "Câmera Esquerda", "side": "left"}
}

BROADCAST_INTERVAL = 2.0          # segundos entre broadcasts
CAMERA_TIMEOUT     = 12.0         # segundos sem frame → considera desconectada
DISPLAY_WINDOW     = "Visão Estéreo: Esquerda | Direita"
DISPLAY_SIZE       = (1200, 480)

# Variáveis compartilhadas (protegidas por lock)
stop_event = threading.Event()
frame_lock = threading.Lock()
latest_frames = {port: None for port in CAMERAS}
frame_timestamps = {port: 0.0 for port in CAMERAS}
fps_data = {port: {"frames": 0, "last_time": time.time(), "fps": 0.0} for port in CAMERAS}
connected_cameras = set()  # portas que receberam pelo menos 1 frame


def format_timestamp(ts: float) -> str:
    """Formata timestamp para nome de arquivo."""
    return datetime.fromtimestamp(ts).strftime("%Y%m%d_%H%M%S_%f")[:-3]


def send_broadcast():
    """Envia mensagem de broadcast periodicamente até ambas câmeras conectarem."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.settimeout(1.0)

    print("[BROADCAST] Iniciando envio periódico...")

    while not stop_event.is_set() and len(connected_cameras) < 2:
        try:
            sock.sendto(BROADCAST_MSG, (BROADCAST_ADDR, BROADCAST_PORT))
            print(f"[BROADCAST] Enviado ({len(connected_cameras)}/2 câmeras conectadas)")
        except Exception as e:
            print(f"[BROADCAST ERRO] {e}")
        time.sleep(BROADCAST_INTERVAL)

    print("[BROADCAST] Ambas câmeras conectadas ou parada → finalizando.")
    sock.close()


def stream_receiver(port: int, camera_name: str, side: str):
    """Thread que recebe frames UDP de uma câmera específica."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("", port))
    sock.settimeout(1.0)

    print(f"[STREAM {side.upper()}] Aguardando frames na porta {port}...")

    while not stop_event.is_set():
        try:
            data, addr = sock.recvfrom(65535)
            print(f"[RECEBIDO {side.upper()}] {len(data)} bytes de {addr}")

            frame = cv2.imdecode(np.frombuffer(data, np.uint8), cv2.IMREAD_COLOR)
            if frame is None:
                print(f"[ERRO DECODE {side.upper()}] JPEG inválido ou corrompido")
                continue

            now = time.time()
            with frame_lock:
                latest_frames[port] = frame.copy()
                frame_timestamps[port] = now

                # Atualiza FPS
                fps_data[port]["frames"] += 1
                dt = now - fps_data[port]["last_time"]
                if dt >= 1.0:
                    fps_data[port]["fps"] = fps_data[port]["frames"] / dt
                    fps_data[port]["frames"] = 0
                    fps_data[port]["last_time"] = now

            if port not in connected_cameras:
                print(f"[CONECTADA] {camera_name} ({addr[0]}) enviando frames")
                connected_cameras.add(port)

        except socket.timeout:
            continue
        except Exception as e:
            print(f"[ERRO {camera_name}] {e}")

    sock.close()
    print(f"[STREAM {side.upper()}] Encerrado.")


def watchdog():
    """Verifica periodicamente se alguma câmera parou de enviar frames."""
    while not stop_event.is_set():
        time.sleep(5.0)
        now = time.time()
        with frame_lock:
            for port in list(connected_cameras):
                if now - frame_timestamps[port] > CAMERA_TIMEOUT:
                    print(f"[DESCONEXÃO] Porta {port} sem frames há >{CAMERA_TIMEOUT}s → removendo")
                    connected_cameras.remove(port)
                    latest_frames[port] = None


def display_loop():
    picture_num = 0
    os.rmdir("./stereoLeft")
    os.rmdir("./stereoRight")
    os.makedirs("stereoLeft", exist_ok=True)
    os.makedirs("stereoRight", exist_ok=True)
    """Loop principal de exibição (deve rodar na thread principal para evitar warnings Qt)."""
    cv2.namedWindow(DISPLAY_WINDOW, cv2.WINDOW_NORMAL)
    cv2.resizeWindow(DISPLAY_WINDOW, *DISPLAY_SIZE)

    print("\n[DISPLAY] Clique na janela e use:")
    print("  q → sair")
    print("  s → salvar frames atuais\n")

    while not stop_event.is_set():
        with frame_lock:
            left_frame  = latest_frames[STREAM_PORT_LEFT]
            right_frame = latest_frames[STREAM_PORT_RIGHT]
            fps_left    = fps_data[STREAM_PORT_LEFT]["fps"]
            fps_right   = fps_data[STREAM_PORT_RIGHT]["fps"]

        if len(connected_cameras) < 2 or left_frame is None or right_frame is None:
            placeholder = np.zeros((240, 640, 3), dtype=np.uint8)
            msg = f"Aguardando câmeras... ({len(connected_cameras)}/2)"
            cv2.putText(placeholder, msg, (60, 120), cv2.FONT_HERSHEY_SIMPLEX, 0.9, (0, 180, 255), 2)
            cv2.imshow(DISPLAY_WINDOW, placeholder)
        else:
            # Resize para visualização
            left_resized  = cv2.resize(left_frame,  (320, 240))
            right_resized = cv2.resize(right_frame, (320, 240))

            # Canvas com linha divisória
            canvas = np.hstack([left_resized, right_resized])
            cv2.line(canvas, (320, 0), (320, 240), (200, 200, 200), 2)

            # Rótulos e FPS
            cv2.putText(canvas, "ESQUERDA", (10,  30), cv2.FONT_HERSHEY_SIMPLEX, 0.9, (0, 255, 100), 2)
            cv2.putText(canvas, "DIREITA",  (330, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.9, (0, 255, 100), 2)
            cv2.putText(canvas, f"FPS: {fps_left:.1f}",  (180, 220), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 255), 2)
            cv2.putText(canvas, f"FPS: {fps_right:.1f}", (500, 220), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 255), 2)

            cv2.imshow(DISPLAY_WINDOW, canvas)

        key = cv2.waitKey(10) & 0xFF
        if key == ord('q'):
            print("[KEY] 'q' pressionado → saindo")
            stop_event.set()
        elif key == ord('s'):
            with frame_lock:
                fl = latest_frames[STREAM_PORT_LEFT]
                fr = latest_frames[STREAM_PORT_RIGHT]

            if fl is not None and fr is not None:
                nl = f"./stereoLeft/{picture_num}.jpg"
                nr = f"./stereoRight/{picture_num}.jpg"
                cv2.imwrite(nl, fl)
                cv2.imwrite(nr, fr)
                print(f"[SALVO] {nl} e {nr}")
                picture_num += 1
            else:
                print("[SALVAR] Frames não disponíveis ainda")

    cv2.destroyAllWindows()
    print("[DISPLAY] Janela fechada.")


def main():
    """Função principal: inicia threads e gerencia o ciclo de vida."""
    # Suprime warnings Qt conhecidos no shutdown
    warnings.filterwarnings("ignore", category=RuntimeWarning, module="cv2")

    threads = []

    # Broadcast (só até conectar as duas câmeras)
    t_broadcast = threading.Thread(target=send_broadcast, daemon=True)
    t_broadcast.start()
    threads.append(t_broadcast)

    # Receptores de stream
    for port, info in CAMERAS.items():
        t = threading.Thread(
            target=stream_receiver,
            args=(port, info["name"], info["side"]),
            daemon=True
        )
        t.start()
        threads.append(t)

    # Watchdog para desconexão
    t_watchdog = threading.Thread(target=watchdog, daemon=True)
    t_watchdog.start()
    threads.append(t_watchdog)

    print("Servidor iniciado. Pressione 'q' na janela para sair.\n")

    try:
        # Loop de display na thread principal (evita warnings Qt)
        display_loop()
    except KeyboardInterrupt:
        print("\nInterrompido pelo usuário (Ctrl+C)")
    finally:
        stop_event.set()

        # Aguarda threads terminarem
        for t in threads:
            t.join(timeout=3.0)

        print("Servidor finalizado com sucesso.")


if __name__ == "__main__":
    main()