import collections
import pathlib
from typing import TextIO, List, Dict, Set
import html

import prettytable

from .mbed_test_database import MbedTestDatabase, DriverType, TestResult


def write_global_stylesheet(gen_path: pathlib.Path):
    """
    Write out the global stylesheet to a location
    """
    gen_path.write_text("""
body {
    margin-left: 30px;
    margin-right: 30px;
    margin-top: 30px;
    overflow: scroll;
}
div.passed-marker {
    width: 100%;
    height: 100%;
    background-color: lightgreen
}
div.skipped-marker {
    width: 100%;
    height: 100%;
    background-color: lightgray
}
div.failed-marker {
    width: 100%;
    height: 100%;
    background-color: lightpink
}
""")

def write_html_header(output_file: TextIO, page_title: str):
    """
    Write the common HTML header to a file.  Includes Semantic CSS and applies the given title.
    """
    output_file.write(f"""
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <title>{page_title}</title>
    <link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/semantic-ui@2.4.2/dist/semantic.min.css">
    <link rel="stylesheet" href="../mbed-results-site.css">
    <script
      src="https://code.jquery.com/jquery-3.1.1.min.js"
      integrity="sha256-hVVnYaiADRTO2PzUGmuLJr8BLUSjGIZsDYGmIJLv2b8="
      crossorigin="anonymous"></script>
    <script src="https://code.jsdelivr.net/npm/semantic-ui@2.4.2/dist/semantic.min.js"></script>
</head>
<body>
    <h1>{page_title}</h1>""")


def generate_drivers_index_page(database: MbedTestDatabase, out_path: pathlib.Path):
    """
    Generate the driver index page.  This page contains tables with summaries of the features and components.
    :param database:
    :param out_path:
    :return:
    """

    with open(out_path, "w") as index_page:
        write_html_header(index_page, "Mbed Drivers Index")

        for driver_type in DriverType:
            index_page.write(f"<h2>Drivers of type {driver_type.value}</h2>\n")

            if driver_type == DriverType.PERIPHERAL:
                index_page.write("<p>This lists peripheral drivers (drivers for internal parts of the MCU) that exist"
                                 " in Mbed CE.  Check these pages carefully: If a MCU's datasheet shows that it "
                                 " has a peripheral, but "
                                 " Mbed does not list a driver for that peripheral, then the peripheral will not"
                                 " be accessible via Mbed APIs on that MCU.</p>\n")
            elif driver_type == DriverType.COMPONENT:
                index_page.write("<p>This lists drivers which support external components (or in some cases "
                                 "co-processors) which are present on a target board so that they are supported"
                                 " out of the box.</p>\n")
            elif driver_type == DriverType.FEATURE:
                index_page.write("<p>Larger Mbed OS optional features supported for certain targets.</p>\n")

            driver_table = prettytable.PrettyTable()
            driver_table.field_names = ["Name", "Mbed Internal Name", "Description"]

            drivers_of_type = database.get_all_drivers(driver_type)
            for driver in drivers_of_type:
                driver_table.add_row([
                    f'<a href="{driver.name}.html">{driver.friendly_name}</a>',
                    f'<code>{driver.name}</code>',
                    driver.description
                ])

            index_page.write(html.unescape(driver_table.get_html_string(attributes={"class": "ui celled table"})))

        index_page.write("\n</body>")


