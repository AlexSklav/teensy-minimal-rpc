import pytest
import teensy_minimal_rpc as tr


@pytest.fixture
def proxy():
    proxy = tr.SerialProxy()
    yield proxy
    del proxy
