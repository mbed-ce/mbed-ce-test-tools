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
#if !DEVICE_I2C
#error [NOT_SUPPORTED] I2C not supported on this platform, add 'DEVICE_I2C' definition to your platform.
#endif

#include "mbed.h"
#include "static_pinmap.h"
#include "greentea-client/test_env.h"
#include "unity.h"
#include "utest.h"
#include "ci_test_common.h"

using namespace utest::v1;

// 7-bit I2C address of the Mbed MCU
#define MBED_I2C_ADDRESS 0x72
#define MBED_I2C_ADDRESS_STR "0x72"


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
    i2cSlave->address(MBED_I2C_ADDRESS << 1);
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
            i2cSlave->read(reinterpret_cast<char*>(&byteRxed), sizeof(uint8_t));
            break;
        }
    }

    TEST_ASSERT_EQUAL_UINT8(byteRxed, 0x1);
}

void test_destroy_recreate_object()
{
    delete i2cSlave;
    create_i2c_object();
}

utest::v1::status_t test_setup(const size_t number_of_cases)
{
	// Create I2C
    create_i2c_object();

    // Initialize logic analyzer for I2C pinouts
    static BusOut funcSelPins(PIN_FUNC_SEL0, PIN_FUNC_SEL1, PIN_FUNC_SEL2);
    funcSelPins = 0b001;

	// Setup Greentea using a reasonable timeout in seconds
	GREENTEA_SETUP(20, "i2c_slave_comms");
	return verbose_test_setup_handler(number_of_cases);
}

void test_teardown(const size_t passed, const size_t failed, const failure_t failure)
{
    delete i2cSlave;
    return greentea_test_teardown_handler(passed, failed, failure);
}


// Test cases
Case cases[] = {
	Case("Write one byte to slave", test_write_one_byte_to_slave),
};

Specification specification(test_setup, cases, test_teardown, greentea_continue_handlers);

// Entry point into the tests
int main()
{
	return !Harness::run(specification);
}