def generate_driver_page(database: MbedTestDatabase, driver_info: MbedTestDatabase.TargetDriverInfo, out_path: pathlib.Path):
    """
    Generate a webpage for a feature.  This includes information about the feature like which
    targets have it.
    """

    with open(out_path, "w") as target_page:
        write_html_header(target_page, f"Mbed CE Driver Info: {driver_info.friendly_name}")
        target_page.write('<p><a href="index.html">Back to Drivers Index ^</a></p>\n')
        target_page.write(f"""
<p><strong>Mbed Internal Name:</strong> <code>{driver_info.name}</code></p>
<p><strong>Description:</strong> {driver_info.description}</p>
""")

        target_page.write("<h2>Target Families With this Feature</h2>")
        target_table = prettytable.PrettyTable()
        target_table.field_names = ["Target Family", "MCU Vendor", "Board Targets With Feature"]

        target_families_cursor = database.get_targets_with_driver_by_family(driver_info.name)
        for row in target_families_cursor:

            mcu_family_target = row["mcuFamilyTarget"]
            target_family_link = f"<a href=\"../targets/{mcu_family_target}.html\">{mcu_family_target}</a>"
            all_targets_in_family = row["targetNames"].split(",")
            cpu_vendor_name = row["cpuVendorName"]

            # Special handling for boards with no MCU target family (avoid linking to None.html)
            if mcu_family_target is None:
                target_family_link = "&lt;None&gt;"
                cpu_vendor_name = ""

            target_table.add_row([target_family_link,
                                  cpu_vendor_name,
                                  ", ".join(sorted(all_targets_in_family))])

        # Write the table to the page.
        # Note: html.unescape() prevents HTML in the cells from being escaped in the page (which prettytable
        # seems to do)
        target_page.write(html.unescape(target_table.get_html_string(attributes={"class": "ui celled table"})))

        target_page.write("\n</body>")


def generate_targets_index_page(database: MbedTestDatabase, mcu_family_targets: List[str], out_path: pathlib.Path):
    """
    Generates the index page for the targets folder.
    This includes a link to each MCU target family and other information about it.
    """
    with open(out_path, "w") as targets_index:
        write_html_header(targets_index, "Mbed CE MCU Target Families")
        targets_index.write("<p>Each row on this page represents one microcontroller family that Mbed supports."
                            " Microcontroller family, here, refers to a group of Mbed targets that use chips from the"
                            " same family and use the same low level drivers, with the differences only being memory "
                            " sizes, specific chip part numbers, and the board that the microcontroller is mounted on"
                            ".</p>\n")

        target_table = prettytable.PrettyTable()
        target_table.field_names = ["Target Family", "MCU Vendor", "Board Targets", "Features", "Peripherals"]
        for mcu_family_target in mcu_family_targets:
            target_link = f"<a href=\"{mcu_family_target}.html\">{mcu_family_target}</a>"

            mcu_vendor_name = ""
            boards_cursor = database.get_all_boards_in_mcu_family(mcu_family_target)
            boards_list: List[str] = []
            for row in boards_cursor:
                boards_list.append(row["name"])

                # Not every board may have the CPU vendor set (since the JSON doesn't require the 'device_name'
                # property to be set so the board may not get linked to CMSIS) so only store this field if it exists.
                if row["cpuVendorName"] is not None:
                    mcu_vendor_name = row["cpuVendorName"]
            boards_cursor.close()

            target_features = database.get_target_drivers(mcu_family_target)
            features_list = []
            peripherals_list = []
            for feature in target_features:
                feature_string = f'<a href="../drivers/{feature.name}.html">{feature.friendly_name}</a>'

                # Features and peripherals each go in separate table entries.  We ignore COMPONENTs for now
                # because those are a property of the board not the MCU.
                if feature.type == DriverType.FEATURE:
                    features_list.append(feature_string)
                elif feature.type == DriverType.PERIPHERAL:
                    peripherals_list.append(feature_string)

            target_table.add_row([target_link,
                                  mcu_vendor_name,
                                  ", ".join(sorted(boards_list)),
                                  ", ".join(sorted(features_list)),
                                  ", ".join(sorted(peripherals_list))])

        # Write the table to the page.
        # Note: html.unescape() prevents HTML in the cells from being escaped in the page (which prettytable
        # seems to do)
        targets_index.write(html.unescape(target_table.get_html_string(attributes={"class": "ui celled table"})))

        targets_index.write("\n</body>")


