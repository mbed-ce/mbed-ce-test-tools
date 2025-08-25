"""
A test executor for Mbed CE.

Test Executor performs the following steps:
1. Read a config file for what boards are connected to the current machine and how to talk to them
2. Compile Mbed for each of the available boards. (compile jobs happen one at a time, since each one will use all the cores on the machine)
3. Run a CTest job for each of the boards. This happens in parallel.
"""

import sys
import pathlib
import subprocess

import pyjson5
import pydantic

class BoardConfiguration(pydantic.BaseModel):
    mbed_target: str
    """ Mbed target name of the board """

    upload_method: str
    """ Upload method to use for this board. """

    baremetal: bool
    """ Whether to build mbed-baremetal or mbed-os. """

    upload_serial_number: str | None = None
    """ Serial number that will be passed to MBED_UPLOAD_SERIAL_NUMBER """

    com_port: str | None = None
    """ COM port name or TTY path for this board. Defaults to '/dev/tty<mbed_target>' if unset. """

    extra_cmake_options: dict[str, str] = {}
    """ Additional options to pass to CMake, as k-v pairs """

class TestExecutorConfig(pydantic.BaseModel):
    """
    Top-level configuration for the test executor.
    """

    boards: list[BoardConfiguration]

def build_mbed_ce(source_dir: pathlib.Path, build_dir: pathlib.Path, configuration: BoardConfiguration) -> None:
    # Create build dir if it doesn't exist
    build_dir.mkdir(parents=True, exist_ok=True)

    # Build CMake args
    cmake_command = [
        "cmake",
        str(source_dir),
        "-GNinja",
        "-DMBED_TARGET=" + configuration.mbed_target,
        "-DUPLOAD_METHOD=" + configuration.upload_method,
        "-DMBED_BUILD_GREENTEA_TESTS=TRUE",

        # Use Release build as it generates the smallest build dirs and ensures that we find bugs caused
        # by optimization in testing. (though the cost is that we can't use a debugger with code built
        # by this script).
        "-DCMAKE_BUILD_TYPE=Release",
    ]
    if configuration.upload_serial_number is not None:
        cmake_command.append("-DMBED_UPLOAD_SERIAL_NUMBER=" + configuration.upload_serial_number)
    if configuration.com_port is not None:
        cmake_command.append("-DMBED_GREENTEA_SERIAL_PORT=" + configuration.com_port)
    else:
        cmake_command.append("-DMBED_GREENTEA_SERIAL_PORT=/dev/tty" + configuration.mbed_target)
    app_json_file_name = "greentea_baremetal.json5" if configuration.baremetal else "greentea_full.json5"
    cmake_command.append(f"-DMBED_APP_JSON_PATH={str(source_dir.resolve())}/TESTS/configs/{app_json_file_name}")
    cmake_command.extend(f"-D{var}={val}" for var, val in configuration.extra_cmake_options.items())

    print(f">> Configuring Mbed for {configuration.mbed_target}...")
    cmake_result = subprocess.run(cmake_command, cwd=str(build_dir), text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if cmake_result.returncode != 0:
        print("CMake configuration failed. Output was:")
        print(cmake_result.stdout)
        sys.exit(1)
    print(f">> Configuring Mbed for {configuration.mbed_target} -- done")

    print(f">> Building Mbed for {configuration.mbed_target}...")
    build_result = subprocess.run(["ninja"], cwd=str(build_dir), text=True, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT)
    if build_result.returncode != 0:
        print("Build failed. Output was:")
        print(build_result.stdout)
        sys.exit(1)
    print(f">> Building Mbed for {configuration.mbed_target} -- done")

def run_ctest(build_dir: pathlib.Path, test_result_dir: pathlib.Path, configuration: BoardConfiguration) -> subprocess.Popen:
    result_junit_path = test_result_dir / f"mbed-tests-{configuration.mbed_target}.xml"
    ctest_command = [
        "ctest",
        "--output-on-failure",
        "--output-junit",
        str(result_junit_path),
        "--test-output-size-passed",
        "100000",
        "--test-output-size-failed",
        "100000"
    ]
    print(f">> Running greentea tests for {configuration.mbed_target}...")
    return subprocess.Popen(ctest_command, cwd=str(build_dir))

def main():
    if len(sys.argv) != 5:
        print("Usage: python -m test_result_evaluator.test_executor <config_file> <source dir path> <top build dir> <test result dir>")
        sys.exit(1)

    config_file = pathlib.Path(sys.argv[1])
    source_dir = pathlib.Path(sys.argv[2])
    top_build_dir = pathlib.Path(sys.argv[3])
    test_result_dir = pathlib.Path(sys.argv[4])

    # Load configuration
    config_deserialized_json = pyjson5.loads(config_file.read_text())
    loaded_config = TestExecutorConfig.model_validate(config_deserialized_json)

    # Configure and build
    for board in loaded_config.boards:
        build_mbed_ce(source_dir, top_build_dir / board.mbed_target, board)

    # Run tests
    processes = []
    test_result_dir.mkdir(parents=True, exist_ok=True)
    for board in loaded_config.boards:
        processes.append(run_ctest(top_build_dir / board.mbed_target, test_result_dir, board))

    # Wait for tests to complete
    try:
        for process in processes:
            process.wait()
        print(f">> Test run complete.")
    except KeyboardInterrupt:
        for process in processes:
            process.kill()
        print(f">> Test run aborted.")

if __name__ == "__main__":
    main()