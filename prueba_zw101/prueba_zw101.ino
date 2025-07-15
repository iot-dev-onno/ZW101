/*
  prueba_zw101.ino
  ESP32 + ZW101/ZW111 Fingerprint Module
*/

#include <Arduino.h>

// Pines del ESP32
#define SENSOR_RX_PIN   16    // UART2 RX (sensor TX)
#define SENSOR_TX_PIN   17    // UART2 TX (sensor RX)
#define TOUCH_OUT_PIN   23    // Señal TOUCH_OUT del sensor

// Parámetros UART
#define SENSOR_BAUD     57600 // Velocidad del sensor

// Dirección por defecto del dispositivo (4 bytes)
const uint8_t DEVICE_ADDR[4] = { 0xFF, 0xFF, 0xFF, 0xFF };

// ——————————————
// 1) Definición de tipos y prototipos
// ——————————————

struct Packet {
  uint8_t confirmation;
  uint8_t dataLen;
  uint8_t data[32];
};

void      flushSerial2();
void      printHex(const uint8_t* buf, size_t len);
void      dumpCommand(uint8_t instruction, const uint8_t* params = nullptr, uint16_t paramLen = 0);
void      dumpResponse();
void      sendCommand(uint8_t instruction, const uint8_t* params = nullptr, uint16_t paramLen = 0);
bool      readPacket(Packet &pkt);
bool      handshake();
bool      checkSensor();
bool      getImage(Packet &p);
bool      genChar(uint8_t bufId, Packet &p);
bool      regModel(Packet &p);
bool      storeModel(uint16_t addr, Packet &p);
bool      search(uint16_t start, uint16_t count, uint8_t bufId, Packet &p);

// ——————————————
// 2) Setup y loop
// ——————————————

void setup() {
  Serial.begin(115200);
  Serial2.begin(SENSOR_BAUD, SERIAL_8N1, SENSOR_RX_PIN, SENSOR_TX_PIN);
  pinMode(TOUCH_OUT_PIN, INPUT);
  delay(200);

  // Handshake
  while (!handshake()) {
    delay(500);
  }
  // Verificar sensor
  if (!checkSensor()) {
    Serial.println("¡No se pudo validar el sensor!");
    while (true) delay(1000);
  }
}

void loop() {
  if (digitalRead(TOUCH_OUT_PIN) == HIGH) {
    Serial.println("¡Dedo detectado! Empezando enrolamiento...");
    Packet p;

    // 1) Captura imagen
    if (!getImage(p) || p.confirmation) {
      Serial.println("Error en GetImage");
      return;
    }
    // 2) GenChar en buffer 1
    if (!genChar(0x01, p) || p.confirmation) {
      Serial.println("Error en GenChar(1)");
      return;
    }
    Serial.println("Característica 1 lista, retire el dedo...");
    delay(2000);

    // 3) Captura segunda imagen
    while (digitalRead(TOUCH_OUT_PIN) == LOW); // Esperar dedo
    if (!getImage(p) || p.confirmation) {
      Serial.println("Error en GetImage (2)");
      return;
    }
    if (!genChar(0x02, p) || p.confirmation) {
      Serial.println("Error en GenChar(2)");
      return;
    }
    // 4) RegModel
    if (!regModel(p) || p.confirmation) {
      Serial.println("Error en RegModel");
      return;
    }
    // 5) Store en addr=1
    if (!storeModel(0x0001, p) || p.confirmation) {
      Serial.println("Error en StoreModel");
      return;
    }

    Serial.println("Huella enrolada con éxito en addr=1");
    delay(3000);
  }
}

// ——————————————
// 3) Implementación de funciones
// ——————————————

void flushSerial2() {
  while (Serial2.available()) Serial2.read();
}

void printHex(const uint8_t* buf, size_t len) {
  for (size_t i = 0; i < len; i++) {
    Serial.printf("%02X ", buf[i]);
  }
  Serial.println();
}

