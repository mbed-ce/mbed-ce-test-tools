/*
 * Copyright (c) 2024 ARM Limited
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

#ifndef CI_TEST_PINS_H
#define CI_TEST_PINS_H

// This header's job is to define the following set of macros for each target in the CI system.
// These macros are used by the test so that it knows which pins to use for what.
// It needs to match the physical hardware setup of the CI system in order to work.
// In some cases (boards not in Arduino form factor), the assignments are arbitrary, 
// so this header shall be the source of truth.

// PIN_I2C_SCL       - Pin connected to I2C_SCL on the test shield.  Default D15.
// PIN_I2C_SDA       - Pin connected to I2C_SCL on the test shield.  Default D14.
// PIN_FUNC_SEL0     - Pin connected to FUNC_SEL0 on the test shield.  Can be any GPIO.  Default A3.
// PIN_FUNC_SEL1     - Pin connected to FUNC_SEL1 on the test shield.  Can be any GPIO.  Default A4.
// PIN_FUNC_SEL2     - Pin connected to FUNC_SEL2 on the test shield.  Can be any GPIO.  Default A5.
// PIN_SPI_SCLK      - Pin connected to SPI_SCLK on the test shield.  Default D13.
// PIN_SPI_MISO      - Pin connected to SPI_MISO on the test shield.  Default D12
// PIN_SPI_MOSI      - Pin connected to SPI_MOSI on the test shield.  Default D11.
// PIN_SPI_HW_CS     - Pin connected to SPI_HW_CS on the test shield.  Must be a hardware CS pin.  Default D10.
// PIN_SPI_SD_CS     - Pin connected to SPI_SD_CS on the test shield.  Can be any GPIO.  Default D8
// PIN_ANALOG_IN     - Pin connected to ANALOG_IN on the test shield.  Must support AnalogIn.  Default A0.
// PIN_GPOUT_2       - Pin connected to GPOUT_2 on the test shield.  Can be any GPIO.  Default D7.
// PIN_GPIN_2        - Pin connected to GPIN_2 on the test shield.  Can be any GPIO.  Default D6.
// PIN_GPOUT_1_PWM   - Pin connected to GPOUT_1 on the test shield.  Must support PWM.  Default D5.
// PIN_GPIN_1        - Pin connected to GPIN_1 on the test shield.  Can be any GPIO.  Default D4.
// PIN_GPOUT_0       - Pin connected to GPOUT_0 on the test shield.  Can be any GPIO.  Default D3.
// PIN_GPIN_0        - Pin connected to GPIN_0 on the test shield.  Can be any GPIO.  Default D2.


// Overrides for RP2040
#if TARGET_RASPBERRY_PI_PICO
#define PIN_ANALOG_IN A0
#define PIN_GPOUT_1_PWM p27
#endif


// Default definitions, if not overridden above.  These use the Arduino Uno form factor.
#ifdef TARGET_FF_ARDUINO_UNO

#ifndef PIN_I2C_SCL
#define PIN_I2C_SCL ARDUINO_UNO_D15
#endif

#ifndef PIN_I2C_SDA
#define PIN_I2C_SDA ARDUINO_UNO_D14
#endif

#ifndef PIN_FUNC_SEL0
#define PIN_FUNC_SEL0 ARDUINO_UNO_A3
#endif

#ifndef PIN_FUNC_SEL1
#define PIN_FUNC_SEL1 ARDUINO_UNO_A4
#endif

#ifndef PIN_FUNC_SEL2
#define PIN_FUNC_SEL2 ARDUINO_UNO_A5
#endif

#ifndef PIN_SPI_SCLK
#define PIN_SPI_SCLK ARDUINO_UNO_D13
#endif

#ifndef PIN_SPI_MISO
#define PIN_SPI_MISO ARDUINO_UNO_D12
#endif

#ifndef PIN_SPI_MOSI
#define PIN_SPI_MOSI ARDUINO_UNO_D11
#endif

#ifndef PIN_SPI_HW_CS
#define PIN_SPI_HW_CS ARDUINO_UNO_D10
#endif

#ifndef PIN_SPI_SD_CS
#define PIN_SPI_SD_CS ARDUINO_UNO_D8
#endif

#ifndef PIN_ANALOG_IN
#define PIN_ANALOG_IN ARDUINO_UNO_A0
#endif

#ifndef PIN_GPOUT_2
#define PIN_GPOUT_2 ARDUINO_UNO_D7
#endif

#ifndef PIN_GPIN_2
#define PIN_GPIN_2 ARDUINO_UNO_D6
#endif

#ifndef PIN_GPOUT_1_PWM
#define PIN_GPOUT_1_PWM ARDUINO_UNO_D5
#endif

#ifndef PIN_GPIN_1
#define PIN_GPIN_1 ARDUINO_UNO_D4
#endif

#ifndef PIN_GPOUT_0
#define PIN_GPOUT_0 ARDUINO_UNO_D3
#endif

#ifndef PIN_GPIN_0
#define PIN_GPIN_0 ARDUINO_UNO_D2
#endif

#endif

#endif /* CI_TEST_PINS_H */
