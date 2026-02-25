from mbed_host_tests import BaseHostTest
from mbed_host_tests.host_tests_logger import HtrunLogger

import sys
import os
import pathlib
import contextlib

from typing import Optional

import serial
import cy_serial_bridge

# Unfortunately there's no easy way to make the test runner add a directory to its module path...
this_script_dir = pathlib.Path(os.path.dirname(__file__))
sys.path.append(str(this_script_dir / ".."))

from host_test_utils.usb_serial_numbers import CY7C65211_SERIAL_NUMBER
from host_test_utils.sigrok_interface import SigrokUARTRecorder

class UARTHostTest(BaseHostTest):

    """
    Host test for the UART test suite. Sends and receives strings to the Mbed target device, and
    also records data with the logic analyzer.
    """

    # String which gets sent and received from the target
    TEST_STRING = b"The quick brown fox jumps over the lazy dog.\n"

    def __init__(self):
        super(UARTHostTest, self).__init__()

        self.logger = HtrunLogger('TEST')
        self.recorder = SigrokUARTRecorder()
    
        # Open serial bridge chip
        self.cy_usb_context = cy_serial_bridge.CyScbContext()

    def _callback_setup_port_at_baud(self, key: str, value: str, timestamp):
        """
        Set up the serial port by flushing any data and changing the baudrate.

        Arguments are the baudrate and whether data will be sent to the MCU first, separated by a space

        Also begins a sigrok recording.
        """

        # Parse args
        value_components = value.split(" ")
        baudrate = int(value_components[0])
        data_to_mcu_first = value_components[1] == "true"

        self.uart.baudrate = baudrate
        self.uart.reset_input_buffer()

        # Record for enough time to send 1024 bytes at the selected baudrate or 2 seconds, whichver is less
        record_time = min(2, 1024*10/self.uart.baudrate)

        self.recorder.record(record_time=record_time, 
                             baudrate=self.uart.baudrate, 
                             test_name=self.config.test_name, 
                             test_case_name=self.current_test_case_name,
                             mcu_rx_first=data_to_mcu_first)

        self.send_kv('setup_port_at_baud', 'complete')

    def _callback_verify_repeated_test_string(self, key: str, value: str, timestamp):
        """
        Verify that the current recorded UART Rx buffer contains n repetitions of the test string.
        """

        # try to read more than the expected number of chars from the buffer, to prove that the
        # MCU sent only the expected number.
        expected_num_test_strs = int(value)
        rx_data = self.uart.read(len(self.TEST_STRING) * expected_num_test_strs * 2)

        expected_rx_data = self.TEST_STRING * expected_num_test_strs

        if rx_data == expected_rx_data:
            self.send_kv('verify_repeated_test_string', 'complete')
        else:
            self.logger.prn_err(f"Expected {expected_rx_data!r}, got {rx_data!r}!")
            self.send_kv('verify_repeated_test_string', 'failed')

    def _callback_send_test_string(self, key: str, value: str, timestamp):
        """
        Send repetitions of the test string to the MCU.
        """

        self.uart.write(self.TEST_STRING * int(value))

        self.send_kv('send_test_string', 'started')

    def _callback_show_logic_analyzer_recording(self, key: str, value: str, timestamp):
        self.recorder.get_result()
        self.send_kv('show_logic_analyzer_recording', 'complete')

    def setup(self):

        # Open serial port
        self.uart: serial.Serial = self.cy_usb_context.open_device(cy_serial_bridge.DEFAULT_VID, 
                                cy_serial_bridge.DEFAULT_PID, 
                                cy_serial_bridge.OpenMode.UART_CDC,
                                CY7C65211_SERIAL_NUMBER)
        
        # Use port in nonblocking mode -- we want to take all data available at the time the MCU tells us to check
        self.uart.timeout = 0
        
        self.register_callback('setup_port_at_baud', self._callback_setup_port_at_baud)
        self.register_callback('send_test_string', self._callback_send_test_string)
        self.register_callback('verify_repeated_test_string', self._callback_verify_repeated_test_string)
        self.register_callback('show_logic_analyzer_recording', self._callback_show_logic_analyzer_recording)

        self.logger.prn_inf("UART Test host test setup complete.")

    def teardown(self):
        self.uart.close()

        # Briefly reopen the bridge as I2C so that it does not drive any of the bus lines
        with self.cy_usb_context.open_device(
                cy_serial_bridge.DEFAULT_VID,
                cy_serial_bridge.DEFAULT_PID,
                cy_serial_bridge.OpenMode.I2C_CONTROLLER,
                CY7C65211_SERIAL_NUMBER) as i2c_bridge:
            pass

