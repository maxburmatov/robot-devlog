#pragma once

// Exactly 16 bytes. Obtain it locally with TinyTuya and never commit it.
constexpr char TUYA_LOCAL_KEY[] = "0123456789abcdef";

// Reserve this address in the router and keep the real LAN layout local.
constexpr uint8_t TUYA_PLUG_IP[4] = {192, 168, 1, 100};
