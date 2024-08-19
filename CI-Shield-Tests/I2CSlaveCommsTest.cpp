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

// check if I2C is supported on this device
#if !DEVICE_I2CSLAVE
#error [NOT_SUPPORTED] I2C slave not supported on this platform
#endif

#include "mbed.h"
#include "static_pinmap.h"
#include "greentea-client/test_env.h"
#include "unity.h"
#include "utest.h"
#include "ci_test_common.h"

using namespace utest::v1;

// 8-bit I2C address of the Mbed MCU
#define MBED_I2C_ADDRESS 0xE4
#define MBED_I2C_ADDRESS_STR "0xE4"


// Single instance of I2C slave used in the test.
// Prefer to use a single instance so that, if it gets in a bad state and cannot execute further
// transactions, this will be visible in the test.
I2CSlave * i2cSlave;

/*
 * Uses the host test to start I2C logging from the device
 */
void host_start_i2c_logging()
{
    // Note: Value is not important but cannot be empty
    greentea_send_kv("start_recording_i2c", "please");
    assert_next_message_from_host("start_recording_i2c", "complete");
}


#if STATIC_PINMAP_READY
// Must be declared globally as I2C stores the pointer
constexpr auto i2cPinmap = get_i2c_pinmap(PIN_I2C_SDA, PIN_I2C_SCL);
#endif

void create_i2c_object()
{
    // Use static pinmap if supported for this device
#if STATIC_PINMAP_READY
    i2cSlave = new I2CSlave(i2cPinmap);
#else	
    i2cSlave = new I2CSlave(PIN_I2C_SDA, PIN_I2C_SCL);
#endif
    i2cSlave->address(MBED_I2C_ADDRESS);
	i2cSlave->frequency(400000);
}

void test_write_one_byte_to_slave()
{
    host_start_i2c_logging();

    // Kick off the host test doing an I2C transaction
    greentea_send_kv("write_bytes_to_slave", "addr " MBED_I2C_ADDRESS_STR " data 0x1");

    uint8_t byteRxed = 0;
    while(true)
    {
        auto event = i2cSlave->receive();
        if(event == I2CSlave::WriteAddressed)
        {
            TEST_ASSERT_EQUAL_INT(sizeof(uint8_t), i2cSlave->read(reinterpret_cast<char*>(&byteRxed), sizeof(uint8_t)));
            break;
        }
    }

    TEST_ASSERT_EQUAL_UINT8(byteRxed, 0x1);

    assert_next_message_from_host("write_bytes_to_slave", "complete");
}

void test_write_one_byte_to_general_call()
{
    host_start_i2c_logging();

    // Kick off the host test doing an I2C transaction.
    // Some Mbed devices which implement I2C, e.g. LPC1768, can only receive 1 byte to the general call address.
    // Some values are reserved in the I2C spec, so we write 0x70 which is not reserved.
    greentea_send_kv("write_bytes_to_slave", "addr 0x0 data 0x70");

    uint8_t byteRxed = 0;
    while(true)
    {
        auto event = i2cSlave->receive();
        if(event == I2CSlave::WriteGeneral)
        {
            TEST_ASSERT_EQUAL_INT(sizeof(uint8_t), i2cSlave->read(reinterpret_cast<char*>(&byteRxed), sizeof(uint8_t)));
            break;
        }
    }

    TEST_ASSERT_EQUAL_UINT8(byteRxed, 0x70);

    assert_next_message_from_host("write_bytes_to_slave", "complete");
}

