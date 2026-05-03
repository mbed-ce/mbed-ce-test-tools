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

#include "mbed.h"
#include "greentea-client/test_env.h"
#include "unity.h"
#include "utest.h"

#include <cinttypes>

#include "ci_test_common.h"

using namespace utest::v1;

// Measures propagation time from one digital I/O to another.
template <PinName dout_pin, PinName din_pin>
void DigitalIO_PropagationTime_Test()
{
    DigitalOut dout(dout_pin);
    DigitalIn din(din_pin);

    Timer propTimer;

    // Start with low
    dout = 0;
    rtos::ThisThread::sleep_for(1ms);

    // Send a high
    propTimer.start();
    dout = 1;

    while(!din) {}
    propTimer.stop();

    // Make sure it stays high continuously after going high
    Timer keepValueTimer;
    keepValueTimer.start();
    while(keepValueTimer.elapsed_time() < 10ms) {
        TEST_ASSERT(din.read() == 1);
    }

    const auto zeroToOneTime = propTimer.elapsed_time();

    propTimer.reset();

    // Send a low
    propTimer.start();
    dout = 0;

    while(din) {}
    propTimer.stop();

    // Make sure it stays low after going low
    keepValueTimer.reset();
    keepValueTimer.start();
    while(keepValueTimer.elapsed_time() < 10ms) {
        TEST_ASSERT(din.read() == 0);
    }

    const auto threshold = (dout_pin == PIN_GPIN_1) ? GPIO_PROPAGATION_TIME_GPIN1_TO_GPOUT1 : GPIO_PROPAGATION_TIME;
    printf("0 -> 1 propagation took %" PRIi64 "us.\n", zeroToOneTime.count());
    TEST_ASSERT(zeroToOneTime <= threshold);
    printf("1 -> 0 propagation took %" PRIi64 "us.\n", propTimer.elapsed_time().count());
    TEST_ASSERT(propTimer.elapsed_time() <= threshold);
}

utest::v1::status_t test_setup(const size_t number_of_cases) {
    // Setup Greentea using a reasonable timeout in seconds
    GREENTEA_SETUP(30, "default_auto");

    // Disconnect serial bridge
    static BusOut funcSelPins(PIN_FUNC_SEL0, PIN_FUNC_SEL1, PIN_FUNC_SEL2);
    funcSelPins = 0b111;

#ifdef PIN_ANALOG_IN
    // Analog in pin is connected to GPOUT1 so make sure to tristate it for this test
    static DigitalIn analogInPin(PIN_ANALOG_IN, PullNone);
#endif

#ifdef PIN_ANALOG_OUT
    // DAC pin is connected to GPOUT1 so make sure to tristate it for this test
    static DigitalIn dacPin(PIN_ANALOG_OUT, PullNone);
#endif

    return verbose_test_setup_handler(number_of_cases);
}

// Test cases
Case cases[] = {
    Case("Digital I/O GPOUT_0 -> GPIN_0", DigitalIO_PropagationTime_Test<PIN_GPOUT_0, PIN_GPIN_0>),
    Case("Digital I/O GPIN_0 -> GPOUT_0", DigitalIO_PropagationTime_Test<PIN_GPIN_0, PIN_GPOUT_0>),
    Case("Digital I/O GPOUT_1 -> GPIN_1", DigitalIO_PropagationTime_Test<PIN_GPOUT_1_PWM, PIN_GPIN_1>),
    Case("Digital I/O GPIN_1 -> GPOUT_1", DigitalIO_PropagationTime_Test<PIN_GPIN_1, PIN_GPOUT_1_PWM>),
    Case("Digital I/O GPOUT_2 -> GPIN_2", DigitalIO_PropagationTime_Test<PIN_GPOUT_2, PIN_GPIN_2>),
    Case("Digital I/O GPIN_2 -> GPOUT_2", DigitalIO_PropagationTime_Test<PIN_GPIN_2, PIN_GPOUT_2>),
};

Specification specification(test_setup, cases, greentea_continue_handlers);

// Entry point into the tests
int main()
{
    return !Harness::run(specification);
}
