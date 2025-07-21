#include <HardwareSerial.h>
#include <Preferences.h>

// ===== PROTOTIPOS =====
void menu();
void doEnroll();
void doSearch();
bool captureImageWithRetry(uint8_t instrCode, uint8_t& ack);
bool sendCmd(uint8_t code, uint8_t* params, uint16_t paramsLen);
bool readAck(uint8_t& confirmation);
bool readPacket(uint8_t& pkgId, uint8_t* data, uint16_t& dataLen);
const char* decodeAck(uint8_t instr, uint8_t code);

// ===== PINS =====
#define ZW101_RX_PIN    16
#define ZW101_TX_PIN    17
#define TOUCH_OUT_PIN   23

// ===== UART ZW101 =====
HardwareSerial zwSerial(2);
const uint32_t ZW101_BAUD = 57600;
const uint8_t DEV_ADDR[4] = { 0xFF,0xFF,0xFF,0xFF };

// ===== NVS =====
Preferences prefs;

// ===== IRQ FLAG =====
volatile bool fingerDetected = false;
void IRAM_ATTR onFingerIRQ() { fingerDetected = true; }

void setup() {
  Serial.begin(115200);
  while(!Serial);

  // Inicializa UART módulo
  zwSerial.begin(ZW101_BAUD, SERIAL_8N1, ZW101_RX_PIN, ZW101_TX_PIN);

  // Configura TOUCH_OUT IRQ
  pinMode(TOUCH_OUT_PIN, INPUT_PULLDOWN);
  attachInterrupt(TOUCH_OUT_PIN, onFingerIRQ, RISING);

  // Abre NVS
  prefs.begin("fingerDB", false);
  // Si no existe, inicializa nextId a 0
  if (!prefs.isKey("nextId")) prefs.putUInt("nextId", 0);

  Serial.println(F("=== ZW101 Fingerprint Menu ==="));
}

void loop() {
  menu();
  delay(200);
}

void menu() {
  Serial.println();
  Serial.println(F("1. Guardar huella"));
  Serial.println(F("2. Buscar huella"));
  Serial.print  (F("Opción: "));
  while (!Serial.available()) delay(10);
  char c = Serial.read(); Serial.read();
  Serial.println(c);

  if (c == '1') doEnroll();
  else if (c == '2') doSearch();
  else Serial.println(F("Opción inválida."));
}

// ===== ENROLL =====
void doEnroll() {
  Serial.println("\n== Enroll ==");
  uint8_t ack;

  // 1) Captura con retry
  if (!captureImageWithRetry(0x29, ack)) {
    Serial.println(F("ERROR: no capturó imagen."));  
    return;
  }

  // 2) GenChar buf1
  uint8_t buf1 = 1;
  sendCmd(0x02, &buf1, 1);
  readAck(ack);
  Serial.printf("PS_GenChar(1) ACK=0x%02X (%s)\n",
                ack, decodeAck(0x02,ack));
  if (ack!=0x00) return;

  delay(500);

  // 3) Segunda captura
  if (!captureImageWithRetry(0x29, ack)) {
    Serial.println(F("ERROR: no segunda imagen."));  
    return;
  }

  // 4) GenChar buf2
  uint8_t buf2 = 2;
  sendCmd(0x02, &buf2, 1);
  readAck(ack);
  Serial.printf("PS_GenChar(2) ACK=0x%02X (%s)\n",
                ack, decodeAck(0x02,ack));
  if (ack!=0x00) return;

  // 5) RegModel retry
  bool regOk = false;
  for(int r=1; r<=3; ++r){
    delay(50);
    while(zwSerial.available()) zwSerial.read();
    sendCmd(0x05, nullptr, 0);
    readAck(ack);
    Serial.printf("PS_RegModel intento %d → ACK=0x%02X (%s)\n",
                  r,ack,decodeAck(0x05,ack));
    if (ack==0x00){ regOk=true; break; }
  }
  if (!regOk) {
    Serial.println(F("ERROR: RegModel falló."));  
    return;
  }

  // 6) Asigna ID desde nextId
  uint16_t page = prefs.getUInt("nextId", 0);

  // 7) StoreChar retry
  bool storeOk = false;
  uint8_t sp[2] = { 1, uint8_t(page) };
  for(int s=1; s<=3; ++s){
    delay(50);
    while(zwSerial.available()) zwSerial.read();
    sendCmd(0x06, sp, 2);
    readAck(ack);
    Serial.printf("PS_StoreChar intento %d → ACK=0x%02X (%s)\n",
                  s,ack,decodeAck(0x06,ack));
    if (ack==0x00){ storeOk=true; break; }
  }
  if (!storeOk) {
    Serial.println(F("ERROR: StoreChar falló."));  
    return;
  }

  // 8) Incrementa nextId y guarda
  prefs.putUInt("nextId", page + 1);

  Serial.printf("Enrolado en ID %u\n", page);
  Serial.print(F("Nombre para ID "));
  Serial.print(page);
  Serial.print(F(": "));
  String name = Serial.readStringUntil('\n'); name.trim();
  prefs.putString((String("u")+page).c_str(), name);
  Serial.printf("Guardado: %s\n", name.c_str());
}

