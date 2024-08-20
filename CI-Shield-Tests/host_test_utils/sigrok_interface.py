## Module to interface with the test shield's internal
## Sigrok-based logic analyzer.
## Handles running the Sigrok command and parsing the results.
import abc
import binascii
# Note: This module file cannot be in the host_tests directory, because the test runner iterates through
# and imports all the modules in that directory.  So, if it's in there, it gets imported twice, and really
# bad stuff happens.

import re
import os
import shlex
import subprocess
import sys
import time
from typing import List, cast, Optional, Tuple
from dataclasses import dataclass

import usb1

from . import usb_serial_numbers

from mbed_host_tests.host_tests_logger import HtrunLogger

LOGIC_ANALYZER_FREQUENCY = 8 # MHz

#if sys.platform == "win32":
    # Sigrok must be run through WSL on Windows, see
    # https://github.com/mbed-ce/mbed-ce-ci-shield-v2?tab=readme-ov-file#side-note-sigrok-windows-issues
#    SIGROK_COMMAND = ["wsl", "sigrok-cli"]
#else:
SIGROK_COMMAND = ["sigrok-cli"]


# How long to wait, in seconds, after starting a sigrok recording before we can start the test.
# It would sure be nice if sigrok had some sort of "I'm ready to capture data" printout...
SIGROK_START_DELAY = 1.0 # s


class SigrokRecorderBase(abc.ABC):
    """
    Base class for sigrok recorder classes.
    Handles starting and stopping the sigrok process.
    """

    def __init__(self):
        self._sigrok_process: Optional[subprocess.Popen] = None
        self.logger = HtrunLogger('SigrokRecorderBase')

    def _search_for_fx2lafw_usb_device(self, serno: str) -> Tuple[int, int]:
        """
        Search for an FX2LAFW USB device with the given serial number.
        Returns tuple of (bus, address) that can be passed to Sigrok.
        """

        # See example here of how to iterate through USB devices:
        # https://github.com/vpelletier/python-libusb1/blob/master/examples/scan_device_tree.py
        with usb1.USBContext() as context:
            for dev in context.getDeviceIterator(skip_on_error=True):
                dev: usb1.USBDevice
                if dev.getVendorID() == 0x1d50 and dev.getProductID() == 0x608c: # FX2LAFW VID and PID
                    try:
                        # The script that builds fx2lafw firmware images sets the serial number to this format
                        # (see here: https://github.com/mbed-ce/mbed-ce-ci-shield-v2/blob/master/Firmware/fx2lafw_update_serial_number.py#L29)
                        if dev.getSerialNumber() == serno:
                            return (dev.getBusNumber(), dev.getDeviceAddress())
                    except usb1.USBError:
                        self.logger.prn_wrn(f"Found USB device matching FX2LAFW logic analyzer VID & PID, but cannot open it.  Check udev rules (on Linux) or WinUSB driver (on Windows)")
            
            raise RuntimeError("Could not find logic analyzer USB device for shield with serial number " + usb_serial_numbers.CI_SHIELD_SERNO)


    def _start_sigrok(self, sigrok_args: List[str], record_time: float):
        """
        Starts recording data using the given Sigrok command.
        :param record_time: Time to run sigrok for in seconds.  If the command includes a trigger clause,
            this is the time after the trigger occurs.
        """

        if usb_serial_numbers.FX2LAFW_SERIAL_NUMBER is None:
            driver_conn_string = ""
        else:
            # Connect to specific bus and address where this serial number of logic analyzer lives
            # https://manpages.ubuntu.com/manpages/jammy/man1/sigrok-cli.1.html#:~:text=To%20select%20a%20specific%20USB,%2Dut61e%3Aconn%3D1a86.
            fx2lafw_bus, fx2lafw_address = self._search_for_fx2lafw_usb_device(usb_serial_numbers.FX2LAFW_SERIAL_NUMBER)
            driver_conn_string = f":conn={fx2lafw_bus}.{fx2lafw_address}"

        # Run sigrok for the specified amount of milliseconds
        command = [*SIGROK_COMMAND,
                    "--driver", "fx2lafw" + driver_conn_string,  # Set driver to FX2LAFW

                    # For decoding messages right at the trigger, we need to change the "capture ratio"
                    # option so that just a few samples are kept from before the trigger.
                    # Details here: https://sigrok.org/bugzilla/show_bug.cgi?id=1657
                    # The hard part was figuring out how to change the capture ratio from the CLI as 
                    # there is zero documentation.
                    # It appears that it's a percentage from 0 to 100.
                    "--config", f"samplerate={LOGIC_ANALYZER_FREQUENCY} MHz:captureratio=5",

                    "--time", str(round(record_time * 1000)),

                    *sigrok_args]
        
        # print("Executing: " + " ".join(command))
        self._sigrok_process = subprocess.Popen(command, text=True, stdout = subprocess.PIPE)
        time.sleep(SIGROK_START_DELAY)

    def _get_sigrok_output(self) -> List[str]:
        """
        Get the output from sigrok as a list of text lines.
        It would sure be nice if Sigrok CLI had some way to output decoded data as a machine readable
        file format, but
        "Data processed by decoders can't be saved into output file by argument, only by redirection of STDOUT."
        (per https://sigrok.org/wiki/Input_output_formats)
        Sadness.
        """

        try:
            # Timeout is a guess for how long the sigrok process will take to record data and exit.
            # If the trigger condition is not reached, this timeout will trigger.
            output, errs = self._sigrok_process.communicate(timeout=5)
        except subprocess.TimeoutExpired:

            # Recommended by the subprocess docs to kill manually if the timeout has expired
            self._sigrok_process.kill()
            raise

        if self._sigrok_process.returncode != 0:
            raise RuntimeError("Sigrok failed!")

        return output.split("\n")

    def teardown(self):
        """
        Call from test case teardown function.  Ensures that sigrok is stopped
        e.g. in the event of a device hang.
        """
        if self._sigrok_process is not None:
            if self._sigrok_process.poll() is None:
                self._sigrok_process.terminate()


