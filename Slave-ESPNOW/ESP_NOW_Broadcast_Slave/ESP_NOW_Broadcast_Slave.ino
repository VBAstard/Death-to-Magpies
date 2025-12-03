#include "ESP32_NOW.h"
#include "WiFi.h"
#include "esp_sleep.h"
#include <esp_mac.h>
#include <vector>

#define ACTIVATION_PIN 0
#define ESPNOW_WIFI_CHANNEL 6
#define AWAKE_DURATION 1000        // ms, time ESP32 stays awake (adjust as needed)
#define SLEEP_INTERVAL_SEC 5       // sleep duration in seconds

unsigned long pin_high_until = 0;

class ESP_NOW_Peer_Class : public ESP_NOW_Peer {
public:
    ESP_NOW_Peer_Class(const uint8_t *mac_addr, uint8_t channel, wifi_interface_t iface, const uint8_t *lmk)
        : ESP_NOW_Peer(mac_addr, channel, iface, lmk) {}
    ~ESP_NOW_Peer_Class() {}
    bool add_peer() {
        if (!add()) {
            log_e("Failed to register the broadcast peer");
            return false;
        }
        return true;
    }
    void onReceive(const uint8_t *data, size_t len, bool broadcast) {
        Serial.printf("Received from master " MACSTR " (%s)\n", MAC2STR(addr()), broadcast ? "broadcast" : "unicast");
        Serial.printf("  Message: %s\n", (char *)data);
        if (broadcast) {
            digitalWrite(ACTIVATION_PIN, HIGH);
            pin_high_until = millis() + 3000;
            Serial.println("ACTIVATION_PIN set HIGH for 3 seconds");
        }
    }
};

std::vector<ESP_NOW_Peer_Class *> masters;

void register_new_master(const esp_now_recv_info_t *info, const uint8_t *data, int len, void *arg) {
    if (memcmp(info->des_addr, ESP_NOW.BROADCAST_ADDR, 6) == 0) {
        Serial.printf("Unknown peer " MACSTR " sent a broadcast\n", MAC2STR(info->src_addr));
        Serial.println("Registering the peer as a master");
        ESP_NOW_Peer_Class *new_master = new ESP_NOW_Peer_Class(info->src_addr, ESPNOW_WIFI_CHANNEL, WIFI_IF_STA, nullptr);
        if (!new_master->add_peer()) {
            Serial.println("Failed to register new master");
            delete new_master;
            return;
        }
        masters.push_back(new_master);
        Serial.printf("Registered master " MACSTR " (total: %zu)\n", MAC2STR(new_master->addr()), masters.size());
    } else {
        log_v("Received unicast from " MACSTR, MAC2STR(info->src_addr));
        log_v("Ignoring message");
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(ACTIVATION_PIN, OUTPUT);
    digitalWrite(ACTIVATION_PIN, LOW);

    WiFi.mode(WIFI_STA);
    WiFi.setChannel(ESPNOW_WIFI_CHANNEL);
    while (!WiFi.STA.started()) delay(100);

    Serial.println("ESP-NOW Safe Example - Broadcast Slave");
    Serial.println("Wi-Fi:");
    Serial.println("  Mode: STA");
    Serial.println("  MAC: " + WiFi.macAddress());
    Serial.printf("  Channel: %d\n", ESPNOW_WIFI_CHANNEL);

    if (!ESP_NOW.begin()) {
        Serial.println("Failed to initialize ESP-NOW");
        Serial.println("Rebooting in 5 seconds...");
        delay(5000);
        ESP.restart();
    }

    Serial.printf("ESP-NOW version: %d, max data length: %d\n", ESP_NOW.getVersion(), ESP_NOW.getMaxDataLen());
    ESP_NOW.onNewPeer(register_new_master, nullptr);
    Serial.println("Setup complete. Waiting for master broadcast...");

    unsigned long awake_start = millis();
    // Stay awake for AWAKE_DURATION ms
    while (millis() - awake_start < AWAKE_DURATION) {
        // Process the timer for ACTIVATION_PIN and masters/debug as usual
        if (pin_high_until && millis() > pin_high_until) {
            digitalWrite(ACTIVATION_PIN, LOW);
            Serial.println("ACTIVATION_PIN set LOW");
            pin_high_until = 0;
        }
        static unsigned long last_debug = 0;
        if (millis() - last_debug > 10000) {
            last_debug = millis();
            Serial.printf("Registered masters: %zu\n", masters.size());
            for (size_t i = 0; i < masters.size(); ++i)
                if (masters[i])
                    Serial.printf("  Master %zu: " MACSTR "\n", i, MAC2STR(masters[i]->addr()));
        }
        delay(10);
    }

    Serial.println("Going to deep sleep...");
    esp_sleep_enable_timer_wakeup(SLEEP_INTERVAL_SEC * 1000000ULL); // 4 sec sleep
    delay(250);
    esp_deep_sleep_start();
}

void loop() {
    // Not used: all active-period logic is handled in setup to maintain deep sleep reliability.
}
