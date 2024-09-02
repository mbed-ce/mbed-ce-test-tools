from mbed_host_tests import BaseHostTest
from mbed_host_tests.host_tests_logger import HtrunLogger

import sys
import os
import pathlib
import subprocess
import contextlib
import binascii
import traceback
from typing import List, Optional

import cy_serial_bridge

# Unfortunately there's no easy way to make the test runner add a directory to its module path...
this_script_dir = pathlib.Path(os.path.dirname(__file__))
sys.path.append(str(this_script_dir / ".."))

from host_test_utils.sigrok_interface import SigrokI2CRecorder, pretty_print_i2c_data, I2CStart, I2CRepeatedStart, I2CWriteToAddr, I2CReadFromAddr, I2CDataByte, I2CAck, I2CNack, I2CStop, I2CBusData, pretty_diff_i2c_data
from host_test_utils.usb_serial_numbers import CY7C65211_SERIAL_NUMBER

class I2CSlaveCommsTest(BaseHostTest):

    """
    Host test which sends and receives data from the Mbed MCU (which is in I2C slave mode) using the
    CY7C65211.
    """

    def __init__(self):
        super(I2CSlaveCommsTest, self).__init__()

        self.logger = HtrunLogger('TEST')

        self.recorder = SigrokI2CRecorder()

        self.exit_stack: Optional[contextlib.ExitStack] = None
    
        # Open serial bridge chip
        self.cy_usb_context = cy_serial_bridge.CyScbContext()

    def _callback_start_recording_i2c(self, key: str, value: str, timestamp):
        """
        Called at the start of every test case.  Should start a recording of I2C data.
        """

        self.recorder.record(0.1) # Everything we do in this test should complete in under 0.1s

        self.send_kv('start_recording_i2c', 'complete')

    def _callback_write_bytes_to_slave(self, key: str, value: str, timestamp):
        """
        Command to the host test to write bytes to a specific slave I2C address.
        Argument looks like:
        'addr 0xa0 data 0x00 0x01 0x02 0x03...'
        Address is an 8 bit address!
        """

        # Process arguments
        command_parts = value.split(" ")
        if command_parts[0] != "addr" or command_parts[2] != "data":
            raise RuntimeError("Invalid command for write_bytes_to_slave")
        addr = int(command_parts[1], 0)
        bytes_to_write = bytes([int(data_byte_str, 0) for data_byte_str in command_parts[3:]])

        # Write data to slave device
        success = True
        try:
            self.i2c_bridge.i2c_write(addr >> 1, bytes_to_write)
        except Exception:
            self.logger.prn_err("Error writing to I2C slave: " + traceback.format_exc())
            success = False

        # Generate expected data for the logic analyzer
        expected_i2c_data = [I2CStart(), I2CWriteToAddr(addr), I2CAck()]
        for data_byte in bytes_to_write:
            expected_i2c_data.append(I2CDataByte(data_byte))
            expected_i2c_data.append(I2CAck())
        
        expected_i2c_data.append(I2CStop())

        # Check logic analyzer data
        i2c_data = self.recorder.get_result()
        if not pretty_diff_i2c_data(self.logger, expected_i2c_data, i2c_data):
            success = False

        self.send_kv('write_bytes_to_slave', 'complete' if success else 'error')

    def _callback_try_write_to_wrong_address(self, key: str, value: str, timestamp):
        """
        Command to the host test to try and write data to an incorrect address.  We expect a NACK!
        Value is the 8-bit address to try and write to.
        """

        addr = int(value, 0)

        # Write data to slave device
        try:
            self.i2c_bridge.i2c_write(addr >> 1, bytes([0x1, 0x2])) # Need to write >=2 bytes to avoid CY7C65211 bug where NACK is not detected
            raise RuntimeError("I2C operation should have thrown an exception but didn't!")
        except cy_serial_bridge.driver.I2CNACKError:
            pass # this is expected

        # Check logic analyzer data
        i2c_data = self.recorder.get_result()
        expected_i2c_data = [I2CStart(), I2CWriteToAddr(addr), I2CNack(), I2CStop()]

        self.send_kv('try_write_to_wrong_address', 'complete' if pretty_diff_i2c_data(self.logger, expected_i2c_data, i2c_data) else 'failed')

    def _callback_read_bytes_from_slave(self, key: str, value: str, timestamp):
        """
        Command to the host test to read and verify bytes from a specific slave I2C address.
        Argument looks like:
        'addr 0xa0 expected-data 0x00 0x01 0x02 0x03...'
        Address is an 8 bit address (can be read or write address)!
        """

        # Process arguments
        command_parts = value.split(" ")
        if command_parts[0] != "addr" or command_parts[2] != "expected-data":
            raise RuntimeError("Invalid command for write_bytes_to_slave")
        addr = int(command_parts[1], 0)
        expected_data_bytes = bytes([int(data_byte_str, 0) for data_byte_str in command_parts[3:]])

        # Read data from slave device
        success = True

        try:
            read_data = self.i2c_bridge.i2c_read(addr >> 1, len(expected_data_bytes))
        except Exception:
            self.logger.prn_err("Error reading from I2C slave: " + traceback.format_exc())
            success = False
        
        if read_data != expected_data_bytes:
            self.logger.prn_err(f"Expected '{binascii.b2a_hex(expected_data_bytes).decode('ASCII')}' but read '{binascii.b2a_hex(read_data).decode('ASCII')}'")
            success = False

        # Generate expected data fpr the logic analyzer
        expected_i2c_data = [I2CStart(), I2CReadFromAddr(addr | 1)]
        for data_byte in expected_data_bytes:
            expected_i2c_data.append(I2CAck())
            expected_i2c_data.append(I2CDataByte(data_byte))
        
        # Expect a NACK after the last read byte
        expected_i2c_data.append(I2CNack())
        expected_i2c_data.append(I2CStop())

        # Check logic analyzer data
        i2c_data = self.recorder.get_result()
        if not pretty_diff_i2c_data(self.logger, expected_i2c_data, i2c_data):
            success = False

        self.send_kv('read_bytes_from_slave', 'complete' if success else 'error')

    def _callback_reinit_i2c_bridge(self, key: str, value: str, timestamp):
        """
        Reinitialize the I2C bridge.  This has to be done if I2C is reinitialized on the Mbed device side.
        """
        self._destroy_i2c_bridge()
        self._initialize_i2c_bridge()
        self.send_kv('reinit_i2c_bridge', 'complete')

    def _initialize_i2c_bridge(self):
        """
        Initialize the i2c bridge driver.
        """
        self.i2c_bridge: cy_serial_bridge.CyI2CControllerBridge = self.cy_usb_context.open_device(
                                cy_serial_bridge.DEFAULT_VID, 
                                cy_serial_bridge.DEFAULT_PID, 
                                cy_serial_bridge.OpenMode.I2C_CONTROLLER,
                                CY7C65211_SERIAL_NUMBER)
        
        # Enter serial bridge
        with contextlib.ExitStack() as temp_exit_stack: # Creates a temporary ExitStack
            temp_exit_stack.enter_context(self.i2c_bridge) # Enter the serial bridge using the temporary stack

            self.i2c_bridge.set_i2c_configuration(cy_serial_bridge.driver.CyI2CConfig(frequency=400000))

            self.exit_stack = temp_exit_stack.pop_all() # Creates a new exit stack with ownership of i2c_bridge "moved" into it

    def _destroy_i2c_bridge(self):
        """
        Destroy the i2c bridge driver
        """

        # Exit serial bridge
        if self.exit_stack is not None:
            self.exit_stack.close() # This exits each object saved in the stack
        self.exit_stack = None

    def setup(self):
        self._initialize_i2c_bridge()

        self.register_callback('start_recording_i2c', self._callback_start_recording_i2c)
        self.register_callback('write_bytes_to_slave', self._callback_write_bytes_to_slave)
        self.register_callback('try_write_to_wrong_address', self._callback_try_write_to_wrong_address)
        self.register_callback('read_bytes_from_slave', self._callback_read_bytes_from_slave)
        self.register_callback('reinit_i2c_bridge', self._callback_reinit_i2c_bridge)

        self.logger.prn_inf("I2C Slave Comms host test setup complete.")

    def teardown(self):
        self.recorder.teardown()

        # Noticed that, if the I2C slave implementation is broken and doesn't acknowledge the
        # CY7C65211, it can get "stuck" and keep the I2C bus low, preventing subsequent tests
        # from working.  Closing and reopening it one last time seems to reset it, preventing
        # this from happening.
        self._destroy_i2c_bridge()
        self._initialize_i2c_bridge()

        self._destroy_i2c_bridge()
        
