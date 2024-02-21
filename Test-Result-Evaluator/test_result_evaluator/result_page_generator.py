import pathlib
from typing import TextIO

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

        index_page.write("</body>")


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

