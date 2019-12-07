#include "ProgrammingMode.h"
#include "SerialConfig.h"
#include "ONSplash.c"
#include <M5Stack.h> 
#include <string.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <math.h>
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"
#include "AudioFileSourcePROGMEM.h"
#include "AudioClip.h"
#include "ASPLogo.h"
#include <esp_now.h>
#include "ProgrammingMode.h"
#include <EEPROM.h>
#include "Free_Fonts.h" // Include the header file attached to this sketch
#include "GlobalSettings.h"

#define PAYMENT_FAILED 0
#define PAYMENT_CANCELLED -1
#define PAYMENT_SUCCESS 1

#define INCLUDE_vTaskDelete 1

const char* server = "api.opennode.co";
const int httpsPort = 443;