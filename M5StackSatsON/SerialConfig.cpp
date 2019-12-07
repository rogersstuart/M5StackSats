#include "SerialConfig.h"

void parse_cmd()
{
	if (Serial.available() >= 5)
	{
		uint8_t cmd = Serial.read();
		if (cmd == MODE_ENTRY_CMD)
		{
			for (int i = 0; i < 4; i++)
				if (Serial.read() == mode_entry_key[i])
					continue;
				else
					return;

			

			Serial.write(ACK);

			//command and key have been entered

			while (Serial.available() < 1)
				delay(1);

			cmd = Serial.read();

			char buffer[200];
			memset(buffer, 0, 200);

			if (cmd == READ_CREDENTIALS)
			{
				//write credentials serilization to the serial interface

				EEPROM.get(2, buffer);
				Serial.write((uint8_t*)buffer, 200);
			}
			else
				if (cmd == WRITE_CREDENTIALS)
				{
					//read credentials serilization from the serial interface and write to EEPROM

					

					while (Serial.available() < 200)
						delay(1);

					Serial.readBytes(buffer, 200);

					EEPROM.put(2, buffer);
					EEPROM.write(1, 0xaf);
					EEPROM.commit();

					Serial.write(ACK);

					//M5.Lcd.println("ok");
				}
				else
					if (cmd == READ_PARAMS)
					{
						//write parameters serilization to the serial interface

						EEPROM.get(EEPROM_ADDITIONAL_PARAMS, buffer);
						Serial.write((uint8_t*)buffer, 200);
					}
					else
						if (cmd == WRITE_PARAMS)
						{
							//read parameters serilization from the serial interface and write to EEPROM

							while (Serial.available() < 200)
								delay(1);

							Serial.readBytes(buffer, 200);

							EEPROM.put(EEPROM_ADDITIONAL_PARAMS, buffer);
							EEPROM.commit();

							Serial.write(ACK);
						}
						else
							Serial.write(NACK);
		}
	}
}