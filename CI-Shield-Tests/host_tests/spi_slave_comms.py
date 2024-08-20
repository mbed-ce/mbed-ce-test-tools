from cy_serial_bridge import CySPIMode

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

from host_test_utils.sigrok_interface import SigrokSPIRecorder, SPITransaction, pretty_diff_spi_data
from host_test_utils.usb_serial_numbers import CY7C65211_SERIAL_NUMBER


class SPISlaveCommsTest(BaseHostTest):

    """
    Host test which sends and receives data from the Mbed MCU (which is in I2C slave mode) using the
    CY7C65211.
    """

    def __init__(self):
        super(SPISlaveCommsTest, self).__init__()

        self.logger = HtrunLogger('TEST')

        self.recorder = SigrokSPIRecorder()
        self.spi_mode = 0

        self.exit_stack: Optional[contextlib.ExitStack] = None
    
        # Open serial bridge chip
        self.cy_usb_context = cy_serial_bridge.CyScbContext()

    def _callback_set_spi_mode(self, key: str, value: str, timestamp):
        """
        Set the SPI mode that the SPI bridge will use.
        Value is a string of the integer mode.
        """

        cy_spi_mode_mapping = [
            CySPIMode.MOTOROLA_MODE_0,
            CySPIMode.MOTOROLA_MODE_1,
            CySPIMode.MOTOROLA_MODE_2,
            CySPIMode.MOTOROLA_MODE_3
        ]

        self.spi_mode = int(value)

        # Update serial bridge configuration
        curr_config = self.spi_bridge.read_spi_configuration()
        curr_config.mode = cy_spi_mode_mapping[self.spi_mode]
        self.spi_bridge.set_spi_configuration(curr_config)

    def _callback_set_sclk_freq(self, key: str, value: str, timestamp):
        """
        Set the SCLK frequency that the SPI bridge will use.
        Value is a string of the integer frequency.
        """

        curr_config = self.spi_bridge.read_spi_configuration()
        curr_config.frequency = int(value)
        self.spi_bridge.set_spi_configuration(curr_config)

    def _callback_start_recording_spi(self, key: str, value: str, timestamp):
        """
        Called at the start of every test case.  Should start a recording of SPI data.
        """

        # Everything we do in this test should complete in under 0.1s
        self.recorder.record(cs_pin="D0", record_time=0.1, spi_mode=self.spi_mode)

        self.send_kv('start_recording_spi', 'complete')

    def _callback_do_transaction(self, key: str, value: str, timestamp):
        """
        Command to the host test to write bytes over SPI.
        Arguments are data bytes separated by spaces, then 'expected_response', then the expected response data bytes
        e.g. '0x00 0x01 0x02 0x03 expected_response 0xFF 0xFF 0xFF 0xFE'
        """

        # Process arguments
        tx_bytes_string, rx_bytes_string = value.split("expected_response")
        bytes_to_write = bytes([int(data_byte_str, 0) for data_byte_str in tx_bytes_string.strip().split(" ")])
        expected_response = bytes([int(data_byte_str, 0) for data_byte_str in rx_bytes_string.strip().split(" ")])

        # Write data to slave device
        success = True
        try:
            result = self.spi_bridge.spi_transfer(bytes_to_write)
        except Exception:
            self.logger.prn_err("Error writing to SPI slave: " + traceback.format_exc())
            success = False
            result = None

        # Check logic analyzer data
        recorded_transactions = self.recorder.get_result()
        expected_transactions = [SPITransaction(mosi_bytes=bytes_to_write, miso_bytes=expected_response)]
        success = pretty_diff_spi_data(self.logger, expected_transactions, recorded_transactions)

        # Check returned data
        self.logger.prn_inf("Serial bridge sent %s and got back %s" % (binascii.b2a_hex(bytes_to_write).decode("ASCII"), binascii.b2a_hex(result).decode("ASCII")))
        if result != expected_response:
            self.logger.prn_err("Incorrect response read back on master.  Expected %s" % (binascii.b2a_hex(expected_response).decode("ASCII"),))
            success = False

        self.send_kv('do_transaction', 'complete' if success else 'error')

    def _initialize_spi_bridge(self):
        """
        Initialize the spi bridge driver.
        """
        self.spi_bridge: cy_serial_bridge.CySPIControllerBridge = self.cy_usb_context.open_device(
                                cy_serial_bridge.DEFAULT_VID, 
                                cy_serial_bridge.DEFAULT_PID, 
                                cy_serial_bridge.OpenMode.SPI_CONTROLLER,
                                CY7C65211_SERIAL_NUMBER)
        
        # Enter serial bridge
        with contextlib.ExitStack() as temp_exit_stack: # Creates a temporary ExitStack
            temp_exit_stack.enter_context(self.spi_bridge) # Enter the serial bridge using the temporary stack

            self.spi_bridge.set_spi_configuration(cy_serial_bridge.driver.CySPIConfig(frequency=500000))

            self.exit_stack = temp_exit_stack.pop_all() # Creates a new exit stack with ownership of spi_bridge "moved" into it

    def _destroy_spi_bridge(self):
        """
        Destroy the spi bridge driver
        """

        # Exit serial bridge
        if self.exit_stack is not None:
            self.exit_stack.close() # This exits each object saved in the stack
        self.exit_stack = None

    def setup(self):
        self._initialize_spi_bridge()

        self.register_callback('start_recording_spi', self._callback_start_recording_spi)
        self.register_callback('set_spi_mode', self._callback_set_spi_mode)
        self.register_callback('set_sclk_freq', self._callback_set_sclk_freq)
        self.register_callback('do_transaction', self._callback_do_transaction)

        self.logger.prn_inf("SPI Slave Comms host test setup complete.")

    def teardown(self):
        self.recorder.teardown()
        self._destroy_spi_bridge()

        # Briefly reopen the bridge as I2C so that it does not drive any of the bus lines
        with self.cy_usb_context.open_device(
                cy_serial_bridge.DEFAULT_VID,
                cy_serial_bridge.DEFAULT_PID,
                cy_serial_bridge.OpenMode.I2C_CONTROLLER,
                CY7C65211_SERIAL_NUMBER) as i2c_bridge:
            pass
