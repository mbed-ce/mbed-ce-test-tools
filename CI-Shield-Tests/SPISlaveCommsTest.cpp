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
    if(spi != nullptr)
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

template<int SPIMode>
void test_one_byte_transaction()
{
    spi->format(8, SPIMode);
    std::string spiMode = std::to_string(SPIMode);
    greentea_send_kv("set_spi_mode", spiMode.c_str());

    // Kick off the host test doing an SPI transaction
    host_start_spi_logging();
    greentea_send_kv("do_transaction", "0x1 expected_response 0x2");

    // Preload reply
    spi->reply(0x2);

    Timer transactionTimer;
    transactionTimer.start();

    uint8_t byteRxed = 0;
    while(true)
    {
        if(spi->receive())
        {
            byteRxed = spi->read();
            break;
        }

        if(transactionTimer.elapsed_time() > 1s)
        {
            TEST_FAIL_MESSAGE("No data seen by slave device!");
            return;
        }
    }

    TEST_ASSERT_EQUAL_UINT8(0x1, byteRxed);

    assert_next_message_from_host("do_transaction", "complete");
}

template<int SPIMode>
void test_one_16bit_word_transaction()
{
    spi->format(16, SPIMode);
    std::string spiMode = std::to_string(SPIMode);
    greentea_send_kv("set_spi_mode", spiMode.c_str());

    // Kick off the host test doing an SPI transaction.
    host_start_spi_logging();
    greentea_send_kv("do_transaction", "0x1 0x2 expected_response 0x3 0x4");

    // Preload reply
    spi->reply(0x0304);

    Timer transactionTimer;
    transactionTimer.start();

    uint16_t wordRxed = 0;
    while(true)
    {
        if(spi->receive())
        {
            wordRxed = spi->read();
            break;
        }

        if(transactionTimer.elapsed_time() > 1s)
        {
            TEST_FAIL_MESSAGE("No data seen by slave device!");
            return;
        }
    }

    TEST_ASSERT_EQUAL_UINT16(0x0102, wordRxed);

    assert_next_message_from_host("do_transaction", "complete");
}

/*
 * NOTE: If this test fails, check that the spi_free() function remaps all pins back to GPIO
 * function.  If it does not, then the MISO pin will still have its previous MISO function
 * instead of being tristated.
 */
void test_one_byte_rx_only()
{
    // disable MISO
    create_spi_object(false, true);

    // Set word size back to 8 and start recording.
    // Also reduce SCLK frequency so that the mirror resistor can work
    spi->format(8, 0);
    greentea_send_kv("set_spi_mode", "0");
    greentea_send_kv("set_sclk_freq", "100000");
    host_start_spi_logging();

    // Note: because of the SPI mirror resistor, if this MCU does not drive MISO, MISO it should match MOSI.
    greentea_send_kv("do_transaction", "0x25 expected_response 0x25");

    uint8_t byteRxed = 0;

    Timer transactionTimer;
    transactionTimer.start();

    while(true)
    {
        if(spi->receive())
        {
            byteRxed = spi->read();
            break;
        }

        if(transactionTimer.elapsed_time() > 1s)
        {
            TEST_FAIL_MESSAGE("No data seen by slave device!");
            return;
        }
    }

    TEST_ASSERT_EQUAL_UINT8(0x25, byteRxed);

    assert_next_message_from_host("do_transaction", "complete");
}

void test_one_byte_tx_only()
{
    // disable MISO
    create_spi_object(true, false);

    // Set word size back to 8 and start recording.
    // Also change SCLK frequency back to the orig value
    spi->format(8, 3);
    greentea_send_kv("set_spi_mode", "3");
    greentea_send_kv("set_sclk_freq", "500000");
    host_start_spi_logging();

    greentea_send_kv("do_transaction", "0x77 expected_response 0x88");

    spi->reply(0x88);

    Timer transactionTimer;
    transactionTimer.start();

    while(true)
    {
        if(spi->receive())
        {
            // Note: with MOSI disabled, the API makes no guarantees about the return value here
            spi->read();
            break;
        }

        if(transactionTimer.elapsed_time() > 1s)
        {
            TEST_FAIL_MESSAGE("No data seen by slave device!");
            return;
        }
    }

    assert_next_message_from_host("do_transaction", "complete");
}

void test_four_byte_transaction()
{
    // Reenable full duplex comms
    create_spi_object(false, false);

    spi->format(8, 0);
    greentea_send_kv("set_spi_mode", "0");

    // Kick off the host test doing an SPI transaction
    host_start_spi_logging();
    uint8_t const txData[] = {0x1, 0x2, 0x3, 0x4};
    size_t txIndex = 0;
    uint8_t rxData[sizeof(txData)];
    size_t rxIndex = 0;

    // Pre-fill the FIFO with data.  This is the only way I've found to get even moderately fast
    // clock rates (100kHz) to work for multi-byte transfers.
    // What sucks is that SPISlave does not provide an API to determine how big the hardware FIFO is.
    for(size_t dataIndex = 0; dataIndex < sizeof(txData); ++dataIndex)
    {
        // Preload reply
        spi->reply(txData[txIndex++]);
    }

    greentea_send_kv("do_transaction", "0x5 0x6 0x7 0x8 expected_response 0x1 0x2 0x3 0x4");

    Timer transactionTimer;
    transactionTimer.start();

    for(size_t dataIndex = 0; dataIndex < sizeof(txData); ++dataIndex)
    {
        // Wait for data
        while(!spi->receive())
        {
            if(transactionTimer.elapsed_time() > 1s)
            {
                printf("Only saw %zu bytes.\n", dataIndex);
                TEST_FAIL_MESSAGE("No data seen by slave device!");
                return;
            }
        }

        // Read response
        rxData[rxIndex++] = spi->read();
    }

    uint8_t const expectedRxData[] = {0x5, 0x6, 0x7, 0x8};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expectedRxData, rxData, sizeof(txData));

    assert_next_message_from_host("do_transaction", "complete");
}

// TODO test what happens if the master sends a byte before the code has called reply().
// It's not defined in the HAL API what happens in this case so we currently cannot test it.

utest::v1::Case cases[] = {
    utest::v1::Case("One byte transaction (mode 0)", test_one_byte_transaction<0>),
    utest::v1::Case("One byte transaction (mode 1)", test_one_byte_transaction<1>),
    utest::v1::Case("One byte transaction (mode 2)", test_one_byte_transaction<2>),
    utest::v1::Case("One byte transaction (mode 3)", test_one_byte_transaction<3>),
    utest::v1::Case("One word transaction (mode 0)", test_one_16bit_word_transaction<0>),
    utest::v1::Case("One word transaction (mode 1)", test_one_16bit_word_transaction<1>),
    utest::v1::Case("One word transaction (mode 2)", test_one_16bit_word_transaction<2>),
    utest::v1::Case("One word transaction (mode 3)", test_one_16bit_word_transaction<3>),
    utest::v1::Case("One byte, MISO tristated", test_one_byte_rx_only),
    utest::v1::Case("One byte, MOSI tristated", test_one_byte_tx_only),
    utest::v1::Case("Four bytes", test_four_byte_transaction),
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
    GREENTEA_SETUP(60, "spi_slave_comms");
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