def generate_target_family_page(database: MbedTestDatabase, mcu_family_target: str, out_path: pathlib.Path):
    """
    Generate a webpage for a target family.  This includes info like the target's features and the targets in the family.
    """

    with open(out_path, "w") as target_page:
        write_html_header(target_page, mcu_family_target + " Target Family Info")
        target_page.write('<p><a href="index.html">Back to Target Families Index ^</a></p>')

        # Lookup features for each target.  Builds a dict from a target name to its features,
        # and a set containing the intersections of all targets' features
        targets_cursor = database.get_all_boards_in_mcu_family(mcu_family_target)
        targets_in_family_info = [(row["name"], row["imageURL"]) for row in targets_cursor]
        targets_cursor.close()

        target_features: Dict[str, Dict[DriverType, Set[MbedTestDatabase.TargetDriverInfo]]] = dict()
        for target_name, _ in targets_in_family_info:
            this_target_features = database.get_target_drivers(target_name)
            this_target_features_by_type = collections.defaultdict(set)
            for feature in this_target_features:
                this_target_features_by_type[feature.type].add(feature)
            target_features[target_name] = this_target_features_by_type

        common_features_by_type: Dict[DriverType, Set[MbedTestDatabase.TargetDriverInfo]] = dict()
        for feature_type in DriverType:
            common_features_by_type[feature_type] = set.intersection(*[feature_dict[feature_type] for feature_dict in target_features.values()])

        # Generate features list (with the features and peripherals)
        target_page.write("<h2>Features Supported</h2>\n<ul>")
        for feature in common_features_by_type[DriverType.FEATURE]:
            target_page.write(f'<li><a href="../drivers/{feature.name}.html">{feature.friendly_name}</a>: {feature.description}</li>\n')
        target_page.write("</ul>\n<h2>Peripheral Drivers Supported</h2>\n<ul>")
        for peripheral in common_features_by_type[DriverType.PERIPHERAL]:
            target_page.write(f'<li><a href="../drivers/{peripheral.name}.html">{peripheral.friendly_name}</a>: {peripheral.description}</li>\n')
        target_page.write("</ul>\n")

        # Generate board table
        target_page.write("<h2>Boards in this Target Family</h2>")
        target_table = prettytable.PrettyTable()
        target_table.field_names = ["Board", "Extra Features", "Extra Peripheral Drivers", "Components", "RAM Banks", "Flash Banks"]
        for target_name, image_url in targets_in_family_info:

            # Format board image
            if image_url is None:
                board_image_html = ""
            else:
                board_image_html = f'<img src="{image_url}" alt="{target_name} Image" width="200px" style="display: block;">'

            # Figure out any extra features that this target contains which the MCU family doesn't
            target_unique_features = target_features[target_name][DriverType.FEATURE] - common_features_by_type[DriverType.FEATURE]
            target_unique_peripherals = target_features[target_name][DriverType.PERIPHERAL] - common_features_by_type[DriverType.PERIPHERAL]

            # Make lists for each of the features
            target_unique_feature_strings = [f'<a href="../drivers/{feature.name}.html">{feature.friendly_name}</a>'
                                             for feature in target_unique_features]
            target_unique_periph_strings = [f'<a href="../drivers/{feature.name}.html">{feature.friendly_name}</a>'
                                             for feature in target_unique_peripherals]
            target_component_strings = [f'<a href="../drivers/{feature.name}.html">{feature.friendly_name}</a>'
                                        for feature in target_features[target_name][DriverType.COMPONENT]]

            # Make lists for each of the memory banks
            ram_bank_string = "<ul>"
            rom_bank_string = "<ul>"

            memory_bank_cursor = database.get_target_memories(target_name)
            for row in memory_bank_cursor:
                bank_str = f"<li>{row['bankName']}: {row['size'] // 1024} kiB</li>"
                if row["isFlash"] == 1:
                    rom_bank_string += bank_str
                else:
                    ram_bank_string += bank_str

            ram_bank_string += "</ul>"
            rom_bank_string += "</ul>"

            target_table.add_row([
                target_name + board_image_html,
                ", ".join(target_unique_feature_strings),
                ", ".join(target_unique_periph_strings),
                ", ".join(target_component_strings),
                ram_bank_string,
                rom_bank_string
            ])
            memory_bank_cursor.close()

        # Write the table to the page.
        # Note: html.unescape() prevents HTML in the cells from being escaped in the page (which prettytable
        # seems to do)
        target_page.write(html.unescape(target_table.get_html_string(attributes={"class": "ui celled table"})))

        # Generate inheritance graph
        target_page.write("<h2>Inheritance Graph</h2>")

        inheritance_graph = database.get_inheritance_graph(mcu_family_target)
        inheritance_graph_basename = out_path.parent / "assets" / f"{mcu_family_target}.dot"
        inheritance_graph_svg_file = pathlib.Path(inheritance_graph.render(inheritance_graph_basename, format="svg"))
        target_page.write(f'<img src="assets/{inheritance_graph_svg_file.name}" alt="Inheritance Graph"/>\n')

        target_page.write("\n</body>")


