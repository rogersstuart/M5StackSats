// ProgrammingMode.h

#ifndef _PROGRAMMINGMODE_h
#define _PROGRAMMINGMODE_h

#if defined(ARDUINO) && ARDUINO >= 100
	#include "arduino.h"
#else
	#include "WProgram.h"
#endif


#endif

#include <esp_now.h>

void prog_setup();
void prog_loop(uint8_t*, int);
void deletePeer();
void InitESPNow();
void ScanForSlave();
bool manageSlave();
void deletePeer();
void OnDataSent(const uint8_t*, esp_now_send_status_t);
