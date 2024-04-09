"""
Module for creating and accessing an SQLite database of Mbed test results
"""
import collections
import pathlib
import sqlite3
import enum
from typing import Set, List, Optional, Dict, Any, Tuple
import dataclasses

import graphviz

from mbed_tools.targets._internal.target_attributes import get_target_attributes

import json5
import cmsis_pack_manager


class TestResult(enum.IntEnum):
    """
    Enumeration of the possible results a test or test case can have
    """
    PASSED = 1  # Test ran and passed
    FAILED = 2  # Test ran and failed
    SKIPPED = 3  # Test was not run because it is not supported on this target


class DriverType(enum.Enum):
    """
    Enumeration of the possible types a driver can be
    """
    PERIPHERAL = "Peripheral"  # Peripheral present on this target and supported by Mbed CE
    FEATURE = "Feature"  # Larger Mbed OS optional feature supported for this target
    COMPONENT = "Component"  # External component present on this board


class MbedTestDatabase:

    def __init__(self, database_path: pathlib.Path):
        """Create an MbedTestDatabase, passing the path to the database file."""
        self._database = sqlite3.connect(str(database_path))

        # Enable accessing rows by their name in cursors
        # https://stackoverflow.com/a/20042292/7083698
        self._database.row_factory = sqlite3.Row

        # turn off transaction management (we do that ourselves)
        self._database.isolation_level = None

        # turn on foreign keys
        self._database.execute("PRAGMA foreign_keys = 1")

    def close(self):
        """
        Close the database file.
        """
        self._database.close()

    def create_database(self):
        """Create all the needed tables in an empty database"""

        self._database.execute("BEGIN")

        # -- Tests table
        # Holds details about tests for each target
        self._database.execute(
            "CREATE TABLE Tests("
            "testName TEXT, "  # Name of the test
            "targetName TEXT, "  # Name of the target it was ran for
            "executionTime REAL, "  # Time in seconds it took to run the test
            "result INTEGER,"  # TestResult of the test
            "output TEXT,"  # Output that the complete test printed (not divided into test cases)
            "FOREIGN KEY(targetName) REFERENCES Targets(name), "
            "UNIQUE(testName, targetName)"  # Combo of test name - target name must be unique
            ")"
        )

        # -- TestCases table
        # Holds the result of each test case for each target
        self._database.execute(
            "CREATE TABLE TestCases("
            "testName TEXT, "  # Name of the test
            "testCaseName TEXT, "  # Name of the test case
            "targetName TEXT, "  # Name of the target it was ran for
            "result INTEGER,"  # TestResult of the test
            "output TEXT,"  # Output that this test case printed specifically
            "FOREIGN KEY(targetName) REFERENCES Targets(name), "
            "UNIQUE(testName, testCaseName, targetName)"  # Combo of test name - test case name - target name must be unique
            ")"
        )

        # -- Drivers table
        # Lists target features
        self._database.execute(
            "CREATE TABLE Drivers("
            "name TEXT PRIMARY KEY, "  # Driver name, matching how it's named in code.  This is a string like
                                       # DEVICE_SERIAL, FEATURE_BLE, or COMPONENT_SPIF
            "friendlyName TEXT, "  # Human readable name, like FEATURE_BLE would have "Bluetooth Low Energy"
            "description TEXT, "  # Description, if available
            "type TEXT, "  # Type, value of DriverType
            "hidden INTEGER"  # 1 if the feature is an internal one and should not be shown in docs, 0 otherwise
            ")"
        )

        # -- Targets table
        # Lists targets and their attibutes
        self._database.execute(
            "CREATE TABLE Targets("
            "name TEXT PRIMARY KEY, "  # Name of the target
            "isPublic INTEGER, "  # 1 if the target is a public target (a board that can be built for).
                                  # 0 if not (the target is just used as a parent for other targets).
            "isMCUFamily INTEGER, " # 1 if this is the top-level target for one family of MCUs and boards.
                                    # (for example, STM32L4 is the family target for all STM32L4 MCUs and boards).
                                    # Family targets are used to group together documentation and test results.
                                    # 0 otherwise (target is above or below the family level).
            "cpuVendorName TEXT, "  # Name of the vendor which the CPU on this target comes from.  From the CMSIS
                                    # database.  NULL if this target does not have a valid link to the CMSIS database.
            "mcuFamilyTarget TEXT NULL, "  # Name of the MCU family target which is a parent of this target.
                                           # Only set iff a target is or has a parent which is an MCU family target.
                                           # If isMCUFamily = 1, this will contain the target's own name.
            "imageURL TEXT NULL  "  # URL of the image that will be shown in the target table for this board, if set 
                                    # in JSON.
            ")"
        )

        # -- TargetGraph table
        # Contains target parent/child relationships
        self._database.execute(
            "CREATE TABLE TargetGraph("
            "parentTarget TEXT REFERENCES Targets(name), "  # Name of the parent target
            "childTarget TEXT REFERENCES Targets(name), "  # Name of the child target
            "UNIQUE(parentTarget, childTarget)"
            ")"
        )

        # -- TargetDrivers table
        # Maps targets to the features they support
        self._database.execute(
            "CREATE TABLE TargetDrivers("
            "targetName TEXT REFERENCES Targets(name), "  # Name of the target
            "driver TEXT REFERENCES Drivers(name),"  # Name of driver or component.
            "UNIQUE(targetName, driver)"  # Combo of target name - driver name must be unique
            ")"
        )

        # -- TargetMemories table
        # Stores information about the memory banks available on a target
        self._database.execute(
            "CREATE TABLE TargetMemories("
            "targetName TEXT REFERENCES Targets(name), "  # Name of the target
            "bankName TEXT NOT NULL, "  # Name of memory.  Directly from the CMSIS json.
            "size INTEGER NOT NULL, "  # Size in bytes
            "isFlash INTEGER NOT NULL, "  # 1 if flash memory, 0 if RAM.  This comes from the "writeable" 
                                          # attribute from the CMSIS JSON.
            "UNIQUE(targetName, bankName)"  # Combo of target name - bank name must be unique
            ")"
        )

        # -- TestDrivers table
        # Maps test cases to the Mbed drivers / components they test
        self._database.execute(
            "CREATE TABLE DriverTests("
            "testName TEXT, "  # Name of the test
            "driverTested TEXT,"  # Name of driver or component that the test tests.
            "FOREIGN KEY(driverTested) REFERENCES Drivers(name)"
            ")"
        )

        # now commit the initial transaction
        self._database.commit()

    def populate_targets_and_drivers(self, mbed_os_path: pathlib.Path, cmsis_device_dict: Dict[str, Any]):
        """
        Populate Targets, Drivers, and related tables from a given Mbed OS path.
        CMSIS cache is used to get attributes like RAM sizes from CMSIS.
        """

        target_json5_file = mbed_os_path / "targets" / "targets.json5"
        targets_data = json5.loads(target_json5_file.read_text())

        drivers_json5_file = mbed_os_path / "targets" / "drivers.json5"
        drivers_data = json5.loads(drivers_json5_file.read_text())

        # First assemble a list of all the drivers.
        # For this we want to process the JSON directly rather than dealing with target inheritance, because
        # we just want a list of all the drivers used everywhere.
        component_names: Set[str] = set()
        feature_names: Set[str] = set()
        peripheral_names: Set[str] = set()

        for target_name, target_data in targets_data.items():

            # Note: The names are built matching the logic in mbed_tools/build/_internal/templates/mbed_config.tmpl
            # Also note that top level targets will define e.g. 'components' while child targets will define
            # e.g. 'components_add', so we have to check both attributes.
            if "device_has" in target_data:
                peripheral_names.update("DEVICE_" + entry for entry in target_data["device_has"])
            if "device_has_add" in target_data:
                peripheral_names.update("DEVICE_" + entry for entry in target_data["device_has_add"])
            if "features" in target_data:
                feature_names.update("FEATURE_" + entry for entry in target_data["features"])
            if "features_add" in target_data:
                feature_names.update("FEATURE_" + entry for entry in target_data["features_add"])
            if "components" in target_data:
                component_names.update("COMPONENT_" + entry for entry in target_data["components"])
            if "components_add" in target_data:
                component_names.update("COMPONENT_" + entry for entry in target_data["components_add"])

        # First add the targets
        # Note that we don't need to use get_target_attributes() here because none of the attributes we care
        # about are inherited (so far)
        for target_name, target_data in targets_data.items():

            is_public = 1 if target_data.get("public", True) else 0  # targets are public by default
            is_mcu_family = 1 if target_data.get("is_mcu_family_target", False) else 0
            image_url = target_data.get("image_url", None)

            self._database.execute(
                "INSERT INTO Targets(name, isPublic, isMCUFamily, imageURL) VALUES(?, ?, ?, ?)",
                (target_name,
                 is_public,
                 is_mcu_family,
                 image_url))

            # Also add the parents for each target
            for parent in target_data.get("inherits", []):
                self._database.execute(
                    "INSERT INTO TargetGraph(parentTarget, childTarget) VALUES(?, ?)",
                    (parent, target_name)
                )

        # Now add the drivers to the database
        self._database.execute("BEGIN")
        types_and_drivers = (
            (DriverType.PERIPHERAL, peripheral_names),
            (DriverType.FEATURE, feature_names),
            (DriverType.COMPONENT, component_names)
        )
        for type, driver_names in types_and_drivers:
            for driver_name in driver_names:

                # Look up driver friendly name, description, etc in json file
                if driver_name not in drivers_data[type.value]:
                    raise RuntimeError(f"drivers.json5 section '{type.value}' is missing information on {driver_name}!")

                if "friendly_name" not in drivers_data[type.value][driver_name]:
                    raise RuntimeError(f"drivers.json5 section {type.value}.{driver_name} is missing 'friendly_name'!")
                if "description" not in drivers_data[type.value][driver_name]:
                    raise RuntimeError(f"drivers.json5 section {type.value}.{driver_name} is missing 'description'!")

                hidden = 0
                if "hidden_from_docs" in drivers_data[type.value][driver_name]:
                    hidden = 1 if drivers_data[type.value][driver_name]["hidden_from_docs"] else 0

                self._database.execute("INSERT INTO Drivers(name, friendlyName, description, type, hidden) VALUES(?, ?, ?, ?, ?)",
                                       (driver_name,
                                        drivers_data[type.value][driver_name]["friendly_name"],
                                        drivers_data[type.value][driver_name]["description"],
                                        type.value,
                                        hidden))

        for target_name in targets_data.keys():
            # Next, add the drivers for each target
            target_attrs = get_target_attributes(targets_data, target_name, True)

            for feature_name in target_attrs["features"]:
                feature_full_name = "FEATURE_" + feature_name
                self._database.execute(
                    "INSERT INTO TargetDrivers(targetName, driver) VALUES(?, ?)",
                    (target_name, feature_full_name))

            for component_name in target_attrs["components"]:
                component_full_name = "COMPONENT_" + component_name
                self._database.execute(
                    "INSERT INTO TargetDrivers(targetName, driver) VALUES(?, ?)",
                    (target_name, component_full_name))

            # Note: device_has can contain duplicates, so we have to wrap it in set()
            for peripheral_name in set(target_attrs.get("device_has", [])):
                peripheral_full_name = "DEVICE_" + peripheral_name
                self._database.execute(
                    "INSERT INTO TargetDrivers(targetName, driver) VALUES(?, ?)",
                    (target_name, peripheral_full_name))

            # Also, while we have the target attributes handy, look up the target in the CMSIS
            # CPU database if possible.
            cmsis_device_name: Optional[str] = target_attrs.get("device_name", None)

            if cmsis_device_name is not None:
                if cmsis_device_name not in cmsis_device_dict:
                    raise RuntimeError(
                        f"Target {target_name} specifies CMSIS device name {cmsis_device_name} which "
                        f"does not exist in CMSIS pack index. Error in 'device_name' targets.json5 "
                        f"attribute?")
                cmsis_cpu_data = cmsis_device_dict[cmsis_device_name]
                cpu_vendor_name = cmsis_cpu_data["vendor"]

                # Set vendor name in the database.
                # In the JSON file the vendor name has a colon then a number after it.  I think this is
                # some sort of vendor ID but can't find actual docs.
                cpu_vendor_name = cpu_vendor_name.split(":")[0]
                self._database.execute("UPDATE Targets SET cpuVendorName = ? WHERE name == ?",
                                       (cpu_vendor_name, target_name))

                # Add target memories based on the CMSIS json data
                for bank_name, bank_data in cmsis_cpu_data["memories"].items():

                    # The MIMXRT series of devices list a "ROMCP" memory bank, but this
                    # actually represents the hardcoded ROM init code, not an actual flash bank on the device.
                    # Unfortunately, there is no way to know that it isn't programmable based on the JSON.
                    # So we have to exclude it from the output manually.
                    if "MIMXRT" in cmsis_device_name and bank_name == "ROMCP":
                        continue

                    self._database.execute(
                        "INSERT INTO TargetMemories(targetName, bankName, size, isFlash) VALUES(?, ?, ?, ?)",
                        (target_name,
                         bank_name,
                         bank_data["size"],
                         0 if bank_data["access"]["write"] else 1)
                    )

        # Match targets with their MCU family targets.
        for mcu_family_target in self.get_mcu_family_targets():
            mcu_family_targets = self.get_all_target_children(mcu_family_target)
            mcu_family_targets.add(mcu_family_target)
            for target in mcu_family_targets:
                self._database.execute("UPDATE Targets SET mcuFamilyTarget = ? WHERE name = ?", (mcu_family_target, target))

        self._database.commit()

    @dataclasses.dataclass(eq=True, frozen=True)  # note: eq and frozen must be enabled to make the dataclass hashable
    class TargetDriverInfo:
        """
        Convenience type for returning information about a given target driver
        """
        name: str
        friendly_name: str
        description: str
        type: DriverType

    def get_all_drivers(self, type: DriverType = None) -> Set[TargetDriverInfo]:
        """
        Get a all target drivers, optionally filtering by type
        """
        cursor = self._database.execute(f"""
SELECT
    name,
    friendlyName,
    description,
    type
FROM 
    Drivers
    INNER JOIN TargetDrivers ON Drivers.name = TargetDrivers.driver
WHERE
    Drivers.hidden == 0
    {'AND Drivers.type == ?' if type is not None else ''}
GROUP BY Drivers.name
ORDER BY friendlyName ASC""", (type.value,) if type is not None else ())

        result = set()
        for row in cursor:
            result.add(MbedTestDatabase.TargetDriverInfo(row["name"], row["friendlyName"], row["description"],
                                                         DriverType(row["type"])))
        cursor.close()

        return result

    def get_target_drivers(self, target_name: str) -> Set[TargetDriverInfo]:
        """
        Get a set of the drivers of all types that the given target has
        """
        cursor = self._database.execute("""
SELECT
    name,
    friendlyName,
    description,
    type
FROM 
    Drivers
    INNER JOIN TargetDrivers ON Drivers.name = TargetDrivers.driver
WHERE
    Drivers.hidden == 0
    AND TargetDrivers.targetName == ?
""", (target_name, ))

        result = set()
        for row in cursor:
            result.add(MbedTestDatabase.TargetDriverInfo(row["name"], row["friendlyName"], row["description"], DriverType(row["type"])))
        cursor.close()

        return result

    def get_mcu_family_targets(self) -> List[str]:
        """
        Get all the targets that are MCU family targets and should have
        webpages generated for them
        """
        mcu_family_targets = []

        cursor = self._database.execute("""
SELECT name
FROM Targets
WHERE isMCUFamily == 1
ORDER BY cpuVendorName ASC, name ASC
""")

        for row in cursor:
            mcu_family_targets.append(row["name"])

        cursor.close()

        return mcu_family_targets

    def _get_target_parents(self, target_name: str) -> List[str]:
        """
        Get the parent(s) of a target
        """
        parents = []

        cursor = self._database.execute("""
SELECT parentTarget
FROM TargetGraph
WHERE childTarget == ?
ORDER BY parentTarget ASC
""", (target_name, ))

        for row in cursor:
            parents.append(row["parentTarget"])

        cursor.close()

        return parents

    def _get_target_children(self, target_name: str) -> List[str]:
        """
        Get the children of a target
        """
        parents = []

        cursor = self._database.execute("""
SELECT childTarget
FROM TargetGraph
WHERE parentTarget == ?
ORDER BY childTarget ASC
""", (target_name,))

        for row in cursor:
            parents.append(row["childTarget"])

        cursor.close()

        return parents

    def get_inheritance_graph(self, target_name: str) -> graphviz.Digraph:
        """
        Get the inheritance graph for a target, showing its parents and children.
        """

        # Create the graph with a nice blue color for all nodes.
        # The "rankdir=BT" attr puts the parent node at the top.
        inheritance_graph = graphviz.Digraph(comment=f"Inheritance graph for {target_name}",
                                             node_attr={"fillcolor": "lightblue", "style": "filled"},
                                             graph_attr={"rankdir": "BT"})

        # Give the MCU family target some special styling
        inheritance_graph.node(target_name, label=f"<<B>{target_name}</B>>", _attributes={"shape": "box"})

        # Note: There is probably some 400 IQ way to do this in one SQLite query, but I
        # think we can leave that to figure out later.  For now we just do BFS upwards and downwards.
        nodes_to_explore = collections.deque()
        nodes_to_explore.append(target_name)

        # BFS parent targets
        while len(nodes_to_explore) > 0:
            curr_target = nodes_to_explore.pop()
            parents = self._get_target_parents(curr_target)

            for parent_target in parents:
                inheritance_graph.node(parent_target)
                inheritance_graph.edge(curr_target, parent_target)
                nodes_to_explore.append(parent_target)

        # BFS child targets
        nodes_to_explore = collections.deque()
        nodes_to_explore.append(target_name)

        while len(nodes_to_explore) > 0:
            curr_target = nodes_to_explore.pop()
            children = self._get_target_children(curr_target)

            for child_target in children:
                inheritance_graph.node(child_target)
                inheritance_graph.edge(child_target, curr_target)
                nodes_to_explore.append(child_target)

        return inheritance_graph

    def get_all_target_children(self, target_name: str) -> Set[str]:
        """
        Get all targets which inherit from the given target anywhere in the inheritance hierarchy
        """

        all_children: Set[str] = set()

        # BFS child targets
        nodes_to_explore = collections.deque()
        nodes_to_explore.append(target_name)

        while len(nodes_to_explore) > 0:
            curr_target = nodes_to_explore.pop()

            this_target_children = self._get_target_children(curr_target)
            all_children.update(this_target_children)

            for child_target in this_target_children:
                nodes_to_explore.append(child_target)

        return all_children

    def get_all_boards_in_mcu_family(self, mcu_family_name: str) -> sqlite3.Cursor:
        """
        Get all boards (public targets) which are in an MCU family.
        Returns a cursor containing the name, image, and the CPU vendor name
        """
        return self._database.execute("SELECT name, imageURL, cpuVendorName "
                                      "FROM Targets "
                                      "WHERE "
                                          "isPublic == 1 AND "
                                          "mcuFamilyTarget == ?",
                                      (mcu_family_name, ))

    def get_target_memories(self, target_name: str) -> sqlite3.Cursor:
        """
        Get all memory banks for a target.
        Returns a cursor containing the name and the CPU vendor name
        """

        return self._database.execute("SELECT bankName, size, isFlash "
                                      "FROM TargetMemories "
                                      "WHERE "
                                          "targetName == ?"
                                      "ORDER BY bankName ASC",
                                      (target_name, ))

    def get_targets_with_driver_by_family(self, driver_name: str) -> sqlite3.Cursor:
        """
        Get all the targets that have a given driver, grouped by MCU family
        Returns a cursor containing the MCU family and grouped targets
        """

        # Note: In the query below, we use max(cpuVendorName) to select any non-null value for
        # cpuVendorName.

        return self._database.execute("""
        SELECT
            group_concat(TargetDrivers.targetName, ',') AS targetNames,
            Targets.mcuFamilyTarget AS mcuFamilyTarget,
            max(Targets.cpuVendorName) AS cpuVendorName
        FROM 
            TargetDrivers
            INNER JOIN Targets ON TargetDrivers.targetName = Targets.name
        WHERE
            TargetDrivers.driver == ?
            AND Targets.isPublic == 1
        GROUP BY Targets.mcuFamilyTarget
        ORDER BY 
            max(Targets.cpuVendorName) ASC, 
            Targets.mcuFamilyTarget ASC
            """,
                                      (driver_name,))

    def add_test_record(self, test_name: str, target_name: str, execution_time: float, result: TestResult, output: str):
        """
        Add a record of a test to the Tests table.
        Replaces the record if it already exists
        """
        self._database.execute("INSERT OR REPLACE INTO Tests(testName, targetName, executionTime, result, output) "
                               "VALUES(?, ?, ?, ?, ?)",
                               (test_name, target_name, execution_time, result.value, output))