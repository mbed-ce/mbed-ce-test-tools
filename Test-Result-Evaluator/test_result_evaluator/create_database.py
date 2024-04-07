"""
Script to create a database for storing Mbed testing information and initially populate it with data.
If the output file path already exists it will be deleted and recreated.
"""

import pathlib
import datetime
import sys

from junitparser import JUnitXml
import cmsis_pack_manager
import json5

from test_result_evaluator import mbed_test_database

if len(sys.argv) != 3:
    print(f"Usage: {sys.argv[0]} <path to Mbed OS> <path to database to initialize>")
    sys.exit(1)

# Set up cmsis-pack-manager cache.  This downloads the list of latest devices if needed,
# then loads it and merges with some extra devices.
# Note that data is cached by default in the folder returned by appdirs.user_data_dir("cmsis-pack-manager")
cmsis_cache = cmsis_pack_manager.Cache(False, False)

# Compile the index if it does not exist or is more than 30 days out of date.
index_file_path = pathlib.Path(cmsis_cache.index_path)
cutoff_time = datetime.datetime.utcnow() - datetime.timedelta(days=30)
if not index_file_path.exists() or datetime.datetime.fromtimestamp(index_file_path.stat().st_mtime) < cutoff_time:
    print(">> Downloading CMSIS device descriptions, this may take some time...")
    cmsis_cache.cache_clean()
    cmsis_cache.cache_descriptors()

cmsis_extra_devices_path = pathlib.Path("cmsis_pack_extra_devices.json5")
cmsis_extra_devices = json5.load(open(cmsis_extra_devices_path, "r"))

cmsis_all_devices = dict(cmsis_cache.index)
cmsis_all_devices.update(cmsis_extra_devices)

# Create database
db_path = pathlib.Path(sys.argv[2])
db_path.unlink(missing_ok=True)
database = mbed_test_database.MbedTestDatabase(db_path)
print(">> Creating Database...")
database.create_database()
print(">> Populating Target and Driver Info into Database...")
database.populate_targets_and_drivers(pathlib.Path(sys.argv[1]), cmsis_all_devices)

database.close()
print("Done.")
