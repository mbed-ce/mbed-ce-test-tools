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
#ifndef CI_TEST_CONFIG_H
#define CI_TEST_CONFIG_H

#include "utest_print.h"
#include "greentea-client/test_env.h"
#include "ci_test_pins.h"

// Set to 1 to enable debug messages from the test shield tests
#define TESTSHIELD_DEBUG_MESSAGES 0

#if TESTSHIELD_DEBUG_MESSAGES
#define DEBUG_PRINTF(...) do { utest_printf(__VA_ARGS__); } while(0)
#else
#define DEBUG_PRINTF(...) {}
#endif

// How long to wait after changing a GPIO output pin for the signal to propagate to the input pin.
constexpr int GPIO_PROPAGATION_TIME = 100; // us

// Allow a 1.5% tolerance on the read ADC values.  That should be about right because most Mbed targets
// have between an 8 bit and a 12 bit ADC.
// The least accurate ADC observed so far is on the RP2040, which was up to 1.1% off.
constexpr float ADC_TOLERANCE_PERCENT = .015f;

// How long to wait when setting a PWM value for the hardware filter to settle
constexpr std::chrono::milliseconds PWM_FILTER_DELAY = 50ms; // nominal time constant 10ms

/*
 * Wait for the next host message with the given key, and then assert that its
 * value is expectedVal.
 */
inline void assert_next_message_from_host(char const * key, char const * expectedVal) {

    // Based on the example code: https://os.mbed.com/docs/mbed-os/v6.16/debug-test/greentea-for-testing-applications.html
    char receivedKey[64], receivedValue[64];
    while (1) {
        greentea_parse_kv(receivedKey, receivedValue, sizeof(receivedKey), sizeof(receivedValue));

        if(strncmp(key, receivedKey, sizeof(receivedKey) - 1) == 0) {
            TEST_ASSERT_EQUAL_STRING_LEN(expectedVal, receivedValue, sizeof(receivedKey) - 1);
            break;
        }
    }
}

#endif