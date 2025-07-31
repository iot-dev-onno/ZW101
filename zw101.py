#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
fingerprint_manager.py
Programa para gestionar huellas con un sensor ZFM (p. ej. GT-511C3)
Se añaden comandos:
 - PS_GetImage
 - PS_GenChar
 - PS_RegModel
 - PS_Store
 - PS_Search
 - PS_LoadChar
 - PS_Match
 - PS_DeleteChar

Cada función devuelve (code, message) para poder inspeccionar el código
de confirmación directamente.
"""

import sys
import time
import argparse
import serial
import serial.tools.list_ports

# --- Protocolo ZFM ---
HEADER         = bytes([0xEF, 0x01])
ADDRESS        = bytes([0xFF, 0xFF, 0xFF, 0xFF])
CMD_PACKET     = 0x01
ACK_PACKET     = 0x07

# Códigos de comando
PS_GET_IMAGE    = 0x01
PS_GEN_CHAR     = 0x02
PS_MATCH        = 0x03
PS_SEARCH       = 0x04
PS_REG_MODEL    = 0x05
PS_STORE        = 0x06
PS_LOAD_CHAR    = 0x07
PS_DELETE_CHAR  = 0x0C

# Todos los códigos de confirmación (según manual PDF)
CONFIRM_CODES = {
    0x00: "OK / Éxito",
    0x01: "Error: paquete inválido o error de recepción",
    0x02: "Error: captura",
    0x03: "Error: imagen demasiado ruidosa",
    0x06: "Error: imagen desordenada, no caracteriza",
    0x07: "Error: pocos puntos de característica",
    0x08: "Huella no coincide / mismatch",
    0x09: "No hay coincidencias en búsqueda",
    0x0A: "Error de fusión de características",
    0x0B: "Error: ancho de imagen",
    0x0C: "Error: longitud de paquete",
    0x15: "No hay imagen cruda válida en buffer",
    0x17: "Huella residual o sin movimiento entre capturas",
    0x18: "Error de escritura en FLASH",
    0x28: "Asociación con características previas",
    0x31: "Nivel de cifrado no soportado",
    0x35: "Datos ilegales",
}

def calc_checksum(packet_type: int, payload: bytes) -> bytes:
    length = len(payload) + 2
    hi = (length >> 8) & 0xFF
    lo = length & 0xFF
    s  = packet_type + hi + lo + sum(payload)
    return bytes([ (s >> 8) & 0xFF, s & 0xFF ])

def build_packet(cmd: int, params: bytes = b'') -> bytes:
    payload = bytes([cmd]) + params
    chk     = calc_checksum(CMD_PACKET, payload)
    length  = len(payload) + 2
    return HEADER + ADDRESS + bytes([CMD_PACKET, (length>>8)&0xFF, length&0xFF]) + payload + chk

def read_packet(ser: serial.Serial, timeout: float = 1.0) -> bytes:
    deadline = time.time() + timeout
    buf = b''
    while time.time() < deadline:
        if ser.in_waiting:
            buf += ser.read(ser.in_waiting)
        else:
            time.sleep(0.01)
    return buf

def parse_ack(packet: bytes) -> (int, bytes):
    if len(packet) < 12 or packet[6] != ACK_PACKET:
        raise ValueError("Paquete inválido o demasiado corto")
    length  = (packet[7]<<8) | packet[8]
    payload = packet[9:9 + length - 2]
    return payload[0], payload

# --- Funciones de alto nivel que devuelven (code, message) ---

def ps_get_image(ser):
    ser.reset_input_buffer()
    ser.write(build_packet(PS_GET_IMAGE))
    code, _ = parse_ack(read_packet(ser))
    return code, CONFIRM_CODES.get(code, hex(code))

def ps_gen_char(ser, buf_id):
    ser.reset_input_buffer()
    ser.write(build_packet(PS_GEN_CHAR, bytes([buf_id])))
    code, _ = parse_ack(read_packet(ser))
    return code, CONFIRM_CODES.get(code, hex(code))

def ps_reg_model(ser):
    ser.reset_input_buffer()
    ser.write(build_packet(PS_REG_MODEL))
    code, _ = parse_ack(read_packet(ser))
    return code, CONFIRM_CODES.get(code, hex(code))

def ps_store(ser, page_id, buf_id):
    ser.reset_input_buffer()
    params = bytes([buf_id, (page_id>>8)&0xFF, page_id&0xFF])
    ser.write(build_packet(PS_STORE, params))
    code, _ = parse_ack(read_packet(ser))
    return code, CONFIRM_CODES.get(code, hex(code))

def ps_search(ser, buf_id, start, count):
    ser.reset_input_buffer()
    params = bytes([
        buf_id,
        (start>>8)&0xFF, start&0xFF,
        (count>>8)&0xFF, count&0xFF
    ])
    ser.write(build_packet(PS_SEARCH, params))
    code, payload = parse_ack(read_packet(ser))
    msg   = CONFIRM_CODES.get(code, hex(code))
    if code == 0x00:
        mid   = (payload[1]<<8) | payload[2]
        score = (payload[3]<<8) | payload[4]
        return code, msg, mid, score
    else:
        return code, msg, None, None

def ps_load_char(ser, page_id, buf_id):
    ser.reset_input_buffer()
    params = bytes([buf_id, (page_id>>8)&0xFF, page_id&0xFF])
    ser.write(build_packet(PS_LOAD_CHAR, params))
    code, _ = parse_ack(read_packet(ser))
    return code, CONFIRM_CODES.get(code, hex(code))

def ps_match(ser, b1, b2):
    ser.reset_input_buffer()
    ser.write(build_packet(PS_MATCH, bytes([b1, b2])))
    code, payload = parse_ack(read_packet(ser))
    msg = CONFIRM_CODES.get(code, hex(code))
    if code == 0x00:
        score = (payload[1]<<8) | payload[2]
        return code, msg, score
    else:
        return code, msg, None

def ps_delete_char(ser, page_id, count=1):
    """
    Elimina 'count' plantillas a partir de page_id.
    """
    ser.reset_input_buffer()
    params = bytes([(page_id>>8)&0xFF, page_id&0xFF, count])
    ser.write(build_packet(PS_DELETE_CHAR, params))
    code, _ = parse_ack(read_packet(ser))
    return code, CONFIRM_CODES.get(code, hex(code))

# --- Menú de usuario ---

def list_ports():
    return [(p.device, p.description) for p in serial.tools.list_ports.comports()]

def choose_port(ports):
    for i,(dev, desc) in enumerate(ports):
        print(f"[{i}] {dev} — {desc}")
    return ports[int(input("Elige un puerto: "))][0]

def main():
    p = argparse.ArgumentParser("Gestión de huellas ZFM")
    p.add_argument("-p","--port", default=None)
    p.add_argument("-b","--baud", type=int, default=57600)
    args = p.parse_args()

    port = args.port or choose_port(list_ports())
    ser  = serial.Serial(port, args.baud, timeout=0.1)
    print(f"Conectado a {port} @ {args.baud} baudios\n")

    try:
        while True:
            print("Menú:")
            print(" 1) Enrolar huella")
            print(" 2) Buscar huella en base")
            print(" 3) Match ID específico")
            print(" 4) Eliminar huella")
            print(" 5) Salir")
            opt = input("> ").strip()

            # 1) Enrolar
            if opt == '1':
                pid        = int(input("ID para almacenar (0–65535): "))
                ok         = True
                error_msg  = None

                # 1. GetImage 1
                code, msg = ps_get_image(ser)
                print(f" GetImage  → {msg}")
                if code != 0:
                    ok, error_msg = False, msg

                # 2. GenChar buffer 1
                if ok:
                    code, msg = ps_gen_char(ser, 1)
                    print(f" GenChar1 → {msg}")
                    if code != 0:
                        ok, error_msg = False, msg

                # 3. GetImage 2
                if ok:
                    time.sleep(1.5)
                    code, msg = ps_get_image(ser)
                    print(f" GetImage  → {msg}")
                    if code != 0:
                        ok, error_msg = False, msg

                # 4. GenChar buffer 2
                if ok:
                    code, msg = ps_gen_char(ser, 2)
                    print(f" GenChar2 → {msg}")
                    if code != 0:
                        ok, error_msg = False, msg

                # 5. RegModel
                if ok:
                    code, msg = ps_reg_model(ser)
                    print(f" RegModel → {msg}")
                    if code != 0:
                        ok, error_msg = False, msg

                # 6. Store
                if ok:
                    code, msg = ps_store(ser, pid, 1)
                    print(f" Store     → {msg}")
                    if code == 0:
                        print("→ Enrolamiento completado.\n")
                    else:
                        print("→ Falló Store, no se guardó la huella.\n")
                else:
                    print(f"\nAbortando enrolamiento: {error_msg}\n")

            # 2) Buscar en base
            elif opt == '2':
                start = int(input("Página inicio: "))
                count = int(input("Número páginas: "))
                ok        = True
                error_msg = None

                code, msg = ps_get_image(ser)
                print(f" GetImage  → {msg}")
                if code != 0:
                    ok, error_msg = False, msg

                if ok:
                    code, msg = ps_gen_char(ser, 1)
                    print(f" GenChar1  → {msg}")
                    if code != 0:
                        ok, error_msg = False, msg

                if ok:
                    code, msg, mid, score = ps_search(ser, 1, start, count)
                    if code == 0:
                        print(f"→ Match en ID={mid}, Score={score}\n")
                    else:
                        print(f"→ No coincide: {msg}\n")
                else:
                    print(f"\nAbortando búsqueda: {error_msg}\n")

            # 3) Match ID específico
            elif opt == '3':
                pid       = int(input("ID a comparar: "))
                ok        = True
                error_msg = None

                code, msg = ps_get_image(ser)
                print(f" GetImage  → {msg}")
                if code != 0:
                    ok, error_msg = False, msg

                if ok:
                    code, msg = ps_gen_char(ser, 1)
                    print(f" GenChar1  → {msg}")
                    if code != 0:
                        ok, error_msg = False, msg

                if ok:
                    code, msg = ps_load_char(ser, pid, 2)
                    print(f" LoadChar  → {msg}")
                    if code != 0:
                        ok, error_msg = False, msg

                if ok:
                    code, msg, score = ps_match(ser, 1, 2)
                    if code == 0:
                        print(f"→ Match OK, Score={score}\n")
                    else:
                        print(f"→ No coincide: {msg}\n")
                else:
                    print(f"\nAbortando match: {error_msg}\n")

            # 4) Eliminar huella
            elif opt == '4':
                pid = int(input("ID a eliminar: "))
                code, msg = ps_delete_char(ser, pid)
                print(f" DeleteChar(ID={pid}) → {msg}\n")
            # 5) Salir
            elif opt == '5':
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
