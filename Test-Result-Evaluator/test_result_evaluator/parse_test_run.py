"""
Script to parse the XML resulting from an Mbed test run and add the results to a given Mbed test database.
"""
import pathlib
import sys

import junitparser.junitparser
from junitparser import JUnitXml

from test_result_evaluator import mbed_test_database
from test_result_evaluator.mbed_test_database import TestResult

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
    if test_report.is_passed:
        test_suite_result = TestResult.PASSED
    elif test_report.is_skipped:
        test_suite_result = TestResult.SKIPPED
    else:
        test_suite_result = TestResult.FAILED

    print(f"Parsing results of {test_report.classname} ({test_suite_result.name})...")

    database.add_test_record(test_report.classname, mbed_target, test_report.time, test_suite_result, test_report.system_out)

print(">> Done.")
database.close()

