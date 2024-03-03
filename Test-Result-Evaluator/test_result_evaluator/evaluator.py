import pathlib

from junitparser import JUnitXml

from test_result_evaluator import mbed_test_database
from test_result_evaluator.result_page_generator import generate_tests_and_targets_website

# Create database
db_path = pathlib.Path("mbed_tests.db")
db_path.unlink(missing_ok=True)
database = mbed_test_database.MbedTestDatabase(db_path)
print(">> Creating Database...")
database.create_database()
print(">> Populating Targets and Features into Database...")
database.populate_targets_features(pathlib.Path("../CI-Shield-Tests/mbed-os"))


target_name = "ARDUINO_NANO33BLE"
test_report = JUnitXml.fromfile(f"demo-test-configs/mbed-tests-{target_name}.xml")

for suite in test_report:
    print(repr(suite))