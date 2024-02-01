/*
 * Copyright (c) 2022 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// check if SPI is supported on this device
#if !DEVICE_SPI
#error [NOT_SUPPORTED] SPI is not supported on this platform, add 'DEVICE_SPI' definition to your platform.
#endif

#include "mbed.h"
#include "greentea-client/test_env.h"
#include "unity.h"
#include "utest.h"
#include "ci_test_common.h"
#include "FATFileSystem.h"
#include "SDBlockDevice.h"

using namespace utest::v1;

#define SD_TEST_STRING_MAX 100

char SD_TEST_STRING[SD_TEST_STRING_MAX] = {0};

alignas(SDBlockDevice) uint8_t sdBlockDevMemory[sizeof(SDBlockDevice)];

/*
 * Wait for the next host message with the given key, and then assert that its
 * value is expectedVal.
 */
//void assert_next_message_from_host(char const * key, char const * expectedVal) {
//
//    // Based on the example code: https://os.mbed.com/docs/mbed-os/v6.16/debug-test/greentea-for-testing-applications.html
//    char receivedKey[64], receivedValue[64];
//    while (1) {
//        greentea_parse_kv(receivedKey, receivedValue, sizeof(receivedKey), sizeof(receivedValue));
//
//        if(strncmp(key, receivedKey, sizeof(receivedKey) - 1) == 0) {
//            TEST_ASSERT_EQUAL_STRING_LEN(expectedVal, receivedValue, sizeof(receivedKey) - 1);
//            break;
//        }
//    }
//}

/*
 * Uses the host test to start SPI logging from the device
 */
void host_start_spi_logging()
{
    // Note: Value is not important but cannot be empty
    //greentea_send_kv("start_recording_spi", "please");
    //assert_next_message_from_host("start_recording_spi", "complete");
}

/*
 * Ask the host to print SPI data from the device
 */
void host_print_spi_data()
{
    // Note: Value is not important but cannot be empty
    //greentea_send_kv("print_spi_data", "please");
    //assert_next_message_from_host("print_spi_data", "complete");
}


void init_string()
{
    int x = 0;
    for(x = 0; x < SD_TEST_STRING_MAX-1; x++){
        SD_TEST_STRING[x] = 'A' + (rand() % 26);
    }
    SD_TEST_STRING[SD_TEST_STRING_MAX-1] = 0;

    DEBUG_PRINTF("\r\n****\r\nSD Test String = %s\r\n****\r\n",SD_TEST_STRING);
}

// Construct an SPI object in spiMemory
SDBlockDevice * constructSDBlockDev(uint64_t spiFreq)
{
    return new (sdBlockDevMemory) SDBlockDevice(PIN_SPI_MOSI, PIN_SPI_MISO, PIN_SPI_SCLK, PIN_SPI_SD_CS,spiFreq, true);
}

void destroySDBlockDev(SDBlockDevice * sdDev)
{
    // we used placement new so we don't need to call delete, just invoke the destructor
    sdDev->~SDBlockDevice();
}

// Test object constructor / destructor
void test_object()
{
    SDBlockDevice * sdDev = constructSDBlockDev(1000000);
    TEST_ASSERT_MESSAGE(true,"If the tests hangs here then there is a problem with the SD or SPI objects"); // helpful debug message for if the test hangs
    destroySDBlockDev(sdDev);
}

// Test for SD Card being present on the shield
template<uint64_t spiFreq, bool useAsync, DMAUsage dmaHint>
void test_card_present()
{
    SDBlockDevice * sdDev = constructSDBlockDev(spiFreq);

    host_start_spi_logging();

    sdDev->set_async_spi_mode(useAsync, dmaHint);

    int ret = sdDev->init();
    TEST_ASSERT_MESSAGE(ret == BD_ERROR_OK, "Failed to connect to SD card");

    sdDev->deinit();
    destroySDBlockDev(sdDev);

    host_print_spi_data();
}

// Test which mounts the filesystem and creates a file
template<uint64_t spiFreq, bool useAsync, DMAUsage dmaHint>
void mount_fs_create_file()
{
    SDBlockDevice * sdDev = constructSDBlockDev(spiFreq);

    host_start_spi_logging();

    FATFileSystem fs("sd");

    sdDev->set_async_spi_mode(useAsync, dmaHint);

	int ret = sdDev->init();
    TEST_ASSERT_MESSAGE(ret == BD_ERROR_OK, "Failed to connect to SD card");

    ret = fs.mount(sdDev);

    if(ret)
	{
		// This is expected if the SD card was not formatted previously
		ret = fs.reformat(sdDev);
	}

    TEST_ASSERT_MESSAGE(ret==0,"SD file system mount failed.");

    FILE * file = fopen("/sd/card-present.txt", "w+");

    TEST_ASSERT_MESSAGE(file != nullptr,"Failed to create file");

	fclose(file);

    ret = fs.unmount();
    TEST_ASSERT_MESSAGE(ret==0,"SD file system unmount failed.");

    destroySDBlockDev(sdDev);

    host_print_spi_data();
}

