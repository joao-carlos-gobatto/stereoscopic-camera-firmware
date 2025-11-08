import socket, struct, numpy as np, cv2, threading

PORT = 8080
DISCOVERY_RESPONSE = b"OK"

def server_thread():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.bind(('', PORT))
    print(f"[SERVER] Aguardando pacotes UDP na porta {PORT}...")

    frame_buffer = {}
    expected_packets = {}

    while True:
        packet, addr = sock.recvfrom(1500)
        if len(packet) < 10:
            # Mensagem curta → broadcast inicial
            print(f"[DISCOVERY] Recebido '{packet.decode()}' de {addr}")
            sock.sendto(DISCOVERY_RESPONSE, addr)
            continue

        # Caso contrário → pacote de frame
        frame_number, packet_id, total_packets = struct.unpack('<IHH', packet[:8])
        data = packet[8:]

        if frame_number not in frame_buffer:
            frame_buffer[frame_number] = {}
            expected_packets[frame_number] = total_packets

        frame_buffer[frame_number][packet_id] = data

        if len(frame_buffer[frame_number]) == total_packets:
            chunks = [frame_buffer[frame_number][i] for i in range(total_packets)]
            img_data = b''.join(chunks)
            frame = cv2.imdecode(np.frombuffer(img_data, np.uint8), cv2.IMREAD_COLOR)
            if frame is not None:
                cv2.imshow("ESP32-CAM", frame)
                cv2.waitKey(1)
            del frame_buffer[frame_number]
            del expected_packets[frame_number]

if __name__ == "__main__":
    server_thread()