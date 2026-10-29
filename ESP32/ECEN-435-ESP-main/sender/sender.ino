// Wi-Fi JPEG/GPS/TOF Transmitter Over ESP-NOW v1
// *** Enter your receiver ESP's MAC address into the 'peerAddress' buffer.
// Target: ESP32-C6-WROOM-1 (transmits to an ESP8266 receiver)
// ECEN 435 University of Nebraska–Lincoln
// Author: Paul Scalise

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <algorithm>
//#include "jpeg_test.h"  // For debugging purposes. Found in ../validation/sender/
#include "stdbool.h"

#define _inline static inline __attribute__((always_inline))

#define ESP_UART_BAUD   115200
#define ESP_UART        Serial0   // UART0 -> STM32. Always UART0, whatever the USB CDC menu says.
#define ESP_DEBUG       Serial    // USB Serial/JTAG when "USB CDC On Boot" is Enabled, else UART0.

#if !ARDUINO_USB_CDC_ON_BOOT
#warning "Tools > USB CDC On Boot is Disabled: debug output will be muxed onto the STM32 UART line."
#endif

// UART0 pins on the ESP32-C6-WROOM-1 (U0RXD / U0TXD).
static const int8_t UART_RX_PIN = RX;   // GPIO17 <- STM32 TX
static const int8_t UART_TX_PIN = TX;   // GPIO16 -> STM32 RX

// The ESP8266 receiver parks on channel 1 when it is not associated with an AP.
// ESP-NOW only crosses when both radios sit on the same channel.
static const uint8_t ESPNOW_CHANNEL = 1;

static const unsigned      PACKET_GAP_US   = 6000;
static const size_t        MAX_PACKET_SIZE = 32 * 1024;
static uint8_t             packetBuf[MAX_PACKET_SIZE];
static size_t              packetLen;

// Replace with your receiver’s MAC
static uint8_t peerAddress[6] = {0x48, 0x3F, 0xDA, 0x9D, 0x43, 0x6C};

// ESPNOW v1 max payload per packet
static const size_t CHUNK_SIZE = 250;


void onDataSent(const esp_now_send_info_t *tx_info, esp_now_send_status_t status) {
  // optionally ACK back to STM32
  // ESP_UART.write(status);
}

_inline void readHeaderTypeBytes(uint8_t *dst, size_t N) {
  size_t got = 0;
  while (got < N) {
    if (ESP_UART.available()) {
      dst[got++] = ESP_UART.read();
    }
    yield();
  }
}

bool receivePacket() {
  readHeaderTypeBytes(packetBuf, 3);
  packetLen = 3;

  if (memcmp(packetBuf, "IMG", 3) == 0) {
    // Read until JPEG EOI marker
    while (packetLen + 1 < MAX_PACKET_SIZE) {
      if (!ESP_UART.available()) { yield(); continue; }
      uint8_t b = ESP_UART.read();
      packetBuf[packetLen++] = b;
      if (packetBuf[packetLen - 2] == 0xFF && packetBuf[packetLen - 1] == 0xD9){
        return true;
      }
    }
    return false;
  }
  else if (memcmp(packetBuf, "GPS", 3) == 0 || memcmp(packetBuf, "TOF", 3) == 0) {
    // Read until newline
    while (packetLen < MAX_PACKET_SIZE) {
      if (!ESP_UART.available()) { yield(); continue; }
      uint8_t b = ESP_UART.read();
      packetBuf[packetLen++] = b;
      if (b == '\n') return true;
    }
    return false;
  }
  return false;
}

bool sendBufferESPNOW(uint8_t *buf, size_t len) {
  size_t off = 0;
  while (off < len) {
    size_t n = std::min(CHUNK_SIZE, len - off);
    if (esp_now_send(peerAddress, (uint8_t*)buf + off, n) != ESP_OK) {
      return false;
    }
    for (size_t i = 0; i < n; ++i) {
      ESP_DEBUG.printf("%02X ", buf[off + i]);
    }
    off += n;
    // Pace the chunks: the ESP32 ESP-NOW TX queue is bounded, and back-to-back
    // sends of a full JPEG will otherwise start returning ESP_ERR_ESPNOW_NO_MEM.
    if (off < len) delayMicroseconds(PACKET_GAP_US);
  }
  return true;
}

void ESP_NOW_Init(){
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.setSleep(false);   // modem sleep drops ESP-NOW frames

  // The C6 advertises 802.11ax by default; an ESP8266 cannot demodulate ax rates.
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    ESP_DEBUG.println(F("*** Error initializing ESP-NOW"));
    while (true) delay(1000);
  }

  esp_now_register_send_cb(onDataSent);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, peerAddress, 6);
  peerInfo.channel = 0;                 // 0 = use the interface's current channel
  peerInfo.ifidx   = WIFI_IF_STA;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    while (true){
      ESP_DEBUG.println(F("*** Failed to add ESP-NOW peer"));
      delay(1000);
    }
  }
}

void setup() {
  ESP_UART.setRxBufferSize(4096);   // the ESP32 UART driver buffers in software; keep the STM32 stream from overrunning during a send
  ESP_UART.begin(ESP_UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  while (!ESP_UART);
#if ARDUINO_USB_CDC_ON_BOOT
  ESP_DEBUG.begin(ESP_UART_BAUD);
#endif
  ESP_NOW_Init();
}

void loop() {
  if (receivePacket()) {
    if (!sendBufferESPNOW(packetBuf, packetLen)) {
      ESP_DEBUG.println(F("*** Send error"));
    }
  }
  yield();
}
