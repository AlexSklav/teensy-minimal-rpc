'''
Sanity checks for the on-device floating-point and integer benchmarks.

__N.B.,__ the reference figures below were measured empirically (mean of 1000
tests) on a Teensy 3.1/3.2 at its default clock.  They are *not* portable: a
device running at a different core clock (or built with different optimization
flags) legitimately produces a different figure.  The original assertions used
``rtol=1e-2`` against these constants, which fails on any such device.

The assertions therefore only check broad, order-of-magnitude sanity: the
measured per-operation time must be positive and within a factor of
:data:`TOLERANCE_FACTOR` of the reference.  That is still meaningful -- it
catches a benchmark that returns zero, a negative/garbage value, or a result
that is wrong by orders of magnitude -- while tolerating clock differences.
'''

#: Allowed deviation from the reference figures (multiplicative, both ways).
TOLERANCE_FACTOR = 10.

#: Empirical mean of 1000 tests (Teensy 3.1/3.2 at default clock).
REFERENCE_FLOP_US = 0.041857543945312499
#: Empirical mean of 1000 tests (Teensy 3.1/3.2 at default clock).
REFERENCE_IOP_US = 0.041865478515624999


def assert_within_factor(reference, measured, factor=TOLERANCE_FACTOR):
    '''
    Assert ``measured`` is positive and within ``factor`` of ``reference``.
    '''
    assert measured > 0, f'Non-positive benchmark result: {measured}'
    assert reference / factor <= measured <= reference * factor, (
        f'Benchmark result {measured} is not within a factor of {factor} of '
        f'the reference {reference}.')


def test_float_performance(proxy):
    '''
    Compare float performance against empirical results.
    '''
    N = 8 << 10
    flop_us = proxy.benchmark_flops_us(N) / float(N)
    assert_within_factor(REFERENCE_FLOP_US, flop_us)


def test_int_performance(proxy):
    '''
    Compare integer performance against empirical results.
    '''
    N = 8 << 10
    iop_us = proxy.benchmark_iops_us(N) / float(N)
    assert_within_factor(REFERENCE_IOP_US, iop_us)
