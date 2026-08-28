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
#include <random>

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

// Random generator with constant seed
std::minstd_rand randGen(1239454);

// Set up the serial port at a specific baudrate. Also configures the host test to start logging at this baudrate
// Also, because Sigrok can only trigger on Rx OR Tx, not both, we need to know whether data will
// be sent to the MCU first, or from the MCU first.
// Can optionally configure parity, using the PySerial encoding (N = None, O = Odd, E = Even)
void init_uart(int baudrate, bool data_to_mcu_first, char* parity = "N")
{
    uart->set_baud(baudrate);
    uart->clear_rx_full_watermark();

    // Clear out any data currently in the UART
    char data;
    while (uart->rx_buffer_size() > 0) {
        uart->read(&data, 1);
    }

    std::string value = std::to_string(baudrate) + " " + (data_to_mcu_first ? "true" : "false") + " " + parity;
    greentea_send_kv("setup_port_at_baud", value.c_str());
    assert_next_message_from_host("setup_port_at_baud", "complete");
}

// Display and save the logic analyzer recording on the host.
void show_logic_analyzer_recording()
{
    greentea_send_kv("show_logic_analyzer_recording", "please");
    assert_next_message_from_host("show_logic_analyzer_recording", "complete");
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

// How many repetitions do we need to exceed the UART buffer?
constexpr size_t NUM_REPETITIONS_FOR_LONG_TEST = MBED_CONF_DRIVERS_UART_SERIAL_RXBUF_SIZE * 2 / TEST_STRING_LEN;
constexpr size_t LONG_TEST_TOTAL_LEN = NUM_REPETITIONS_FOR_LONG_TEST * TEST_STRING_LEN;

char rxBuffer[LONG_TEST_TOTAL_LEN];

// Send the test string to the host once
template<int baudrate>
void mcu_tx_test_string()
{
#ifdef TARGET_AMA3B1KK
    if (baudrate > 1500000) {
        TEST_SKIP_MESSAGE("Baudrate unsupported");
    }
#endif

    init_uart(baudrate, false);
    uart->write(TEST_STRING, TEST_STRING_LEN);
    uart->sync();

    // Give it time to transmit
    rtos::ThisThread::sleep_for(std::chrono::ceil<std::chrono::milliseconds>(get_time_to_transmit(baudrate, TEST_STRING_LEN)));

    show_logic_analyzer_recording();
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
    init_uart(baudrate, true);
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
        if(timeoutTimer.elapsed_time() > get_time_to_transmit(baudrate, TEST_STRING_LEN) + 50ms)
        {
            printf("Receive timed out after %" PRIi64 "us, only received %zu chars.\n",
                std::chrono::duration_cast<std::chrono::microseconds>(timeoutTimer.elapsed_time()).count(),
                totalBytesRead);
            show_logic_analyzer_recording();
            TEST_FAIL_MESSAGE("Receive timed out");
            return;
        }

        // We do actually want the "buffering" part to get tested, so we don't want to just constantly
        // poll the serial. So, sleep for the time it would take to receive 128 chars or 100ms, whichever is shorter.
        const auto sleepTime = std::min(
            std::chrono::ceil<std::chrono::milliseconds>(get_time_to_transmit(baudrate, 128) + 50ms),
             100ms);
        rtos::ThisThread::sleep_for(sleepTime);
    };

    show_logic_analyzer_recording();
    TEST_ASSERT_EQUAL_STRING_LEN(TEST_STRING, rxBuffer, TEST_STRING_LEN);
    TEST_ASSERT_EQUAL_UINT32(totalBytesRead, TEST_STRING_LEN);
}

