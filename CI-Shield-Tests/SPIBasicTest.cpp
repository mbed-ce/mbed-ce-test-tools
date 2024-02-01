/*
 * Copyright (c) 2023 ARM Limited
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
#if !DEVICE_SPI
#error [NOT_SUPPORTED] SPI not supported on this platform, add 'DEVICE_SPI' definition to your platform.
#endif

#include "mbed.h"
#include "greentea-client/test_env.h"
#include "unity.h"
#include "utest.h"
#include "ci_test_common.h"
#include <cinttypes>

using namespace utest::v1;

// Single instance of SPI used in the test.
// Prefer to use a single instance so that, if it gets in a bad state and cannot execute further
// transactions, this will be visible in the test.
SPI * spi;

// Bytes of the data message that each test sends
uint8_t const standardMessageBytes[] = {0x01, 0x02, 0x04, 0x08};

// Should produce the same wire data as above, but encoded as uint16s.
// Note: regardless of endianness, SPI operates in MSB-first mode, so the most significant
// digits will get clocked out first
uint16_t const standardMessageUint16s[] = {0x0102, 0x0408};

// Should produce the same data above, but encoded as uint32s
uint32_t const standardMessageUint32 = 0x01020408;

// Default write value for the SPI bus
uint8_t const DEFAULT_WRITE_VALUE = 0xAF;

// Response from the SPI mirror resistor when writing the default write value for 4 bytes
uint8_t const defaultWriteResponse[] = {DEFAULT_WRITE_VALUE, DEFAULT_WRITE_VALUE, DEFAULT_WRITE_VALUE, DEFAULT_WRITE_VALUE};

// Get the correct message pointer from the above arrays based on the word size
inline void const * getMessage(size_t wordSize)
{
    switch(wordSize)
    {
        case sizeof(uint8_t):
            return standardMessageBytes;
        case sizeof(uint16_t):
            return standardMessageUint16s;
        case sizeof(uint32_t):
            return &standardMessageUint32;
        default:
            return nullptr;
    }
}

// Long data message used in a few tests.  Starts with a recognizeable pattern.
uint8_t const longMessage[32] = {0x01, 0x02, };

const uint32_t spiFreq = 1000000;
const uint8_t spiMode = 0;

/*
 * Uses the host test to start SPI logging from the device
 */
void host_start_spi_logging()
{
    // Note: Value is not important but cannot be empty
    greentea_send_kv("start_recording_spi", "please");
    assert_next_message_from_host("start_recording_spi", "complete");
}

/*
 * Ask the host to print SPI data from the device
 */
void host_print_spi_data()
{
    // Note: Value is not important but cannot be empty
    greentea_send_kv("print_spi_data", "please");
    assert_next_message_from_host("print_spi_data", "complete");
}

/*
 * Assert that the host machine has seen the "standard message" over the SPI bus
 */
void host_assert_standard_message()
{
    // Note: Value is not important but cannot be empty
    greentea_send_kv("verify_sequence", "standard_word");
    assert_next_message_from_host("verify_sequence", "complete");
}

/*
 * Uses the single-word API, transfers bytes
 */
void write_single_word_uint8()
{
    host_start_spi_logging();

    spi->format(8, spiMode);
    for(uint8_t word : standardMessageBytes)
    {
        // Thanks to the SPI mirror resistor, when nothing is using the miso line,
        // the data on mosi will appear on miso
        TEST_ASSERT_EQUAL_UINT8(word, spi->write(word));
    }

    host_assert_standard_message();
}

/*
 * Uses the single-word API, transfers 16-bit words
 */
void write_single_word_uint16()
{
    host_start_spi_logging();

    spi->format(16, spiMode);
    for(uint16_t word : standardMessageUint16s)
    {
        // Thanks to the SPI mirror resistor, when nothing is using the miso line,
        // the data on mosi will appear on miso
        TEST_ASSERT_EQUAL_UINT16(word, spi->write(word));
    }

    host_assert_standard_message();
}

/*
 * Uses the single-word API, transfers 32-bit words
 */
void write_single_word_uint32()
{
    host_start_spi_logging();

    spi->format(32, spiMode);
    TEST_ASSERT_EQUAL_UINT32(standardMessageUint32, spi->write(standardMessageUint32));

    host_assert_standard_message();
}

