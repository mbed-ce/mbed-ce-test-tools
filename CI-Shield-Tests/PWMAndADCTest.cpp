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

#include "mbed.h"
#include "static_pinmap.h"
#include "greentea-client/test_env.h"
#include "unity.h"
#include "utest.h"

#include "ci_test_common.h"

#include <random>

using namespace utest::v1;

// Use static pinmaps if available for this target
#if STATIC_PINMAP_READY
constexpr auto adcPinmap = get_analogin_pinmap(PIN_ANALOG_IN);
AnalogIn adc(adcPinmap);
constexpr auto pwmPinmap = get_pwm_pinmap(PIN_GPOUT_1_PWM);
PwmOut pwmOut(pwmPinmap);
#else
AnalogIn adc(PIN_ANALOG_IN);
PwmOut pwmOut(PIN_GPOUT_1_PWM);
#endif

// GPIO output voltage expressed as a percent of the ADC reference voltage.  Experimentally determined by the first test case.
float ioVoltageADCPercent;

std::minstd_rand randomGen(287327); // Fixed random seed for repeatability

/*
 * Get and return the frequency and duty cycle of the current signal via the host test.
 */
std::pair<float, float> read_freq_and_duty_cycle_via_host_test()
{
    // Use the host test to measure the signal attributes
    greentea_send_kv("analyze_signal", "please");

    char receivedKey[64], receivedValue[64];
    float measuredFrequencyHz = 0;
    float measuredDutyCycle = 0;
    while (1) {
        greentea_parse_kv(receivedKey, receivedValue, sizeof(receivedKey), sizeof(receivedValue));

        if(strncmp("frequency", receivedKey, sizeof(receivedKey) - 1) == 0)
        {
            measuredFrequencyHz = atof(receivedValue);
        }
        if(strncmp("duty_cycle", receivedKey, sizeof(receivedKey) - 1) == 0)
        {
            measuredDutyCycle = atof(receivedValue);

            // We get the duty cycle second so we can break once we have it
            break;
        }
    }

    return std::make_pair(measuredFrequencyHz, measuredDutyCycle);
}

/*
 * Generate a failure if this target's JSON does not set the target.default-adc-vref option
 */
void verify_target_default_adc_vref_set()
{
    if(isnan(MBED_CONF_TARGET_DEFAULT_ADC_VREF))
    {
        TEST_FAIL_MESSAGE("target.default-adc-vref not defined!");
    }
}

/*
 * Uses the logic analyzer on the host side to verify the frequency and duty cycle of the current PWM signal
 */
void verify_pwm_freq_and_duty_cycle(float expectedFrequencyHz, float expectedDutyCyclePercent)
{
    auto measuredData = read_freq_and_duty_cycle_via_host_test();

    float measuredFrequencyHz = measuredData.first;
    float measuredDutyCycle = measuredData.second;

    // For frequency, the host test measures for 100 ms, meaning that it should be able to
    // detect frequency within +-10Hz.  We'll double to 20Hz that to be a bit generous.
    // Note that we only run at even power-of-10 frequencies, so most MCUs *should* be able to hit them
    // precisely with a clock divider. There's also oscillator tolerance to consider. The CI shield clock
    // is fairly decent (+-100ppm), but board oscillators can be worse.
    // Current best accuracy is around +-0.15%, on Ambiq Apollo3
    const float frequencyTolerance = 20 + .0015 * expectedFrequencyHz;

    // For duty cycle, implementations should hopefully be at least 0.1% accurate (top count >=1000).
    // However, at high clock frequencies, the resolution often gets worse, because the timer concerned might
    // only be counting to a few hundred before resetting.
    // Example: on RP2040, at 1MHz, the PWM counts to 125 before resetting, so we have an accuracy of 0.01 us
    // on a period of 1 us (giving a resolution a bit better than 1%)
    // Ambiq Apollo3 is even worse -- the source clock is 12MHz, so at 1MHz we only have about
    // +-0.084us (resolution of 8.4%)
    // So, we'll approximate and say that we must only be as accurate as 0.1us if 0.1us is more
    // than 0.1% of the period, so at 1MHz the requirement would only be 10% duty cycle accuracy.
    const float dutyCycleTolerance = std::max(.001f, expectedFrequencyHz / 1e7f);

    printf("Expected PWM frequency was %.00f Hz (+- %.00f Hz) and duty cycle was %.02f%% (+-%.02f%%), host measured frequency %.00f Hz and duty cycle %.02f%%\n",
            expectedFrequencyHz,
            frequencyTolerance,
            expectedDutyCyclePercent * 100.0f,
            dutyCycleTolerance * 100.0f,
            measuredFrequencyHz,
            measuredDutyCycle * 100.0f);

    TEST_ASSERT_FLOAT_WITHIN(frequencyTolerance, expectedFrequencyHz, measuredFrequencyHz);
    TEST_ASSERT_FLOAT_WITHIN(dutyCycleTolerance, expectedDutyCyclePercent, measuredDutyCycle);

    // Extra test: make sure PwmOut::read() works
    TEST_ASSERT_FLOAT_WITHIN(dutyCycleTolerance, expectedDutyCyclePercent, pwmOut.read());
}

