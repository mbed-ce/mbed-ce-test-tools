/*
 * Copyright (c) 2016 ARM Limited
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
#include "greentea-client/test_env.h"
#include "unity.h"
#include "utest.h"
#include "ci_test_common.h"

using namespace utest::v1;

// Configuration for 24FC02-I/SN
#define EEPROM_I2C_ADDRESS 0xA0 // 8-bit address

// Single instance of I2C used in the test.
// Prefer to use a single instance so that, if it gets in a bad state and cannot execute further
// transactions, this will be visible in the test.
I2C * i2c;

/*
 * Uses the host test to start I2C logging from the device
 */
void host_start_i2c_logging()
{
    // Note: Value is not important but cannot be empty
    greentea_send_kv("start_recording_i2c", "please");
    assert_next_message_from_host("start_recording_i2c", "complete");
}

/*
 * Check that the host test saw the specified sequence on the wire
 */
void host_verify_sequence(char const * sequenceName)
{
    greentea_send_kv("verify_sequence", sequenceName);
    assert_next_message_from_host("verify_sequence", "complete");
}

// Test that we can address the EEPROM with its correct address
void test_correct_addr_single_byte()
{
    host_start_i2c_logging();

	i2c->start();
	TEST_ASSERT_EQUAL(I2C::Result::ACK, i2c->write_byte(EEPROM_I2C_ADDRESS));
	i2c->stop();

    host_verify_sequence("correct_addr_only");
}
void test_correct_addr_transaction()
{
    host_start_i2c_logging();
	TEST_ASSERT_EQUAL(I2C::Result::ACK, i2c->write(EEPROM_I2C_ADDRESS, nullptr, 0, false));
    host_verify_sequence("correct_addr_only");
}
void test_correct_addr_read_transaction()
{
	TEST_ASSERT_EQUAL(I2C::Result::ACK, i2c->read(EEPROM_I2C_ADDRESS | 1, nullptr, 0));
}

// Test that we receive a NACK when trying to use an address that doesn't exist
void test_incorrect_addr_single_byte()
{
    host_start_i2c_logging();

	i2c->start();
	TEST_ASSERT_EQUAL(I2C::Result::NACK, i2c->write_byte(0x20));
	i2c->stop();

    host_verify_sequence("incorrect_addr_only_write");
}
void test_incorrect_addr_zero_len_transaction() // Special test for 0-length transactions because some HALs special case this
{
    host_start_i2c_logging();
	TEST_ASSERT_EQUAL(I2C::Result::NACK, i2c->write(0x20, nullptr, 0, false));
    host_verify_sequence("incorrect_addr_only_write");
}
void test_incorrect_addr_write_transaction()
{
    host_start_i2c_logging();
    uint8_t const data[3] = {0x0, 0x01, 0x03}; // Writes 0x3 to address 1
	TEST_ASSERT_EQUAL(I2C::Result::NACK, i2c->write(0x20, reinterpret_cast<const char *>(data), sizeof(data), false));
    host_verify_sequence("incorrect_addr_only_write");
}
void test_incorrect_addr_read_transaction()
{
    host_start_i2c_logging();
    uint8_t readByte = 0;
	TEST_ASSERT_EQUAL(I2C::Result::NACK, i2c->read(0x20 | 1, reinterpret_cast<char *>(&readByte), 1));
    host_verify_sequence("incorrect_addr_only_read");
}

#if DEVICE_I2C_ASYNCH
void test_incorrect_addr_async()
{
    host_start_i2c_logging();
    uint8_t const data[3] = {0x0, 0x01, 0x03}; // Writes 0x3 to address 1
    TEST_ASSERT_EQUAL(I2C::Result::NACK, i2c->transfer_and_wait(0x20,
                                                               reinterpret_cast<const char *>(data), sizeof(data),
                                                               nullptr, 0,
                                                               1s));
    host_verify_sequence("incorrect_addr_only_write");
}
#endif

// The following tests write one byte in EEPROM, then read it back.  Each pair of tests does the same thing,
// but using a different API.
void test_simple_write_single_byte()
{
    host_start_i2c_logging();

    // Write 0x2 to address 1
    i2c->start();
    TEST_ASSERT_EQUAL(I2C::Result::ACK, i2c->write_byte(EEPROM_I2C_ADDRESS));
    TEST_ASSERT_EQUAL(I2C::Result::ACK, i2c->write_byte(0x0)); // address high
    TEST_ASSERT_EQUAL(I2C::Result::ACK, i2c->write_byte(0x1)); // address low
    TEST_ASSERT_EQUAL(I2C::Result::ACK, i2c->write_byte(0x2)); // data
    i2c->stop();

    // Maximum program time before the EEPROM responds again
    ThisThread::sleep_for(5ms);

    host_verify_sequence("write_2_to_0x1");
}

