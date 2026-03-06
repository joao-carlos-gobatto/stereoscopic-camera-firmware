#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Servidor UDP para streaming estéreo de duas ESP32-CAM
Direita -> porta 8080
Esquerda -> porta 8081

Recursos:
- Descoberta automática via broadcast
- Visualização estéreo lado a lado (VGA)
- FPS por câmera
- Reconexão automática
- Salvar pares de imagens para calibração

Teclas:
q -> sair
s -> salvar par de imagens
"""

import socket
import cv2
import os
import numpy as np
import threading
import time
from datetime import datetime
import warnings
import shutil


# ==============================
# CONFIGURAÇÕES
# ==============================

BROADCAST_MSG = b"ESP_DISCOVERY"
BROADCAST_ADDR = "192.168.15.255"
BROADCAST_PORT = 12345

STREAM_PORT_RIGHT = 8080
STREAM_PORT_LEFT  = 8081

CAMERAS = {
    STREAM_PORT_RIGHT: {"name": "Câmera Direita", "side": "right"},
    STREAM_PORT_LEFT:  {"name": "Câmera Esquerda", "side": "left"}
}

BROADCAST_INTERVAL = 2.0
CAMERA_TIMEOUT = 12.0

# resolução VGA
FRAME_WIDTH = 640
FRAME_HEIGHT = 480

DISPLAY_WINDOW = "Visão Estéreo"
DISPLAY_SIZE = (FRAME_WIDTH * 2, FRAME_HEIGHT)

leftCalibrationFolder = "stereoCalibrationLeft"
rightCalibrationFolder = "stereoCalibrationRight"


# ==============================
# VARIÁVEIS COMPARTILHADAS
# ==============================

stop_event = threading.Event()

frame_lock = threading.Lock()

latest_frames = {port: None for port in CAMERAS}
frame_timestamps = {port: 0.0 for port in CAMERAS}

fps_data = {
    port: {"frames": 0, "last_time": time.time(), "fps": 0.0}
    for port in CAMERAS
}

connected_cameras = set()


# ==============================
# UTILIDADES
# ==============================

def format_timestamp(ts: float) -> str:
    return datetime.fromtimestamp(ts).strftime("%Y%m%d_%H%M%S_%f")[:-3]


def delete_folder_contents(folder):

    if not os.path.isdir(folder):
        return

    for filename in os.listdir(folder):

        path = os.path.join(folder, filename)

        try:

            if os.path.isfile(path) or os.path.islink(path):
                os.unlink(path)

            elif os.path.isdir(path):
                shutil.rmtree(path)

        except Exception as e:
            print("Erro ao deletar", path, e)


def stereo_calibration_setup():

    os.makedirs(leftCalibrationFolder, exist_ok=True)
    os.makedirs(rightCalibrationFolder, exist_ok=True)

    delete_folder_contents(leftCalibrationFolder)
    delete_folder_contents(rightCalibrationFolder)

    print("Pastas de calibração prontas")


# ==============================
# BROADCAST
# ==============================

def send_broadcast():

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

    print("[BROADCAST] iniciado")

    while not stop_event.is_set() and len(connected_cameras) < 2:

        try:

            sock.sendto(BROADCAST_MSG, (BROADCAST_ADDR, BROADCAST_PORT))

            print(f"[BROADCAST] enviado ({len(connected_cameras)}/2 conectadas)")

        except Exception as e:

            print("Erro broadcast:", e)

        time.sleep(BROADCAST_INTERVAL)

    sock.close()


# ==============================
# RECEPÇÃO DE STREAM
# ==============================

def stream_receiver(port, camera_name, side):

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("", port))
    sock.settimeout(1.0)

    print(f"[STREAM {side.upper()}] aguardando porta {port}")

    while not stop_event.is_set():

        try:

            data, addr = sock.recvfrom(65535)

            frame = cv2.imdecode(
                np.frombuffer(data, np.uint8),
                cv2.IMREAD_COLOR
            )

            if frame is None:
                continue

            now = time.time()

            with frame_lock:

                latest_frames[port] = frame.copy()
                frame_timestamps[port] = now

                fps_data[port]["frames"] += 1

                dt = now - fps_data[port]["last_time"]

                if dt >= 1.0:

                    fps_data[port]["fps"] = fps_data[port]["frames"] / dt
                    fps_data[port]["frames"] = 0
                    fps_data[port]["last_time"] = now

            if port not in connected_cameras:

                print(f"[CONECTADA] {camera_name} {addr[0]}")
                connected_cameras.add(port)

        except socket.timeout:
            continue

        except Exception as e:
            print("Erro stream:", e)

    sock.close()


# ==============================
# WATCHDOG
# ==============================

def watchdog():

    while not stop_event.is_set():

        time.sleep(5)

        now = time.time()

        with frame_lock:

            for port in list(connected_cameras):

                if now - frame_timestamps[port] > CAMERA_TIMEOUT:

                    print("Câmera desconectada porta", port)

                    connected_cameras.remove(port)
                    latest_frames[port] = None


# ==============================
# DISPLAY
# ==============================

def display_loop():

    picture_num = 0

    cv2.namedWindow(DISPLAY_WINDOW, cv2.WINDOW_NORMAL)
    cv2.resizeWindow(DISPLAY_WINDOW, *DISPLAY_SIZE)

    print("Pressione q para sair")
    print("Pressione s para salvar imagens")

    while not stop_event.is_set():

        with frame_lock:

            left = latest_frames[STREAM_PORT_LEFT]
            right = latest_frames[STREAM_PORT_RIGHT]

            fps_left = fps_data[STREAM_PORT_LEFT]["fps"]
            fps_right = fps_data[STREAM_PORT_RIGHT]["fps"]

        if left is None or right is None:

            placeholder = np.zeros((FRAME_HEIGHT, FRAME_WIDTH * 2, 3), dtype=np.uint8)

            msg = f"Aguardando cameras ({len(connected_cameras)}/2)"

            cv2.putText(
                placeholder,
                msg,
                (FRAME_WIDTH * 2 // 3, FRAME_HEIGHT // 2),
                cv2.FONT_HERSHEY_SIMPLEX,
                1,
                (0,180,255),
                2
            )

            cv2.imshow(DISPLAY_WINDOW, placeholder)

        else:

            canvas = np.hstack([left, right])

            cv2.line(
                canvas,
                (FRAME_WIDTH, 0),
                (FRAME_WIDTH, FRAME_HEIGHT),
                (200,200,200),
                2
            )

            cv2.putText(
                canvas,
                "ESQUERDA",
                (10,40),
                cv2.FONT_HERSHEY_SIMPLEX,
                1,
                (0,255,100),
                2
            )

            cv2.putText(
                canvas,
                "DIREITA",
                (FRAME_WIDTH + 10,40),
                cv2.FONT_HERSHEY_SIMPLEX,
                1,
                (0,255,100),
                2
            )

            cv2.putText(
                canvas,
                f"FPS: {fps_left:.1f}",
                (10, FRAME_HEIGHT - 20),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.8,
                (0,255,255),
                2
            )

            cv2.putText(
                canvas,
                f"FPS: {fps_right:.1f}",
                (FRAME_WIDTH + 10, FRAME_HEIGHT - 20),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.8,
                (0,255,255),
                2
            )

            cv2.imshow(DISPLAY_WINDOW, canvas)

        key = cv2.waitKey(10) & 0xFF

        if key == ord('q'):

            stop_event.set()

        elif key == ord('s'):

            with frame_lock:

                fl = latest_frames[STREAM_PORT_LEFT]
                fr = latest_frames[STREAM_PORT_RIGHT]

            if fl is not None and fr is not None:

                name_l = f"{leftCalibrationFolder}/{picture_num}.jpg"
                name_r = f"{rightCalibrationFolder}/{picture_num}.jpg"

                cv2.imwrite(name_l, fl)
                cv2.imwrite(name_r, fr)

                print("Salvo:", name_l, name_r)

                picture_num += 1

    cv2.destroyAllWindows()


# ==============================
# MAIN
# ==============================

def main():

    warnings.filterwarnings("ignore", category=RuntimeWarning, module="cv2")

    stereo_calibration_setup()

    threads = []

    t = threading.Thread(target=send_broadcast, daemon=True)
    t.start()
    threads.append(t)

    for port, info in CAMERAS.items():

        t = threading.Thread(
            target=stream_receiver,
            args=(port, info["name"], info["side"]),
            daemon=True
        )

        t.start()
        threads.append(t)

    t = threading.Thread(target=watchdog, daemon=True)
    t.start()
    threads.append(t)

    try:

        display_loop()

    except KeyboardInterrupt:

        print("Interrompido")

    finally:

        stop_event.set()

        for t in threads:
            t.join(timeout=2)

        print("Servidor finalizado")


if __name__ == "__main__":
    main()