/*
 * This test writes data in the Tx direction only using the transactional API.
 * Data is verified by the test shield logic analyzer.
 */
template<typename Word>
void write_transactional_tx_only()
{
    host_start_spi_logging();
    spi->format(sizeof(Word) * 8, spiMode);
    spi->write(reinterpret_cast<Word const *>(getMessage(sizeof(Word))),
               sizeof(standardMessageBytes),
               nullptr,
               0);
    host_assert_standard_message();
}

/*
 * This test reads data in the Rx direction only using the transactional API.
 * Data is not verified, this is just a "did it crash" smoke test.
 */
template<typename Word>
void write_transactional_rx_only()
{
    host_start_spi_logging();
    spi->format(sizeof(Word) * 8, spiMode);

    char rxBytes[sizeof(standardMessageBytes) / sizeof(Word)] {};
    spi->write(nullptr, 0, rxBytes, sizeof(standardMessageBytes));

    host_print_spi_data();

    TEST_ASSERT_EQUAL_HEX8_ARRAY(defaultWriteResponse, rxBytes, sizeof(defaultWriteResponse));
}

/*
 * This test does a bidirectional transfer using the transactional API.
 * MOSI data is verified by the test shield logic analyzer.
 */
template<typename Word>
void write_transactional_tx_rx()
{
    host_start_spi_logging();
    spi->format(sizeof(Word) * 8, spiMode);
    Word rxBytes[sizeof(standardMessageBytes) / sizeof(Word)] {};
    spi->write(reinterpret_cast<Word const *>(getMessage(sizeof(Word))),
               sizeof(standardMessageBytes),
               rxBytes,
               sizeof(standardMessageBytes));
    host_assert_standard_message();
}

/*
 * Tests that we can do operations on the bus using multiple SPI objects without weirdness
 */
void use_multiple_spi_objects()
{
    host_start_spi_logging();

    auto * spi2 = new SPI(PIN_SPI_MOSI, PIN_SPI_MISO, PIN_SPI_SCLK);
    auto * spi3 = new SPI(PIN_SPI_MOSI, PIN_SPI_MISO, PIN_SPI_SCLK);

    for(SPI * spi : {spi, spi2, spi3})
    {
        spi->format(8, spiMode);
        spi->frequency(spiFreq);
    }

    spi->write(standardMessageBytes, 1, nullptr, 0);
    spi2->write(standardMessageBytes + 1, 1, nullptr, 0);
    delete spi2;
    spi3->write(standardMessageBytes + 2, 1, nullptr, 0);
    delete spi3;
    spi->write(standardMessageBytes + 3, 1, nullptr, 0);

    host_assert_standard_message();
}

/*
 * Tests that we can delete the SPI object (causing the peripheral to be deleted) and
 * create it again without bad effects
 */
void free_and_reallocate_spi()
{
    host_start_spi_logging();

    delete spi;

    spi = new SPI(PIN_SPI_MOSI, PIN_SPI_MISO, PIN_SPI_SCLK);
    spi->frequency(spiFreq);
    spi->set_dma_usage(DMA_USAGE_NEVER);

    spi->write(standardMessageBytes, 4, nullptr, 0);

    host_assert_standard_message();
}

#if DEVICE_SPI_ASYNCH

StaticCacheAlignedBuffer<uint8_t, sizeof(standardMessageBytes)> dmaRxBuffer;

template<DMAUsage dmaUsage>
void write_async_tx_only()
{
    host_start_spi_logging();
    spi->format(8, spiMode);
    spi->set_dma_usage(dmaUsage);
    auto ret = spi->transfer_and_wait(standardMessageBytes, sizeof(standardMessageBytes), nullptr, 0);
    TEST_ASSERT_EQUAL(ret, 0);
    host_assert_standard_message();
}

template<DMAUsage dmaUsage>
void write_async_rx_only()
{
    host_start_spi_logging();
    spi->set_dma_usage(dmaUsage);
    auto ret = spi->transfer_and_wait(nullptr, 0, dmaRxBuffer, sizeof(standardMessageBytes), 1s);
    TEST_ASSERT_EQUAL(ret, 0);

    // Note: Currently Mbed does not respect the default write value for async SPI transactions.
    // What's written when the tx buffer is technically undefined but is 0xFF on most platforms.
    // See https://github.com/ARMmbed/mbed-os/issues/13941
    printf("Got: %hhx %hhx %hhx %hhx\n", dmaRxBuffer[0], dmaRxBuffer[1], dmaRxBuffer[2], dmaRxBuffer[3]);

    host_print_spi_data();
}