class I2CBusData:
    """
    Empty base class for all I2C .
    Subclasses must define __eq__ and __str__.
    """


class I2CStart(I2CBusData):
    """
    Represents an I2C start condition
    """

    def __str__(self):
        return "Start"

    def __eq__(self, other):
        return isinstance(other, I2CStart)


class I2CRepeatedStart(I2CBusData):
    """
    Represents an I2C repeated start condition
    """

    def __str__(self):
        return "RepeatedStart"

    def __eq__(self, other):
        return isinstance(other, I2CRepeatedStart)


class I2CWriteToAddr(I2CBusData):
    """
    Represents an I2C write to the given (8-bit) address
    """

    def __init__(self, address: int):
        self._address = address

    def __str__(self):
        return f"Wr[0x{self._address:02x}]"

    def __eq__(self, other):
        if isinstance(other, I2CWriteToAddr):
            return other._address == self._address
        return False


class I2CReadFromAddr(I2CBusData):
    """
    Represents an I2C read from the given (8-bit) address
    """

    def __init__(self, address: int):
        self._address = address

    def __str__(self):
        return f"Rd[0x{self._address:02x}]"

    def __eq__(self, other):
        if isinstance(other, I2CReadFromAddr):
            return other._address == self._address
        return False


class I2CDataByte(I2CBusData):
    """
    Represents an I2C data byte on the bus.
    """

    def __init__(self, data: int):
        self._data = data

    def __str__(self):
        return f"0x{self._data:02x}"

    def __eq__(self, other):
        if isinstance(other, I2CDataByte):
            return other._data == self._data
        return False


class I2CAck(I2CBusData):
    """
    Represents an acknowledge on the I2C bus
    """

    def __str__(self):
        return "Ack"

    def __eq__(self, other):
        return isinstance(other, I2CAck)

