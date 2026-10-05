// The WiFi radio for CYD-to-CYD play (device only): ESP-NOW broadcasts on
// one channel, no router and no password. Off except while a player is in
// "Play Nearby" or a wireless game (radio_on / radio_off).
#pragma once

#include <cstddef>
#include <cstdint>

bool   radio_on();                                          // false = couldn't start
void   radio_off();
bool   radio_send(const uint8_t* data, size_t len);         // broadcast one packet
// Next packet that came in, 0 = none. Called from the main loop.
size_t radio_recv(uint8_t mac[6], uint8_t* buf, size_t cap, int8_t* rssi);   // rssi in dBm
void   radio_mac(uint8_t mac[6]);                           // this board's address
// Doze (battery): the radio sleeps and wakes for net::kDozeWindowMs every
// net::kDozeIntervalMs (the driver's ESP-NOW power saving - no stop/start,
// which takes far longer). false = fully awake.
void   radio_doze(bool doze);
uint32_t radio_start_ms();                                  // how long the last radio_on() took
