#include <Arduino.h>

// ---------------- Configuration ----------------
#define FINGERPRINT_RX_PIN    16    // GPIO16 → sensor TX
#define FINGERPRINT_TX_PIN    17    // GPIO17 ← sensor RX
#define FINGERPRINT_BAUDRATE 57600
#define CONSOLE_BAUDRATE    115200

// ---------------- Protocol constants ----------------
const uint8_t HEADER[2]    = { 0xEF, 0x01 };
const uint8_t ADDRESS[4]   = { 0xFF, 0xFF, 0xFF, 0xFF };
const uint8_t CMD_PACKET   = 0x01;
const uint8_t ACK_PACKET   = 0x07;

enum {
  PS_GET_IMAGE = 0x01,
  PS_GEN_CHAR  = 0x02,
  PS_MATCH     = 0x03,
  PS_SEARCH    = 0x04,
  PS_REG_MODEL = 0x05,
  PS_STORE     = 0x06,
  PS_LOAD_CHAR = 0x07
};

// ---------------- Confirmation messages ----------------
const char* getConfirmationMessage(uint8_t code) {
  switch(code) {
    case 0x00: return "OK";
    case 0x01: return "Error: no hay dedo";
    case 0x02: return "Error: captura fallida";
    case 0x03: return "Error: imagen ruidosa";
    case 0x06: return "Timeout esperando dedo";
    case 0x07: return "Error: extracción de características";
    case 0x08: return "Error: buffer lleno";
    case 0x09: return "Error: página no encontrada";
    case 0x0A: return "Error: comando inválido";
    case 0x0B: return "Error: ancho de imagen";
    case 0x0C: return "Error: longitud de paquete";
    default:   return "Código desconocido";
  }
}

// ---------------- Globals ----------------
HardwareSerial fingerSerial(2);
uint8_t respBuf[64];
int respLen = 0;

// ---------------- Low-level packet I/O ----------------
void sendPacket(uint8_t cmd, const uint8_t* params = nullptr, uint8_t paramsLen = 0) {
  uint8_t buf[32];
  int idx = 0;
  // header + address
  buf[idx++] = HEADER[0];
  buf[idx++] = HEADER[1];
  memcpy(buf + idx, ADDRESS, 4); idx += 4;
  // packet type + length
  buf[idx++] = CMD_PACKET;
  uint16_t length = (1 + paramsLen) + 2;        // cmd + params + checksum
  buf[idx++] = (length >> 8) & 0xFF;
  buf[idx++] = length & 0xFF;
  // payload: cmd + params
  buf[idx++] = cmd;
  for(int i = 0; i < paramsLen; i++) buf[idx++] = params[i];
  // checksum
  uint16_t sum = CMD_PACKET + ((length>>8)&0xFF) + (length&0xFF) + cmd;
  for(int i = 0; i < paramsLen; i++) sum += params[i];
  buf[idx++] = (sum >> 8) & 0xFF;
  buf[idx++] = sum & 0xFF;
  // send
  fingerSerial.write(buf, idx);
}

void readPacket(uint16_t timeoutMs = 1000) {
  uint32_t start = millis();
  respLen = 0;
  while (millis() - start < timeoutMs) {
    while (fingerSerial.available() && respLen < sizeof(respBuf)) {
      respBuf[respLen++] = fingerSerial.read();
    }
  }
}

uint8_t parseAck(uint8_t& outPayload0) {
  // minimal ACK length = 12 bytes
  if (respLen < 12 || respBuf[6] != ACK_PACKET) {
    outPayload0 = 0xFF;
    return 0xFF;
  }
  // length field
  uint16_t length = (respBuf[7] << 8) | respBuf[8];
  uint16_t payloadLen = length - 2;
  outPayload0 = respBuf[9];
  return outPayload0;
}

// ---------------- High-level commands ----------------
uint8_t psGetImage() {
  sendPacket(PS_GET_IMAGE);
  readPacket();
  uint8_t code;
  parseAck(code);
  return code;
}

uint8_t psGenChar(uint8_t bufferId) {
  sendPacket(PS_GEN_CHAR, &bufferId, 1);
  readPacket();
  uint8_t code;
  parseAck(code);
  return code;
}

uint8_t psRegModel() {
  sendPacket(PS_REG_MODEL);
  readPacket();
  uint8_t code;
  parseAck(code);
  return code;
}

uint8_t psStore(uint16_t pageId, uint8_t bufferId) {
  uint8_t params[3] = { bufferId,
                        uint8_t(pageId >> 8),
                        uint8_t(pageId & 0xFF) };
  sendPacket(PS_STORE, params, 3);
  readPacket();
  uint8_t code;
  parseAck(code);
  return code;
}

uint8_t psSearch(uint8_t bufferId, uint16_t startPage, uint16_t numPages,
                 uint16_t& matchId, uint16_t& matchScore) {
  uint8_t params[5] = { bufferId,
                        uint8_t(startPage >> 8), uint8_t(startPage & 0xFF),
                        uint8_t(numPages >> 8),  uint8_t(numPages & 0xFF) };
  sendPacket(PS_SEARCH, params, 5);
  readPacket();
  uint8_t code;
  parseAck(code);
  if (code == 0x00 && respLen >= 15) {
    matchId    = (respBuf[10] << 8) | respBuf[11];
    matchScore = (respBuf[12] << 8) | respBuf[13];
  } else {
    matchId = matchScore = 0xFFFF;
  }
  return code;
}

uint8_t psLoadChar(uint16_t pageId, uint8_t bufferId) {
  uint8_t params[3] = { bufferId,
                        uint8_t(pageId >> 8),
                        uint8_t(pageId & 0xFF) };
  sendPacket(PS_LOAD_CHAR, params, 3);
  readPacket();
  uint8_t code;
  parseAck(code);
  return code;
}

