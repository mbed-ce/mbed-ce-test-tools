from mbed_host_tests import BaseHostTest
from mbed_host_tests.host_tests_logger import HtrunLogger

import traceback
import binascii
import sys
import os
import pathlib
import subprocess

# Unfortunately there's no easy way to make the test runner add a directory to its module path...
this_script_dir = pathlib.Path(os.path.dirname(__file__))
sys.path.append(str(this_script_dir / ".." / "host_test_utils"))

from sigrok_interface import I2CStart, I2CRepeatedStart, I2CWriteToAddr, I2CReadFromAddr, I2CDataByte, I2CAck, I2CNack, I2CStop, SigrokI2CRecorder, pretty_print_i2c_data, pretty_diff_i2c_data


class I2CBasicTestHostTest(BaseHostTest):

    """
    Host test for the I2C Basic Test testsuite.
    Handles logging data using the logic analyzer and verifying certain test results.
    """

    # Sequence definitions.
    # The embedded test has a large variety of test cases, but they should all produce one of a few fixed
    # patterns of wire data.
    SEQUENCES = {

        # Write to EEPROM address, then stop
        "correct_addr_only": [I2CStart(), I2CWriteToAddr(0xA0), I2CAck(), I2CStop()],

        # Write to incorrect EEPROM address, then stop
        "incorrect_addr_only_write": [I2CStart(), I2CWriteToAddr(0x20), I2CNack(), I2CStop()],

        # Read from incorrect EEPROM address, then stop
        "incorrect_addr_only_read": [I2CStart(), I2CReadFromAddr(0x21), I2CNack(), I2CStop()],

        # Write the byte 2 to address 0x1
        "write_2_to_0x1": [I2CStart(), I2CWriteToAddr(0xA0), I2CAck(), I2CDataByte(0x0), I2CAck(), I2CDataByte(0x1), I2CAck(), I2CDataByte(0x2), I2CAck(), I2CStop()],

        # Write the byte 3 to address 0x1
        "write_3_to_0x1": [I2CStart(), I2CWriteToAddr(0xA0), I2CAck(), I2CDataByte(0x0), I2CAck(), I2CDataByte(0x1), I2CAck(), I2CDataByte(0x3), I2CAck(), I2CStop()],

        # Read the byte 2 from address 0x1
        "read_2_from_0x1": [I2CStart(), I2CWriteToAddr(0xA0), I2CAck(), I2CDataByte(0x0), I2CAck(), I2CDataByte(0x1), I2CAck(),
                           I2CRepeatedStart(), I2CReadFromAddr(0xA1), I2CAck(), I2CDataByte(0x2), I2CNack(), I2CStop()],

        # Read the byte 3 from address 0x1
        "read_3_from_0x1": [I2CStart(), I2CWriteToAddr(0xA0), I2CAck(), I2CDataByte(0x0), I2CAck(), I2CDataByte(0x1), I2CAck(),
                            I2CRepeatedStart(), I2CReadFromAddr(0xA1), I2CAck(), I2CDataByte(0x3), I2CNack(), I2CStop()],
    }

    def __init__(self):
        super(I2CBasicTestHostTest, self).__init__()

        self.logger = HtrunLogger('TEST')
        self.recorder = SigrokI2CRecorder()

    def _callback_start_recording_i2c(self, key: str, value: str, timestamp):
        """
        Called at the start of every test case.  Should start a recording of I2C data.
        """

        self.recorder.record(0.05) # Everything we do in this test should complete in under 0.05s

        self.send_kv('start_recording_i2c', 'complete')

    def _callback_verify_sequence(self, key: str, value: str, timestamp):
        """
        Verify that the current recorded I2C data matches the given sequence
        """
        recorded_data = self.recorder.get_result()

        self.send_kv('verify_sequence', 'complete' if pretty_diff_i2c_data(self.logger, self.SEQUENCES[value], recorded_data) else 'failed')

    def setup(self):

        self.register_callback('start_recording_i2c', self._callback_start_recording_i2c)
        self.register_callback('verify_sequence', self._callback_verify_sequence)

        self.logger.prn_inf("I2C Basic Test host test setup complete.")

    def teardown(self):
        self.recorder.teardown()