void test_simple_read_single_byte()
{
    host_start_i2c_logging();

    // Set read address to 1
    i2c->start();
    TEST_ASSERT_EQUAL(I2C::Result::ACK, i2c->write_byte(EEPROM_I2C_ADDRESS));
    TEST_ASSERT_EQUAL(I2C::Result::ACK, i2c->write_byte(0x0)); // address high
    TEST_ASSERT_EQUAL(I2C::Result::ACK, i2c->write_byte(0x1)); // address low
    // Do NOT call stop() so that we do a repeated start

    // Read the byte
    i2c->start();
    TEST_ASSERT_EQUAL(I2C::Result::ACK, i2c->write_byte(EEPROM_I2C_ADDRESS | 1));
    int readByte = i2c->read_byte(false);
    i2c->stop();
    TEST_ASSERT_EQUAL(0x2, readByte);

    host_verify_sequence("read_2_from_0x1");
}

void test_simple_write_transaction()
{
    host_start_i2c_logging();

    // Writes 0x3 to address 1
    // Note: It's worthwhile to actually change the value vs earlier in the test, so that we can
    // verify that the EEPROM is accepting our write operations.
    uint8_t const data[3] = {0x0, 0x01, 0x03};
    TEST_ASSERT_EQUAL(I2C::Result::ACK, i2c->write(EEPROM_I2C_ADDRESS, reinterpret_cast<const char *>(data), sizeof(data)));

    // Maximum program time before the EEPROM responds again
    ThisThread::sleep_for(5ms);

    host_verify_sequence("write_3_to_0x1");
}

void test_simple_read_transaction()
{
    host_start_i2c_logging();

    // Set read address to 1
    uint8_t const data[2] = {0x0, 0x01};
    TEST_ASSERT_EQUAL(I2C::Result::ACK, i2c->write(EEPROM_I2C_ADDRESS, reinterpret_cast<const char *>(data), sizeof(data), true));

    // Read the byte back
    uint8_t readByte = 0;
    TEST_ASSERT_EQUAL(I2C::Result::ACK, i2c->read(EEPROM_I2C_ADDRESS | 1, reinterpret_cast<char *>(&readByte), 1));
    TEST_ASSERT_EQUAL_UINT8(0x3, readByte);

    host_verify_sequence("read_3_from_0x1");
}

// Test that we can do a single byte, then a repeated start, then a transaction
void test_repeated_single_byte_to_transaction()
{
    host_start_i2c_logging();

    // Set read address to 1
    i2c->start();
    TEST_ASSERT_EQUAL(I2C::Result::ACK, i2c->write_byte(EEPROM_I2C_ADDRESS));
    TEST_ASSERT_EQUAL(I2C::Result::ACK, i2c->write_byte(0x0)); // address high
    TEST_ASSERT_EQUAL(I2C::Result::ACK, i2c->write_byte(0x1)); // address low
    // Do NOT call stop() so that we do a repeated start

    ThisThread::sleep_for(1ms);

    // Read the byte back
    uint8_t readByte = 0;
    TEST_ASSERT_EQUAL(I2C::Result::ACK, i2c->read(EEPROM_I2C_ADDRESS | 1, reinterpret_cast<char *>(&readByte), 1));
    TEST_ASSERT_EQUAL_UINT8(0x3, readByte);

    host_verify_sequence("read_3_from_0x1");
}

// Test that we can do a transaction, then a repeated start, then a single byte
void test_repeated_transaction_to_single_byte()
{
    host_start_i2c_logging();

    // Set read address to 1
    uint8_t const data[2] = {0x0, 0x01};
    TEST_ASSERT_EQUAL(I2C::Result::ACK, i2c->write(EEPROM_I2C_ADDRESS, reinterpret_cast<const char *>(data), sizeof(data), true));

    // Read the byte
    i2c->start();
    TEST_ASSERT_EQUAL(I2C::Result::ACK, i2c->write_byte(EEPROM_I2C_ADDRESS | 1));
    int readByte = i2c->read_byte(false);
    i2c->stop();
    TEST_ASSERT_EQUAL(0x3, readByte);

    host_verify_sequence("read_3_from_0x1");
}

