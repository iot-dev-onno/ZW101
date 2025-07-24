#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
fingerprint_manager.py

Programa para enrolar y buscar huellas en un sensor ZFM (e.g. GT-511C3)
Mediante comandos:
 - PS_GetImage
 - PS_GenChar
 - PS_RegModel
 - PS_Store
 - PS_Search
 - PS_LoadChar
 - PS_Match

Requisitos:
    pip install pyserial
"""

import sys
import time
import argparse
import serial
import serial.tools.list_ports

# --- Protocolo ZFM ---
HEADER      = bytes([0xEF, 0x01])
ADDRESS     = bytes([0xFF, 0xFF, 0xFF, 0xFF])
CMD_PACKET  = 0x01
ACK_PACKET  = 0x07

# Códigos de comando
PS_GET_IMAGE  = 0x01
PS_GEN_CHAR   = 0x02
PS_REG_MODEL  = 0x05
PS_STORE      = 0x06
PS_SEARCH     = 0x04
PS_LOAD_CHAR  = 0x07
PS_MATCH      = 0x03

# Meaning of confirmation codes (common)
CONFIRM_CODES = {
    0x00: "OK",
    0x01: "Error: turno dedo",
    0x02: "Error: captura",
    0x03: "Error: imagen ruidosa",
    0x06: "Timeout colocando dedo",
    0x07: "Error: no se pueden extraer características",
    0x08: "Error: buffer lleno",
    0x09: "Error: página no encontrada",
    0x0A: "Error: comando inválido",
    0x0B: "Error: ancho de imagen",
    0x0C: "Error: longitud de paquete",
    # añadir más si es necesario...
}

def calc_checksum(packet_type: int, payload: bytes) -> bytes:
    length = len(payload) + 2  # payload + 2 checksum bytes
    hi = (length >> 8) & 0xFF
    lo = length & 0xFF
    s = packet_type + hi + lo + sum(payload)
    return bytes([ (s >> 8) & 0xFF, s & 0xFF ])

def build_packet(cmd: int, params: bytes = b'') -> bytes:
    """
    Construye un paquete de comando:
    [HEADER][ADDRESS][CMD_PACKET][LEN_HI][LEN_LO][CMD][params...][CHECKSUM_HI][CHECKSUM_LO]
    """
    payload = bytes([cmd]) + params
    chk = calc_checksum(CMD_PACKET, payload)
    length = len(payload) + 2
    return HEADER + ADDRESS + bytes([CMD_PACKET, (length >> 8)&0xFF, length&0xFF]) + payload + chk

def read_packet(ser: serial.Serial, timeout: float = 1.0) -> bytes:
    """
    Lee todos los bytes entrantes durante 'timeout' segundos.
    """
    deadline = time.time() + timeout
    buf = b''
    while time.time() < deadline:
        n = ser.in_waiting
        if n:
            buf += ser.read(n)
        else:
            time.sleep(0.01)
    return buf

def parse_ack(packet: bytes) -> (int, bytes):
    """
    Parsea un ACK_PACKET y devuelve (confirmation_code, full_payload_bytes).
    Asume que packet contiene HEADER+ADDRESS+ACK+LEN_HI+LEN_LO+payload+checksum.
    """
    if len(packet) < 12 or packet[6] != ACK_PACKET:
        raise ValueError("Paquete inválido o demasiado corto")
    length = (packet[7] << 8) | packet[8]
    payload = packet[9: 9 + length - 2]
    conf_code = payload[0]
    return conf_code, payload

# --- Funciones de alto nivel ---

def ps_get_image(ser):
    pkt = build_packet(PS_GET_IMAGE)
    ser.write(pkt)
    resp = read_packet(ser)
    code, _ = parse_ack(resp)
    return code

def ps_gen_char(ser, buffer_id: int):
    pkt = build_packet(PS_GEN_CHAR, bytes([buffer_id]))
    ser.write(pkt)
    resp = read_packet(ser)
    code, _ = parse_ack(resp)
    return code

def ps_reg_model(ser):
    pkt = build_packet(PS_REG_MODEL)
    ser.write(pkt)
    resp = read_packet(ser)
    code, _ = parse_ack(resp)
    return code

def ps_store(ser, page_id: int, buffer_id: int):
    params = bytes([buffer_id, (page_id>>8)&0xFF, page_id&0xFF])
    pkt = build_packet(PS_STORE, params)
    ser.write(pkt)
    resp = read_packet(ser)
    code, _ = parse_ack(resp)
    return code

def ps_search(ser, buffer_id: int, start_page: int, num_pages: int):
    params = bytes([buffer_id,
                    (start_page>>8)&0xFF, start_page&0xFF,
                    (num_pages>>8)&0xFF, num_pages&0xFF])
    pkt = build_packet(PS_SEARCH, params)
    ser.write(pkt)
    resp = read_packet(ser)
    code, payload = parse_ack(resp)
    if code == 0x00:
        match_id  = (payload[1]<<8) | payload[2]
        match_score = (payload[3]<<8) | payload[4]
        return code, match_id, match_score
    else:
        return code, None, None

def ps_load_char(ser, page_id: int, buffer_id: int):
    params = bytes([buffer_id, (page_id>>8)&0xFF, page_id&0xFF])
    pkt = build_packet(PS_LOAD_CHAR, params)
    ser.write(pkt)
    resp = read_packet(ser)
    code, _ = parse_ack(resp)
    return code

def ps_match(ser, buf1: int, buf2: int):
    params = bytes([buf1, buf2])
    pkt = build_packet(PS_MATCH, params)
    ser.write(pkt)
    resp = read_packet(ser)
    code, payload = parse_ack(resp)
    if code == 0x00:
        score = (payload[1]<<8) | payload[2]
        return code, score
    else:
        return code, None

# --- Utilidades de puerto serie ---

def list_ports():
    return [(p.device, p.description) for p in serial.tools.list_ports.comports()]

def choose_port(ports):
    print("Puertos serie disponibles:")
    for i, (dev, desc) in enumerate(ports):
        print(f"[{i}] {dev} — {desc}")
    sel = input("Elige un puerto: ")
    return ports[int(sel)][0]

# --- Interfaz principal ---

def main():
    parser = argparse.ArgumentParser(description="Gestión de huellas ZFM")
    parser.add_argument("-p", "--port", help="Puerto COM (e.g. COM3)", default=None)
    parser.add_argument("-b", "--baud", help="Baudios (por defecto 57600)",
                        type=int, default=57600)
    args = parser.parse_args()

    port = args.port
    if not port:
        ports = list_ports()
        if not ports:
            sys.exit("No se hallaron puertos serie.")
        port = choose_port(ports)

    ser = serial.Serial(port, args.baud, timeout=0.1)
    print(f"Conectado a {port} @ {args.baud} baudios.\n")

    try:
        while True:
            print("Menú:")
            print(" 1) Enrolar huella (ID)")
            print(" 2) Buscar huella en base")
            print(" 3) Match contra ID específico")
            print(" 4) Salir")
            opt = input("> ").strip()

            if opt == '1':
                pid = int(input("ID para almacenar (0…65535): "))
                print("Coloca dedo para primera captura…")
                c = ps_get_image(ser); print(" GetImage →", CONFIRM_CODES.get(c, hex(c)))
                print("Generando características buffer 1…")
                c = ps_gen_char(ser, 1); print(" GenChar1  →", CONFIRM_CODES.get(c, hex(c)))
                print("Quita y vuelve a colocar dedo…")
                time.sleep(1.5)
                c = ps_get_image(ser); print(" GetImage →", CONFIRM_CODES.get(c, hex(c)))
                c = ps_gen_char(ser, 2); print(" GenChar2  →", CONFIRM_CODES.get(c, hex(c)))
                print("Registrando modelo…")
                c = ps_reg_model(ser); print(" RegModel  →", CONFIRM_CODES.get(c, hex(c)))
                print(f"Almacenando en página {pid}…")
                c = ps_store(ser, pid, 1); print(" Store     →", CONFIRM_CODES.get(c, hex(c)))
                print("Enrolamiento completado.\n")

            elif opt == '2':
                start = int(input("Página inicio (0): "))
                count = int(input("Número de páginas a buscar: "))
                print("Coloca dedo para captura…")
                c = ps_get_image(ser); print(" GetImage →", CONFIRM_CODES.get(c, hex(c)))
                c = ps_gen_char(ser, 1); print(" GenChar1  →", CONFIRM_CODES.get(c, hex(c)))
                print("Buscando en base…")
                c, mid, score = ps_search(ser, 1, start, count)
                if c == 0x00:
                    print(f"Match en ID={mid}, Score={score}")
                else:
                    print("No hay coincidencias.", CONFIRM_CODES.get(c, hex(c)))
                print()

            elif opt == '3':
                pid = int(input("ID a comparar: "))
                print("Coloca dedo para captura…")
                c = ps_get_image(ser); print(" GetImage →", CONFIRM_CODES.get(c, hex(c)))
                c = ps_gen_char(ser, 1); print(" GenChar1  →", CONFIRM_CODES.get(c, hex(c)))
                print(f"Cargando plantilla ID={pid} en buffer 2…")
                c = ps_load_char(ser, pid, 2); print(" LoadChar  →", CONFIRM_CODES.get(c, hex(c)))
                print("Comparando buffers…")
                c, score = ps_match(ser, 1, 2)
                if c == 0x00:
                    print(f"Match OK, Score={score}")
                else:
                    print("No match.", CONFIRM_CODES.get(c, hex(c)))
                print()

            elif opt == '4':
                break
            else:
                print("Opción inválida.\n")

    except KeyboardInterrupt:
        pass
    finally:
        ser.close()
        print("Puerto cerrado, hasta luego.")

if __name__ == "__main__":
    main()
