"""
Script to parse the XML resulting from an Mbed test run and add the results to a given Mbed test database.
"""
import pathlib
import sys
import re
from typing import Tuple, List

import junitparser.junitparser
from junitparser import JUnitXml

from test_result_evaluator import mbed_test_database
from test_result_evaluator.mbed_test_database import TestResult

# Regexes for parsing Greentea output
# ------------------------------------------------------------------------

# Matches one line of the test case list sent at the start of each test
GREENTEA_TESTCASE_NAME_RE = re.compile(r"\{\{__testcase_name;([^}]+)}}")

# Matches a test case which completes (either successfully or not) and allows extracting the output
GREENTEA_TESTCASE_OUTPUT_RE = re.compile(r"(\{\{__testcase_start;[^|]+?}}.+?\{\{__testcase_finish;[^|]+?;(\d);\d}})", re.DOTALL)

if len(sys.argv) != 4:
    print(f"Usage: {sys.argv[0]} <path to database to use> <Mbed target> <path to JUnit XML to parse>")
    sys.exit(1)

db_path = pathlib.Path(sys.argv[1])
mbed_target = sys.argv[2]
junit_xml = sys.argv[3]

database = mbed_test_database.MbedTestDatabase(db_path)

junit_report = JUnitXml.fromfile(junit_xml)

test_report: junitparser.junitparser.TestCase
for test_report in junit_report:

    # First record info about the larger test.  We can do this entirely using the data recorded by CTest.
    if test_report.is_passed:
        test_suite_result = TestResult.PASSED
    elif test_report.is_skipped:
        test_suite_result = TestResult.SKIPPED
    else:
        test_suite_result = TestResult.FAILED

    print(f"Parsing results of {test_report.classname} ({test_suite_result.name})...")
    database.add_test_record(test_report.classname, mbed_target, test_report.time, test_suite_result,
                             test_report.system_out)

    if test_suite_result != TestResult.SKIPPED:
        # Now things get a bit more complicated as we have to parse Greentea's output directly to determine
        # the list of tests.
        # First use a regex to extract the list of test cases...
        test_case_names = re.findall(GREENTEA_TESTCASE_NAME_RE, test_report.system_out)

        if len(test_case_names) > 0:
            # This is a "normal" test with test cases.  Parse them.
            # Regex returns tuple of (output, passed/failed indicator)
            test_case_records: List[Tuple[str, str]] = re.findall(GREENTEA_TESTCASE_OUTPUT_RE, test_report.system_out)

            if len(test_case_records) < len(test_case_names):
                # Did one test case crash the test?
                # See if we can find the start of this test case but no end.
                crashing_test_name = test_case_names[len(test_case_records)]
                crash_re = re.compile(r"\{\{__testcase_start;" + crashing_test_name + r"}}(.+?)teardown\(\) finished", re.DOTALL)
                test_case_crash_output = re.search(crash_re, test_report.system_out)

                if test_case_crash_output is None:
                    raise RuntimeError(f"Parse error: found {len(test_case_names)} test cases but only found {len(test_case_records)} records of test cases executing")
                else:

                    print(f"Note: Test case '{crashing_test_name}' in test {test_report.classname} appears to have crashed and prevented {len(test_case_names) - len(test_case_records) - 1} subsequent tests from running")
                test_case_records.append((test_case_crash_output.group(0), "0"))

        # However, there are some tests (e.g. test-mbed-drivers-dev-null) which don't use the greentea
        # system in a standard way and therefore can't be divided evenly into test cases.  These tests need special
        # handling.

print(">> Done.")
database.close()