#if DEVICE_I2C_ASYNCH
void test_simple_write_async()
{
    host_start_i2c_logging();

    uint8_t const data[3] = {0x0, 0x01, 0x02}; // Writes 0x2 to address 1
    TEST_ASSERT_EQUAL(I2C::Result::ACK, i2c->transfer_and_wait(EEPROM_I2C_ADDRESS,
                                                               reinterpret_cast<const char *>(data), sizeof(data),
                                                               nullptr, 0,
                                                               1s));

    // Maximum program time before the EEPROM responds again
    ThisThread::sleep_for(5ms);

    host_verify_sequence("write_2_to_0x1");
}

void test_simple_read_async()
{
    host_start_i2c_logging();

    // Set read address to 1, then read the data back in one fell swoop.
    uint8_t const writeData[2] = {0x0, 0x01};
    uint8_t readByte = 0;
    TEST_ASSERT_EQUAL(I2C::Result::ACK, i2c->transfer_and_wait(EEPROM_I2C_ADDRESS, reinterpret_cast<const char *>(writeData), sizeof(writeData),
                                                               reinterpret_cast<char *>(&readByte), 1,
                                                               1s));

    TEST_ASSERT_EQUAL_UINT8(0x2, readByte);

    host_verify_sequence("read_2_from_0x1");
}

// Test that we can do an async transaction, then a repeated start, then a transaction
void test_repeated_async_to_transaction()
{
    host_start_i2c_logging();

    // Set read address to 1
    uint8_t const writeData[2] = {0x0, 0x01};
    TEST_ASSERT_EQUAL(I2C::Result::ACK, i2c->transfer_and_wait(EEPROM_I2C_ADDRESS, reinterpret_cast<const char *>(&writeData), sizeof(writeData),
                                                               nullptr, 0,
                                                               1s, true));

    ThisThread::sleep_for(1ms);

    // Read the byte back
    uint8_t readByte = 0;
    TEST_ASSERT_EQUAL(I2C::Result::ACK, i2c->read(EEPROM_I2C_ADDRESS | 1, reinterpret_cast<char *>(&readByte), 1));
    TEST_ASSERT_EQUAL_UINT8(0x2, readByte);

    host_verify_sequence("read_2_from_0x1");
}

// Test that we can do an async transaction, then a repeated start, then a single byte
void test_repeated_async_to_single_byte()
{
    host_start_i2c_logging();

    // Set read address to 1
    uint8_t const writeData[2] = {0x0, 0x01};
    TEST_ASSERT_EQUAL(I2C::Result::ACK, i2c->transfer_and_wait(EEPROM_I2C_ADDRESS, reinterpret_cast<const char *>(&writeData), sizeof(writeData),
                                                               nullptr, 0,
                                                               1s, true));

    ThisThread::sleep_for(1ms);

    // Read the byte
    i2c->start();
    TEST_ASSERT_EQUAL(I2C::Result::ACK, i2c->write_byte(EEPROM_I2C_ADDRESS | 1));
    int readByte = i2c->read_byte(false);
    i2c->stop();
    TEST_ASSERT_EQUAL(0x2, readByte);

    host_verify_sequence("read_2_from_0x1");
}

// Test that we can do a transaction, then a repeated start, then an async transaction
void test_repeated_transaction_to_async()
{
    host_start_i2c_logging();

    // Set read address to 1
    uint8_t const writeData[2] = {0x0, 0x01};
    TEST_ASSERT_EQUAL(I2C::Result::ACK, i2c->write(EEPROM_I2C_ADDRESS, reinterpret_cast<const char *>(writeData), sizeof(writeData), true));

    // Read the byte
    uint8_t readByte = 0;
    TEST_ASSERT_EQUAL(I2C::Result::ACK, i2c->transfer_and_wait(EEPROM_I2C_ADDRESS, nullptr, 0,
                                                               reinterpret_cast<char *>(&readByte), 1,
                                                               1s));

    TEST_ASSERT_EQUAL_UINT8(0x2, readByte);

    host_verify_sequence("read_2_from_0x1");
}

// Test that we can do a transaction, then a repeated start, then an async transaction
void test_repeated_single_byte_to_async()
{
    host_start_i2c_logging();

    // Set read address to 1
    i2c->start();
    TEST_ASSERT_EQUAL(I2C::Result::ACK, i2c->write_byte(EEPROM_I2C_ADDRESS));
    TEST_ASSERT_EQUAL(I2C::Result::ACK, i2c->write_byte(0x0)); // address high
    TEST_ASSERT_EQUAL(I2C::Result::ACK, i2c->write_byte(0x1)); // address low
    // Do NOT call stop() so that we do a repeated start

    // Read the byte
    uint8_t readByte = 0;
    TEST_ASSERT_EQUAL(I2C::Result::ACK, i2c->transfer_and_wait(EEPROM_I2C_ADDRESS, nullptr, 0,
                                                               reinterpret_cast<char *>(&readByte), 1,
                                                               1s));

    TEST_ASSERT_EQUAL_UINT8(0x2, readByte);

    host_verify_sequence("read_2_from_0x1");
}