class I2CNack(I2CBusData):
    """
    Represents a not-acknowledge on the I2C bus
    """

    def __str__(self):
        return "Nack"

    def __eq__(self, other):
        return isinstance(other, I2CNack)


class I2CStop(I2CBusData):
    """
    Represents a stop event on the I2C bus
    """

    def __str__(self):
        return "Stop"

    def __eq__(self, other):
        return isinstance(other, I2CStop)


# Regexes for parsing the sigrok I2C output
SR_I2C_REPEATED_START = re.compile(r'i2c-1: Start repeat')
SR_I2C_START = re.compile(r'i2c-1: Start')
SR_I2C_WRITE_TO_ADDR = re.compile(r'i2c-1: Address write: (..)')
SR_I2C_READ_FROM_ADDR = re.compile(r'i2c-1: Address read: (..)')
SR_I2C_DATA_BYTE = re.compile(r'i2c-1: Data [^ ]+: (..)') # matches "Data read" or "Data write"
SR_I2C_ACK = re.compile(r'i2c-1: ACK')
SR_I2C_NACK = re.compile(r'i2c-1: NACK')
SR_I2C_STOP = re.compile(r'i2c-1: Stop')


def pretty_print_i2c_data(data: List[I2CBusData]) -> str:
    """
    Pretty-print a list of I2C data to a string, printing a newline after the end of each transaction.
    """
    result = ""
    strings_this_transaction = []
    for bus_data in data:
        strings_this_transaction.append(str(bus_data))
        if isinstance(bus_data, I2CStop):
            result += " ".join(strings_this_transaction) + "\n"
            strings_this_transaction = []

    # Grab anything after the last stop
    if len(strings_this_transaction):
        result += " ".join(strings_this_transaction) + "\n"
    
    return result

def pretty_diff_i2c_data(logger: HtrunLogger, expected: List[I2CBusData], actual: List[I2CDataByte]) -> bool:
    """
    Diff expected I2C data against actual.  Always prints the actual data to the console, and prints the expected
    too if they don't match.
    """

    if len(actual) > 0:
        logger.prn_inf("Saw on the I2C bus:\n" + pretty_print_i2c_data(actual))
    else:
        logger.prn_inf("Saw nothing the I2C bus.")

    match = True
    if len(expected) != len(actual):
        logger.prn_err("Expected length differs from actual!")
        match = False
    else:
        for index, (expected_item, actual_item) in enumerate(zip(expected, actual)):
            if expected_item != actual_item:
                logger.prn_err(f"Data item at index {index}: expected {str(expected_item)} but got {str(actual_item)}")
                match = False

    if not match:
        logger.prn_inf("We expected:\n" + pretty_print_i2c_data(expected))

    return match

