# Test Result Evaluator
This Python package processes Mbed device JSONs and Mbed test runs.  It generates a website containing:
- A list of all targets and their supported features, organized by family
- An index of all features/components and which devices they exist on
- A matrix of all tests run for each device and whether they passed or failed

The primary use is so that we can easily tell which tests are having trouble on which device(s).

Note: To collect a test run for use with this program, you need to use the following command when running CTest:
```
$ ctest --repeat until-pass:3 --output-on-failure --output-junit mbed-tests-YOUR_MBED_TARGETF.xml --test-output-size-passed 100000 --test-output-size-failed 100000 .
```
The --test-output-size options are especially important as without them CTest will throw away the console output from each test that this script needs.

