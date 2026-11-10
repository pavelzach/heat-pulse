// Wi-Fi credentials template.
//
// Copy this file to `secrets.h` (in the same folder) and fill in your own values.
// `secrets.h` is listed in .gitignore, so your passwords never end up in the repository.
// Without `secrets.h` the firmware builds with these example values.

#pragma once

// Home Wi-Fi – 2.4 GHz only (the ESP32 does not support 5 GHz).
const char *WIFI_SSID = "YOUR_WIFI_2G4";
const char *WIFI_PASSWORD = "YOUR_PASSWORD";

// Fallback access point, used when the home network cannot be joined.
// The password must have at least 8 characters. The example password is public – always change it!
const char *AP_SSID = "HeatPulse";
const char *AP_PASSWORD = "CHANGE-TO-A-STRONG-PASSWORD";