template<DMAUsage dmaUsage>
void write_async_tx_rx()
{
    host_start_spi_logging();
    spi->set_dma_usage(dmaUsage);

    auto ret = spi->transfer_and_wait(standardMessageBytes, sizeof(standardMessageBytes), dmaRxBuffer, sizeof(standardMessageBytes), 1s);
    TEST_ASSERT_EQUAL(ret, 0);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(standardMessageBytes, dmaRxBuffer.data(), sizeof(standardMessageBytes));
    host_assert_standard_message();
}

/*
 * This test measures how long it takes to do an asynchronous transaction and how much of that time may
 * be used to execute a foreground thread.
 */
template<DMAUsage dmaUsage>
void benchmark_async_transaction()
{
    spi->set_dma_usage(dmaUsage);

    Timer transactionTimer;
    Timer backgroundTimer;

    volatile bool transactionDone = false;

    event_callback_t transferCallback([&](int event) {
        transactionDone = true;
    });

    // Kick off the transaction in the main thread
    transactionTimer.start();
    spi->transfer(longMessage, sizeof(longMessage), nullptr, 0, transferCallback);

    // Now count how much time we have free while the transaction executes in the background
    backgroundTimer.start();
    while(!transactionDone)
    {}
    backgroundTimer.stop();
    transactionTimer.stop();

    printf("Transferred %zu bytes @ %" PRIu32 "kHz in %" PRIi64 "us, with %" PRIi64 "us occurring in the background.\n",
           sizeof(longMessage), spiFreq / 1000,
           std::chrono::duration_cast<std::chrono::microseconds>(transactionTimer.elapsed_time()).count(),
           std::chrono::duration_cast<std::chrono::microseconds>(backgroundTimer.elapsed_time()).count());
    auto oneClockPeriod = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<float>(1.0/spiFreq));
    printf("Note: Based on the byte count and frequency, the theoretical best time for this SPI transaction is %" PRIi64 "us\n",
            std::chrono::duration_cast<std::chrono::microseconds>(oneClockPeriod * sizeof(longMessage) * 8).count());
    printf("Note: the above background time does not include overhead from interrupts, which may be significant.\n");
}

template<DMAUsage dmaUsage>
void async_queue_and_abort()
{
    host_start_spi_logging();

    // Change SPI frequency to run at a lower rate, so we have more time for the test.
    // Out of all devices tested so far, STM32L4 is the limiting factor since its lowest SPI frequency supported
    // is 325kHz.
    // At 325kHz, the transfer will take ~25us/byte.
    spi->frequency(325000);

    spi->format(8, spiMode);
    spi->set_dma_usage(dmaUsage);

    DynamicCacheAlignedBuffer<uint8_t> logMessageRxData1(sizeof(longMessage));
    DynamicCacheAlignedBuffer<uint8_t> logMessageRxData2(sizeof(longMessage));

    // Fill buffers with a specific pattern.
    // The data that we'll get off the line is arbitrary but it will overwrite this pattern
    // so we can tell how much of each buffer was written.
    const uint8_t TEST_PATTERN = 0xAF;
    memset(logMessageRxData1.data(), TEST_PATTERN, sizeof(longMessage));
    memset(logMessageRxData2.data(), TEST_PATTERN, sizeof(longMessage));

    // Set up a callback to save the value of the event, if delivered
    volatile int callbackEvent1 = 0;
    event_callback_t transferCallback1([&](int event) {
        callbackEvent1 = event;
    });

    volatile int callbackEvent2 = 0;
    event_callback_t transferCallback2([&](int event) {
        callbackEvent2 = event;
    });

    // Start two transfers: one which we're going to abort, and one which we will allow to complete.
    auto ret = spi->transfer(longMessage, sizeof(longMessage), logMessageRxData1, sizeof(longMessage), transferCallback1, SPI_EVENT_ALL);
    TEST_ASSERT_EQUAL(ret, 0);
    ret = spi->transfer(longMessage, sizeof(longMessage), logMessageRxData2, sizeof(longMessage), transferCallback2, SPI_EVENT_ALL);
    TEST_ASSERT_EQUAL(ret, 0);

    // Allow enough time to get about halfway through the first transfer.
    wait_us(384);

    // Now cancel the first transfer
    spi->abort_transfer();

    // Allow the second transfer to run to completion
    rtos::ThisThread::sleep_for(10ms);

    // The first transfer should have been canceled after writing at least one byte but before filling the entire Rx buffer
    size_t testPatternCountBuf1 = std::count(logMessageRxData1.begin(), logMessageRxData1.end(), TEST_PATTERN);
    // Depending on DMA behavior, some or none of the bytes may have been written back to the buffer.
    // However, the full count of bytes should not have been written.
    TEST_ASSERT(testPatternCountBuf1 < sizeof(longMessage));

    // The second transfer should have overwritten the entire Rx buffer
    size_t testPatternCountBuf2 = std::count(logMessageRxData2.begin(), logMessageRxData2.end(), TEST_PATTERN);
    TEST_ASSERT_EQUAL(0, testPatternCountBuf2);

    // The first transfer should have delivered no flags.
    // The second transfer should have delivered a completion flag.
    TEST_ASSERT_EQUAL(callbackEvent1, 0);
    TEST_ASSERT_EQUAL(callbackEvent2, SPI_EVENT_COMPLETE);

    greentea_send_kv("verify_queue_and_abort_test", "please");
    assert_next_message_from_host("verify_queue_and_abort_test", "pass");
}

