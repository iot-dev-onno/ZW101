/*
  prueba_zw101_raw_enter_v3.ino
  ESP32 + ZW101/ZW111 Fingerprint Module
  Menú interactivo leyendo línea completa (ENTER)
  • Opción 1: Verificar dedo
  • Opción 2: PS_GetImage con descripción, volcado TX/RX e interpretación
  • Opción 3: PS_Search (deshabilitado)
*/

#include <Arduino.h>

// Pines del ESP32
#define SENSOR_RX_PIN   16  // UART2 RX (sensor TX)
#define SENSOR_TX_PIN   17  // UART2 TX (sensor RX)
#define TOUCH_OUT_PIN   23  // Señal TOUCH_OUT del sensor

// Parámetros UART
#define SENSOR_BAUD     57600

// Dirección por defecto (4 bytes)
const uint8_t DEVICE_ADDR[4] = { 0xFF,0xFF,0xFF,0xFF };

// ————— Prototipos —————
void    flushSerial2();
void    printHex(const uint8_t* buf, size_t len);
void    dumpCommand(uint8_t inst, const uint8_t* params = nullptr, uint16_t paramLen = 0);

void    cmdCheckFinger();
void    cmdGetImage();
void    cmdSearchFlash();

// ————— Variables globales —————
String inputBuffer = "";

// ————— Setup y loop —————
void setup() {
  Serial.begin(115200);
  while (!Serial) {}
  Serial2.begin(SENSOR_BAUD, SERIAL_8N1, SENSOR_RX_PIN, SENSOR_TX_PIN);
  pinMode(TOUCH_OUT_PIN, INPUT);
  delay(200);

  Serial.println(F("\n=== MENÚ HUELLAS ZW101 ==="));
  Serial.println(F("1: Verificar dedo"));
  Serial.println(F("2: PS_GetImage"));
  Serial.println(F("3: PS_Search (deshabilitado)"));
  Serial.println(F("Elige 1, 2 o 3 + ENTER"));
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      inputBuffer.trim();
      if      (inputBuffer == "1") cmdCheckFinger();
      else if (inputBuffer == "2") cmdGetImage();
      else if (inputBuffer == "3") cmdSearchFlash();
      else if (inputBuffer.length() > 0)
        Serial.println(F("Opción inválida. Elige 1, 2 o 3."));
      inputBuffer = "";
      Serial.println();
      Serial.println(F("1: Verificar dedo | 2: PS_GetImage | 3: PS_Search (deshab.)"));
    } else {
      inputBuffer += c;
    }
  }
}

// ————— Helpers —————
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
  buf[idx++] = 0xEF; buf[idx++] = 0x01;
  memcpy(buf + idx, DEVICE_ADDR, 4); idx += 4;
  buf[idx++] = pid;
  buf[idx++] = highByte(pktLen); buf[idx++] = lowByte(pktLen);
  buf[idx++] = inst;
  for (uint16_t i = 0; i < paramLen; i++) buf[idx++] = params[i];
  uint16_t sum = pid + highByte(pktLen) + lowByte(pktLen) + inst;
  for (uint16_t i = 0; i < paramLen; i++) sum += params[i];
  buf[idx++] = highByte(sum);
  buf[idx++] = lowByte(sum);

  Serial.print(F("TX: "));
  printHex(buf, idx);
  Serial2.write(buf, idx);
}

// ————— Comandos menú —————

void cmdCheckFinger() {
  Serial.println(F("\n--- Comando 1: Verificar dedo ---"));
  int v = digitalRead(TOUCH_OUT_PIN);
  if (v == HIGH)
    Serial.println(F("Mensaje: ¡Dedo detectado!"));
  else
    Serial.println(F("Mensaje: No hay dedo."));
  Serial.printf("RX (crudo pin D23): 0x%02X\n", v);
}

void cmdGetImage() {
  Serial.println(F("\n--- Comando 2: PS_GetImage ---"));
  // 1) Descripción de la función
  Serial.println(F("Función: When verifying fingerprints, the finger is detected,"));
  Serial.println(F("  and after detection, the fingerprint image is recorded and"));
  Serial.println(F("  stored in the image buffer."));
  Serial.println(F("Input parameters: none"));
  Serial.println(F("Return: confirmation code"));

  // 2) Verificar dedo antes de enviar comando
  if (digitalRead(TOUCH_OUT_PIN) != HIGH) {
    Serial.println(F("Mensaje: No hay dedo. Coloca el dedo sobre el sensor."));
    return;
  }
  Serial.println(F("Mensaje: Dedo detectado, iniciando PS_GetImage..."));

  // 3) Enviar y volcar
  flushSerial2();
  dumpCommand(0x01);

  // 4) Leer respuesta cruda
  unsigned long t0 = millis();
  while (!Serial2.available() && millis() - t0 < 500) {}
  delay(20);
  size_t n = Serial2.available();
  uint8_t buf[64];
  n = Serial2.readBytes(buf, min(n, sizeof(buf)));
  Serial.print(F("RX: "));
  printHex(buf, n);

  // 5) Interpretación
  if (n >= 12) {
    uint8_t conf = buf[9];
    switch (conf) {
      case 0x00:
        Serial.println(F("Interpretación: 0x00 = success, imagen capturada correctamente."));
        break;
      case 0x01:
        Serial.println(F("Interpretación: 0x01 = fail to enroll (impossible to collect image)."));
        break;
      case 0x02:
        Serial.println(F("Interpretación: 0x02 = no finger detected."));
        break;
      default:
        Serial.printf("Interpretación: código 0x%02X no documentado.\n", conf);
        break;
    }
  } else {
    Serial.println(F("Interpretación: trama demasiado corta para parsear."));
  }
}

void cmdSearchFlash() {
  Serial.println(F("\n--- Comando 3: PS_Search (deshabilitado) ---"));
  Serial.println(F("Esta función está deshabilitada por el momento."));
}
