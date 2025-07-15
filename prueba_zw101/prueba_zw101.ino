/*
  prueba_zw101_menu_fixed3.ino
  ESP32 + ZW101/ZW111 Fingerprint Module
  Menú interactivo leyendo línea completa (ENTER)
  Opciones:
    1: Verificar dedo
    2: PS_GetImage
    3: PS_GenChar
    4: PS_Match
  PS_GenChar corregido para leer siempre los 12 bytes de ACK.
*/

#include <Arduino.h>

// Pines del ESP32
#define SENSOR_RX_PIN   16  // UART2 RX (sensor TX)
#define SENSOR_TX_PIN   17  // UART2 TX (sensor RX)
#define TOUCH_OUT_PIN   23  // Señal TOUCH_OUT del sensor

// UART a 57600 bps
#define SENSOR_BAUD     57600

// Dirección por defecto del módulo (4 bytes)
const uint8_t DEVICE_ADDR[4] = { 0xFF, 0xFF, 0xFF, 0xFF };

// ——— Prototipos ———
void flushSerial2();
void printHex(const uint8_t* buf, size_t len);
void dumpCommand(uint8_t inst, const uint8_t* params = nullptr, uint16_t paramLen = 0);

void cmdCheckFinger();
void cmdGetImage();
void cmdGenChar();
void cmdMatch();

String inputBuffer = "";

void setup() {
  Serial.begin(115200);
  while (!Serial) {}
  Serial2.begin(SENSOR_BAUD, SERIAL_8N1, SENSOR_RX_PIN, SENSOR_TX_PIN);
  pinMode(TOUCH_OUT_PIN, INPUT);
  Serial.println("\n=== MENÚ HUELLAS ZW101 ===");
  Serial.println("1: Verificar dedo");
  Serial.println("2: PS_GetImage");
  Serial.println("3: PS_GenChar");
  Serial.println("4: PS_Match");
  Serial.println("Elige 1-4 + ENTER");
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') return;
    if (c == '\n') {
      inputBuffer.trim();
      if      (inputBuffer == "1") cmdCheckFinger();
      else if (inputBuffer == "2") cmdGetImage();
      else if (inputBuffer == "3") cmdGenChar();
      else if (inputBuffer == "4") cmdMatch();
      else if (inputBuffer.length() > 0)
        Serial.println(F("Opción inválida. Elige 1-4."));
      inputBuffer = "";
      Serial.println();
      Serial.println("1|2|3|4 + ENTER");
    } else {
      inputBuffer += c;
    }
  }
}

// ——— Helpers ———
void flushSerial2() {
  while (Serial2.available()) Serial2.read();
}

void printHex(const uint8_t* buf, size_t len) {
  for (size_t i = 0; i < len; i++) {
    Serial.printf("%02X ", buf[i]);
  }
  Serial.println();
}

void dumpCommand(uint8_t inst, const uint8_t* params, uint16_t paramLen) {
  uint8_t pid = 0x01;
  uint16_t pktLen = 1 + paramLen + 2;
  uint8_t buf[9 + paramLen];
  int idx = 0;
  // Header + dirección
  buf[idx++] = 0xEF; buf[idx++] = 0x01;
  memcpy(buf + idx, DEVICE_ADDR, 4); idx += 4;
  // Packet ID + longitud
  buf[idx++] = pid;
  buf[idx++] = highByte(pktLen); buf[idx++] = lowByte(pktLen);
  // Instrucción
  buf[idx++] = inst;
  // Parámetros
  for (uint16_t i = 0; i < paramLen; i++) buf[idx++] = params[i];
  // Checksum
  uint16_t sum = pid + highByte(pktLen) + lowByte(pktLen) + inst;
  for (uint16_t i = 0; i < paramLen; i++) sum += params[i];
  buf[idx++] = highByte(sum); buf[idx++] = lowByte(sum);

  Serial.print(F("TX: "));
  printHex(buf, idx);
  Serial2.write(buf, idx);
}