class SigrokI2CRecorder(SigrokRecorderBase):

    # i2c sigrok command
    SIGROK_I2C_COMMAND = ["--protocol-decoders",
                          "i2c:scl=D1:sda=D2:address_format=unshifted",  # Set up I2C decoder
                          "--protocol-decoder-annotations",
                          "i2c=address-read:address-write:data-read:data-write:start:repeat-start:ack:nack:stop",  # Request output of all detectable conditions

                          # Trigger on falling edge of SCL
                          "--triggers",
                          "D1=f",
                          ]
    def __init__(self):
        super().__init__()
        self.logger = HtrunLogger('SigrokI2CRecorder')

    def record(self, record_time: float):
        """
        Starts recording I2C data from the logic analyzer.
        :param record_time: Time after the first clock edge to record data for
        """
        self._start_sigrok(self.SIGROK_I2C_COMMAND, record_time)

    def get_result(self) -> List[I2CBusData]:
        """
        Get the data that was recorded.
        
        :return: Data recorded (list of I2CBusData subclasses).  If nothing was seen before the timeout (logic analyzer never triggered), returns [].
        """

        try:
            sigrok_output = self._get_sigrok_output()
        except subprocess.TimeoutExpired:
            return []

        i2c_transaction: List[I2CBusData] = []

        # Parse output
        for line in self._sigrok_process.communicate()[0].split("\n"):
            # Note: Must check repeated start first because repeated start is a substring of start,
            # so the start regex will match it as well.
            if SR_I2C_REPEATED_START.match(line):
                i2c_transaction.append(I2CRepeatedStart())
            elif SR_I2C_START.match(line):
                i2c_transaction.append(I2CStart())
            elif SR_I2C_WRITE_TO_ADDR.match(line):
                write_address = int(SR_I2C_WRITE_TO_ADDR.match(line).group(1), 16)
                i2c_transaction.append(I2CWriteToAddr(write_address))
            elif SR_I2C_READ_FROM_ADDR.match(line):
                read_address = int(SR_I2C_READ_FROM_ADDR.match(line).group(1), 16)
                i2c_transaction.append(I2CReadFromAddr(read_address))
            elif SR_I2C_DATA_BYTE.match(line):
                data = int(SR_I2C_DATA_BYTE.match(line).group(1), 16)
                i2c_transaction.append(I2CDataByte(data))
            elif SR_I2C_ACK.match(line):
                i2c_transaction.append(I2CAck())
            elif SR_I2C_NACK.match(line):
                i2c_transaction.append(I2CNack())
            elif SR_I2C_STOP.match(line):
                i2c_transaction.append(I2CStop())
            elif line == "i2c-1: Read" or line == "i2c-1: Write" or len(line) == 0:
                # we can ignore these ones
                pass
            else:
                self.logger.prn_wrn(f"Unparsed Sigrok output: '{line}'")

        return i2c_transaction

    def teardown(self):
        """
        Call from test case teardown function.  Ensures that sigrok is stopped
        e.g. in the event of a device hang.
        """
        if self._sigrok_process is not None:
            if self._sigrok_process.poll() is None:
                self._sigrok_process.terminate()


@dataclass
class SPITransaction():
    # Bytes seen on MOSI
    mosi_bytes: bytes

    # Bytes seen on MISO.  Note: Always has the same length as mosi_bytes
    miso_bytes: bytes

    def __init__(self, mosi_bytes, miso_bytes):
        """
        Construct an SPITransaction.  Accepts values for the mosi and miso bytes that are convertible
        to bytes (e.g. bytes, bytearray, or an iterable of integers).
        """
        self.mosi_bytes = bytes(mosi_bytes)
        self.miso_bytes = bytes(miso_bytes)
        if len(mosi_bytes) != len(miso_bytes):
            raise ValueError("MOSI and MISO bytes are not the same length!")

    def __str__(self):
        return f"[mosi: {binascii.b2a_hex(self.mosi_bytes)}, miso: {binascii.b2a_hex(self.miso_bytes)}]"

    def __cmp__(self, other):
        if not isinstance(other, SPITransaction):
            return False

        return other.mosi_bytes == self.mosi_bytes and other.miso_bytes == self.miso_bytes

# Regex for one SPI data byte
SR_SPI_DATA_BYTE = re.compile(r'spi-1: ([0-9A-F][0-9A-F])')

# Regex for multiple data bytes in one transaction
SR_SPI_DATA_BYTES = re.compile(r'spi-1: ([0-9A-F ]+)')


