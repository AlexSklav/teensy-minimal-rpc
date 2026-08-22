import pytest

import teensy_minimal_rpc as tr


@pytest.fixture
def proxy():
    """
    Connect to a Teensy device over serial.

    All tests in this package require physical hardware, so skip (rather than
    error) when the generated proxy classes are unavailable or no device is
    connected.
    """
    if tr.SerialProxy is None:
        pytest.skip('`teensy_minimal_rpc.SerialProxy` is not available (the '
                    'generated `node` module is missing).')
    try:
        proxy = tr.SerialProxy()
    except IOError as e:
        pytest.skip(f'No Teensy device available: {e}')

    try:
        yield proxy
    finally:
        # Explicitly tear down the serial connection; relying on `del` leaves
        # the reader thread (and the serial port) alive until garbage
        # collection, which can make subsequent tests fail to connect.
        try:
            proxy.terminate()
        except Exception:
            pass