// Receive a long string (too long to fit in the BufferedSerial Rx buffer)
template<int baudrate>
void mcu_rx_long_string()
{
    init_uart(baudrate, true);

    host_send_test_string(NUM_REPETITIONS_FOR_LONG_TEST);
    uart->set_blocking(false);

    // Wait until we have the right number of bytes in the Rx buffer
    Timer timeoutTimer;
    timeoutTimer.start();
    size_t totalBytesRead = 0;

    // Distributor for how many character times we sleep
    std::uniform_int_distribution<size_t> sleepTimeCharsDistrib(0, MBED_CONF_DRIVERS_UART_SERIAL_RXBUF_SIZE / 2);

    while(true)
    {
        ssize_t readResult = uart->read(rxBuffer + totalBytesRead, sizeof(rxBuffer) - totalBytesRead);
        //printf("Read: \"%s\"\n", rxBuffer + totalBytesRead);
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

        if(totalBytesRead >= LONG_TEST_TOTAL_LEN)
        {
            break;
        }

        // Check timeout
        if(timeoutTimer.elapsed_time() > get_time_to_transmit(baudrate, LONG_TEST_TOTAL_LEN) + 50ms)
        {
            printf("Receive timed out after %" PRIi64 "us, only received %zu chars, overflow? %s\n",
                std::chrono::duration_cast<std::chrono::microseconds>(timeoutTimer.elapsed_time()).count(),
                totalBytesRead,
                uart->get_rx_full_watermark() ? "y" : "n");
            show_logic_analyzer_recording();
            TEST_FAIL_MESSAGE("Receive timed out");
            return;
        }

        // Allow the buffer to fill up for a bit by doing a sleep
        wait_us(get_time_to_transmit(baudrate, sleepTimeCharsDistrib(randGen)));
    };

    // Should NOT have overflowed
    TEST_ASSERT_FALSE(uart->get_rx_full_watermark());

    show_logic_analyzer_recording();
    for (size_t repetition = 0; repetition < NUM_REPETITIONS_FOR_LONG_TEST; repetition++) {
        if (strncmp(TEST_STRING, rxBuffer + repetition * TEST_STRING_LEN, TEST_STRING_LEN) != 0) {
            printf("Error with repetition %zu of received string:\n", repetition);
            TEST_ASSERT_EQUAL_STRING_LEN(TEST_STRING, rxBuffer + repetition * TEST_STRING_LEN, TEST_STRING_LEN);
        }
    }

    TEST_ASSERT_EQUAL_UINT32(totalBytesRead, LONG_TEST_TOTAL_LEN);
}

// Tests what happens if the Rx buffer in BufferedSerial overflows.
// Can we continue to receive characters?
template<int baudrate>
void mcu_rx_overflow()
{
    init_uart(baudrate, true);
    uart->set_blocking(false);

    // Have the host send a long test string, longer than the Rx buffer
    host_send_test_string(NUM_REPETITIONS_FOR_LONG_TEST);

    // Wait until the host is done transmitting (factoring in that the host has a delay before transmitting)
    wait_us(get_time_to_transmit(baudrate, LONG_TEST_TOTAL_LEN) + 50ms);

    // We should see the buffer be full
    TEST_ASSERT_EQUAL_UINT(uart->rx_buffer_size(), MBED_CONF_DRIVERS_UART_SERIAL_RXBUF_SIZE);

    // The buffer full flag should be set
    TEST_ASSERT_TRUE(uart->get_rx_full_watermark());

    show_logic_analyzer_recording();
}

// Tests the size of the hardware FIFO by seeing how many characters we can buffer with interrupts disabled.
template<int baudrate>
void mcu_rx_hw_fifo()
{
    init_uart(baudrate, true);
    uart->set_blocking(false);

    // Have the host send a long test string
    host_send_test_string(NUM_REPETITIONS_FOR_LONG_TEST);

    // Should not have received anything yet
    TEST_ASSERT_EQUAL_UINT(0, uart->rx_buffer_size());

    // Disable all UART IRQs from firing while we receive the .
    __disable_irq();
    wait_us(get_time_to_transmit(baudrate, LONG_TEST_TOTAL_LEN) + 50ms);
    __enable_irq();

    // This should NOT have been able to set because of the interrupt disable
    TEST_ASSERT_FALSE(uart->get_rx_full_watermark());

    // How many did we get?
    printf("Apparent HW Rx FIFO size: %zu\n", uart->rx_buffer_size());

    // Should have gotten a correct sequence of bytes
    const size_t bytesRead = uart->read(rxBuffer, sizeof(rxBuffer));

    // Shouldn't have gotten anywhere near the full byte sequence but should have gotten more than 0
    TEST_ASSERT(bytesRead > 0);
    TEST_ASSERT(bytesRead < 128); // arbitrary, nothing so far has a fifo this big

    size_t bytesRemaining = bytesRead;
    for (size_t repetition = 0; repetition < NUM_REPETITIONS_FOR_LONG_TEST && bytesRemaining > 0; repetition++) {
        const size_t compareSize = std::min(TEST_STRING_LEN, bytesRemaining);
        if (strncmp(TEST_STRING, rxBuffer + repetition * TEST_STRING_LEN, compareSize) != 0) {
            printf("Error with repetition %zu of received string:\n", repetition);
            TEST_ASSERT_EQUAL_STRING_LEN(TEST_STRING, rxBuffer + repetition * TEST_STRING_LEN, compareSize);
        }
        bytesRemaining -= compareSize;
    }

    show_logic_analyzer_recording();
}



