/*
 * Copyright (c) 2024 Jamie Smith
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

// check if SPI Slave is supported on this device
#if !DEVICE_SPISLAVE
#error [NOT_SUPPORTED] SPI slave not supported on this platform
#endif

#include "mbed.h"
#include "static_pinmap.h"
#include "greentea-client/test_env.h"
#include "unity.h"
#include "utest.h"
#include "ci_test_common.h"

// Single instance of SPI used in the test.
// Prefer to use a single instance so that, if it gets in a bad state and cannot execute further
// transactions, this will be visible in the test.
SPISlave * spi = nullptr;

#if STATIC_PINMAP_READY
// must be declared globally as SPI stores the pointer
constexpr auto spiPinmap = get_spi_pinmap(PIN_SPI_MOSI, PIN_SPI_MISO, PIN_SPI_SCLK, PIN_SPI_HW_CS);
#endif

/*
 * Create the SPI object.  The MOSI or MISO line can optionally be set to NC to test that aspect.
 */
void create_spi_object(bool mosiNC, bool misoNC)
{
    // Destroy if previously created
    if(spi)
    {
        delete spi;
    }

#if STATIC_PINMAP_READY
    if(!mosiNC && !misoNC)
    {
        // Use static pinmap if available.  Currently SPI static pinmaps do not support NC MOSI or MISO pins.
        spi = new SPISlave(spiPinmap);
        return;
    }
#endif
    spi = new SPISlave(mosiNC ? NC : PIN_SPI_MOSI, misoNC ? NC : PIN_SPI_MISO, PIN_SPI_SCLK, PIN_SPI_HW_CS);
}

/*
 * Uses the host test to start SPI logging from the device
 */
void host_start_spi_logging()
{
    // Note: Value is not important but cannot be empty
    greentea_send_kv("start_recording_spi", "please");
    assert_next_message_from_host("start_recording_spi", "complete");
}

void test_one_byte_transaction()
{
    host_start_spi_logging();

    // Kick off the host test doing an SPI transaction
    greentea_send_kv("do_transaction", "0x1 expected_response 0x2");

    // Preload reply
    spi->reply(0x2);

    uint8_t byteRxed = 0;
    while(true)
    {
        if(spi->receive())
        {
            byteRxed = spi->read();
            break;
        }
    }

    TEST_ASSERT_EQUAL_UINT8(byteRxed, 0x1);

    assert_next_message_from_host("do_transaction", "complete");
}

// TODO test what happens if the master sends a byte before the code has called reply().
// It's not defined in the HAL API what happens in this case so we currently cannot test it.

utest::v1::Case cases[] = {
    utest::v1::Case("One byte transaction", test_one_byte_transaction),
};

utest::v1::status_t test_setup(const size_t number_of_cases)
{
    // Create SPI.
    create_spi_object(false, false);

    // Start with word size of 8, mode 0
    spi->format(8, 0);

    // Initialize logic analyzer for SPI pinouts
    static BusOut funcSelPins(PIN_FUNC_SEL0, PIN_FUNC_SEL1, PIN_FUNC_SEL2);
    funcSelPins = 0b010;

    // make sure SD card is disabled and disconnected
    static DigitalOut sdcardEnablePin(PIN_SDCARD_ENABLE, 0);

    // Setup Greentea using a reasonable timeout in seconds
    GREENTEA_SETUP(45, "spi_slave_comms");
    return utest::v1::verbose_test_setup_handler(number_of_cases);
}

void test_teardown(const size_t passed, const size_t failed, const utest::v1::failure_t failure)
{
    delete spi;
    return greentea_test_teardown_handler(passed, failed, failure);
}

utest::v1::Specification specification(test_setup, cases, test_teardown, utest::v1::greentea_continue_handlers);

// Entry point into the tests
int main()
{
    return !utest::v1::Harness::run(specification);
}