uint8_t psMatch(uint8_t buf1, uint8_t buf2, uint16_t& score) {
  uint8_t params[2] = { buf1, buf2 };
  sendPacket(PS_MATCH, params, 2);
  readPacket();
  uint8_t code;
  parseAck(code);
  if (code == 0x00 && respLen >= 14) {
    score = (respBuf[10] << 8) | respBuf[11];
  } else {
    score = 0xFFFF;
  }
  return code;
}

// ---------------- Menu actions ----------------
void enrollFingerprint() {
  Serial.println("\nEnrolamiento de huella:");
  Serial.print("ID para almacenar (0-65535): ");
  while (!Serial.available());
  uint16_t id = Serial.parseInt();
  Serial.println(id);

  Serial.println("Coloca el dedo primera vez...");
  uint8_t c = psGetImage();
  Serial.printf(" GetImage → %s (0x%02X)\n", getConfirmationMessage(c), c);
  if (c != 0x00) return;

  Serial.println("GenChar buffer 1...");
  c = psGenChar(1);
  Serial.printf(" GenChar1 → %s (0x%02X)\n", getConfirmationMessage(c), c);
  if (c != 0x00) return;

  delay(1500);
  Serial.println("Quita y vuelve a colocar dedo...");
  c = psGetImage();
  Serial.printf(" GetImage → %s (0x%02X)\n", getConfirmationMessage(c), c);
  if (c != 0x00) return;

  Serial.println("GenChar buffer 2...");
  c = psGenChar(2);
  Serial.printf(" GenChar2 → %s (0x%02X)\n", getConfirmationMessage(c), c);
  if (c != 0x00) return;

  Serial.println("RegModel...");
  c = psRegModel();
  Serial.printf(" RegModel → %s (0x%02X)\n", getConfirmationMessage(c), c);
  if (c != 0x00) return;

  Serial.printf("Store en página %u...\n", id);
  c = psStore(id, 1);
  Serial.printf(" Store → %s (0x%02X)\n", getConfirmationMessage(c), c);
  if (c == 0x00) Serial.println("Enrolamiento completado.");
}

void searchFingerprint() {
  Serial.println("\nBúsqueda en base:");
  Serial.print("Página inicio (0): ");
  while (!Serial.available());
  uint16_t start = Serial.parseInt();
  Serial.println(start);
  Serial.print("Num páginas a buscar: ");
  while (!Serial.available());
  uint16_t count = Serial.parseInt();
  Serial.println(count);

  Serial.println("Coloca el dedo...");
  uint8_t c = psGetImage();
  Serial.printf(" GetImage → %s (0x%02X)\n", getConfirmationMessage(c), c);
  if (c != 0x00) return;

  Serial.println("GenChar buffer 1...");
  c = psGenChar(1);
  Serial.printf(" GenChar1 → %s (0x%02X)\n", getConfirmationMessage(c), c);
  if (c != 0x00) return;

  Serial.println("PS_Search...");
  uint16_t matchId, matchScore;
  c = psSearch(1, start, count, matchId, matchScore);
  if (c == 0x00) {
    Serial.printf("Match en ID=%u, Score=%u\n", matchId, matchScore);
  } else {
    Serial.printf("No match: %s (0x%02X)\n", getConfirmationMessage(c), c);
  }
}

void matchSpecific() {
  Serial.println("\nMatch contra ID específico:");
  Serial.print("ID a comparar: ");
  while (!Serial.available());
  uint16_t id = Serial.parseInt();
  Serial.println(id);

  Serial.println("Coloca el dedo...");
  uint8_t c = psGetImage();
  Serial.printf(" GetImage → %s (0x%02X)\n", getConfirmationMessage(c), c);
  if (c != 0x00) return;

  Serial.println("GenChar buffer 1...");
  c = psGenChar(1);
  Serial.printf(" GenChar1 → %s (0x%02X)\n", getConfirmationMessage(c), c);
  if (c != 0x00) return;

  Serial.printf("LoadChar ID=%u en buffer 2...\n", id);
  c = psLoadChar(id, 2);
  Serial.printf(" LoadChar → %s (0x%02X)\n", getConfirmationMessage(c), c);
  if (c != 0x00) return;

  Serial.println("PS_Match...");
  uint16_t score;
  c = psMatch(1, 2, score);
  if (c == 0x00) {
    Serial.printf("Match OK, Score=%u\n", score);
  } else {
    Serial.printf("No match: %s (0x%02X)\n", getConfirmationMessage(c), c);
  }
}

void printMenu() {
  Serial.println("\n=== Menú Sensor Huella ===");
  Serial.println("1) Enrolar huella");
  Serial.println("2) Buscar huella en base");
  Serial.println("3) Match contra ID específico");
  Serial.println("4) Salir (reinicia ESP32)");
  Serial.print("Elige opción: ");
}

void setup() {
  Serial.begin(CONSOLE_BAUDRATE);
  fingerSerial.begin(FINGERPRINT_BAUDRATE, SERIAL_8N1, FINGERPRINT_RX_PIN, FINGERPRINT_TX_PIN);
  delay(500);
  Serial.println("\nSensor de huella ESP32 inicializado.");
  printMenu();
}

void loop() {
  if (Serial.available()) {
    char opt = Serial.read();
    switch (opt) {
      case '1': enrollFingerprint(); break;
      case '2': searchFingerprint(); break;
      case '3': matchSpecific();   break;
      case '4': Serial.println("Reiniciando..."); ESP.restart(); break;
      default: 
        if (opt != '\r' && opt != '\n') Serial.println("Opción inválida.");
        break;
    }
    printMenu();
  }
}
