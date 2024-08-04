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

// Tristate GPOUT1
DigitalIn gpout1Pin(PIN_GPOUT_1_PWM, PullNone);

// DAC and ADC
AnalogOut dac(PIN_ANALOG_OUT);
AnalogIn adc(PIN_ANALOG_IN);

/*
 * Outputs an analog voltage with the DAC and reads it with the ADC.
 *
 * Note: This test assumes that the ADC and DAC use the same reference voltage.  Have yet
 * to encounter a target where this is not the case, but the test will need updates if that
 * does happen.
 */
void dac_adc_test()
{
    const size_t maxStep = 10;

    for(size_t stepIdx = 0; stepIdx <= maxStep; ++stepIdx)
    {
        // Write the analog value
        const float dutyCyclePercent = stepIdx / static_cast<float>(maxStep);
        dac.write(dutyCyclePercent);

        // DAC output also goes through the PWM filter so we also have to wait.
        ThisThread::sleep_for(PWM_FILTER_DELAY);

        // Get and check the result
        float adcPercent = adc.read();
        printf("DAC output of %.01f%% produced an ADC reading of %.01f%%\n",
               dutyCyclePercent * 100.0f, adcPercent * 100.0f, dutyCyclePercent * 100.0f);
        TEST_ASSERT_FLOAT_WITHIN(ADC_TOLERANCE_PERCENT, dutyCyclePercent, adcPercent);

        // Also check the read value
        const float readTolerance = 1/256.0; // Assume at least an 8 bit DAC
        TEST_ASSERT_FLOAT_WITHIN(readTolerance, dutyCyclePercent, dac.read());
    }
}

utest::v1::status_t test_setup(const size_t number_of_cases) {
    // Setup Greentea using a reasonable timeout in seconds
    GREENTEA_SETUP(30, "default_auto");
    return verbose_test_setup_handler(number_of_cases);
}

// Test cases
Case cases[] = {
        Case("DAC to ADC", dac_adc_test),
};

Specification specification(test_setup, cases, greentea_continue_handlers);

// Entry point into the tests
int main()
{
    return !Harness::run(specification);
}