def generate_tests_index_page(database: MbedTestDatabase, out_path: pathlib.Path):

    """
    Generate the page with the index of all the tests and their status on each target
    """

    with open(out_path, "w") as targets_index:
        write_html_header(targets_index, "Mbed CE MCU Target Families")

        test_table = prettytable.PrettyTable()

        # Figure out list of targets that we have test data for
        targets_with_test_data = []
        target_table_header_text = ["Test Name"]
        targets_cursor = database.get_targets_with_tests()
        for row in targets_cursor:
            targets_with_test_data.append(row["targetName"])
            target_table_header_text.append(f'<div style="writing-mode: vertical-lr;"><a href="../targets/{row["mcuFamilyTarget"]}.html">{row["targetName"]}</a></div>')
        targets_cursor.close()

        # Now fill in test results
        for test_name, target_test_results in database.get_test_results().items():
            row_content = [test_name]

            for target in targets_with_test_data:
                if target in target_test_results:
                    if target_test_results[target] == TestResult.PASSED:
                        row_content.append('<div class="passed-marker">Passed</div>')
                    elif target_test_results[target] == TestResult.FAILED:
                        row_content.append('<div class="failed-marker">Failed</div>')
                    else: # skipped
                        row_content.append('<div class="skipped-marker">Skipped</div>')
                else:
                    row_content.append("") # Test not supported for this target
            test_table.add_row(row_content)

        # Write the table to the page.
        # Note: html.unescape() prevents HTML in the cells from being escaped in the page (which prettytable
        # seems to do)
        test_table.field_names = target_table_header_text
        targets_index.write(html.unescape(test_table.get_html_string(attributes={"class": "ui celled table"})))

        targets_index.write("\n</body>")


def generate_tests_and_targets_website(database: MbedTestDatabase, gen_path: pathlib.Path):
    """
    Generate a static website containing info about all the Mbed tests and targets.

    :param database: Database object to use
    :param gen_path: Path to generate the site at
    """

    gen_path.mkdir(exist_ok=True)

    # Generate CSS
    write_global_stylesheet(gen_path / "mbed-results-site.css")

    # Generate drivers subdirectory
    drivers_dir = gen_path / "drivers"
    drivers_dir.mkdir(exist_ok=True)
    generate_drivers_index_page(database, drivers_dir / "index.html")

    all_drivers = database.get_all_drivers()
    for driver in all_drivers:
        generate_driver_page(database, driver, drivers_dir / f"{driver.name}.html")

    # Generate targets subdirectory
    targets_dir = gen_path / "targets"
    targets_dir.mkdir(exist_ok=True)
    targets_assets_dir = targets_dir / "assets"
    targets_assets_dir.mkdir(exist_ok=True)

    mcu_family_targets = database.get_mcu_family_targets()
    generate_targets_index_page(database, mcu_family_targets, targets_dir / "index.html")
    for mcu_family_target in mcu_family_targets:
        generate_target_family_page(database, mcu_family_target, targets_dir / f"{mcu_family_target}.html")

    # Generate tests subdirectory
    tests_dir = gen_path / "tests"
    tests_dir.mkdir(exist_ok=True)

    generate_tests_index_page(database, tests_dir / "index.html")

