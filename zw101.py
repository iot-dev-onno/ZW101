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

# Códigos de confirmación según manual
CONFIRM_CODES = {
    0x00: "OK / Éxito",
    0x01: "Error: paquete inválido o error de recepción",
    0x02: "Error: captura",
    0x03: "Error: imagen demasiado ruidosa",
    0x06: "Error: imagen desordenada, no caracteriza",
    0x07: "Error: pocos puntos de característica",
    0x08: "Huella no coincide / mismatch",
    0x09: "No hay coincidencias en búsqueda",
    0x0A: "Error de fusión de características (merge failure)",
    0x0B: "Error: ancho de imagen",
    0x0C: "Error: longitud de paquete",
    0x15: "No hay imagen cruda válida en buffer",
    0x17: "Huella residual o sin movimiento entre capturas",
    0x18: "Error de escritura en FLASH",
    0x28: "Asociación con características previas (feature link on)",
    0x31: "Nivel de cifrado no soportado",
    0x35: "Datos ilegales",
}

def calc_checksum(packet_type: int, payload: bytes) -> bytes:
    length = len(payload) + 2
    hi = (length >> 8) & 0xFF
    lo = length & 0xFF
    s = packet_type + hi + lo + sum(payload)
    return bytes([ (s >> 8) & 0xFF, s & 0xFF ])

def build_packet(cmd: int, params: bytes = b'') -> bytes:
    payload = bytes([cmd]) + params
    chk = calc_checksum(CMD_PACKET, payload)
    length = len(payload) + 2
    return HEADER + ADDRESS + bytes([CMD_PACKET, (length>>8)&0xFF, length&0xFF]) + payload + chk

def read_packet(ser: serial.Serial, timeout: float = 1.0) -> bytes:
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
    if len(packet) < 12 or packet[6] != ACK_PACKET:
        raise ValueError("Paquete inválido o demasiado corto")
    length = (packet[7]<<8) | packet[8]
    payload = packet[9:9 + length - 2]
    return payload[0], payload

# --- Funciones de alto nivel ---

def ps_get_image(ser):
    ser.reset_input_buffer()
    ser.write(build_packet(0x01))
    code, _ = parse_ack(read_packet(ser))
    return code

def ps_gen_char(ser, buf_id):
    ser.reset_input_buffer()
    ser.write(build_packet(0x02, bytes([buf_id])))
    code, _ = parse_ack(read_packet(ser))
    return code

def ps_reg_model(ser):
    ser.reset_input_buffer()
    ser.write(build_packet(0x05))
    code, _ = parse_ack(read_packet(ser))
    return code

def ps_store(ser, page_id, buf_id):
    ser.reset_input_buffer()
    params = bytes([buf_id, (page_id>>8)&0xFF, page_id&0xFF])
    ser.write(build_packet(0x06, params))
    code, _ = parse_ack(read_packet(ser))
    return code

def ps_search(ser, buf_id, start, count):
    ser.reset_input_buffer()
    params = bytes([
        buf_id,
        (start>>8)&0xFF, start&0xFF,
        (count>>8)&0xFF, count&0xFF
    ])
    ser.write(build_packet(0x04, params))
    code, payload = parse_ack(read_packet(ser))
    if code == 0x00:
        mid   = (payload[1]<<8) | payload[2]
        score = (payload[3]<<8) | payload[4]
        return code, mid, score
    else:
        return code, None, None

def ps_load_char(ser, page_id, buf_id):
    ser.reset_input_buffer()
    params = bytes([buf_id, (page_id>>8)&0xFF, page_id&0xFF])
    ser.write(build_packet(0x07, params))
    code, _ = parse_ack(read_packet(ser))
    return code

def ps_match(ser, b1, b2):
    ser.reset_input_buffer()
    ser.write(build_packet(0x03, bytes([b1, b2])))
    code, payload = parse_ack(read_packet(ser))
    if code == 0x00:
        score = (payload[1]<<8) | payload[2]
        return code, score
    else:
        return code, None

# --- Puerto Serie y Menú ---

def list_ports():
    return [(p.device, p.description) for p in serial.tools.list_ports.comports()]

def choose_port(ports):
    for i,(d,desc) in enumerate(ports):
        print(f"[{i}] {d} — {desc}")
    return ports[int(input("Elige un puerto: "))][0]