// 1: Verificar dedo
void cmdCheckFinger() {
  Serial.println(F("\n--- 1: Verificar dedo ---"));
  int v = digitalRead(TOUCH_OUT_PIN);
  Serial.printf("Mensaje: %s\n", v==HIGH ? "¡Dedo detectado!" : "No hay dedo.");
  Serial.printf("RX (crudo pin D23): 0x%02X\n", v);
}

// 2: PS_GetImage
void cmdGetImage() {
  Serial.println(F("\n--- 2: PS_GetImage ---"));
  if (digitalRead(TOUCH_OUT_PIN) != HIGH) {
    Serial.println(F("No hay dedo. Coloca el dedo."));
    return;
  }
  Serial.println(F("Enviando PS_GetImage..."));
  flushSerial2();
  dumpCommand(0x01);

  unsigned long t0 = millis();
  while (!Serial2.available() && millis()-t0 < 500) {}
  delay(20);

  size_t avail = Serial2.available();
  size_t cnt   = avail < 64 ? avail : 64;
  uint8_t buf[64];
  size_t n = Serial2.readBytes(buf, cnt);
  Serial.print(F("RX: "));
  printHex(buf, n);

  if (n >= 12) {
    uint8_t conf = buf[9];
    Serial.printf("Interpretación: 0x%02X = %s\n",
      conf,
      conf==0x00 ? "éxito" :
      conf==0x02 ? "no finger" :
      "otro");
  } else {
    Serial.println(F("Trama demasiado corta."));
  }
}

// 3: PS_GenChar (ahora lee exactamente 12 bytes de ACK)
void cmdGenChar() {
  Serial.println(F("\n--- 3: PS_GenChar ---"));
  Serial.println(F("Ingrese BufferID (1 o 2) + ENTER:"));
  String bid="";
  while (true) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c=='\n') break;
      if (c!='\r') bid += c;
    }
  }
  bid.trim();
  uint8_t bufId = (bid=="2")?2:1;
  Serial.printf("Enviando PS_GenChar con BufferID=%u...\n", bufId);

  flushSerial2();
  dumpCommand(0x02, &bufId, 1);

  // Leer exactamente 12 bytes de ACK (bloquea hasta timeout)
  uint8_t ack[12];
  size_t na = Serial2.readBytes(ack, 12);  // espera hasta 1s por defecto
  Serial.print(F("RX (ACK 12 bytes): "));
  printHex(ack, na);

  if (na < 12) {
    Serial.println(F("Error: ACK incompleto."));
    return;
  }
  uint8_t conf = ack[9];
  Serial.printf("Interpretación: 0x%02X = %s\n",
    conf,
    conf==0x00 ? "éxito" :
    conf==0x01 ? "error paquete" :
    conf==0x02 ? "no finger" :
    conf==0x06 ? "imagen muy ruidosa" :
    conf==0x07 ? "pocos puntos" :
    "otro");
}

// 4: PS_Match
void cmdMatch() {
  Serial.println(F("\n--- 4: PS_Match ---"));
  Serial.println(F("Enviando PS_Match..."));
  flushSerial2();
  dumpCommand(0x03);

  // Leer 14 bytes mínimo (ACK+score)
  uint8_t resp[14];
  size_t nr = Serial2.readBytes(resp, 14);
  Serial.print(F("RX (14 bytes): "));
  printHex(resp, nr);

  if (nr < 14) {
    Serial.println(F("Trama demasiado corta."));
    return;
  }
  uint8_t conf = resp[9];
  uint16_t score = (resp[10]<<8)|resp[11];
  Serial.printf("Interpretación: 0x%02X = %s\n",
    conf,
    conf==0x00?"match OK":
    conf==0x08?"no match":
    "otro");
  if (conf==0x00) {
    Serial.printf("Score: %u\n", score);
  }
}
