/*
 * Copyright (c) 2025 Jamie Smith
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

#include "mbed.h"
#include "static_pinmap.h"
#include "greentea-client/test_env.h"
#include "unity.h"
#include "utest.h"

#include <cinttypes>
#include <algorithm>

#include "ci_test_common.h"

// check if I2C is supported on this device
#if !DEVICE_SERIAL
#error [NOT_SUPPORTED] Serial not supported on this platform, add 'DEVICE_SERIAL' definition to your platform.
#endif

#if STATIC_PINMAP_READY
// Must be declared globally as Serial stores the pointer
constexpr auto serialPinmap = get_uart_pinmap(PIN_UART_MCU_TX, PIN_UART_MCU_RX);
#endif

BufferedSerial * uart = nullptr;


// Set up the serial port at a specific baudrate. Also configures the host test to start logging at this baudrate
void init_uart(int baudrate)
{
    uart->set_baud(baudrate);

    greentea_send_kv("setup_port_at_baud", baudrate);
    assert_next_message_from_host("setup_port_at_baud", "complete");
}

void assert_host_received_test_string(unsigned int repetitions)
{
    greentea_send_kv("verify_repeated_test_string", repetitions);
    assert_next_message_from_host("verify_repeated_test_string", "complete");
}

// Ask the host to begin sending N repetitions of the test string.
// Returns once the host has begun transmitting.
void host_send_test_string(unsigned int repetitions)
{
    greentea_send_kv("send_test_string", repetitions);
    assert_next_message_from_host("send_test_string", "started");
}

// Get the ideal time that a UART would need to transmit the given number of chars at the given
// baudrate
constexpr std::chrono::microseconds get_time_to_transmit(int baudrate, size_t numChars)
{
    // Each char takes 10 clock periods to transmit
    return std::chrono::ceil<std::chrono::microseconds>(numChars * 10 * std::chrono::duration<float>(1.0f/baudrate));
}

char const * const TEST_STRING = "The quick brown fox jumps over the lazy dog.\n";
constexpr size_t TEST_STRING_LEN = 45;

char rxBuffer[128];

// Send the test string to the host once
template<int baudrate>
void mcu_tx_test_string()
{
#ifdef TARGET_AMA3B1KK
    if (baudrate > 1500000) {
        TEST_SKIP_MESSAGE("Baudrate unsupported");
    }
#endif

    init_uart(baudrate);
    uart->write(TEST_STRING, TEST_STRING_LEN);
    uart->sync();

    // Give it time to transmit
    rtos::ThisThread::sleep_for(std::chrono::ceil<std::chrono::milliseconds>(get_time_to_transmit(baudrate, TEST_STRING_LEN)));

    assert_host_received_test_string(1);
}

// Receive the test string from the host once
template<int baudrate>
void mcu_rx_test_string()
{
#ifdef TARGET_AMA3B1KK
    if (baudrate > 1500000) {
        TEST_SKIP_MESSAGE("Baudrate unsupported");
    }
#endif
    init_uart(baudrate);
    host_send_test_string(1);
    uart->set_blocking(false);

    // Wait until we have the right number of bytes in the Rx buffer
    Timer timeoutTimer;
    timeoutTimer.start();
    size_t totalBytesRead = 0;

    while(true)
    {
    
        ssize_t readResult = uart->read(rxBuffer + totalBytesRead, sizeof(rxBuffer) - totalBytesRead);
        if(readResult == -EAGAIN)
        {
            // Nothing to read
        }
        else if(readResult > 0)
        {
            totalBytesRead += readResult;
        }
        else
        {
            TEST_FAIL_MESSAGE("Unexpected read result.");
            return;
        }

        if(totalBytesRead >= TEST_STRING_LEN)
        {
            break;
        }

        // Check timeout
        if(timeoutTimer.elapsed_time() > get_time_to_transmit(baudrate, TEST_STRING_LEN))
        {
            printf("Receive timed out after %" PRIi64 "ms, only received %zu chars.\n",
                std::chrono::duration_cast<std::chrono::microseconds>(timeoutTimer.elapsed_time()).count(),
                totalBytesRead);
            TEST_FAIL_MESSAGE("Receive timed out");
            return;
        }

        // We do actually want the "buffering" part to get tested, so we don't want to just constantly
        // poll the serial. So, sleep for the time it would take to receive 128 chars or 100ms, whichever is shorter.
        const auto sleepTime = std::min(
            std::chrono::ceil<std::chrono::milliseconds>(get_time_to_transmit(baudrate, 128)),
             100ms);
        rtos::ThisThread::sleep_for(sleepTime);
    };

    TEST_ASSERT_EQUAL_STRING_LEN(TEST_STRING, rxBuffer, TEST_STRING_LEN);
    TEST_ASSERT_EQUAL_UINT32(totalBytesRead, TEST_STRING_LEN);
}

utest::v1::status_t test_setup(const size_t number_of_cases) {
    // Setup Greentea using a reasonable timeout in seconds
    GREENTEA_SETUP(30, "uart_test");

    // Use static pinmap if supported for this device
#if STATIC_PINMAP_READY
    uart = new BufferedSerial(serialPinmap);
#else	
    uart = new BufferedSerial(PIN_UART_MCU_TX, PIN_UART_MCU_RX);
#endif

    // Set up mux for UART
    static BusOut funcSelPins(PIN_FUNC_SEL0, PIN_FUNC_SEL1, PIN_FUNC_SEL2);
    funcSelPins = 0b000;

    return utest::v1::verbose_test_setup_handler(number_of_cases);
}

// Test cases
utest::v1::Case cases[] = {
    // Try sending and receiving at a variety of different baudrates. This may reveal issues in the MCU clock code.
    // Note that the CY7C65211 can handle up to 3Mbaud.
    utest::v1::Case("Send test string from MCU once (1200 baud)", mcu_tx_test_string<1200>),
    utest::v1::Case("Receive test string from PC once (1200 baud)", mcu_rx_test_string<1200>),
    utest::v1::Case("Send test string from MCU once (9600 baud)", mcu_tx_test_string<9600>),
    utest::v1::Case("Receive test string from PC once (9600 baud)", mcu_rx_test_string<9600>),
    utest::v1::Case("Send test string from MCU once (115200 baud)", mcu_tx_test_string<115200>),
    utest::v1::Case("Receive test string from PC once (115200 baud)", mcu_rx_test_string<115200>),
    utest::v1::Case("Send test string from MCU once (921600 baud)", mcu_tx_test_string<921600>),
    utest::v1::Case("Receive test string from PC once (921600 baud)", mcu_rx_test_string<921600>),
    utest::v1::Case("Send test string from MCU once (3000000 baud)", mcu_tx_test_string<3000000>),
    utest::v1::Case("Receive test string from PC once (3000000 baud)", mcu_rx_test_string<3000000>),
};

utest::v1::Specification specification(test_setup, cases, utest::v1::greentea_continue_handlers);

// Entry point into the tests
int main()
{
    return !utest::v1::Harness::run(specification);
}