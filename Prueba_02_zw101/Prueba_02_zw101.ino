#include <HardwareSerial.h>
#include <Preferences.h>

// ===== PINS =====
#define ZW101_RX_PIN    16   // al TX del módulo
#define ZW101_TX_PIN    17   // al RX del módulo
#define TOUCH_OUT_PIN   23   // IRQ Wake-up (módulo PIN2)

// ===== UART ZW101 =====
HardwareSerial zwSerial(2);
const uint32_t ZW101_BAUD = 57600;
const uint8_t DEV_ADDR[4] = { 0xFF,0xFF,0xFF,0xFF };

// ===== NVS =====
Preferences prefs;

// ===== Banderas de IRQ =====
volatile bool fingerDetected = false;

void IRAM_ATTR onFingerIRQ() {
  fingerDetected = true;
}

// ===== PROTOTIPOS =====
void menu();
void enroll();
void search();
bool sendCmd(uint8_t code, uint8_t* params, uint16_t paramsLen);
bool readAck(uint8_t& confirmation);
bool readPacket(uint8_t& pkgId, uint8_t* data, uint16_t& dataLen);
uint16_t calcChecksum(const uint8_t* buf, uint16_t len);

void setup() {
  Serial.begin(115200);
  while(!Serial);

  // UART del módulo
  zwSerial.begin(ZW101_BAUD, SERIAL_8N1, ZW101_RX_PIN, ZW101_TX_PIN);

  // IRQ touch-out
  pinMode(TOUCH_OUT_PIN, INPUT_PULLDOWN);
  attachInterrupt(TOUCH_OUT_PIN, onFingerIRQ, RISING);

  // NVS
  prefs.begin("fingerDB", false);

  Serial.println("=== ZW101 Fingerprint Menu ===");
}

void loop() {
  menu();
  delay(200);
}

void menu() {
  Serial.println("\n1. Guardar huella");
  Serial.println("2. Buscar huella");
  Serial.print("Opción: ");
  while(!Serial.available()) delay(10);
  char c = Serial.read(); Serial.read(); // descartar '\n'
  Serial.println(c);

  if (c=='1') enroll();
  else if (c=='2') search();
  else Serial.println("Opción inválida.");
}

// ===== ENROLL =====
void enroll() {
  Serial.println("\n== Enroll ==");
  // Esperar dedo (IRQ)
  Serial.println("Coloca el dedo...");
  fingerDetected = false;
  while (!fingerDetected) delay(10);
  fingerDetected = false;

  // 1) GetImage
  if (!sendCmd(0x29,nullptr,0)) { Serial.println("Error GetImage"); return; }
  uint8_t ack; readAck(ack);
  if (ack!=0x00) { Serial.println("No detecta dedo."); return; }

  // 2) GenChar buf1
  uint8_t buf1 = 1;
  sendCmd(0x02, &buf1, 1); readAck(ack);
  if (ack!=0x00) { Serial.println("Error GenChar1"); return; }

  Serial.println("Retira el dedo...");
  delay(1000);

  // 3) Re-esperar dedo
  Serial.println("Vuelve a colocar...");
  fingerDetected = false;
  while (!fingerDetected) delay(10);
  fingerDetected = false;

  sendCmd(0x29,nullptr,0); readAck(ack);
  if (ack!=0x00) { Serial.println("Error GetImage2"); return; }
  uint8_t buf2 = 2;
  sendCmd(0x02, &buf2, 1); readAck(ack);
  if (ack!=0x00) { Serial.println("Error GenChar2"); return; }

  // 4) RegModel
  sendCmd(0x05,nullptr,0); readAck(ack);
  if (ack!=0x00) { Serial.println("Error RegModel"); return; }

  // 5) StoreChar en slot 0 (ajusta si quieres dinámico)
  uint8_t sp[2] = {1, 0};
  sendCmd(0x06, sp, 2); readAck(ack);
  if (ack!=0x00) { Serial.println("Error StoreChar"); return; }

  Serial.println("Huella almacenada en ID 0");
  // Pedir nombre
  Serial.print("Nombre para ID 0: ");
  String name = Serial.readStringUntil('\n');
  name.trim();
  prefs.putString("u0", name);
  Serial.printf("Guardado: %s\n", name.c_str());
}

// ===== SEARCH =====
void search() {
  Serial.println("\n== Search ==");
  // Esperar dedo
  Serial.println("Coloca el dedo...");
  fingerDetected = false;
  while (!fingerDetected) delay(10);
  fingerDetected = false;

  // GetImage + GenChar buf1
  sendCmd(0x29,nullptr,0); readAck(*(new uint8_t));
  uint8_t buf1 = 1;
  sendCmd(0x02,&buf1,1); readAck(*(new uint8_t));

  // PS_SearchNow (0x3E)
  uint8_t params[2] = {0x00, 0x32}; // páginas 0-50
  sendCmd(0x3E, params, 2);
  uint8_t ack; readAck(ack);
  if (ack!=0x00) { Serial.println("No encontrada."); return; }

  // Leer paquete con [ID, score_hi, score_lo]
  uint8_t pkgId, data[4]; uint16_t len;
  readPacket(pkgId, data, len);
  uint8_t id    = data[0];
  uint16_t sc   = (data[1]<<8) | data[2];
  Serial.printf("Match ID=%u  Score=%u\n", id, sc);

  String name = prefs.getString("u"+String(id), "SIN_REG");
  Serial.printf("Huella de: %s\n", name.c_str());
}

// ===== COMS =====
bool sendCmd(uint8_t code, uint8_t* params, uint16_t L) {
  uint16_t pktLen = 1 + L + 2;
  uint8_t hdr[9] = {
    0xEF,0x01,
    DEV_ADDR[0],DEV_ADDR[1],DEV_ADDR[2],DEV_ADDR[3],
    0x01,
    uint8_t(pktLen>>8), uint8_t(pktLen&0xFF)
  };
  zwSerial.write(hdr,9);
  zwSerial.write(code);
  if (L) zwSerial.write(params,L);

  uint16_t csum = 0x01 + (pktLen>>8) + (pktLen&0xFF) + code;
  for(uint16_t i=0;i<L;i++) csum += params[i];
  zwSerial.write(uint8_t(csum>>8));
  zwSerial.write(uint8_t(csum&0xFF));
  return true;
}

bool readAck(uint8_t& conf) {
  uint8_t buf[9];
  for(int i=0;i<9;i++){ while(!zwSerial.available()) delay(1); buf[i]=zwSerial.read(); }
  conf = buf[7];
  return buf[6]==0x07;
}

bool readPacket(uint8_t& pkgId, uint8_t* data, uint16_t& DL) {
  uint8_t h[9];
  for(int i=0;i<9;i++){ while(!zwSerial.available()) delay(1); h[i]=zwSerial.read(); }
  pkgId = h[6];
  DL = (h[7]<<8)|h[8];
  for(int i=0;i<DL;i++){ while(!zwSerial.available()) delay(1); data[i]=zwSerial.read(); }
  zwSerial.read(); zwSerial.read(); // checksum
  return true;
}

uint16_t calcChecksum(const uint8_t* b, uint16_t n) {
  uint16_t s=0;
  while(n--) s+= *b++;
  return s;
}