/*
 * Tests that we can see a response on the ADC when setting the PWM pin to a constant high or low value.
 */
void test_adc_digital_value()
{
    // The filter in hardware is set up for a PWM signal of ~10kHz.
    pwmOut.period(.0001);

    // Make sure turning the PWM off gets a zero percent input on the ADC
    pwmOut.write(0);
    ThisThread::sleep_for(PWM_FILTER_DELAY);
    float zeroADCPercent = adc.read();
    printf("With the PWM at full off, the ADC reads %.01f%% of reference voltage.\n", zeroADCPercent * 100.0f);
    TEST_ASSERT_FLOAT_WITHIN(.1f, 0, zeroADCPercent);

    // Now see what happens when we turn the PWM all the way on
    pwmOut.write(1);
    ThisThread::sleep_for(PWM_FILTER_DELAY);
    ioVoltageADCPercent = adc.read();
    printf("With the PWM at full on, the ADC reads %.01f%% of reference voltage.\n", ioVoltageADCPercent * 100.0f);

    // We don't actually know what the IO voltage is relative to the ADC reference voltage, but it's a fair bet
    // that it should be at least 10%, so make sure we got at least some kind of reading
    TEST_ASSERT(ioVoltageADCPercent > 0.1f);
}

/*
 * Test reading analog values with the ADC.
 * The analog values are generated by sending a PWM signal through a hardware filter.
 */
void test_adc_analog_value()
{
    // The filter in hardware is set up for a PWM signal of ~10kHz.
    pwmOut.period(.0001);

    const size_t maxStep = 10;

    for(size_t stepIdx = 0; stepIdx <= maxStep; ++stepIdx)
    {
        // Write the analog value
        const float dutyCyclePercent = stepIdx / static_cast<float>(maxStep);
        pwmOut.write(dutyCyclePercent);
        ThisThread::sleep_for(PWM_FILTER_DELAY);

        // We expect an I/O voltage of 3.3V (for compatibility with the test shield).
        // If that went over the ADC voltage reference, then we expect to see the ADC vref value.
        const float expectedVoltage = dutyCyclePercent * 3.3;
        const float expectedVoltageReading = std::min<float>(expectedVoltage, MBED_CONF_TARGET_DEFAULT_ADC_VREF);
        const float expectedFloatReading = expectedVoltageReading / MBED_CONF_TARGET_DEFAULT_ADC_VREF;

        // Get and check the result. If the
        const float adcPercent = adc.read();


        printf("PWM duty cycle of %.01f%% produced an ADC reading of %.01f%% (expected %.01f%%)\n",
               dutyCyclePercent * 100.0f, adcPercent * 100.0f, expectedFloatReading * 100.0f);
        TEST_ASSERT_FLOAT_WITHIN(ADC_TOLERANCE_PERCENT, expectedFloatReading, adcPercent);
    }
}

/*
 * Test that we are actually hitting the PWM frequencies and duty cycles we are supposed to be.
 * This uses the Sigrok logic analyzer to detect the duty cycle and PWM frequency
 */