void dumpCommand(uint8_t instruction, const uint8_t* params, uint16_t paramLen) {
  uint8_t packetID = 0x01;
  uint16_t packetLen = 1 + paramLen + 2;
  uint8_t buf[9 + paramLen];
  int idx = 0;
  buf[idx++] = 0xEF; buf[idx++] = 0x01;
  memcpy(buf+idx, DEVICE_ADDR, 4); idx += 4;
  buf[idx++] = packetID;
  buf[idx++] = highByte(packetLen); buf[idx++] = lowByte(packetLen);
  buf[idx++] = instruction;
  for (uint16_t i = 0; i < paramLen; i++) buf[idx++] = params[i];
  uint16_t sum = packetID + highByte(packetLen) + lowByte(packetLen) + instruction;
  for (uint16_t i = 0; i < paramLen; i++) sum += params[i];
  buf[idx++] = highByte(sum); buf[idx++] = lowByte(sum);

  Serial.print("TX: ");
  printHex(buf, idx);
  Serial2.write(buf, idx);
}

void dumpResponse() {
  delay(50);
  size_t n = Serial2.available();
  if (n == 0) {
    Serial.println("RX: <vacío>");
    return;
  }
  uint8_t buf[128];
  n = Serial2.readBytes(buf, min(n, sizeof(buf)));
  Serial.print("RX: ");
  printHex(buf, n);
}

void sendCommand(uint8_t instruction, const uint8_t* params, uint16_t paramLen) {
  uint8_t packetID = 0x01;
  uint16_t packetLen = 1 + paramLen + 2;
  uint8_t buf[9 + paramLen];
  int idx = 0;
  buf[idx++] = 0xEF; buf[idx++] = 0x01;
  memcpy(buf+idx, DEVICE_ADDR, 4); idx += 4;
  buf[idx++] = packetID;
  buf[idx++] = highByte(packetLen); buf[idx++] = lowByte(packetLen);
  buf[idx++] = instruction;
  for (uint16_t i = 0; i < paramLen; i++) buf[idx++] = params[i];
  uint16_t sum = packetID + highByte(packetLen) + lowByte(packetLen) + instruction;
  for (uint16_t i = 0; i < paramLen; i++) sum += params[i];
  buf[idx++] = highByte(sum); buf[idx++] = lowByte(sum);
  Serial2.write(buf, idx);
}

bool readPacket(Packet &pkt) {
  const int MIN_BYTES = 12;
  unsigned long start = millis();
  while (Serial2.available() < MIN_BYTES) {
    if (millis() - start > 500) return false;
  }
  while (Serial2.peek() != 0xEF) {
    Serial2.read();
    if (millis() - start > 500) return false;
  }
  if (Serial2.read() != 0xEF || Serial2.read() != 0x01) return false;
  for (int i = 0; i < 4; i++) Serial2.read();
  if (Serial2.read() != 0x07) return false;
  uint16_t len = (Serial2.read() << 8) | Serial2.read();
  if (len < 3) return false;
  pkt.confirmation = Serial2.read();
  pkt.dataLen = len - 3;
  for (uint8_t i = 0; i < pkt.dataLen; i++) {
    pkt.data[i] = Serial2.read();
  }
  Serial2.read(); Serial2.read();
  return true;
}

bool handshake() {
  flushSerial2();
  sendCommand(0x35);
  Packet p;
  if (readPacket(p) && p.confirmation == 0x00) {
    Serial.println("Handshake OK");
    return true;
  }
  Serial.printf("Handshake falló (0x%02X)\n", p.confirmation);
  return false;
}

bool checkSensor() {
  flushSerial2();
  sendCommand(0x36);
  Packet p;
  if (readPacket(p) && p.confirmation == 0x00) {
    Serial.println("Sensor detectado");
    return true;
  }
  Serial.printf("CheckSensor falló (0x%02X)\n", p.confirmation);
  return false;
}

bool getImage(Packet &p) {
  flushSerial2();
  sendCommand(0x01);
  return readPacket(p);
}

bool genChar(uint8_t bufId, Packet &p) {
  flushSerial2();
  sendCommand(0x02, &bufId, 1);
  return readPacket(p);
}

bool regModel(Packet &p) {
  flushSerial2();
  sendCommand(0x05);
  return readPacket(p);
}

bool storeModel(uint16_t addr, Packet &p) {
  uint8_t params[3] = { 0x01, highByte(addr), lowByte(addr) };
  flushSerial2();
  sendCommand(0x06, params, 3);
  return readPacket(p);
}

bool search(uint16_t start, uint16_t count, uint8_t bufId, Packet &p) {
  uint8_t params[5] = {
    highByte(start), lowByte(start),
    highByte(count), lowByte(count),
    bufId
  };
  flushSerial2();
  sendCommand(0x04, params, 5);
  return readPacket(p);
}
