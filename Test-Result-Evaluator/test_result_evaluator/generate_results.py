import pathlib

from test_result_evaluator import mbed_test_database
from test_result_evaluator.result_page_generator import generate_tests_and_targets_website

# Create database
db_path = pathlib.Path("mbed_tests.db")
database = mbed_test_database.MbedTestDatabase(db_path)

print(">> Generating Website...")
html_gen_dir = pathlib.Path("generated-site")
generate_tests_and_targets_website(database, html_gen_dir)