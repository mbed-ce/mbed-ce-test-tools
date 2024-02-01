//
// Created by jamie on 5/22/2022.
//

#include "mbed.h"

#include "SDBlockDevice.h"

int main()
{
	SDBlockDevice sd(ARDUINO_UNO_D11, ARDUINO_UNO_D12, ARDUINO_UNO_D13, ARDUINO_UNO_D10, 10000000, true);

	int ret = sd.init();
	if(ret != BD_ERROR_OK)
	{
		printf("Init failed with ret: %d\n", ret);
	}

	// SD card block size is 512
	const char testString[512] = "Hello EEPROM";
	ret = sd.program(testString, 0, sizeof(testString));

	if(ret == BD_ERROR_OK)
	{
		printf("Programmed: %s\n", testString);
	}
	else
	{
		printf("Program failed with ret: %d\n", ret);
	}

	char readback[512] = {};
	ret = sd.read(readback, 0, sizeof(testString));

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