def main():
    parser = argparse.ArgumentParser("Gestión de huellas ZFM")
    parser.add_argument("-p","--port", default=None)
    parser.add_argument("-b","--baud", type=int, default=57600)
    args = parser.parse_args()

    port = args.port or choose_port(list_ports())
    ser  = serial.Serial(port, args.baud, timeout=0.1)
    print(f"Conectado a {port} @ {args.baud} baudios\n")

    try:
        while True:
            print("Menú:\n 1) Enrolar huella\n 2) Buscar huella\n 3) Match ID específico\n 4) Salir")
            opt = input("> ").strip()

            # 1) Enrolar
            if opt == '1':
                pid        = int(input("ID para almacenar (0–65535): "))
                ok         = True
                error_code = None

                # Paso A: primera captura
                print("Coloca dedo…")
                c = ps_get_image(ser)
                print(f" GetImage → {CONFIRM_CODES.get(c,hex(c))}")
                if c != 0:
                    ok = False; error_code = c

                # Paso B: GenChar1
                if ok:
                    c = ps_gen_char(ser,1)
                    print(f" GenChar1 → {CONFIRM_CODES.get(c,hex(c))}")
                    if c != 0:
                        ok = False; error_code = c

                # Paso C: segunda captura
                if ok:
                    time.sleep(1.5)
                    c = ps_get_image(ser)
                    print(f" GetImage → {CONFIRM_CODES.get(c,hex(c))}")
                    if c != 0:
                        ok = False; error_code = c

                # Paso D: GenChar2
                if ok:
                    c = ps_gen_char(ser,2)
                    print(f" GenChar2 → {CONFIRM_CODES.get(c,hex(c))}")
                    if c != 0:
                        ok = False; error_code = c

                # Paso E: RegModel
                if ok:
                    c = ps_reg_model(ser)
                    print(f" RegModel → {CONFIRM_CODES.get(c,hex(c))}")
                    if c != 0:
                        ok = False; error_code = c

                # Paso F: Store
                if ok:
                    c = ps_store(ser, pid, 1)
                    print(f" Store    → {CONFIRM_CODES.get(c,hex(c))}")
                    if c == 0:
                        print("¡Enrolamiento completado!\n")
                    else:
                        print("Error en Store, no se guardó.\n")
                else:
                    msg = CONFIRM_CODES.get(error_code, hex(error_code))
                    print(f"\nAbortando enrolamiento: {msg}. No se guardará la huella.\n")

            # 2) Buscar
            elif opt == '2':
                start = int(input("Página inicio: "))
                count = int(input("Número páginas: "))
                ok         = True
                error_code = None

                print("Coloca dedo…")
                c = ps_get_image(ser)
                print(f" GetImage → {CONFIRM_CODES.get(c,hex(c))}")
                if c != 0:
                    ok = False; error_code = c

                if ok:
                    c = ps_gen_char(ser,1)
                    print(f" GenChar1 → {CONFIRM_CODES.get(c,hex(c))}")
                    if c != 0:
                        ok = False; error_code = c

                if ok:
                    code, mid, score = ps_search(ser,1,start,count)
                    if code == 0:
                        print(f"Match en ID={mid}, Score={score}\n")
                    else:
                        print(f"No coincide: {CONFIRM_CODES.get(code,hex(code))}\n")
                else:
                    print(f"\nAbortando búsqueda: {CONFIRM_CODES.get(error_code,hex(error_code))}\n")

            # 3) Match ID específico
            elif opt == '3':
                pid        = int(input("ID a comparar: "))
                ok         = True
                error_code = None

                print("Coloca dedo…")
                c = ps_get_image(ser)
                print(f" GetImage → {CONFIRM_CODES.get(c,hex(c))}")
                if c != 0:
                    ok = False; error_code = c

                if ok:
                    c = ps_gen_char(ser,1)
                    print(f" GenChar1 → {CONFIRM_CODES.get(c,hex(c))}")
                    if c != 0:
                        ok = False; error_code = c

                if ok:
                    c = ps_load_char(ser, pid, 2)
                    print(f" LoadChar(ID={pid}) → {CONFIRM_CODES.get(c,hex(c))}")
                    if c != 0:
                        ok = False; error_code = c

                if ok:
                    c, score = ps_match(ser,1,2)
                    if c == 0:
                        print(f"Match OK, Score={score}\n")
                    else:
                        print(f"No coincide: {CONFIRM_CODES.get(c,hex(c))}\n")
                else:
                    print(f"\nAbortando match: {CONFIRM_CODES.get(error_code,hex(error_code))}\n")

            # 4) Salir
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
