/*
 * EnduroSat-Aware AX.25 Digipeater 
 * Detects EnduroSat signals and implements transmission windows
 * Follows NASA safety-critical coding principles
 */

#include <Wire.h>
#include <RH_RF95.h>

#define RFM95_CS   8
#define RFM95_INT  7
#define RFM95_RST  4
#define STATUS_PIN 12  

// Config
#define DIGI_FREQ_MHZ      436.42
#define DIGI_POWER_DBM     20
#define MYCALL             "KQ4NPQ"
#define I2C_CHUNK_SIZE     16

// AX.25 Constants
#define AX25_FLAG          0x7E
#define MAX_DIGI_HOPS      4

// EnduroSat Detection
#define ENDUROSAT_CALLSIGN "XX0UHF" 
#define ESTTC_HEADER       "ES+"     

// Transmission Window Management
#define TRANSMISSION_WINDOW_MS  (8 * 60 * 1000UL) 
#define QUIET_PERIOD_MS         (1 * 20 * 1000UL) //Change this back to 10 mins once done
#define MAX_PACKET_BUFFER       3      

RH_RF95 rf95(RFM95_CS, RFM95_INT);
unsigned long packet_count = 0;

// Transmission window state
bool endurosat_detected = false;
unsigned long last_endurosat_time = 0;
unsigned long transmission_window_start = 0;
bool in_transmission_window = false;
bool in_quiet_period = false;
bool digipeater_alive = true;  // Added missing variable

// Packet buffering for quiet periods
struct BufferedPacket {
  uint8_t data[RH_RF95_MAX_MESSAGE_LEN];
  uint8_t length;
  int16_t rssi;
  bool valid;
};

BufferedPacket packet_buffer[MAX_PACKET_BUFFER];
uint8_t buffer_head = 0;
uint8_t buffer_tail = 0;
uint8_t buffered_count = 0;

// Status/heartbeat management
unsigned long last_heartbeat_time = 0;
#define HEARTBEAT_INTERVAL_MS 1000  // 1 Hz heartbeat
bool status_pin_state = false;

/*
 * Initialize radio
 */
bool initRadio() {
  pinMode(RFM95_CS, OUTPUT);
  digitalWrite(RFM95_CS, HIGH);
  
  if (!rf95.init()) return false;
  if (!rf95.setFrequency(DIGI_FREQ_MHZ)) return false;
  
  rf95.setTxPower(DIGI_POWER_DBM, false);
  rf95.setSignalBandwidth(125000);
  rf95.setSpreadingFactor(7);
  rf95.setCodingRate4(5);
  
  return true;
}

/*
 * Parse callsign from AX.25 address
 */
void parseCallsign(uint8_t* addr, char* call) {
  if (addr == NULL || call == NULL) return;
  
  for (uint8_t i = 0; i < 6; i++) {
    call[i] = (addr[i] >> 1) & 0x7F;
    if (call[i] == ' ') call[i] = '\0';
  }
  call[6] = '\0';
  
  // Remove trailing spaces
  for (int8_t i = 5; i >= 0; i--) {
    if (call[i] == ' ' || call[i] == '\0') {
      call[i] = '\0';
    } else {
      break;
    }
  }
}

/*
 * Detect EnduroSat transmission patterns
 */
bool detectEnduroSat(uint8_t* data, uint8_t len) {
  if (data == NULL || len < 16) return false;
  
  // Check for ESTTC protocol header in raw data
  for (uint8_t i = 0; i < len - 3; i++) {
    if (data[i] == 'E' && data[i+1] == 'S' && data[i+2] == '+') {
      return true;
    }
  }
  
  // Check for EnduroSat callsign in AX.25 header
  char source_call[8] = {0};
  uint8_t pos = 0;
  
  // Skip flag if present
  if (data[pos] == AX25_FLAG) pos++;
  
  // Skip destination (7 bytes)
  pos += 7;
  
  // Parse source
  if (pos + 7 <= len) {
    parseCallsign(&data[pos], source_call);
    if (strncmp(source_call, ENDUROSAT_CALLSIGN, 6) == 0) {
      return true;
    }
  }
  
  return false;
}