class SigrokSPIRecorder(SigrokRecorderBase):

    def __init__(self):
        super().__init__()
        self.logger = HtrunLogger('SigrokSPIRecorder')

    def record(self, cs_pin: Optional[str], record_time: float, spi_mode:int = 0):
        """
        Starts recording SPI data from the logic analyzer.
        :param cs_pin: Logic analyzer pin to use for chip select.  e.g. "D3" or "D4".  May be set to None
           to not use the CS line and record all traffic.
        :param record_time: Time after the first clock edge to record data for
        :param spi_mode: SPI mode from 0-3
        """

        self._has_cs_pin = cs_pin is not None

        # spi sigrok command
        cpol = spi_mode // 2
        cpha = spi_mode % 2
        sigrok_spi_command = [
              # Set up SPI decoder.
              # Note that for now we always use a word size of 8, but that can be changed later.
              "--protocol-decoders",
              f"spi:clk=D3:mosi=D2:miso=D1{':cs=' + cs_pin if self._has_cs_pin else ''}:cpol={cpol}:cpha={cpha}:wordsize=8",
              ]

        if self._has_cs_pin:
            # Trigger on falling edge of CS
            sigrok_spi_command.append("--triggers")
            sigrok_spi_command.append(f"{cs_pin}=f")

            # Output complete transactions
            sigrok_spi_command.append("--protocol-decoder-annotations")
            sigrok_spi_command.append("spi=mosi-transfer:miso-transfer")
        else:
            # Trigger on any edge of clock
            sigrok_spi_command.append("--triggers")
            sigrok_spi_command.append("D3=e")

            # The decoder has no transaction information without CS.
            # So, we have to just get the raw bytes
            sigrok_spi_command.append("--protocol-decoder-annotations")
            sigrok_spi_command.append("spi=mosi-data:miso-data")

        self._start_sigrok(sigrok_spi_command, record_time)

    def get_result(self) -> List[SPITransaction]:
        """
        Get the SPI data recorded by the logic analyzer.
        :return: List of SPI transactions observed.  Note that if CS was not provided, every byte will
            be considered as part of a single transaction.
        """

        sigrok_output = self._get_sigrok_output()

        if self._has_cs_pin:

            # If we have a CS pin then we will have multiple transactions to handle
            spi_data : List[SPITransaction] = []

            # Bytes from the previous line if this is an even line
            previous_line_data: Optional[List[int]] = None

            for line in sigrok_output:

                # Skip empty lines
                if line == "":
                    continue

                match_info = SR_SPI_DATA_BYTES.match(line)
                if not match_info:
                    self.logger.prn_wrn(f"Unparsed Sigrok output: '{line}'")
                    continue

                # Parse list of hex bytes
                byte_strings = match_info.group(1).split(" ")
                byte_values = [int(byte_string, 16) for byte_string in byte_strings]

                if previous_line_data is None:
                    previous_line_data = byte_values
                else:
                    # It appears that sigrok always alternates MISO, then MOSI lines in its CLI output.
                    # This is not documented anywhere, so I had to test it on hardware.
                    spi_data.append(SPITransaction(mosi_bytes=byte_values, miso_bytes=previous_line_data))
                    previous_line_data = None

            return spi_data

        else:

            # When we don't have a CS pin, we will get a bunch of lines with one data byte per line.
            mosi_bytes = []
            miso_bytes = []

            # It appears that sigrok always alternates MISO, then MOSI lines in its CLI output.
            # This is not documented anywhere, so I had to test it on hardware.
            next_line_is_miso = True

            for line in sigrok_output:

                # Skip empty lines
                if line == "":
                    continue

                match_info = SR_SPI_DATA_BYTES.match(line)
                if not match_info:
                    self.logger.prn_wrn(f"Unparsed Sigrok output: '{line}'")
                    continue

                byte_value = int(match_info.group(1), 16)

                if next_line_is_miso:
                    miso_bytes.append(byte_value)
                    next_line_is_miso = False
                else:
                    mosi_bytes.append(byte_value)
                    next_line_is_miso = True

            return [SPITransaction(miso_bytes=miso_bytes, mosi_bytes=mosi_bytes)]

    def teardown(self):
        """
        Call from test case teardown function.  Ensures that sigrok is stopped
        e.g. in the event of a device hang.
        """
        if self._sigrok_process is not None:
            if self._sigrok_process.poll() is None:
                self._sigrok_process.terminate()