template<uint32_t period_us>
void test_pwm()
{
    pwmOut.period_us(period_us);
    const float frequency = 1e6 / period_us;

    const size_t numTrials = 5;

    for(size_t trial = 0; trial < numTrials; ++trial)
    {
        // Randomly choose a duty cycle for the trial.
        // With the logic analyzer running at 4 MHz, each pulse needs to last at least 250 ns for it to be detectable.
        // Example: if period_us is 1us, then the minimum duty cycle is (250ns / 1000ns) = 0.25
        float minPeriodPercent = 250 / (period_us * 1e3);

        float maxPeriodPercent = 1 - minPeriodPercent;

        float dutyCycle = std::uniform_real_distribution<float>(minPeriodPercent, maxPeriodPercent)(randomGen);

        pwmOut.write(dutyCycle);

        verify_pwm_freq_and_duty_cycle(frequency, dutyCycle);

        // Extra test: check that read_pulsewidth_us() produces the correct value
        float pulseWidthUs = dutyCycle * period_us;

        // We do want to catch off by 1 errors in read_pulsewidth_us(), but we also want to be a bit lenient -- if
        // pulseWidthUs is, say, 3.457, we need to be able to accept 4 as that can be valid depending on how the
        // driver rounds the number internally.  So, require the returned value to be within 0.75 us of the exact value.
        TEST_ASSERT_FLOAT_WITHIN(0.75f, pulseWidthUs, pwmOut.read_pulsewidth_us());
    }

    // As one last extra test, make sure that reading the period gets the correct value
    TEST_ASSERT_EQUAL_INT32(period_us, pwmOut.read_period_us());
}

/*
 * Test that a PWM output can be suspended and resumed
 */
void test_pwm_suspend_resume()
{
    // Run at 1kHz, 75.0% duty cycle (chosen arbitrarily)
    pwmOut.period_ms(1);
    pwmOut.pulsewidth_us(750);

    verify_pwm_freq_and_duty_cycle(1000, .75f);

    pwmOut.suspend();

    // Suspending the PWM should make the frequency 0 and should fix pin at either high or low.
    // Note that the Mbed API currently does not specify whether suspend() leaves the pin high or low, just that it cannot toggle.
    auto freqAndDutyCycle = read_freq_and_duty_cycle_via_host_test();
    TEST_ASSERT_FLOAT_WITHIN(1, 0, freqAndDutyCycle.first);
    TEST_ASSERT_TRUE(freqAndDutyCycle.second < .0001 || freqAndDutyCycle.second > .9999); // Duty cycle may be 0% or 100%

    pwmOut.resume();

    verify_pwm_freq_and_duty_cycle(1000, .75f);
}

/*
 * Test that a PWM output maintain its duty cycle when the period is changed
 */
void test_pwm_maintains_duty_cycle()
{
    // Run at 1kHz, 75.0% duty cycle (chosen arbitrarily)
    pwmOut.period_ms(1);
    pwmOut.pulsewidth_us(750);

    verify_pwm_freq_and_duty_cycle(1000, .75f);

    // Increase frequency to 40 kHz but keep duty cycle the same
    pwmOut.period_us(25);

    verify_pwm_freq_and_duty_cycle(40000, .75f);

    // Decrease frequency to 200 Hz but keep duty cycle the same
    pwmOut.period_ms(5);

    verify_pwm_freq_and_duty_cycle(200, .75f);
}

utest::v1::status_t test_setup(const size_t number_of_cases) {
    // Setup Greentea using a reasonable timeout in seconds
    GREENTEA_SETUP(75, "signal_analyzer_test");

#ifdef PIN_ANALOG_OUT
    // DAC pin is connected to GPOUT1 so make sure to tristate it for this test
    static DigitalIn dacPin(PIN_ANALOG_OUT, PullNone);
#endif

    return verbose_test_setup_handler(number_of_cases);
}

// Test cases
Case cases[] = {
    Case("Test that target.default-adc-vref is set", verify_target_default_adc_vref_set),
    Case("Test reading digital values with the ADC", test_adc_digital_value),
    Case("Test reading analog values with the ADC", test_adc_analog_value),
    Case("Test PWM frequency and duty cycle (freq = 50 Hz)", test_pwm<20000>),
    Case("Test PWM frequency and duty cycle (freq = 1 kHz)", test_pwm<1000>),
    Case("Test PWM frequency and duty cycle (freq = 10 kHz)", test_pwm<100>),
    Case("Test PWM frequency and duty cycle (freq = 100 kHz)", test_pwm<10>),

    // As the logic analyzer maxes out at 2MHz, this is the fastest we can measure
    Case("Test PWM frequency and duty cycle (freq = 500 kHz)", test_pwm<5>),

    Case("Test PWM Suspend/Resume (freq = 1kHz)", test_pwm_suspend_resume),
    Case("Test PWM Maintains Duty Cycle (freq = 1kHz)", test_pwm_maintains_duty_cycle)
};

Specification specification(test_setup, cases, greentea_continue_handlers);

// Entry point into the tests
int main()
{
    return !Harness::run(specification);
}