// Test the UART's ability to handle junk on the line without losing its ability to receive characters.
// We do this by configuring the UART for 115200 baud, then having the host send the test string at 9600 baud
// and 921600 baud
void handle_junk_on_line() {

    // Set our Rx baudrate
    uart->set_baud(115200);
    uart->set_blocking(false);

    // Have the host send a test string at 9600 baud
    greentea_send_kv("setup_port_at_baud", "9600 true");
    assert_next_message_from_host("setup_port_at_baud", "complete");
    greentea_send_kv("send_test_string", 1);
    assert_next_message_from_host("send_test_string", "started");
    wait_us(get_time_to_transmit(9600, TEST_STRING_LEN) + 50ms);
    show_logic_analyzer_recording();

    // Have the host send a test string at 921600 baud
    greentea_send_kv("setup_port_at_baud", "921600 true");
    assert_next_message_from_host("setup_port_at_baud", "complete");
    greentea_send_kv("send_test_string", 1);
    assert_next_message_from_host("send_test_string", "started");
    wait_us(get_time_to_transmit(921600, TEST_STRING_LEN) + 50ms);
    show_logic_analyzer_recording();

    // It's OK if we did get some chars, just remove them.
    size_t totalCharsRemoved = 0;
    while(true) {
        ssize_t readResult = uart->read(rxBuffer, sizeof(rxBuffer));
        if(readResult == -EAGAIN)
        {
            // Nothing to read
            break;
        }
        else if(readResult > 0)
        {
            totalCharsRemoved += readResult;
        }
        else
        {
            TEST_FAIL_MESSAGE("Unexpected read result.");
            return;
        }
    }
    printf("UART received %zu junk chars\n", totalCharsRemoved);

    // Now see if we can properly receive data still
    mcu_rx_test_string<115200>();
}

utest::v1::status_t test_setup(const size_t number_of_cases) {
    // Setup Greentea using a reasonable timeout in seconds
    GREENTEA_SETUP(30, "uart_test");

    // Set up mux for UART
    static BusOut funcSelPins(PIN_FUNC_SEL0, PIN_FUNC_SEL1, PIN_FUNC_SEL2);
    funcSelPins = 0b000;

    // Use static pinmap if supported for this device
#if STATIC_PINMAP_READY
    uart = new BufferedSerial(serialPinmap);
#else	
    uart = new BufferedSerial(PIN_UART_MCU_TX, PIN_UART_MCU_RX);
#endif

    return utest::v1::verbose_test_setup_handler(number_of_cases);
}

// Test cases
utest::v1::Case cases[] = {
    // Try sending and receiving at a variety of different baudrates. This may reveal issues in the MCU clock code.
    // Note that the CY7C65211 can handle up to 3Mbaud.
    // utest::v1::Case("Send test string from MCU once (1200 baud)", mcu_tx_test_string<1200>),
    // utest::v1::Case("Receive test string from PC once (1200 baud)", mcu_rx_test_string<1200>),
    // utest::v1::Case("Send test string from MCU once (9600 baud)", mcu_tx_test_string<9600>),
    // utest::v1::Case("Receive test string from PC once (9600 baud)", mcu_rx_test_string<9600>),
    // utest::v1::Case("Send test string from MCU once (115200 baud)", mcu_tx_test_string<115200>),
    // utest::v1::Case("Receive test string from PC once (115200 baud)", mcu_rx_test_string<115200>),
    // utest::v1::Case("Send test string from MCU once (921600 baud)", mcu_tx_test_string<921600>),
    // utest::v1::Case("Receive test string from PC once (921600 baud)", mcu_rx_test_string<921600>),
    // utest::v1::Case("Send test string from MCU once (3000000 baud)", mcu_tx_test_string<3000000>),
    // utest::v1::Case("Receive test string from PC once (3000000 baud)", mcu_rx_test_string<3000000>),

    utest::v1::Case("Receive long string from PC (9600 baud)", mcu_rx_long_string<9600>),
    utest::v1::Case("Receive long string from PC (921600 baud)", mcu_rx_long_string<921600>),

    utest::v1::Case("Rx overflow (9600 baud)", mcu_rx_overflow<9600>),
    utest::v1::Case("Rx overflow (921600 baud)", mcu_rx_overflow<921600>),

    utest::v1::Case("H/W FIFO Test (9600 baud)", mcu_rx_hw_fifo<9600>),
    utest::v1::Case("H/W FIFO Test (921600 baud)", mcu_rx_hw_fifo<921600>),

    utest::v1::Case("Handle Junk on Serial Rx Line", handle_junk_on_line)
};

utest::v1::Specification specification(test_setup, cases, utest::v1::greentea_continue_handlers);

// Entry point into the tests
int main()
{
    return !utest::v1::Harness::run(specification);
}