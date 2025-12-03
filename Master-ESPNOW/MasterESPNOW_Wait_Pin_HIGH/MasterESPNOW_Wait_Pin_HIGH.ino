#include "ESP32_NOW.h"
#include "WiFi.h"
#include "esp_sleep.h"

#define WAKEUP_PIN 0                // LP_GPIO0 (A0/D0)
#define ESPNOW_WIFI_CHANNEL 6
#define BROADCAST_INTERVAL_MS 500   // Interval between sends
#define BROADCAST_TIME_MS 5000      // Total broadcast duration (5 seconds)

class ESP_NOW_Broadcast_Peer : public ESP_NOW_Peer {
public:
  ESP_NOW_Broadcast_Peer(uint8_t channel, wifi_interface_t iface, const uint8_t *lmk)
    : ESP_NOW_Peer(ESP_NOW.BROADCAST_ADDR, channel, iface, lmk) {}

  ~ESP_NOW_Broadcast_Peer() { remove(); }

  bool begin() {
    if (!ESP_NOW.begin() || !add()) {
      log_e("Failed to initialize ESP-NOW or register broadcast peer");
      return false;
    }
    return true;
  }

  bool send_message(const uint8_t *data, size_t len) {
    if (!send(data, len)) {
      log_e("Failed to broadcast message");
      return false;
    }
    return true;
  }
};

// Declare global after setup/heap is ready to minimize crash risk (optional fix)
ESP_NOW_Broadcast_Peer *broadcast_peer = nullptr;

void setup() {
  Serial.begin(115200);
  delay(2000); // Allow USB Serial to enumerate!

  pinMode(WAKEUP_PIN, INPUT_PULLDOWN);

  esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
  Serial.print("Wakeup cause: ");
  Serial.println(wakeup_cause);

  if (wakeup_cause == ESP_SLEEP_WAKEUP_EXT1) {
    Serial.println("Woken by pin - initializing ESP-NOW & WiFi...");

    WiFi.mode(WIFI_STA);
    WiFi.setChannel(ESPNOW_WIFI_CHANNEL);

    while (!WiFi.STA.started()) { delay(100); }
    Serial.println("WiFi started.");
    delay(300);

    // Defer dynamic allocation until after startup
    broadcast_peer = new ESP_NOW_Broadcast_Peer(ESPNOW_WIFI_CHANNEL, WIFI_IF_STA, nullptr);

    if (broadcast_peer->begin()) {
      const char* triggerMsg = "TRIGGER";
      unsigned long send_start = millis();
      unsigned long last_send = 0;
      Serial.println("Starting repeated broadcast for 5 seconds...");
      while (millis() - send_start < BROADCAST_TIME_MS) {
        if (millis() - last_send >= BROADCAST_INTERVAL_MS) {
          if (broadcast_peer->send_message((uint8_t*)triggerMsg, strlen(triggerMsg) + 1)) {
            Serial.println("Trigger broadcast sent.");
          } else {
            Serial.println("Broadcast failed.");
          }
          last_send = millis();
        }
        delay(10); // Yield
      }
    } else {
      Serial.println("ESP-NOW init failed.");
    }

    Serial.println("Done broadcasting; cleaning up...");
    delete broadcast_peer;
    broadcast_peer = nullptr;
  } else {
    Serial.println("Normal boot/sleep; skipping ESP-NOW...");
    delay(5000);
  }

  esp_sleep_enable_ext1_wakeup(1ULL << WAKEUP_PIN, ESP_EXT1_WAKEUP_ANY_HIGH);
  Serial.println("Going to deep sleep...");
  delay(250);
  esp_deep_sleep_start();
}

void loop() {}