def pretty_diff_spi_data(logger: HtrunLogger, expected: List[SPITransaction], actual: List[SPITransaction]) -> bool:
    """
    Diff expected SPI data against actual.  Always prints the actual data to the console, and prints the expected
    too if they don't match.
    """
    if len(actual) > 0:
        logger.prn_inf("Saw on the SPI bus:\n" + "\n".join(str(transaction) for transaction in actual))
    else:
        logger.prn_inf("Saw nothing the SPI bus.")

    if len(actual) != len(expected):
        logger.prn_err("Expected %d transactions but saw %d" % (len(expected), len(actual)))
        return False

    match = True
    for transaction_idx, (actual_txn, expected_txn) in enumerate(zip(actual, expected)):
        if len(actual_txn.mosi_bytes) != len(expected_txn.mosi_bytes):
            logger.prn_err("Transaction %d: expected length %d but saw %d" % (transaction_idx, len(expected_txn.mosi_bytes), len(actual_txn.mosi_bytes)))
            match = False
            continue

        for byte_idx in range(0, len(actual_txn.mosi_bytes)):
            if actual_txn.mosi_bytes[byte_idx] != expected_txn.mosi_bytes[byte_idx]:
                logger.prn_err("Transaction %d MOSI byte %d: expected 0x%x but saw 0x%x" % (transaction_idx, byte_idx, expected_txn.mosi_bytes[byte_idx], actual_txn.mosi_bytes[byte_idx]))
                match = False
            if actual_txn.miso_bytes[byte_idx] != expected_txn.miso_bytes[byte_idx]:
                logger.prn_err("Transaction %d miso byte %d: expected 0x%x but saw 0x%x" % (transaction_idx, byte_idx, expected_txn.miso_bytes[byte_idx], actual_txn.miso_bytes[byte_idx]))
                match = False

    if not match:
        logger.prn_inf("We expected:\n" + "\n".join(str(transaction) for transaction in expected))

    return match


class SigrokSignalAnalyzer(SigrokRecorderBase):
    """
    Class which analyzes a digital signal's frequency and duty cycle using Sigrok.
    Frequency is approximated by measuring the number of rising edges within the sampling period,
    which should have pretty high accuracy even with a relatively slow logic analyzer.
    Duty cycle is determined by measuring the ratio of samples where the signal is high vs low.

    Note that the freq of the signal being measured has to be <= half the logic analyzer sampling frequency,
    or we will go above the nyquist limit and accurate results can't be obtained.
    """

    def __init__(self):
        super().__init__()

    # Recording for 100ms should allow accurate frequency estimates
    RECORD_TIME = 0.1 # s

    def measure_signal(self, pin_num: int) -> Tuple[float, float]:
        """
        Measures a signal.  The signal should have already been started by the embedded
        test case before calling this function, and must remain stable until it returns.
        :param pin_num: Pin number from 0-7 on the logic analyzer that the signal exists on
        :returns: Tuple of [frequency in Hz, duty cycle in percent]
        """

        # Start recording raw samples (not using any decoders for this)
        sigrok_args = [
            "--channels", f"D{pin_num}", "--output-format", "csv"
        ]
        self._start_sigrok(sigrok_args, self.RECORD_TIME)

        # Get the output as soon as it finishes (no trigger clause so it should run quickly)
        sigrok_output = self._get_sigrok_output()

        # For CSV format, the actual data starts on line index 5, and contains one sample per line.
        sigrok_output = sigrok_output[5:]
        channel_samples = [line == "1" for line in sigrok_output]

        num_high_samples = 0
        num_rising_edges = 0

        for sample_idx in range(0, len(channel_samples)):
            # Check rising edge?
            if sample_idx > 1:
                if (not channel_samples[sample_idx - 1]) and channel_samples[sample_idx]:
                    num_rising_edges += 1

            # update duty cycle measurement
            if channel_samples[sample_idx]:
                num_high_samples += 1

        # Compute duty cycle
        duty_cycle = num_high_samples / len(channel_samples)

        # Compute frequency
        frequency = num_rising_edges / self.RECORD_TIME

        return (frequency, duty_cycle)
