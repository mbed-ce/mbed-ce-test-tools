from mbed_host_tests import BaseHostTest
from mbed_host_tests.host_tests_logger import HtrunLogger

import binascii
import sys
import os
import pathlib

# Unfortunately there's no easy way to make the test runner add a directory to its module path...
this_script_dir = pathlib.Path(os.path.dirname(__file__))
sys.path.append(str(this_script_dir / ".." / "host_test_utils"))

from sigrok_interface import SPITransaction, SigrokSPIRecorder

class SpiBasicTestHostTest(BaseHostTest):

    """
    Host test for the SPI Basic Test testsuite.
    Handles logging data using the Bus Pirate and verifying certain test results.
    """

    SEQUENCES = {

        # Standard word data sent from the basic tests
        "standard_word": SPITransaction(
            mosi_bytes=[0x1, 0x2, 0x4, 0x8],
            miso_bytes=[0x1, 0x2, 0x4, 0x8]
        )
    }

    def __init__(self):
        super(SpiBasicTestHostTest, self).__init__()

        self.logger = HtrunLogger('TEST')
        self.recorder = SigrokSPIRecorder()

    def _callback_start_recording_spi(self, key: str, value: str, timestamp):
        """
        Called at the start of every test case.  Should start a recording of SPI data.
        """

        self.recorder.record(None, .05) # .05 seconds should be enough for every test in this suite

        self.send_kv('start_recording_spi', 'complete')

    def _callback_verify_sequence(self, key: str, value: str, timestamp):
        """
        Verify that the current recorded SPI data matches the given sequence
        """

        spi_transaction = self.recorder.get_result()[0]
        self.logger.prn_inf("Saw on the SPI bus: " + str(spi_transaction))

        if self.SEQUENCES[value] == spi_transaction:
            self.logger.prn_inf("PASS")
            self.send_kv('verify_sequence', 'complete')
        else:
            self.logger.prn_inf("We expected: " + str(self.SEQUENCES[value]))
            self.logger.prn_inf("FAIL")
            self.send_kv('verify_sequence', 'failed')

    def _callback_verify_queue_and_abort_test(self, key: str, value: str, timestamp):
        """
        Verify that the current recorded SPI data matches the queueing and abort test
        """

        spi_transaction = self.recorder.get_result()[0]
        self.logger.prn_inf("Saw on the SPI bus: " + str(spi_transaction))

        data_valid = False

        # We should see a block starting with \x01\x02, then less than 30 0 bytes.
        # Then, we should see another \x01\x02 and then 30 0 bytes
        messages = spi_transaction.mosi_bytes.split(b"\x01")

        if len(messages) == 3: # Note: 0 byte message will be seen at the start due to how split() works
            print("Correct number of messages")
            if messages[1][0] == 0x2 and len(messages[1]) < 31:
                print("First message looks OK")
                if messages[2][0] == 0x2 and len(messages[2]) == 31:
                    print("Second message looks OK")
                    data_valid = True

        if data_valid:
            self.send_kv('verify_queue_and_abort_test', 'pass')
        else:
            self.logger.prn_err("Incorrect MOSI data for queue and abort test")
            self.send_kv('verify_queue_and_abort_test', 'fail')

    def _callback_print_spi_data(self, key: str, value: str, timestamp):
        """
        Called at the end of test cases which do not do verification and just want to print the recorded data.
        """

        self.logger.prn_inf("Saw on the SPI bus: " + str(self.recorder.get_result()[0]))

        self.send_kv('print_spi_data', 'complete')

    def setup(self):

        self.register_callback('start_recording_spi', self._callback_start_recording_spi)
        self.register_callback('verify_sequence', self._callback_verify_sequence)
        self.register_callback('verify_queue_and_abort_test', self._callback_verify_queue_and_abort_test)
        self.register_callback('print_spi_data', self._callback_print_spi_data)

        self.logger.prn_inf("SPI Basic Test host test setup complete.")

    def teardown(self):
        self.recorder.teardown()