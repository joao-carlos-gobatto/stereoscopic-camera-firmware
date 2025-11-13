import socket
import cv2
import numpy as np
import struct

# === CONFIGURAÇÕES ===
UDP_PORT = 8080  # deve ser igual a CONFIG_UDP_STREAM_PORT
BROADCAST_MSG = b"ESP_DISCOVERY"
RESPONSE_MSG = b"SERVER_OK"

# === SOCKET PRINCIPAL ===
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("", UDP_PORT))
sock.settimeout(0.5)

print(f"[SERVER] Servidor UDP iniciado na porta {UDP_PORT}")

esp_clients = set()
current_frame = bytearray()

# === LOOP PRINCIPAL ===
while True:
    try:
        data, addr = sock.recvfrom(65535)

        # --- ETAPA 1: DISCOVERY ---
        if data == BROADCAST_MSG:
            print(f"[DISCOVERY] Pedido recebido de {addr[0]}:{addr[1]}")
            sock.sendto(RESPONSE_MSG, addr)
            esp_clients.add(addr[0])
            print(f"[DISCOVERY] Resposta enviada para {addr[0]}")
            continue

        # --- ETAPA 2: STREAMING ---
        if addr[0] in esp_clients:
            # Aqui estamos recebendo o buffer de camera_fb_t (dados brutos)
            # Em alguns firmwares, isso é o JPEG diretamente, em outros, é o ponteiro. Teste!
            # Tentamos decodificar diretamente como JPEG:
            frame_data = bytearray(data)
            frame = cv2.imdecode(np.frombuffer(frame_data, dtype=np.uint8), cv2.IMREAD_COLOR)
            
            if frame is not None:
                cv2.imshow(f"ESP32-CAM [{addr[0]}]", frame)
                if cv2.waitKey(1) & 0xFF == ord('q'):
                    break
            else:
                print(f"[WARN] Frame inválido recebido de {addr[0]} (tamanho={len(data)})")

    except socket.timeout:
        pass
    except Exception as e:
        print("[ERRO]", e)

sock.close()
cv2.destroyAllWindows()