void test_doesnt_ack_other_slave_address()
{
    host_start_i2c_logging();

    // Kick off the host test doing an I2C transaction
    greentea_send_kv("try_write_to_wrong_address", "0xE6");

    Timer timeoutTimer;
    timeoutTimer.start();

    uint8_t byteRxed = 0;
    while(timeoutTimer.elapsed_time() < 250ms) // Ballpark guess, give the host some time to start the transaction
    {
        auto event = i2cSlave->receive();
        if(event == I2CSlave::WriteAddressed)
        {
            i2cSlave->read(reinterpret_cast<char*>(&byteRxed), sizeof(uint8_t));
            TEST_FAIL_MESSAGE("Write received for wrong address!");
            break;
        }
    }

    assert_next_message_from_host("try_write_to_wrong_address", "complete");

    // We still shouldn't have gotten anything
    TEST_ASSERT_EQUAL_INT(I2CSlave::NoData, i2cSlave->receive());
}

void test_destroy_recreate_object()
{
    delete i2cSlave;
    create_i2c_object();

    // In testing, when we release the I2C pins, it can cause weirdness that prevents the I2C bridge from the host PC from working.
    // So, we tell the host to reinitialize the bridge.
    greentea_send_kv("reinit_i2c_bridge", "please");
    assert_next_message_from_host("reinit_i2c_bridge", "complete");
}

void test_write_multiple_bytes_to_slave()
{
    host_start_i2c_logging();

    // Kick off the host test doing an I2C transaction
    greentea_send_kv("write_bytes_to_slave", "addr " MBED_I2C_ADDRESS_STR " data 0x4 0x5 0x6 0x7");

    uint8_t bytesRxed[4]{};

    while(true)
    {
        auto event = i2cSlave->receive();
        if(event == I2CSlave::WriteAddressed)
        {
            TEST_ASSERT_EQUAL_INT(sizeof(bytesRxed), i2cSlave->read(reinterpret_cast<char*>(bytesRxed), sizeof(bytesRxed)));
            break;
        }
    }

    TEST_ASSERT_EQUAL_UINT8(bytesRxed[0], 0x4);
    TEST_ASSERT_EQUAL_UINT8(bytesRxed[1], 0x5);
    TEST_ASSERT_EQUAL_UINT8(bytesRxed[2], 0x6);
    TEST_ASSERT_EQUAL_UINT8(bytesRxed[3], 0x7);

    assert_next_message_from_host("write_bytes_to_slave", "complete");
}

/*
* Tests that if the master writes less bytes than we expect, the actual number of bytes is returned
*/
void test_write_less_than_expected_bytes_to_slave()
{
    host_start_i2c_logging();

    // Kick off the host test doing an I2C transaction
    greentea_send_kv("write_bytes_to_slave", "addr " MBED_I2C_ADDRESS_STR " data 0x8 0x9");

    uint8_t bytesRxed[4]{};

    while(true)
    {
        auto event = i2cSlave->receive();
        if(event == I2CSlave::WriteAddressed)
        {
            TEST_ASSERT_EQUAL_INT(2, i2cSlave->read(reinterpret_cast<char*>(bytesRxed), sizeof(bytesRxed)));
            break;
        }
    }

    TEST_ASSERT_EQUAL_UINT8(bytesRxed[0], 0x8);
    TEST_ASSERT_EQUAL_UINT8(bytesRxed[1], 0x9);

    assert_next_message_from_host("write_bytes_to_slave", "complete");
}

void test_read_one_byte_from_slave()
{
    host_start_i2c_logging();

    // Kick off the host test doing an I2C transaction
    greentea_send_kv("read_bytes_from_slave", "addr " MBED_I2C_ADDRESS_STR " expected-data 0x10");

    const uint8_t byteToSend = 0x10;
    while(true)
    {
        auto event = i2cSlave->receive();
        if(event == I2CSlave::ReadAddressed)
        {
            TEST_ASSERT_EQUAL_INT(0, i2cSlave->write(reinterpret_cast<char const *>(&byteToSend), sizeof(uint8_t)));
            break;
        }
    }

    assert_next_message_from_host("read_bytes_from_slave", "complete");
}

