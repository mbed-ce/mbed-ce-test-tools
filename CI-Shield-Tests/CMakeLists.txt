cmake_minimum_required(VERSION 3.19)
cmake_policy(VERSION 3.19)

# Initialize Mbed OS build system
set(MBED_APP_JSON_PATH mbed_app.json5)
include(mbed-os/tools/cmake/mbed_toolchain_setup.cmake)

project(mbed-ce-ci-shield-tests)

include(mbed_project_setup)

# Make sure testing is enabled for the project
set(MBED_ENABLE_TESTING TRUE CACHE BOOL "" FORCE)

add_subdirectory(mbed-os)


enable_testing()

# Add tests -------------------------------------------------------
mbed_greentea_add_test(
    TEST_NAME testshield-businout
    TEST_SOURCES BusInOutTest.cpp
)

mbed_greentea_add_test(
    TEST_NAME testshield-interruptin
    TEST_SOURCES InterruptInTest.cpp
)

mbed_greentea_add_test(
    TEST_NAME testshield-digitalio
    TEST_SOURCES DigitalIOTest.cpp
)

mbed_greentea_add_test(
	TEST_NAME testshield-digitalio-prop-time
	TEST_SOURCES DigitalIOPropagationTimeTest.cpp
)

mbed_greentea_add_test(
    TEST_NAME testshield-i2c-basic
    TEST_SOURCES I2CBasicTest.cpp
	HOST_TESTS_DIR host_tests
)

if(NOT "DEVICE_I2CSLAVE=1" IN_LIST MBED_TARGET_DEFINITIONS)
	set(I2C_SLAVE_TEST_SKIPPED "No I2C slave support")
endif()
mbed_greentea_add_test(
    TEST_NAME testshield-i2c-slave-comms
    TEST_SOURCES I2CSlaveCommsTest.cpp
	HOST_TESTS_DIR host_tests
    TEST_SKIPPED ${I2C_SLAVE_TEST_SKIPPED}
)

mbed_greentea_add_test(
    TEST_NAME testshield-i2c-eeprom
    TEST_SOURCES I2CEEPROMTest.cpp
    TEST_REQUIRED_LIBS mbed-storage-i2cee
    HOST_TESTS_DIR host_tests
)

mbed_greentea_add_test(
    TEST_NAME testshield-spi-basic
    TEST_SOURCES SPIBasicTest.cpp
	HOST_TESTS_DIR host_tests
)

mbed_greentea_add_test(
    TEST_NAME testshield-spi-microsd
    TEST_SOURCES SPIMicroSDTest.cpp
    TEST_REQUIRED_LIBS mbed-storage-sd mbed-storage-fat
	HOST_TESTS_DIR host_tests
)

if(NOT "DEVICE_SPISLAVE=1" IN_LIST MBED_TARGET_DEFINITIONS)
	set(SPI_SLAVE_TEST_SKIPPED "No SPI slave support")
endif()
mbed_greentea_add_test(
	TEST_NAME testshield-spi-slave-comms
	TEST_SOURCES SPISlaveCommsTest.cpp
	HOST_TESTS_DIR host_tests
    TEST_SKIPPED ${SPI_SLAVE_TEST_SKIPPED}
)

mbed_greentea_add_test(
	TEST_NAME testshield-pwm-and-adc
	TEST_SOURCES PWMAndADCTest.cpp
	HOST_TESTS_DIR host_tests
)

if(NOT "DEVICE_ANALOGOUT=1" IN_LIST MBED_TARGET_DEFINITIONS)
	set(DAC_ADC_TEST_SKIPPED "No DAC support")
endif()
mbed_greentea_add_test(
	TEST_NAME testshield-dac-to-adc
	TEST_SOURCES DACToADCTest.cpp
	TEST_SKIPPED ${DAC_ADC_TEST_SKIPPED}
)

mbed_greentea_add_test(
	TEST_NAME testshield-uart
	TEST_SOURCES UARTTest.cpp
	HOST_TESTS_DIR host_tests
)