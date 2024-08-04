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

#include "mbed.h"
#include "greentea-client/test_env.h"
#include "unity.h"
#include "utest.h"
#include "ci_test_common.h"

using namespace utest::v1;


// test that all pins can be marked as BusIn
void busin_define_test(){
	BusIn bin(PIN_I2C_SCL,PIN_I2C_SDA,PIN_SPI_SCLK,PIN_SPI_MISO,PIN_SPI_MOSI,PIN_GPIN_2,PIN_GPIN_1,PIN_GPIN_0);
	volatile int x __attribute__((unused)) = 0;
	x = bin.read();
	TEST_ASSERT_MESSAGE(true,"The fact that it hasn't errored out proves this passes the sniff test");
}

// test that all pins can be marked as GPOUT
void busout_define_test(){
	BusOut bout(PIN_I2C_SCL,PIN_I2C_SDA,PIN_SPI_SCLK,PIN_SPI_MISO,PIN_SPI_MOSI,PIN_GPOUT_2,PIN_GPOUT_1_PWM,PIN_GPOUT_0);
	bout = 0;
	volatile int x = 0;
	while(x < 0xFF){
		DEBUG_PRINTF("\r\n*********\r\nvalue of x is: 0x%x\r\n********\r\n",x);
		x++;
		bout.write(x);
	}
	TEST_ASSERT_MESSAGE(true,"The fact that it hasn't errored out proves this passes the sniff test");
}

// test that each bus can become a reader or a writer
void businout_bidirectional_test(){
	BusInOut bio1(PIN_GPOUT_2, PIN_GPOUT_1_PWM, PIN_GPOUT_0);
	BusInOut bio2(PIN_GPIN_2, PIN_GPIN_1, PIN_GPIN_0);
	bio1.output();
	bio2.input();
	bio1 = 0x00;
	volatile int x = 0x00;

	do
	{
		bio1 = x;
		wait_us(GPIO_PROPAGATION_TIME);
		volatile int y = bio2.read();
		printf("\r\n*********\r\nvalue of x,bio2 is: 0x%x, 0x%x\r\n********\r\n",x,y);
		TEST_ASSERT_MESSAGE(y == x,"Value read on bus does not equal value written. ");

        x = x + 1;
	}
	while(x <= 0b111);

    bio1.input();
	
	wait_us(GPIO_PROPAGATION_TIME);

    bio2.output();

	x = 0x00;
	do
	{
		bio2 = x;
        wait_us(GPIO_PROPAGATION_TIME);
		volatile int y = bio1.read();
		printf("\r\n*********\r\nvalue of x,bio1 is: 0x%x, 0x%x\r\n********\r\n",x,y);
		TEST_ASSERT_MESSAGE(y == x,"Value read on bus does not equal value written. ");

        x = x + 1;
	}
	while(x <= 0b111);

	TEST_ASSERT_MESSAGE(true,"The fact that it hasn't errored out proves this passes the sniff test");
}

// Test writing from one bus to another
void busin_to_out_test(){
	BusIn bin(PIN_GPIN_2, PIN_GPIN_1, PIN_GPIN_0);
	BusOut bout(PIN_GPOUT_2, PIN_GPOUT_1_PWM, PIN_GPOUT_0);
	bout = 0;
	volatile int x = 0;
	do
	{
		x++;
		bout.write(x);
        wait_us(GPIO_PROPAGATION_TIME);
		printf("\r\n*********\r\nvalue of bin,bout,x is: 0x%x, 0x%x, 0x%x\r\n********\r\n",bin.read(),bout.read(),x);
		TEST_ASSERT_MESSAGE(bin.read() == bout.read(),"Value read on bin does not equal value written on bout. ")
	}
	while(x < 0b111);
	TEST_ASSERT_MESSAGE(true,"The fact that it hasn't errored out proves this passes the sniff test");
}

utest::v1::status_t test_setup(const size_t number_of_cases) {
	// Setup Greentea using a reasonable timeout in seconds
	GREENTEA_SETUP(40, "default_auto");

#ifdef PIN_ANALOG_OUT
    // DAC pin is connected to GPOUT1 so make sure to tristate it for this test
    static DigitalIn dacPin(PIN_ANALOG_OUT, PullNone);
#endif

	return verbose_test_setup_handler(number_of_cases);
}

// Handle test failures, keep testing, don't stop
utest::v1::status_t greentea_failure_handler(const Case *const source, const failure_t reason) {
	greentea_case_failure_abort_handler(source, reason);
	return STATUS_CONTINUE;
}

// Test cases
Case cases[] = {
		Case("BusIn definable", busin_define_test,greentea_failure_handler),
		Case("BusOut definable", busout_define_test,greentea_failure_handler),
		Case("BusInOut to BusInOut", businout_bidirectional_test,greentea_failure_handler),
		Case("BusIn to BusOut", busin_to_out_test,greentea_failure_handler),
};

Specification specification(test_setup, cases);

// Entry point into the tests
int main()
{
	return !Harness::run(specification);
}