// ===== SEARCH =====
void doSearch() {
  Serial.println("\n== Search ==");
  uint8_t ack;

  if (!captureImageWithRetry(0x29, ack)) {
    Serial.println(F("ERROR: no capturó imagen."));  
    return;
  }

  // GenChar buf1
  uint8_t buf1 = 1;
  sendCmd(0x02, &buf1, 1);
  readAck(ack);
  Serial.printf("PS_GenChar ACK=0x%02X (%s)\n",
                ack, decodeAck(0x02,ack));
  if (ack!=0x00) return;

  // PS_SearchNow
  uint8_t sp[2] = {0x00, 0x32};
  sendCmd(0x3E, sp, 2);
  readAck(ack);
  Serial.printf("PS_SearchNow ACK=0x%02X (%s)\n",
                ack, decodeAck(0x3E,ack));
  if (ack!=0x00) return;

  // Leer resultado
  uint8_t pkgId, dataBuf[4]; uint16_t len;
  if (readPacket(pkgId, dataBuf, len) && len>=3) {
    uint8_t id    = dataBuf[0];
    uint16_t sc   = (uint16_t(dataBuf[1])<<8)|dataBuf[2];
    Serial.printf("Match ID=%u  Score=%u\n", id, sc);
    String key  = String("u") + id;
    String name = prefs.getString(key.c_str(), "SIN_REG");
    Serial.printf("Huella de: %s\n", name.c_str());
  } else {
    Serial.println(F("Error al leer resultado."));
  }
}

// ===== HELPERS =====

bool captureImageWithRetry(uint8_t instrCode, uint8_t& ack) {
  for (int i = 1; i <= 5; ++i) {
    Serial.printf("Intento %d: coloca el dedo...\n", i);
    fingerDetected = false; while (!fingerDetected) delay(10);
    delay(50); fingerDetected = false;
    sendCmd(instrCode, nullptr, 0);
    readAck(ack);
    Serial.printf("PS_GetImage ACK=0x%02X (%s)\n",
                  ack, decodeAck(instrCode,ack));
    if (ack == 0x00) return true;
  }
  return false;
}

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
  if (L) zwSerial.write(params, L);
  uint16_t sum = 0x01 + (pktLen>>8) + (pktLen&0xFF) + code;
  for(uint16_t i=0;i<L;i++) sum += params[i];
  zwSerial.write(uint8_t(sum>>8));
  zwSerial.write(uint8_t(sum&0xFF));
  return true;
}

bool readAck(uint8_t& conf) {
  uint8_t buf[9];
  for(int i=0;i<9;i++){
    while(!zwSerial.available()) delay(1);
    buf[i] = zwSerial.read();
  }
  conf = buf[7];
  return (buf[6] == 0x07);
}

bool readPacket(uint8_t& pkgId, uint8_t* data, uint16_t& DL) {
  uint8_t hdr[9];
  for(int i=0;i<9;i++){
    while(!zwSerial.available()) delay(1);
    hdr[i] = zwSerial.read();
  }
  pkgId = hdr[6];
  DL    = (uint16_t(hdr[7])<<8) | hdr[8];
  for(uint16_t i=0;i<DL;i++){
    while(!zwSerial.available()) delay(1);
    data[i] = zwSerial.read();
  }
  zwSerial.read(); zwSerial.read();  // descarta checksum
  return true;
}

const char* decodeAck(uint8_t instr, uint8_t code) {
  // ver definición genérica :contentReference[oaicite:0]{index=0}L46-L54
  switch(instr) {
    case 0x29:
      if (code==0x00) return "Éxito";
      if (code==0x01) return "Err recepción";
      if (code==0x02) return "No finger";
      return "Desconocido";
    case 0x02:
      switch(code){
        case 0x00: return "Éxito";
        case 0x06: return "Caótico";
        case 0x07: return "Pocos puntos";
        case 0x08: return "No correlación";
        case 0x0A: return "Fusión falló";
        case 0x15: return "Buf vacío";
        case 0x28: return "Ya existe";
      }
      return "Desconocido";
    case 0x05: return code==0x00?"Éxito":"Err registro";
    case 0x06: return code==0x00?"Éxito":"Err store";
    case 0x3E: return code==0x00?"Éxito":"No match";
    default:   return "";
  }
}
