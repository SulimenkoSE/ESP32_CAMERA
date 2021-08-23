#pragma once
/* json
*  using Arduino core for ESP8266 WiFi chip
*  by Serhii
*  Julay 12 2020
*/
#ifndef WIFI_KEYS_H
#define WIFI_KEYS_H

#include <ArduinoJson.h>

/* const size_t CAPACITY = JSON_ARRAY_SIZE(3);

StaticJsonDocument wifiKeys;

deserializeJson(wifiKeys , "[{"ssid" : " Serhii_123 ", "pass" : "Sergik9876632"},
{"ssid" : " Serhii_123 ", "pass" : "Sergik9876632"},
"ssid" : "Nina_Iot", "pass" : "sergik9876632"]");
 */

// REPLACE myPassword YOUR WIFI PASSWORD, IF ANY
const char *ssid_0 = "Serhii_123";
const char *pass_0 = "Sergik9876632";
const char *ssid_1 = "Sergik_Dp";
const char *pass_1 = "sergik9876632";
const char *ssid_2 = "Nina_Iot";
const char *pass_2 = "Sergik9876632";
const char *ssid_3 = "Anna";
const char *pass_3 = "Nusa0091992";
// REPLACE myPassword YOUR WIFI PASSWORD, IF ANY
const char *token = "1554215186:AAEbD7gVmPe8dlIbMob_PsGV7vaE_L6bVHk"; // REPLACE myToken WITH YOUR TELEGRAM BOT TOKEN
																							 //chat_id: 387342374
#endif