import collections
import pathlib
from typing import TextIO, List, Dict, Set
import html

import prettytable

from .mbed_test_database import MbedTestDatabase, FeatureType


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
    <script
      src="https://code.jquery.com/jquery-3.1.1.min.js"
      integrity="sha256-hVVnYaiADRTO2PzUGmuLJr8BLUSjGIZsDYGmIJLv2b8="
      crossorigin="anonymous"></script>
    <script src="https://code.jsdelivr.net/npm/semantic-ui@2.4.2/dist/semantic.min.js"></script>
    
    <style>
        body {{
            margin-left: 30px;
            margin-right: 30px;
            margin-top: 30px;
            overflow: scroll;
        }}
    </style>
</head>
<body>
    <h1>{page_title}</h1>""")


def generate_features_index_page(database: MbedTestDatabase, out_path: pathlib.Path):
    """
    Generate the features index page.  This page contains tables with summaries of the features and components.
    :param database:
    :param out_path:
    :return:
    """

    with open(out_path, "w") as index_page:
        write_html_header(index_page, "Mbed Features Index")

        for feature_type in FeatureType:
            index_page.write(f"<h2>Features of type {feature_type.value}</h2>\n")

            if type == FeatureType.PERIPHERAL:
                index_page.write("This indicates MCU peripherals which are present on this target MCU and supported by Mbed CE.\n")
            elif type == FeatureType.COMPONENT:
                index_page.write("This indicates external components (or in some cases co-processors) which are present on a target board and supported out of the box.\n")
            elif type == FeatureType.FEATURE:
                index_page.write("Larger Mbed OS optional features supported for certain targets.\n")

            feature_table = prettytable.PrettyTable()
            feature_table.field_names = ["Name", "Mbed Internal Name", "Description", "Supported on Targets:"]

            features_cursor = database.get_all_features(feature_type)
            for row in features_cursor:

                # Limit targets amount to 10
                # TODO also link to the target page
                targets_list = row["targetsWithFeature"].split(",")
                targets_str = ", ".join(targets_list[:10])
                if len(targets_list) > 10:
                    targets_str += f", ... ({len(targets_list) - 10} more)"

                # TODO also link to the feature page
                feature_table.add_row([
                    row["friendlyName"],
                    row["name"],
                    row["description"],
                    targets_str,
                ])
            features_cursor.close()

            index_page.write(feature_table.get_html_string(attributes={"class": "ui celled table"}))

        index_page.write("\n</body>")


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

            target_features = database.get_target_features(mcu_family_target)
            features_list = []
            peripherals_list = []
            for feature in target_features:
                feature_string = f'<a href="../features/{feature.name}.html">{feature.friendly_name}</a>'

                # Features and peripherals each go in separate table entries.  We ignore COMPONENTs for now
                # because those are a property of the board not the MCU.
                if feature.type == FeatureType.FEATURE:
                    features_list.append(feature_string)
                elif feature.type == FeatureType.PERIPHERAL:
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
        targets_in_family = [row["name"] for row in targets_cursor]
        targets_cursor.close()

        target_features: Dict[str, Dict[FeatureType, Set[MbedTestDatabase.TargetFeatureInfo]]] = dict()
        for target_name in targets_in_family:
            this_target_features = database.get_target_features(target_name)
            this_target_features_by_type = collections.defaultdict(set)
            for feature in this_target_features:
                this_target_features_by_type[feature.type].add(feature)
            target_features[target_name] = this_target_features_by_type

        common_features_by_type: Dict[FeatureType, Set[MbedTestDatabase.TargetFeatureInfo]] = dict()
        for feature_type in FeatureType:
            common_features_by_type[feature_type] = set.intersection(*[feature_dict[feature_type] for feature_dict in target_features.values()])

        # Generate features list (with the features and peripherals)
        target_page.write("<h2>Features Supported</h2>\n<ul>")
        for feature in common_features_by_type[FeatureType.FEATURE]:
            target_page.write(f'<li><a href="../features/{feature.name}.html">{feature.friendly_name}</a>: {feature.description}</li>\n')
        target_page.write("</ul>\n<h2>Peripheral Drivers Supported</h2>\n<ul>")
        for peripheral in common_features_by_type[FeatureType.PERIPHERAL]:
            target_page.write(f'<li><a href="../features/{peripheral.name}.html">{peripheral.friendly_name}</a>: {peripheral.description}</li>\n')
        target_page.write("</ul>\n")

        # Generate board table
        target_page.write("<h2>Boards in this Target Family</h2>")
        target_table = prettytable.PrettyTable()
        target_table.field_names = ["Board", "Components"]

        # Generate inheritance graph
        target_page.write("<h2>Inheritance Graph</h2>")

        inheritance_graph = database.get_inheritance_graph(mcu_family_target)
        inheritance_graph_basename = out_path.parent / "assets" / f"{mcu_family_target}.dot"
        inheritance_graph_svg_file = pathlib.Path(inheritance_graph.render(inheritance_graph_basename, format="svg"))
        target_page.write(f'<img src="assets/{inheritance_graph_svg_file.name}" alt="Inheritance Graph"/>\n')

        target_page.write("\n</body>")

def generate_tests_and_targets_website(database: MbedTestDatabase, gen_path: pathlib.Path):
    """
    Generate a static website containing info about all the Mbed tests and targets.

    :param database: Database object to use
    :param gen_path: Path to generate the site at
    """

    gen_path.mkdir(exist_ok=True)

    # Generate features subdirectory
    features_dir = gen_path / "features"
    features_dir.mkdir(exist_ok=True)
    generate_features_index_page(database, features_dir / "index.html")

    # Generate targets subdirectory
    targets_dir = gen_path / "targets"
    targets_dir.mkdir(exist_ok=True)
    targets_assets_dir = targets_dir / "assets"
    targets_assets_dir.mkdir(exist_ok=True)

    mcu_family_targets = database.get_mcu_family_targets()
    generate_targets_index_page(database, mcu_family_targets, targets_dir / "index.html")
    for mcu_family_target in mcu_family_targets:
        generate_target_family_page(database, mcu_family_target, targets_dir / f"{mcu_family_target}.html")