/*
 * Tests that we can do operations on the bus using multiple SPI objects without weirdness
 * in asynchronous mode
 */
template<DMAUsage dmaUsage>
void async_use_multiple_spi_objects()
{
    host_start_spi_logging();

    auto * spi2 = new SPI(PIN_SPI_MOSI, PIN_SPI_MISO, PIN_SPI_SCLK);
    auto * spi3 = new SPI(PIN_SPI_MOSI, PIN_SPI_MISO, PIN_SPI_SCLK);

    for(SPI * spi : {spi, spi2, spi3})
    {
        spi->format(8, spiMode);
        spi->frequency(spiFreq);
        spi->set_dma_usage(dmaUsage);
    }

    spi->transfer_and_wait(standardMessageBytes, 1, nullptr, 0);
    spi2->transfer_and_wait(standardMessageBytes + 1, 1, nullptr, 0);
    delete spi2;
    spi3->transfer_and_wait(standardMessageBytes + 2, 1, nullptr, 0);
    delete spi3;
    spi->transfer_and_wait(standardMessageBytes + 3, 1, nullptr, 0);

    host_assert_standard_message();
}

/*
 * Tests that we can delete the SPI object (causing the peripheral to be deleted) and
 * create it again without bad effects
 */
template<DMAUsage dmaUsage>
void async_free_and_reallocate_spi()
{
    host_start_spi_logging();

    delete spi;

    spi = new SPI(PIN_SPI_MOSI, PIN_SPI_MISO, PIN_SPI_SCLK);
    spi->frequency(spiFreq);
    spi->set_dma_usage(dmaUsage);

    spi->transfer_and_wait(standardMessageBytes, 4, nullptr, 0);

    host_assert_standard_message();
}

#endif

utest::v1::status_t test_setup(const size_t number_of_cases)
{
    // Create SPI.  For now, we won't use any CS pin, because we don't want to trigger the MicroSD card
    // to actually respond.
    spi = new SPI(PIN_SPI_MOSI, PIN_SPI_MISO, PIN_SPI_SCLK);
    spi->frequency(spiFreq);
    spi->set_default_write_value(DEFAULT_WRITE_VALUE);

    // For starters, don't use DMA, but we will use it later
    spi->set_dma_usage(DMA_USAGE_NEVER);

    // Initialize logic analyzer for SPI pinouts
    static BusOut funcSelPins(PIN_FUNC_SEL0, PIN_FUNC_SEL1, PIN_FUNC_SEL2);
    funcSelPins = 0b010;

    // Set the SD card CS pin to high so it doesn't try to use the bus
    DigitalOut sdCsPin(PIN_SPI_SD_CS, 1);

    // Setup Greentea using a reasonable timeout in seconds
    GREENTEA_SETUP(45, "spi_basic_test");
    return verbose_test_setup_handler(number_of_cases);
}

