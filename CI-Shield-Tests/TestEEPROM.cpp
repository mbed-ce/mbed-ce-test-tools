//
// Created by jamie on 5/22/2022.
//

#include "mbed.h"

#include "I2CEEBlockDevice.h"

int main()
{
	I2C i2c(ARDUINO_UNO_D14, ARDUINO_UNO_D15);

	// Settings for Microchip 24FC02
	I2CEEBlockDevice eeprom(&i2c, 0xA0, 2048, 8, true);

	int ret = eeprom.init();
	if(ret != BD_ERROR_OK)
	{
		printf("Init failed with ret: %d\n", ret);
	}

	const char testString[] = "Hello EEPROM";
	ret = eeprom.program(testString, 0, sizeof(testString));

	if(ret == BD_ERROR_OK)
	{
		printf("Programmed: %s\n", testString);
	}
	else
	{
		printf("Program failed with ret: %d\n", ret);
	}

	char readback[50] = {};
	ret = eeprom.read(readback, 0, sizeof(testString));

	if(ret == BD_ERROR_OK)
	{
		printf("Read: %s\n", testString);
	}
	else
	{
		printf("Read failed with ret: %d\n", ret);
	}

	printf("Got back: %s\n", readback);

	while(true) {}

	// main() is expected to loop forever.
	// If main() actually returns the processor will crash
	return 0;
}