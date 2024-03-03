"""
Module for creating and accessing an SQLite database of Mbed test results
"""
import collections
import pathlib
import sqlite3
import enum
from typing import Set, List

import graphviz

from mbed_tools.targets._internal.target_attributes import get_target_attributes

import json5


class TestResult(enum.IntEnum):
    """
    Enumeration of the possible results a test or test case can have
    """
    PASSED = 1  # Test ran and passed
    FAILED = 2  # Test ran and failed
    SKIPPED = 3  # Test was not run because it is not supported on this target


class FeatureType(enum.Enum):
    """
    Enumeration of the possible types a feature can be
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

        # -- Features table
        # Lists target features
        self._database.execute(
            "CREATE TABLE Features("
            "name TEXT PRIMARY KEY, "  # Feature name, matching how it's named in code.  This is a string like
                                       # DEVICE_SERIAL, FEATURE_BLE, or COMPONENT_SPIF
            "friendlyName TEXT, "  # Human readable name, like FEATURE_BLE would have "Bluetooth Low Energy"
            "description TEXT, "  # Description, if available
            "type TEXT, "  # Type, value of FeatureType
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
            "isMCUFamily INTEGER"  # 1 if this is the top-level target for one family of MCUs and boards.
                                   # (for example, STM32L4 is the family target for all STM32L4 MCUs and boards).
                                   # Family targets are used to group together documentation and test results.
                                   # 0 otherwise (target is above or below the family level).
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

        # -- TargetFeatures table
        # Maps targets to the features they support
        self._database.execute(
            "CREATE TABLE TargetFeatures("
            "targetName TEXT REFERENCES Targets(name), "  # Name of the target
            "feature TEXT REFERENCES Features(name),"  # Name of feature or component.
            "UNIQUE(targetName, feature)"  # Combo of target name - feature name must be unique
            ")"
        )

        # -- TestFeatures table
        # Maps test cases to the Mbed features / components they test
        self._database.execute(
            "CREATE TABLE TestFeatures("
            "testName TEXT, "  # Name of the test
            "featureTested TEXT,"  # Name of feature or component that the test tests.
            "FOREIGN KEY(featureTested) REFERENCES Features(name)"
            ")"
        )

        # now commit the initial transaction
        self._database.commit()

    def populate_targets_features(self, mbed_os_path: pathlib.Path):
        """
        Populate the Features and TargetFeatures tables from a given Mbed OS path
        """

        target_json5_file = mbed_os_path / "targets" / "targets.json5"
        targets_data = json5.loads(target_json5_file.read_text())

        features_json5_file = mbed_os_path / "targets" / "features.json5"
        features_data = json5.loads(features_json5_file.read_text())

        # First assemble a list of all the features.
        # For this we want to process the JSON directly rather than dealing with target inheritance, because
        # we just want a list of all the features used everywhere.
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

            self._database.execute(
                "INSERT INTO Targets(name, isPublic, isMCUFamily) VALUES(?, ?, ?)",
                (target_name,
                 is_public,
                 is_mcu_family))

            # Also add the parents for each target
            for parent in target_data.get("inherits", []):
                self._database.execute(
                    "INSERT INTO TargetGraph(parentTarget, childTarget) VALUES(?, ?)",
                    (parent, target_name)
                )

        # Now add the features to the database
        self._database.execute("BEGIN")
        types_and_features = (
            (FeatureType.PERIPHERAL, peripheral_names),
            (FeatureType.FEATURE, feature_names),
            (FeatureType.COMPONENT, component_names)
        )
        for type, feature_names in types_and_features:
            for feature_name in feature_names:

                # Look up feature friendly name, description, etc in json file
                if feature_name not in features_data[type.value]:
                    raise RuntimeError(f"features.json5 section '{type.value}' is missing information on {feature_name}!")

                if "friendly_name" not in features_data[type.value][feature_name]:
                    raise RuntimeError(f"features.json5 section {type.value}.{feature_name} is missing 'friendly_name'!")
                if "description" not in features_data[type.value][feature_name]:
                    raise RuntimeError(f"features.json5 section {type.value}.{feature_name} is missing 'description'!")

                hidden = 0
                if "hidden_from_docs" in features_data[type.value][feature_name]:
                    hidden = 1 if features_data[type.value][feature_name]["hidden_from_docs"] else 0

                self._database.execute("INSERT INTO Features(name, friendlyName, description, type, hidden) VALUES(?, ?, ?, ?, ?)",
                                       (feature_name,
                                        features_data[type.value][feature_name]["friendly_name"],
                                        features_data[type.value][feature_name]["description"],
                                        type.value,
                                        hidden))

        # Next, add the features for each target
        for target_name in targets_data.keys():
            target_attrs = get_target_attributes(targets_data, target_name, True)

            for feature_name in target_attrs["features"]:
                feature_full_name = "FEATURE_" + feature_name
                self._database.execute(
                    "INSERT INTO TargetFeatures(targetName, feature) VALUES(?, ?)",
                    (target_name, feature_full_name))

            for component_name in target_attrs["components"]:
                component_full_name = "COMPONENT_" + component_name
                self._database.execute(
                    "INSERT INTO TargetFeatures(targetName, feature) VALUES(?, ?)",
                    (target_name, component_full_name))

            # Note: device_has can contain duplicates, so we have to wrap it in set()
            for peripheral_name in set(target_attrs.get("device_has", [])):
                peripheral_full_name = "DEVICE_" + peripheral_name
                self._database.execute(
                    "INSERT INTO TargetFeatures(targetName, feature) VALUES(?, ?)",
                    (target_name, peripheral_full_name))

        self._database.commit()

    def get_all_features(self, type: FeatureType) -> sqlite3.Cursor:
        """
        Get a cursor containing all database features of the given type
        """
        # TODO Enable returning testsThatTestFeature once that table is populated
        return self._database.execute("""
SELECT
    name,
    friendlyName,
    description,
    group_concat(targetName) AS targetsWithFeature
--    group_concat(testName) AS testsThatTestFeature
FROM 
    Features
    INNER JOIN TargetFeatures ON Features.name = TargetFeatures.feature
--    INNER JOIN TestFeatures ON Features.name = TestFeatures.featureTested
WHERE
    Features.hidden == 0
    AND Features.type == ?
GROUP BY Features.name
ORDER BY friendlyName ASC""", (type.value,))

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
ORDER BY name ASC
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

        inheritance_graph = graphviz.Digraph(comment=f"Inheritance graph for {target_name}")

        inheritance_graph.node(target_name)

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
