import pathlib
import datetime

from junitparser import JUnitXml
import cmsis_pack_manager
import json5

from test_result_evaluator import mbed_test_database
from test_result_evaluator.result_page_generator import generate_tests_and_targets_website

# Set up cmsis-pack-manager cache.  This downloads the list of latest devices if needed,
# then loads it and merges with some extra devices.
cmsis_pack_json_cache = pathlib.Path.home() / ".cache" / "mbed_cmsis_pack_index_cache"
cmsis_pack_json_cache.mkdir(exist_ok=True)
cmsis_cache = cmsis_pack_manager.Cache(False, False, json_path=cmsis_pack_json_cache)

# Compile the index if it does not exist or is more than 30 days out of date.
index_file_path = pathlib.Path(cmsis_cache.index_path)
cutoff_time = datetime.datetime.utcnow() - datetime.timedelta(days=30)
if not index_file_path.exists() or datetime.datetime.fromtimestamp(index_file_path.stat().st_mtime) < cutoff_time:
    print(">> Downloading CMSIS device descriptions, this may take some time...")
    cmsis_cache.cache_descriptors()

cmsis_extra_devices_path = pathlib.Path("cmsis_pack_extra_devices.json5")
cmsis_extra_devices = json5.load(open(cmsis_extra_devices_path, "r"))

cmsis_all_devices = dict(cmsis_cache.index)
cmsis_all_devices.update(cmsis_extra_devices)

# Create database
db_path = pathlib.Path("mbed_tests.db")
db_path.unlink(missing_ok=True)
database = mbed_test_database.MbedTestDatabase(db_path)
print(">> Creating Database...")
database.create_database()
print(">> Populating Targets and Features into Database...")
database.populate_targets_features(pathlib.Path("../CI-Shield-Tests/mbed-os"), cmsis_all_devices)


target_name = "ARDUINO_NANO33BLE"
test_report = JUnitXml.fromfile(f"demo-test-configs/mbed-tests-{target_name}.xml")

for suite in test_report:
    print(repr(suite))