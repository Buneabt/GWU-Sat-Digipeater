/*
 * Adalogger - AX.25 payload storage with filtering
 */

#include <Wire.h>
#include <SPI.h>
#include <SD.h>

#define SD_CS_PIN 4
#define MAX_PACKET_SIZE 250
#define I2C_ADDRESS 4
#define AX25_HEADER_SIZE 15
#define MIN_PAYLOAD_SIZE 5

uint8_t packet_buffer[MAX_PACKET_SIZE];
uint8_t packet_index = 0;
uint8_t expected_length = 0;
int16_t packet_rssi = 0;
bool receiving_packet = false;
unsigned long packet_count = 0;

// Duplicate detection
uint8_t last_payload[50];
uint8_t last_payload_len = 0;

bool initSD() {
  return SD.begin(SD_CS_PIN);
}

bool createFiles() {
  if (!SD.exists("packets.txt")) {
    File f = SD.open("packets.txt", FILE_WRITE);
    if (f) {
      f.println("timestamp,packet_num,payload_length,rssi,payload_hex");
      f.close();
      return true;
    }
    return false;
  }
  return true;
}

uint8_t* extractPayload(uint8_t* packet, uint8_t packet_len, uint8_t* payload_len) {
  if (packet_len <= AX25_HEADER_SIZE) {
    *payload_len = 0;
    return NULL;
  }
  
  uint8_t start_offset = AX25_HEADER_SIZE;
  for (uint8_t i = AX25_HEADER_SIZE; i < packet_len - 2; i++) {
    if (packet[i] >= 0x20 && packet[i] <= 0x7E) {
      start_offset = i;
      break;
    }
  }
  
  *payload_len = packet_len - start_offset;
  return &packet[start_offset];
}

bool isDuplicatePayload(uint8_t* payload, uint8_t len) {
  if (len != last_payload_len) return false;
  
  for (uint8_t i = 0; i < len; i++) {
    if (payload[i] != last_payload[i]) return false;
  }
  return true;
}

bool hasMeaningfulContent(uint8_t* payload, uint8_t len) {
  if (len < MIN_PAYLOAD_SIZE) return false;
  
  uint8_t printable_count = 0;
  for (uint8_t i = 0; i < len; i++) {
    if (payload[i] >= 0x20 && payload[i] <= 0x7E) printable_count++;
  }
  
  return (printable_count * 10 >= len * 7);
}

void setup() {
  Serial.begin(9600);
  delay(1000);
  
  Serial.println("=== AX.25 Logger ===");
  
  if (!initSD()) {
    Serial.println("SD FAIL");
    while (1) delay(1000);
  }
  
  createFiles();
  Wire.begin(I2C_ADDRESS);
  Wire.onReceive(receiveEvent);
  
  Serial.println("Ready");
}

void loop() {
  delay(100);
}

void receiveEvent(int howMany) {
  while (Wire.available()) {
    uint8_t byte_received = Wire.read();
    
    if (!receiving_packet && byte_received == 0xAA) {
      handleStartPacket();
    } else if (receiving_packet && byte_received == 0xDB) {
      handleDataChunk();
    } else if (receiving_packet && byte_received == 0x55) {
      handleEndPacket();
    }
  }
}

void handleStartPacket() {
  receiving_packet = true;
  packet_index = 0;
  
  if (Wire.available() >= 3) {
    expected_length = Wire.read();
    uint8_t rssi_low = Wire.read();
    uint8_t rssi_high = Wire.read();
    packet_rssi = (int16_t)((rssi_high << 8) | rssi_low);
  }
}

void handleDataChunk() {
  if (Wire.available() >= 1) {
    uint8_t chunk_len = Wire.read();
    
    for (uint8_t i = 0; i < chunk_len && Wire.available(); i++) {
      if (packet_index < MAX_PACKET_SIZE) {
        packet_buffer[packet_index++] = Wire.read();
      } else {
        Wire.read();
      }
    }
  }
}

void handleEndPacket() {
  if (saveFilteredPacket()) {
    Serial.print("Saved #");
    Serial.println(packet_count++);
  }
  
  receiving_packet = false;
  packet_index = 0;
}

bool saveFilteredPacket() {
  if (packet_index == 0) return false;
  
  uint8_t payload_len = 0;
  uint8_t* payload = extractPayload(packet_buffer, packet_index, &payload_len);
  
  if (!hasMeaningfulContent(payload, payload_len) || isDuplicatePayload(payload, payload_len)) {
    return false;
  }
  
  File f = SD.open("packets.txt", FILE_WRITE);
  if (!f) return false;
  
  f.print(millis());
  f.print(",");
  f.print(packet_count);
  f.print(",");
  f.print(payload_len);
  f.print(",");
  f.print(packet_rssi);
  f.print(",");
  
  for (uint8_t i = 0; i < payload_len; i++) {
    if (payload[i] < 0x10) f.print("0");
    f.print(payload[i], HEX);
  }
  
  f.println();
  f.close();
  
  // Store for duplicate detection
  if (payload_len < 50) {
    for (uint8_t i = 0; i < payload_len; i++) {
      last_payload[i] = payload[i];
    }
    last_payload_len = payload_len;
  }
  
  return true;
}