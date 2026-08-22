import time

#: Both the millisecond and microsecond counters are 32-bit and wrap around.
UINT32_MODULUS = 1 << 32


def elapsed(time_a, time_b, modulus=UINT32_MODULUS):
    '''
    Counts elapsed between two readings of a free-running counter, accounting
    for wrap-around.

    __N.B.,__ the microsecond counter wraps roughly every 72 minutes, so a
    naive ``time_a < time_b`` assertion is not reliable.
    '''
    return (int(time_b) - int(time_a)) % modulus


def test_milliseconds(proxy):
    '''
    Test reading millisecond counter from device.
    '''
    time_a = proxy.milliseconds()
    time_b = proxy.milliseconds()
    # Two back-to-back reads may return the *same* count, so allow equality.
    # A wrap-safe elapsed count that is small confirms the counter did not go
    # backwards.
    assert 0 <= elapsed(time_a, time_b) < 1000

    # Sleeping must advance the counter by roughly the sleep duration.
    time_c = proxy.milliseconds()
    time.sleep(0.1)
    time_d = proxy.milliseconds()
    assert 50 <= elapsed(time_c, time_d) < 1000


def test_microseconds(proxy):
    '''
    Test reading microsecond counter from device.
    '''
    time_a = proxy.microseconds()
    time_b = proxy.microseconds()
    # Two back-to-back reads may return the *same* count, so allow equality.
    assert 0 <= elapsed(time_a, time_b) < 1000000

    # Sleeping must advance the counter by roughly the sleep duration.
    time_c = proxy.microseconds()
    time.sleep(0.1)
    time_d = proxy.microseconds()
    assert 50000 <= elapsed(time_c, time_d) < 1000000