/*
 * Check if packet should be digipeated
 */
bool shouldDigipeat(uint8_t* data, uint8_t len) {
  if (data == NULL || len < 16) return false;
  
  char source_call[8] = {0};
  uint8_t pos = 0;
  uint8_t hop_count = 0;
  
  // Skip flag if present
  if (data[pos] == AX25_FLAG) pos++;
  
  // Skip destination (7 bytes)
  pos += 7;
  
  // Parse source
  if (pos + 7 > len) return false;
  parseCallsign(&data[pos], source_call);
  pos += 7;
  
  // Don't digipeat own packets
  if (strncmp(source_call, MYCALL, strlen(MYCALL)) == 0) {
    return false;
  }
  
  // Count hops
  while (pos < len && hop_count < MAX_DIGI_HOPS) {
    if (data[pos - 1] & 0x01) break;
    hop_count++;
    pos += 7;
  }
  
  return (hop_count < MAX_DIGI_HOPS);
}

/*
 * Send packet to logger via I2C 
 */
bool sendToLogger(uint8_t* data, uint8_t len, int16_t rssi) {
  if (data == NULL || len == 0) return false;
  
  // Send header
  Wire.beginTransmission(4);  // Your logger is at address 4
  Wire.write(0xAA);
  Wire.write(len);
  Wire.write(rssi & 0xFF);
  Wire.write((rssi >> 8) & 0xFF);
  if (Wire.endTransmission() != 0) return false;
  
  delay(5);
  
  // Send data in chunks
  uint8_t bytes_sent = 0;
  while (bytes_sent < len) {
    uint8_t chunk_size = (len - bytes_sent > I2C_CHUNK_SIZE) ? 
                         I2C_CHUNK_SIZE : (len - bytes_sent);
    
    Wire.beginTransmission(4);
    Wire.write(0xDB);
    Wire.write(chunk_size);
    
    for (uint8_t i = 0; i < chunk_size; i++) {
      Wire.write(data[bytes_sent + i]);
    }
    
    if (Wire.endTransmission() != 0) return false;
    
    bytes_sent += chunk_size;
    delay(5);
  }
  
  // Send end marker
  Wire.beginTransmission(4);
  Wire.write(0x55);
  return (Wire.endTransmission() == 0);
}

/*
 * Buffer packet during quiet period
 */
bool bufferPacket(uint8_t* data, uint8_t len, int16_t rssi) {
  if (buffered_count >= MAX_PACKET_BUFFER) {
    return false; // Buffer full
  }
  
  packet_buffer[buffer_head].length = len;
  packet_buffer[buffer_head].rssi = rssi;
  packet_buffer[buffer_head].valid = true;
  
  for (uint8_t i = 0; i < len; i++) {
    packet_buffer[buffer_head].data[i] = data[i];
  }
  
  buffer_head = (buffer_head + 1) % MAX_PACKET_BUFFER;
  buffered_count++;
  
  return true;
}

/*
 * Process buffered packets during transmission window
 */
void processBufferedPackets() {
  while (buffered_count > 0 && in_transmission_window) {
    BufferedPacket* pkt = &packet_buffer[buffer_tail];
    
    if (pkt->valid && shouldDigipeat(pkt->data, pkt->length)) {
      if (sendToLogger(pkt->data, pkt->length, pkt->rssi)) {
        Serial.print("Buffered packet sent - ");
        packet_count++;
      }
    }
    
    pkt->valid = false;
    buffer_tail = (buffer_tail + 1) % MAX_PACKET_BUFFER;
    buffered_count--;
    
    delay(10); // Small delay between buffered packets
  }
}

/*
 * Update status heartbeat pin
 * Provides 1 Hz heartbeat signal to OBC for health monitoring
 */