// Test which writes, reads, and deletes a file.
template<uint64_t spiFreq, bool useAsync, DMAUsage dmaHint>
void test_sd_file()
{
    SDBlockDevice * sdDev = constructSDBlockDev(spiFreq);

    host_start_spi_logging();

    FATFileSystem fs("sd");

    sdDev->set_async_spi_mode(useAsync, dmaHint);

	int ret = sdDev->init();
    TEST_ASSERT_MESSAGE(ret == BD_ERROR_OK, "Failed to connect to SD card");

    ret = fs.mount(sdDev);
	TEST_ASSERT_MESSAGE(ret==0,"SD file system mount failed.");

	// Write the test string to a file.
    FILE * file = fopen("/sd/test_sd_w.txt", "w");
    TEST_ASSERT_MESSAGE(file != nullptr,"Failed to create file");
	init_string();
    TEST_ASSERT_MESSAGE(fprintf(file, SD_TEST_STRING) > 0,"Writing file to sd card failed");
    fclose(file);

	// Now open it and read the string back.
    // Note: Since fprintf will not print the terminating null to the file, the file will have only
    // sizeof(SD_TEST_STRING) - 1 chars.
	char read_string[SD_TEST_STRING_MAX] = {0};
    file = fopen("/sd/test_sd_w.txt", "r");
	TEST_ASSERT_MESSAGE(file != nullptr,"Failed to open file");

	ret = fread(read_string, sizeof(char), sizeof(SD_TEST_STRING) - 1, file);
	TEST_ASSERT_MESSAGE(ret == (sizeof(SD_TEST_STRING) - 1), "Failed to read data");
	DEBUG_PRINTF("\r\n****\r\nRead '%s' in read test\r\n, read returns %d, string comparison returns %d\r\n****\r\n",read_string, ret, strcmp(read_string,SD_TEST_STRING));
	TEST_ASSERT_MESSAGE(strcmp(read_string,SD_TEST_STRING) == 0,"String read does not match string written");

	// Check that reading one additional char causes an EOF error
	ret = fread(read_string, sizeof(char), 1, file);
	TEST_ASSERT_MESSAGE(ret < 1, "fread did not return error?");
	TEST_ASSERT(feof(file));

	fclose(file);

	// Delete the file and make sure it's gone
	remove("/sd/test_sd_w.txt");
	TEST_ASSERT(fopen("/sd/test_sd_w.txt", "r") == nullptr);

	// Clean up
	ret = fs.unmount();
    TEST_ASSERT_MESSAGE(ret==0,"SD file system unmount failed.");

    destroySDBlockDev(sdDev);

    host_print_spi_data();
}

utest::v1::status_t test_setup(const size_t number_of_cases)
{
    // Setup Greentea using a reasonable timeout in seconds
    GREENTEA_SETUP(120, "default_auto");

    // Initialize logic analyzer for SPI pinouts
    static BusOut funcSelPins(PIN_FUNC_SEL0, PIN_FUNC_SEL1, PIN_FUNC_SEL2);
    funcSelPins = 0b010;

    return verbose_test_setup_handler(number_of_cases);
}

// Test cases
Case cases[] = {
        Case("SPI - Object Definable", test_object),
        Case("SPI - SD card present (1MHz)", test_card_present<1000000, false, DMA_USAGE_NEVER>),
		Case("SPI - Mount FS, Create File (1MHz)", mount_fs_create_file<1000000, false, DMA_USAGE_NEVER>),
		Case("SPI - Write, Read, and Delete File (1MHz)", test_sd_file<1000000, false, DMA_USAGE_NEVER>),

#if DEVICE_SPI_ASYNCH
    Case("[Async Interrupts] SPI - SD card present (1MHz)", test_card_present<1000000, true, DMA_USAGE_NEVER>),
    Case("[Async Interrupts] SPI - Mount FS, Create File (1MHz)", mount_fs_create_file<1000000, true, DMA_USAGE_NEVER>),
    Case("[Async Interrupts] SPI - Write, Read, and Delete File (1MHz)", test_sd_file<1000000, true, DMA_USAGE_NEVER>),
    Case("[Async DMA] SPI - SD card present (1MHz)", test_card_present<1000000, true, DMA_USAGE_ALWAYS>),
    Case("[Async DMA] SPI - Mount FS, Create File (1MHz)", mount_fs_create_file<1000000, true, DMA_USAGE_ALWAYS>),
    Case("[Async DMA] SPI - Write, Read, and Delete File (1MHz)", test_sd_file<1000000, true, DMA_USAGE_ALWAYS>),
#endif
};

Specification specification(test_setup, cases, greentea_continue_handlers);

// // Entry point into the tests
int main() {
    return !Harness::run(specification);
}
