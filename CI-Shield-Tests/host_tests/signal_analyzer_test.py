from mbed_host_tests import BaseHostTest
from mbed_host_tests.host_tests_logger import HtrunLogger

import pathlib
import sys
import os

# Unfortunately there's no easy way to make the test runner add a directory to its module path...
this_script_dir = pathlib.Path(os.path.dirname(__file__))
sys.path.append(str(this_script_dir / ".." / "host_test_utils"))

from sigrok_interface import SigrokSignalAnalyzer

class SignalAnalyzerHostTest(BaseHostTest):

    """
    Host test which analyzes the frequency and duty cycle of a signal using the logic analyzer.
    """

    def __init__(self):
        super(SignalAnalyzerHostTest, self).__init__()

        self.logger = HtrunLogger('TEST')
        self.analyzer = SigrokSignalAnalyzer()

    def _callback_analyze_signal(self, key: str, value: str, timestamp):
        """
        Called to make an analysis of the signal on the PWM pin (logic analyzer pin 6) and
        return the frequency and duty cycle.
        """

        frequency, duty_cycle = self.analyzer.measure_signal(6)

        self.send_kv('frequency', str(frequency))
        self.send_kv('duty_cycle', str(duty_cycle))

    def setup(self):

        self.register_callback('analyze_signal', self._callback_analyze_signal)

        self.logger.prn_inf("Signal Analyzer Test host test setup complete.")

    def teardown(self):
        self.analyzer.teardown()