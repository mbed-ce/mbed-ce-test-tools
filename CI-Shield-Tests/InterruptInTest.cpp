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

#if !DEVICE_INTERRUPTIN
#error [NOT_SUPPORTED] InterruptIn is not supported on this platform, add 'DEVICE_INTERRUPTIN' definition to your platform.
#endif

#include "mbed.h"
#include "greentea-client/test_env.h"
#include "unity.h"
#include "utest.h"
#include "ci_test_common.h"
//#include "rtos.h"

using namespace utest::v1;

volatile bool result = false;

// Callback for all InterruptInput functions
void cbfn(void)
{
	result = true;
}

// Template to check Falling edge and Rising edge interrupts.
template <PinName int_pin, PinName dout_pin>
void InterruptInTest()
{
	result = false;
	InterruptIn intin(int_pin);
	DigitalOut dout(dout_pin);

	// Test Rising Edge InterruptIn
	DEBUG_PRINTF("***** Rising Edge Test \n");
	dout = 0;
	result = false;
	intin.rise(cbfn);
	dout = 1;
	wait_us(GPIO_PROPAGATION_TIME);
	DEBUG_PRINTF("Value of result is : %d\n",result);
	TEST_ASSERT_MESSAGE(result,"cbfn was not triggered on rising edge of pin");

	result = false;

	// Check that callback is not triggered again
	for(size_t checkCounter = 0; checkCounter < 10; ++checkCounter)
	{
		TEST_ASSERT_MESSAGE(!result, "Interrupt was triggered again!")
	}

	// Test Falling Edge InterruptIn
	DEBUG_PRINTF("***** Falling Edge Test \n");
	dout = 1;
	result = false;
	intin.fall(cbfn);
	dout = 0;
    wait_us(GPIO_PROPAGATION_TIME);
	DEBUG_PRINTF("Value of result is : %d\n",result);
	TEST_ASSERT_MESSAGE(result,"cbfn was not triggered on falling edge of pin");

	result = false;

	// Check that callback is not triggered again
	for(size_t checkCounter = 0; checkCounter < 10; ++checkCounter)
	{
		TEST_ASSERT_MESSAGE(!result, "Interrupt was triggered again!")
	}
}

utest::v1::status_t test_setup(const size_t number_of_cases)
{
	// Setup Greentea using a reasonable timeout in seconds
	GREENTEA_SETUP(40, "default_auto");
	return verbose_test_setup_handler(number_of_cases);
}

// Handle test failures, keep testing, dont stop
utest::v1::status_t greentea_failure_handler(const Case *const source, const failure_t reason)
{
	greentea_case_failure_abort_handler(source, reason);
	return STATUS_CONTINUE;
}

// Test cases
Case cases[] = {
		Case("Interrupt from GPOUT_2 -> GPIN_2", InterruptInTest<PIN_GPIN_2,PIN_GPOUT_2>,greentea_failure_handler),
		Case("Interrupt from GPIN_2 -> GPOUT_2", InterruptInTest<PIN_GPOUT_2,PIN_GPIN_2>,greentea_failure_handler),
		Case("Interrupt from GPOUT_1 -> GPIN_1", InterruptInTest<PIN_GPIN_1,PIN_GPOUT_1_PWM>,greentea_failure_handler),
		Case("Interrupt from GPIN_1 -> GPOUT_1", InterruptInTest<PIN_GPOUT_1_PWM,PIN_GPIN_1>,greentea_failure_handler),
		Case("Interrupt from GPOUT_0 -> GPIN_0", InterruptInTest<PIN_GPIN_0,PIN_GPOUT_0>,greentea_failure_handler),
		Case("Interrupt from GPIN_0 -> GPOUT_0", InterruptInTest<PIN_GPOUT_0,PIN_GPIN_0>,greentea_failure_handler),
};

Specification specification(test_setup, cases);

// Entry point into the tests
int main()
{
	return !Harness::run(specification);
}
