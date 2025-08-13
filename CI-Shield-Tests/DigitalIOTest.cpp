/*
 * Copyright (c) 2022 ARM Limited
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

#include "ci_test_common.h"

using namespace utest::v1;

DigitalIn GPIN_0(PIN_GPIN_0);
DigitalIn GPIN_1(PIN_GPIN_1);

// We have one digital out with each initial state, 1 and 0.  This checks
// that the global constructor initialized the pin properly.
DigitalOut GPOUT_0(PIN_GPOUT_0, 0);
DigitalOut GPOUT_1(PIN_GPOUT_1_PWM, 1);

// Test of globally allocated DigitalOuts and DigitalIns
template <DigitalOut & dout, DigitalIn & din, int pin_initial_state>
void DigitalIO_Global_Test()
{
    TEST_ASSERT_MESSAGE(din.read() == pin_initial_state, "Initial state of input pin doesn't match bootup value of output pin.");
    TEST_ASSERT_MESSAGE(dout.read() == pin_initial_state, "Initial state of output pin doesn't match bootup value of output pin.");

    dout = !pin_initial_state;
    wait_us(GPIO_PROPAGATION_TIME);

    TEST_ASSERT_MESSAGE(dout.read() == !pin_initial_state, "Toggled state of output pin doesn't match toggled value of output pin.");
    TEST_ASSERT_MESSAGE(din.read() == !pin_initial_state, "Toggled state of input pin doesn't match toggled value of output pin.");
}

// Test of stack-allocated DigitalOuts and DigitalIns
template <PinName dout_pin, PinName din_pin>
void DigitalIO_StackAllocated_Test()
{
    DigitalOut dout(dout_pin);
    DigitalIn din(din_pin);
    // test 0
    dout = 0;
    wait_us(GPIO_PROPAGATION_TIME);
    TEST_ASSERT_MESSAGE(0 == din.read(),"Expected value to be 0, read value was not zero");
    // test 1
    dout = 1;
    wait_us(GPIO_PROPAGATION_TIME);
    TEST_ASSERT_MESSAGE(1 == din.read(),"Expected value to be 1, read value was not one");
    // test 2
    // Test = operator in addition to the .read() function
    dout = 0;
    wait_us(GPIO_PROPAGATION_TIME);
    TEST_ASSERT_MESSAGE(0 == din,"Expected value to be 0, read value was not zero");
}

// Test of pull up and pull down mode
template <PinName dout_pin, PinName din_pin>
void DigitalIO_PullUpPullDown_Test()
{
    DigitalIn dout(dout_pin);
    DigitalIn din(din_pin, PullNone); // Make sure no pullup/pulldown is active on this pin as some targets have this by default.

    // test 0
    dout.mode(PullDown);
    wait_us(GPIO_PROPAGATION_TIME);
    TEST_ASSERT_MESSAGE(0 == din.read(),"Expected value to be 0, read value was not zero");

    // test 1
    dout.mode(PullUp);
    wait_us(GPIO_PROPAGATION_TIME);
    TEST_ASSERT_MESSAGE(1 == din.read(),"Expected value to be 1, read value was not one");
}

// Test that open-drain mode works as expected
template <PinName dout_pin, PinName din_pin>
void DigitalIO_OpenDrain_Test()
{
    DigitalInOut openDrain(dout_pin, PIN_OUTPUT, OpenDrain, 1);
    DigitalInOut connectedPin(din_pin, PIN_INPUT, PullUp, 0);

    // With the open drain pin not outputting anything, we should see both pins reading high
    wait_us(GPIO_PROPAGATION_TIME);
    TEST_ASSERT_MESSAGE(1 == openDrain.read(), "openDrain was low!");
    TEST_ASSERT_MESSAGE(1 == connectedPin.read(), "connectedPin was low!");

    // Outputting a low on the open drain pin should bring both pins low
    openDrain = 0;
    wait_us(GPIO_PROPAGATION_TIME);
    TEST_ASSERT_MESSAGE(0 == openDrain.read(), "openDrain was high!");
    TEST_ASSERT_MESSAGE(0 == connectedPin.read(), "connectedPin was high!");

    // Outputting a high should cause both pins to go high again
    openDrain = 1;
    wait_us(GPIO_PROPAGATION_TIME);
    TEST_ASSERT_MESSAGE(1 == openDrain.read(), "openDrain was low!");
    TEST_ASSERT_MESSAGE(1 == connectedPin.read(), "connectedPin was low!");

    // If we output a logic low with the other pin, this should be detectable with the open drain pin
    connectedPin.output();
    connectedPin.write(0);
    wait_us(GPIO_PROPAGATION_TIME);
    TEST_ASSERT_MESSAGE(0 == openDrain.read(), "openDrain was high!");
    TEST_ASSERT_MESSAGE(0 == connectedPin.read(), "connectedPin was high!");
}

utest::v1::status_t test_setup(const size_t number_of_cases) {
    // Setup Greentea using a reasonable timeout in seconds
    GREENTEA_SETUP(30, "default_auto");

#ifdef PIN_ANALOG_OUT
    // DAC pin is connected to GPOUT1 so make sure to tristate it for this test
    static DigitalIn dacPin(PIN_ANALOG_OUT, PullNone);
#endif

    return verbose_test_setup_handler(number_of_cases);
}

// Test cases
Case cases[] = {
    Case("Digital I/O GPOUT_0 -> GPIN_0 (Global)", DigitalIO_Global_Test<GPOUT_0, GPIN_0, 0>),
    Case("Digital I/O GPOUT_1 -> GPIN_1 (Global)", DigitalIO_Global_Test<GPOUT_1, GPIN_1, 1>),
    Case("Digital I/O GPIN_2 -> GPOUT_2 (Stack Allocated)", DigitalIO_StackAllocated_Test<PIN_GPOUT_2, PIN_GPIN_2>),
    Case("Digital I/O GPOUT_2 -> GPIN_2 (Stack Allocated)", DigitalIO_StackAllocated_Test<PIN_GPIN_2, PIN_GPOUT_2>),
    Case("Digital I/O Pull-Up and Pull-Down Mode", DigitalIO_PullUpPullDown_Test<PIN_GPIN_2, PIN_GPOUT_2>),
    Case("Digital I/O Open Drain Mode", DigitalIO_OpenDrain_Test<PIN_GPIN_2, PIN_GPOUT_2>)
};

Specification specification(test_setup, cases, greentea_continue_handlers);

// Entry point into the tests
int main()
{
    return !Harness::run(specification);
}
