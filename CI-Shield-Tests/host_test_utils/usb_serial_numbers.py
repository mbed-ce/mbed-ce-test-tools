import os

# Determine CI shield serial number based on environment variable.
if "MBED_CI_SHIELD_SERIAL_NUMBER" in os.environ:
    CI_SHIELD_SERNO = os.environ["MBED_CI_SHIELD_SERIAL_NUMBER"]
    print("Connecting to CI shield with serial number " + CI_SHIELD_SERNO)

    # The CY7C65211 and FX2LAFW serial numbers use the following formats:
    CY7C65211_SERIAL_NUMBER = "Shield" + CI_SHIELD_SERNO
    FX2LAFW_SERIAL_NUMBER = "Mbed CE CI FX2LAFW " + CI_SHIELD_SERNO
else:
    CI_SHIELD_SERNO = None
    CY7C65211_SERIAL_NUMBER = None
    FX2LAFW_SERIAL_NUMBER = None
    print("Will use any connected CI shield for this test.  Export the MBED_CI_SHIELD_SERIAL_NUMBER environment var to select a specific shield.")
    print("e.g. 'export MBED_CI_SHIELD_SERIAL_NUMBER=SN002'")