void updateStatusHeartbeat() {
  unsigned long current_time = millis();

  if (current_time - last_heartbeat_time >= HEARTBEAT_INTERVAL_MS) {
    status_pin_state = !status_pin_state;
    digitalWrite(STATUS_PIN, status_pin_state ? HIGH : LOW);
    last_heartbeat_time = current_time;
  }
}

/*
 * Update transmission window state
 */
void updateTransmissionWindow() {
  unsigned long current_time = millis();
  
  // Check if quiet period has ended
  if (in_quiet_period && 
      (current_time - last_endurosat_time) >= QUIET_PERIOD_MS) {
    in_quiet_period = false;
    in_transmission_window = true;
    transmission_window_start = current_time;
    
    Serial.println("Transmission window OPEN - processing buffered packets");
    processBufferedPackets();
  }
  
  // Check if transmission window has ended
  if (in_transmission_window && 
      (current_time - transmission_window_start) >= TRANSMISSION_WINDOW_MS) {
    in_transmission_window = false;
    Serial.println("Transmission window CLOSED");
  }
}

/*
 * I2C request event - send status to OBC
 */
void requestEvent() {
  uint8_t status = 0;
  if (digipeater_alive) status |= 0x01;
  if (in_transmission_window) status |= 0x02;
  if (in_quiet_period) status |= 0x04;
  
  Wire.write(status);
  Serial.print("I2C status request: 0x");
  Serial.println(status, HEX);
}

/*
 * Setup
 */
void setup() {
  Serial.begin(9600);
  delay(2000);

  Serial.println("=== EnduroSat-Aware Digipeater ===");
  Serial.print("Callsign: ");
  Serial.println(MYCALL);
  Serial.print("Transmission window: ");
  Serial.print(TRANSMISSION_WINDOW_MS / 60000);
  Serial.println(" minutes");
  Serial.print("Quiet period: ");
  Serial.print(QUIET_PERIOD_MS / 60000);
  Serial.println(" minutes");


  if (!initRadio()) {
    Serial.println("Radio FAILED");
    while(1);
  }
  
  Serial.println("Radio OK");
  Serial.println("Ready - listening for EnduroSat signals");
  
  // Initialize in transmission window
  in_transmission_window = true;
  transmission_window_start = millis();
}

/*
 * Main loop
 */
void loop() {
  uint8_t buf[RH_RF95_MAX_MESSAGE_LEN];
  uint8_t len = sizeof(buf);

  // Update status heartbeat for OBC monitoring
  updateStatusHeartbeat();

  // Update transmission window state
  updateTransmissionWindow();
  
  if (rf95.available()) {
    if (rf95.recv(buf, &len)) {
      int16_t rssi = rf95.lastRssi();
      
      // Check for EnduroSat signals
      if (detectEnduroSat(buf, len)) {
        endurosat_detected = true;
        last_endurosat_time = millis();
        in_quiet_period = true;
        in_transmission_window = false;
        
        Serial.print("EnduroSat detected! Entering quiet period for ");
        Serial.print(QUIET_PERIOD_MS / 60000);
        Serial.println(" minutes");
        
        // Always log EnduroSat packets
        sendToLogger(buf, len, rssi);
        return;
      }
      
      Serial.print("RX #");
      Serial.print(packet_count);
      Serial.print(" len=");
      Serial.print(len);
      Serial.print(" RSSI=");
      Serial.print(rssi);
      Serial.print(" - ");
      
      if (shouldDigipeat(buf, len)) {
        if (in_transmission_window) {
          // Normal transmission - send immediately
          if (sendToLogger(buf, len, rssi)) {
            Serial.println("Stored");
            packet_count++;
          } else {
            Serial.println("I2C FAIL");
          }
        } else if (in_quiet_period) {
          // Buffer during quiet period
          if (bufferPacket(buf, len, rssi)) {
            Serial.print("Buffered (");
            Serial.print(buffered_count);
            Serial.println("/3)");
          } else {
            Serial.println("Buffer full - dropped");
          }
        } else {
          Serial.println("Outside window - dropped");
        }
      } else {
        Serial.println("Filtered");
      }
    }
  }
  
  delay(10);
}