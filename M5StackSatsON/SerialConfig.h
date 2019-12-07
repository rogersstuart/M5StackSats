// SerialConfig.h

#ifndef _SERIALCONFIG_h
#define _SERIALCONFIG_h

#if defined(ARDUINO) && ARDUINO >= 100
	#include "arduino.h"
#else
	#include "WProgram.h"
#endif


#endif

#include <EEPROM.h>
#include "GlobalSettings.h"
#include <M5Stack.h> 

#define ACK 'z'
#define NACK 'q'

#define WRITE_CREDENTIALS 'w'
#define READ_CREDENTIALS 'r'
#define WRITE_PARAMS 's'
#define READ_PARAMS 'b'

#define MODE_ENTRY_CMD 'k'

const uint8_t mode_entry_key[] = {'p','r','o','g'};


void parse_cmd();
//String* read_cfg();
//void write_cfg(String*);