void test_read_multiple_bytes_from_slave()
{
    host_start_i2c_logging();

    // Kick off the host test doing an I2C transaction
    greentea_send_kv("read_bytes_from_slave", "addr " MBED_I2C_ADDRESS_STR " expected-data 0x11 0x12 0x13 0x14");

    const uint8_t bytesToSend[4] = {0x11, 0x12, 0x13, 0x14};
    while(true)
    {
        auto event = i2cSlave->receive();
        if(event == I2CSlave::ReadAddressed)
        {
            TEST_ASSERT_EQUAL_INT(0, i2cSlave->write(reinterpret_cast<char const *>(&bytesToSend), sizeof(bytesToSend)));
            break;
        }
    }

    assert_next_message_from_host("read_bytes_from_slave", "complete");
}

/*
* Test that, if the master tries to read less bytes from us than we expect, 
* write() returns an error and the master sees the correct data.
*/
void test_read_less_bytes_than_expected_from_slave()
{
    host_start_i2c_logging();

    // Kick off the host test doing an I2C transaction
    greentea_send_kv("read_bytes_from_slave", "addr " MBED_I2C_ADDRESS_STR " expected-data 0x15 0x16");

    const uint8_t bytesToSend[4] = {0x15, 0x16, 0x17, 0x18};
    while(true)
    {
        auto event = i2cSlave->receive();
        if(event == I2CSlave::ReadAddressed)
        {
            // Unfortunately there's no specification about the return value from write() in this situation other than that it's
            // supposed to be nonzero.
            TEST_ASSERT_NOT_EQUAL(0, i2cSlave->write(reinterpret_cast<char const *>(&bytesToSend), sizeof(bytesToSend)));
            break;
        }
    }

    assert_next_message_from_host("read_bytes_from_slave", "complete");
}

utest::v1::status_t test_setup(const size_t number_of_cases)
{
	// Create I2C
    create_i2c_object();

    // Initialize logic analyzer for I2C pinouts
    static BusOut funcSelPins(PIN_FUNC_SEL0, PIN_FUNC_SEL1, PIN_FUNC_SEL2);
    funcSelPins = 0b001;

	// Setup Greentea using a reasonable timeout in seconds
	GREENTEA_SETUP(30, "i2c_slave_comms");
	return verbose_test_setup_handler(number_of_cases);
}

void test_teardown(const size_t passed, const size_t failed, const failure_t failure)
{
    delete i2cSlave;
    return greentea_test_teardown_handler(passed, failed, failure);
}

// TODO test what happens if the master writes more bytes to a slave than the length of the buffer passed to read().
// The current I2CSlave API does not specify what is supposed to happen in this case -- does the slave NACK, or does it
// accept bytes and then discard them?

// TODO test what happens if the master reads more bytes from a slave than the length of the buffer passed to write().
// The slave cannot NACK the master in this situation.
// Does the slave write junk to the bus?  What error code is returned from write()?
// The current I2CSlave API does not specify what is supposed to happen in this case.

// Note: Sadly, the i2c bridge chip does not support zero-length reads or writes, so we cannot test those automatically.

// Test cases
Case cases[] = {
	Case("Write one byte to slave", test_write_one_byte_to_slave),
    Case("Does not acknowledge other slave address", test_doesnt_ack_other_slave_address),
    Case("Destroy & recreate I2C object", test_destroy_recreate_object),
    Case("Write multiple bytes to slave", test_write_multiple_bytes_to_slave),
    Case("Write less bytes than expected to slave", test_write_less_than_expected_bytes_to_slave),
    Case("Read one byte from slave", test_read_one_byte_from_slave),
    Case("Destroy & recreate I2C object", test_destroy_recreate_object),
    Case("Read multiple bytes from slave", test_read_multiple_bytes_from_slave),
    Case("Read less bytes than expected from slave", test_read_less_bytes_than_expected_from_slave),
};

Specification specification(test_setup, cases, test_teardown, greentea_continue_handlers);

// Entry point into the tests
int main()
{
	return !Harness::run(specification);
}
