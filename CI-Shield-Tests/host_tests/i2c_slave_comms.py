from mbed_host_tests import BaseHostTest
from mbed_host_tests.host_tests_logger import HtrunLogger

import sys
import os
import pathlib
import subprocess
import contextlib

import cy_serial_bridge

# Unfortunately there's no easy way to make the test runner add a directory to its module path...
this_script_dir = pathlib.Path(os.path.dirname(__file__))
sys.path.append(str(this_script_dir / ".." / "host_test_utils"))

from sigrok_interface import SigrokI2CRecorder, pretty_print_i2c_data


class I2CSlaveCommsTest(BaseHostTest):

    """
    Host test which sends and receives data from the Mbed MCU (which is in I2C slave mode) using the
    CY7C65211.
    """

    def __init__(self):
        super(I2CSlaveCommsTest, self).__init__()

        self.logger = HtrunLogger('TEST')

        if "MBED_CI_SHIELD_SERIAL_NUMBER" in os.environ:
            ci_shield_serno = os.environ["MBED_CI_SHIELD_SERIAL_NUMBER"]
            self.logger.prn_inf("Connecting to CI shield with serial number " + ci_shield_serno)
        else:
            ci_shield_serno = None
            self.logger.prn_inf("Will use any connected CI shield for this test.  Export the MBED_CI_SHIELD_SERIAL_NUMBER environment var to select a specific shield.")
            self.logger.prn_inf("e.g. 'export MBED_CI_SHIELD_SERIAL_NUMBER=SN002'")

        self.recorder = SigrokI2CRecorder()
    
        # Open serial bridge chip
        self.cy_usb_context = cy_serial_bridge.CyScbContext()
        self.i2c_bridge: cy_serial_bridge.CyI2CControllerBridge = self.cy_usb_context.open_device(
                         cy_serial_bridge.DEFAULT_VID, 
                         cy_serial_bridge.DEFAULT_PID, 
                         cy_serial_bridge.OpenMode.I2C_CONTROLLER,
                         ci_shield_serno)


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
        Address is a 7 bit address!
        """

        # Process arguments
        command_parts = value.split(" ")
        if command_parts[0] != "addr" or command_parts[2] != "data":
            raise RuntimeError("Invalid command for write_bytes_to_slave")
        addr = int(command_parts[1], 0)
        bytes_to_write = bytes([int(data_byte_str, 0) for data_byte_str in command_parts[3:]])

        # Write data to slave device
        self.i2c_bridge.i2c_write(addr, bytes_to_write)

        self.send_kv('write_bytes_to_slave', 'complete')

    def _callback_display_i2c_data(self, key: str, value: str, timestamp):
        """
        Verify that the current recorded I2C data matches the given sequence
        """

        try:
            recorded_data = self.recorder.get_result()
        except subprocess.TimeoutExpired:
            recorded_data = []

        if len(recorded_data) > 0:
            self.logger.prn_inf("Saw on the I2C bus:\n" + pretty_print_i2c_data(recorded_data))
        else:
            self.logger.prn_inf("WARNING: Logic analyzer saw nothing the I2C bus.")

        self.send_kv('display_i2c_data', 'complete')

    def setup(self):

        # Enter serial bridge
        with contextlib.ExitStack() as temp_exit_stack: # Creates a temporary ExitStack
            temp_exit_stack.enter_context(self.i2c_bridge) # Enter the serial bridge using the temporary stack

            self.i2c_bridge.set_i2c_configuration(cy_serial_bridge.driver.CyI2CConfig(frequency=400000))

            self.exit_stack = temp_exit_stack.pop_all() # Creates a new exit stack with ownership of mcast_socket "moved" into it

        self.register_callback('start_recording_i2c', self._callback_start_recording_i2c)
        self.register_callback('display_i2c_data', self._callback_display_i2c_data)
        self.register_callback('write_bytes_to_slave', self._callback_write_bytes_to_slave)

        self.logger.prn_inf("I2C Record-Only Test host test setup complete.")

    def teardown(self):
        self.recorder.teardown()

        # Exit serial bridge
        if self.exit_stack is not None:
            self.exit_stack.close() # This exits each object saved in the stack
        self.exit_stack = None