volatile bool threadRan = false;
void background_thread_func()
{
    threadRan = true;
}

// Test that the main thread actually goes to sleep when we do an async I2C operation.
void async_causes_thread_to_sleep()
{
    host_start_i2c_logging();

    Thread backgroundThread(osPriorityBelowNormal); // this ensures that the thread will not run unless the main thread is blocked.
    backgroundThread.start(callback(background_thread_func));

    uint8_t const writeData[2] = {0x0, 0x01};
    uint8_t readByte = 0;
    TEST_ASSERT_EQUAL(I2C::Result::ACK, i2c->transfer_and_wait(EEPROM_I2C_ADDRESS, reinterpret_cast<const char *>(writeData), sizeof(writeData),
                                                               reinterpret_cast<char *>(&readByte), 1,
                                                               1s));

    TEST_ASSERT_EQUAL_UINT8(0x2, readByte);
    TEST_ASSERT(threadRan);

    backgroundThread.join();

    host_verify_sequence("read_2_from_0x1");
}

#endif

utest::v1::status_t test_setup(const size_t number_of_cases)
{
	// Create I2C
	i2c = new I2C(PIN_I2C_SDA, PIN_I2C_SCL);
	i2c->frequency(100000); // Use a lower frequency so that a logic analyzer can more easily capture what's up

    // Initialize logic analyzer for I2C pinouts
    static BusOut funcSelPins(PIN_FUNC_SEL0, PIN_FUNC_SEL1, PIN_FUNC_SEL2);
    funcSelPins = 0b001;

	// Setup Greentea using a reasonable timeout in seconds
	GREENTEA_SETUP(20, "i2c_basic_test");
	return verbose_test_setup_handler(number_of_cases);
}

void test_teardown(const size_t passed, const size_t failed, const failure_t failure)
{
    delete i2c;
    return greentea_test_teardown_handler(passed, failed, failure);
}

// Macro to help with async tests (can only run them if the device has the I2C_ASYNCH feature)
#if DEVICE_I2C_ASYNCH
#define ADD_ASYNC_TEST(x) x,
#else
#define ADD_ASYNC_TEST(x)
#endif

// Test cases
Case cases[] = {
		Case("Correct Address - Single Byte", test_correct_addr_single_byte),
		Case("Correct Address - Transaction", test_correct_addr_transaction),
        Case("Incorrect Address - Single Byte", test_incorrect_addr_single_byte),
		Case("Incorrect Address - Zero Length Transaction", test_incorrect_addr_zero_len_transaction),
        Case("Incorrect Address - Write Transaction", test_incorrect_addr_write_transaction),
        Case("Incorrect Address - Read Transaction", test_incorrect_addr_read_transaction),
        ADD_ASYNC_TEST(Case("Incorrect Address - Async", test_incorrect_addr_async))
        Case("Simple Write - Single Byte", test_simple_write_single_byte),
        Case("Simple Read - Single Byte", test_simple_read_single_byte),
        Case("Simple Write - Transaction", test_simple_write_transaction),
        Case("Simple Read - Transaction", test_simple_read_transaction),
        Case("Mixed Usage - Single Byte -> repeated -> Transaction", test_repeated_single_byte_to_transaction),
        Case("Mixed Usage - Transaction -> repeated -> Single Byte", test_repeated_transaction_to_single_byte),
        ADD_ASYNC_TEST(Case("Simple Write - Async", test_simple_write_async))
        ADD_ASYNC_TEST(Case("Simple Read - Async", test_simple_read_async))
        ADD_ASYNC_TEST(Case("Mixed Usage - Async -> repeated -> Transaction", test_repeated_async_to_transaction))
        ADD_ASYNC_TEST(Case("Mixed Usage - Async -> repeated -> Single Byte", test_repeated_async_to_single_byte))
        ADD_ASYNC_TEST(Case("Mixed Usage - Transaction -> repeated -> Async", test_repeated_transaction_to_async))
        ADD_ASYNC_TEST(Case("Mixed Usage - Single Byte -> repeated -> Async", test_repeated_single_byte_to_async))
        ADD_ASYNC_TEST(Case("Async causes thread to sleep?", async_causes_thread_to_sleep))
};

Specification specification(test_setup, cases, test_teardown, greentea_continue_handlers);

// Entry point into the tests
int main()
{
	return !Harness::run(specification);
}
