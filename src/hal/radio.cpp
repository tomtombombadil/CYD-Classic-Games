#include "radio.h"

#include <Arduino.h>
#include <esp_event.h>
#include <esp_mac.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <cstring>
#include "net/wireless.h"

namespace {

// Every board uses this channel: they hear each other without a router
constexpr uint8_t kChannel = 1;
constexpr size_t  kMaxPacket = 128;     // = net::kPacketMax (later Hellos may be longer)
constexpr int     kQueueLen = 16;

struct Rx {
    uint8_t mac[6];
    int8_t  rssi;
    uint8_t len;
    uint8_t data[kMaxPacket];
};

QueueHandle_t rx_queue = nullptr;
bool on = false;
bool dozing = false;
uint32_t start_ms = 0;
const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Runs in the WiFi task: copy the packet into the queue, never wait
void on_recv(const esp_now_recv_info_t* info, const uint8_t* data, int len)
{
    if (!rx_queue || !info || len <= 0 || len > int(kMaxPacket)) return;
    Rx r;
    memcpy(r.mac, info->src_addr, 6);
    r.rssi = info->rx_ctrl ? int8_t(info->rx_ctrl->rssi) : -100;
    r.len = uint8_t(len);
    memcpy(r.data, data, size_t(len));
    xQueueSend(rx_queue, &r, 0);              // a full queue drops it: the next status replaces it
}

} // namespace

bool radio_on()
{
    if (on) return true;
    if (!rx_queue) rx_queue = xQueueCreate(kQueueLen, sizeof(Rx));
    if (!rx_queue) return false;
    xQueueReset(rx_queue);
    const uint32_t t0 = millis();
    // The WiFi driver by itself (no network stack: nothing here needs IP)
    esp_event_loop_create_default();          // already there = fine
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) return false;
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK || esp_wifi_start() != ESP_OK) {
        esp_wifi_deinit();
        return false;
    }
    esp_wifi_set_ps(WIFI_PS_NONE);            // power saving would miss packets
    esp_wifi_set_channel(kChannel, WIFI_SECOND_CHAN_NONE);
    if (esp_now_init() != ESP_OK) { esp_wifi_stop(); esp_wifi_deinit(); return false; }
    esp_now_register_recv_cb(on_recv);
    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, kBroadcast, 6);
    peer.channel = kChannel;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) != ESP_OK) {
        esp_now_deinit();
        esp_wifi_stop();
        esp_wifi_deinit();
        return false;
    }
    on = true;
    dozing = false;
    start_ms = millis() - t0;
    return true;
}

void radio_doze(bool doze)
{
    if (!on || doze == dozing) return;
    dozing = doze;
    if (doze) {
        // Modem sleep between wake windows: the RF wakes by itself for the
        // window (and to send), far quicker than a stop and start
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
        esp_wifi_connectionless_module_set_wake_interval(uint16_t(net::kDozeIntervalMs));
        esp_now_set_wake_window(uint16_t(net::kDozeWindowMs));
    } else {
        esp_now_set_wake_window(65535);           // the default: always awake
        esp_wifi_connectionless_module_set_wake_interval(ESP_WIFI_CONNECTIONLESS_INTERVAL_DEFAULT_MODE);
        esp_wifi_set_ps(WIFI_PS_NONE);
    }
}

uint32_t radio_start_ms() { return start_ms; }

void radio_off()
{
    if (!on) return;
    esp_now_unregister_recv_cb();
    esp_now_deinit();
    esp_wifi_stop();
    esp_wifi_deinit();
    on = false;
    dozing = false;
    if (rx_queue) xQueueReset(rx_queue);
}

bool radio_send(const uint8_t* data, size_t len)
{
    if (!on || len == 0 || len > kMaxPacket) return false;
    return esp_now_send(kBroadcast, data, len) == ESP_OK;
}

size_t radio_recv(uint8_t mac[6], uint8_t* buf, size_t cap, int8_t* rssi)
{
    if (!rx_queue) return 0;
    Rx r;
    if (xQueueReceive(rx_queue, &r, 0) != pdTRUE) return 0;
    const size_t n = r.len < cap ? r.len : cap;
    memcpy(mac, r.mac, 6);
    if (rssi) *rssi = r.rssi;
    memcpy(buf, r.data, n);
    return n;
}

void radio_mac(uint8_t mac[6])
{
    // The station address ESP-NOW sends from, readable with the radio off
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
}