void test_teardown(const size_t passed, const size_t failed, const failure_t failure)
{
    delete spi;
    return greentea_test_teardown_handler(passed, failed, failure);
}

Case cases[] = {
        Case("Send 8 Bit Data via Single Word API", write_single_word_uint8),
        Case("Send 16 Bit Data via Single Word API", write_single_word_uint16),
#if DEVICE_SPI_32BIT_WORDS
        Case("Send 32 Bit Data via Single Word API", write_single_word_uint32),
#endif
        Case("Send 8 Bit Data via Transactional API (Tx only)", write_transactional_tx_only<uint8_t>),
        Case("Send 16 Bit Data via Transactional API (Tx only)", write_transactional_tx_only<uint16_t>),
#if DEVICE_SPI_32BIT_WORDS
        Case("Send 32 Bit Data via Transactional API (Tx only)", write_transactional_tx_only<uint32_t>),
#endif

        Case("Read 8 Bit Data via Transactional API (Rx only)", write_transactional_rx_only<uint8_t>),
        Case("Read 16 Bit Data via Transactional API (Rx only)", write_transactional_rx_only<uint16_t>),
#if DEVICE_SPI_32BIT_WORDS
        Case("Read 32 Bit Data via Transactional API (Rx only)", write_transactional_rx_only<uint32_t>),
#endif

        Case("Transfer 8 Bit Data via Transactional API (Tx/Rx)", write_transactional_tx_rx<uint8_t>),
        Case("Transfer 16 Bit Data via Transactional API (Tx/Rx)", write_transactional_tx_rx<uint16_t>),
#if DEVICE_SPI_32BIT_WORDS
        Case("Transfer 32 Bit Data via Transactional API (Tx/Rx)", write_transactional_tx_rx<uint32_t>),
#endif
        Case("Use Multiple SPI Instances (synchronous API)", use_multiple_spi_objects),
        Case("Free and Reallocate SPI Instance (synchronous API)", free_and_reallocate_spi),

#if DEVICE_SPI_ASYNCH
        Case("Send Data via Async Interrupt API (Tx only)", write_async_tx_only<DMA_USAGE_NEVER>),
        Case("Send Data via Async Interrupt API (Rx only)", write_async_rx_only<DMA_USAGE_NEVER>),
        Case("Free and Reallocate SPI Instance with Interrupts", async_free_and_reallocate_spi<DMA_USAGE_NEVER>),
        Case("Send Data via Async Interrupt API (Tx/Rx)", write_async_tx_rx<DMA_USAGE_NEVER>),
        Case("Benchmark Async SPI via Interrupts", benchmark_async_transaction<DMA_USAGE_NEVER>),
        Case("Queueing and Aborting Async SPI via Interrupts", async_queue_and_abort<DMA_USAGE_NEVER>),
        Case("Use Multiple SPI Instances with Interrupts", async_use_multiple_spi_objects<DMA_USAGE_NEVER>),
        Case("Send Data via Async DMA API (Tx only)", write_async_tx_only<DMA_USAGE_ALWAYS>),
        Case("Send Data via Async DMA API (Rx only)", write_async_rx_only<DMA_USAGE_ALWAYS>),
        Case("Free and Reallocate SPI Instance with DMA", async_free_and_reallocate_spi<DMA_USAGE_ALWAYS>),
        Case("Send Data via Async DMA API (Tx/Rx)", write_async_tx_rx<DMA_USAGE_ALWAYS>),
        Case("Benchmark Async SPI via DMA", benchmark_async_transaction<DMA_USAGE_ALWAYS>),
        Case("Queueing and Aborting Async SPI via DMA", async_queue_and_abort<DMA_USAGE_ALWAYS>),
        Case("Use Multiple SPI Instances with DMA", async_use_multiple_spi_objects<DMA_USAGE_ALWAYS>),
#endif
};

Specification specification(test_setup, cases, test_teardown, greentea_continue_handlers);

// Entry point into the tests
int main()
{
    return !Harness::run(specification);
}
