from mbed_host_tests import BaseHostTest
from mbed_host_tests.host_tests_logger import HtrunLogger

import sys
import os
import pathlib
import subprocess

# Unfortunately there's no easy way to make the test runner add a directory to its module path...
this_script_dir = pathlib.Path(os.path.dirname(__file__))
sys.path.append(str(this_script_dir / ".."))

from host_test_utils.sigrok_interface import SigrokI2CRecorder, pretty_print_i2c_data


class I2CRecordOnlyTestHostTest(BaseHostTest):

    """
    Host test which just logs and prints I2C data during a specific period.
    """

    def __init__(self):
        super().__init__()

        self.logger = HtrunLogger('TEST')
        self.recorder = SigrokI2CRecorder()

    def _callback_start_recording_i2c(self, key: str, value: str, timestamp):
        """
        Called at the start of every test case.  Should start a recording of I2C data.
        """

        self.recorder.record(0.1, self.config.test_name, self.current_test_case_name) # Everything we do in this test should complete in under 0.1s

        self.send_kv('start_recording_i2c', 'complete')

    def _callback_record_i2c_data(self, key: str, value: str, timestamp):
        """
        Verify that the current recorded I2C data matches the given sequence
        """

        try:
            _ = self.recorder.get_result()
        except subprocess.TimeoutExpired:
            pass

        self.send_kv('record_i2c_data', 'complete')

    def setup(self):

        self.register_callback('start_recording_i2c', self._callback_start_recording_i2c)
        self.register_callback('record_i2c_data', self._callback_record_i2c_data)

        self.logger.prn_inf("I2C Record-Only Test host test setup complete.")

    def teardown(self):
        self.recorder.teardown()
