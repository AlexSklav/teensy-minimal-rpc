def test_milliseconds(proxy):
    '''
    Test reading millisecond counter from device.
    '''
    time_a = proxy.milliseconds()
    time_b = proxy.milliseconds()
    assert time_a < time_b


def test_microseconds(proxy):
    '''
    Test reading microsecond counter from device.
    '''
    time_a = proxy.microseconds()
    time_b = proxy.microseconds()
    assert time_a < time_b
