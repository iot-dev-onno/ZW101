#include <Arduino.h>

// Pines del ESP32
#define SENSOR_RX_PIN   16    // UART2 RX (sensor TX)
#define SENSOR_TX_PIN   17    // UART2 TX (sensor RX)
#define TOUCH_OUT_PIN   23    // Señal TOUCH_OUT del sensor

// Parámetros UART
#define SENSOR_BAUD     57600 // Velocidad del sensor

// Dirección por defecto del dispositivo (4 bytes)
const uint8_t DEVICE_ADDR[4] = { 0xFF, 0xFF, 0xFF, 0xFF };

// -----------------------------------------------------
// Helper: limpia cualquier dato pendiente en Serial2
void flushSerial2() {
  while (Serial2.available()) {
    Serial2.read();
  }
}

// -----------------------------------------------------
// Construye y envía un paquete de comando al sensor
void sendCommand(uint8_t instruction, const uint8_t* params = nullptr, uint16_t paramLen = 0) {
  uint8_t packetID = 0x01;              // Packet ID de comando
  uint16_t packetLen = 1 + paramLen + 2; // instr(1) + params + checksum(2)

  // Buffer total: header(2) + addr(4) + pid(1) + len(2) + body + chk(2)
  uint8_t buf[9 + paramLen];
  int idx = 0;

  // Header
  buf[idx++] = 0xEF;
  buf[idx++] = 0x01;
  // Dirección
  memcpy(buf + idx, DEVICE_ADDR, 4);
  idx += 4;
  // Packet ID
  buf[idx++] = packetID;
  // Longitud (alto, bajo)
  buf[idx++] = highByte(packetLen);
  buf[idx++] = lowByte(packetLen);
  // Instrucción
  buf[idx++] = instruction;
  // Parámetros
  for (uint16_t i = 0; i < paramLen; i++) {
    buf[idx++] = params[i];
  }
  // Checksum = packetID + lenH + lenL + instr + params[]
  uint16_t sum = packetID + highByte(packetLen) + lowByte(packetLen) + instruction;
  for (uint16_t i = 0; i < paramLen; i++) sum += params[i];
  buf[idx++] = highByte(sum);
  buf[idx++] = lowByte(sum);

  // Envío por UART2
  Serial2.write(buf, idx);
}

// -----------------------------------------------------
// Lee la respuesta de ACK y extrae el código de confirmation
// Devuelve true si se leyó correctamente el paquete
bool readAck(uint8_t &confirmation) {
  const int EXPECTED_BYTES = 12; // mínimo de bytes para un ACK completo
  unsigned long start = millis();

  // Espera hasta recibir suficientes bytes o timeout
  while (Serial2.available() < EXPECTED_BYTES) {
    if (millis() - start > 500) {
      return false; 
    }
  }

  // Buscamos la cabecera 0xEF 0x01
  while (Serial2.peek() != 0xEF) {
    Serial2.read();
    if (millis() - start > 500) return false;
  }
  // Leer y validar header
  if (Serial2.read() != 0xEF || Serial2.read() != 0x01) return false;
  // Descartar dirección (4 bytes)
  for (int i = 0; i < 4; i++) Serial2.read();
  // Packet ID debe ser 0x07 (respuesta)
  if (Serial2.read() != 0x07) return false;
  // Leer longitud
  uint16_t len = (Serial2.read() << 8) | Serial2.read();
  if (len < 3) return false; // al menos CONF(1) + CHK(2)
  // Leer código de confirmation
  confirmation = Serial2.read();
  // Descartar checksum (2 bytes)
  Serial2.read(); 
  Serial2.read();
  return true;
}

// -----------------------------------------------------
// PS_HandShake (0x35)
bool handshake() {
  flushSerial2();
  sendCommand(0x35);
  uint8_t conf;
  if (readAck(conf) && conf == 0x00) {
    Serial.println("Handshake OK");
    return true;
  }
  Serial.printf("Handshake falló (0x%02X)\n", conf);
  return false;
}

// PS_CheckSensor (0x36)
bool checkSensor() {
  flushSerial2();
  sendCommand(0x36);
  uint8_t conf;
  if (readAck(conf) && conf == 0x00) {
    Serial.println("Sensor detectado");
    return true;
  }
  Serial.printf("CheckSensor falló (0x%02X)\n", conf);
  return false;
}

// -----------------------------------------------------
void setup() {
  Serial.begin(115200);
  // Inicializar UART2: RX, TX
  Serial2.begin(SENSOR_BAUD, SERIAL_8N1, SENSOR_RX_PIN, SENSOR_TX_PIN);
  // Pin de detección táctil
  pinMode(TOUCH_OUT_PIN, INPUT);

  delay(200); // espera a que el módulo arranque y envíe el byte 0x55

  // 1) Handshake
  while (!handshake()) {
    delay(500);
  }
  // 2) Verificar sensor
  if (!checkSensor()) {
    Serial.println("¡No se pudo validar el sensor!");
    while (true) { delay(1000); }
  }
}

void loop() {
  // Lectura de TOUCH_OUT: HIGH = dedo detectado
  if (digitalRead(TOUCH_OUT_PIN) == HIGH) {
    Serial.println("¡Dedo detectado!");
    
    delay(500); // debounce sencillo
  }
}
