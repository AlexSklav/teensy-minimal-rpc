"""
Hardware-free unit tests for :mod:`teensy_minimal_rpc.adc_sampler`.

**No Teensy, no serial port and no firmware are required.**  Every test drives
:class:`~teensy_minimal_rpc.adc_sampler.AdcSampler` and
:class:`~teensy_minimal_rpc.adc_sampler.AdcDmaMixin` through :class:`MockProxy`
-- a stub that stands in for the generated
:class:`teensy_minimal_rpc.node.Proxy` RPC layer.  Device memory allocations,
DMA/ADC register writes and TCD transfers are recorded in-process instead of
being sent over the wire, so the host-side configuration logic is exercised in
isolation.

The one thing that is *not* mocked is the shipped ADC configuration table
(``teensy_minimal_rpc/static/data/adc_configs.csv``): the resolution tests read
the real, packaged CSV.

Contrast with the sibling modules (``test_io.py``, ``test_memory.py``,
``test_performance.py``, ``test_rms.py``, ``test_time.py``) which use the
``proxy`` fixture from ``conftest.py`` and require a connected device.
"""
import gc

import numpy as np
import pandas as pd
import pytest

import arduino_helpers.hardware.teensy as teensy

from teensy_minimal_rpc.adc_sampler import (AdcDmaMixin, AdcSampler,
                                            DEFAULT_ADC_CONFIGS,
                                            TCD_RECORD_DTYPE,
                                            get_adc_configs)


#: Arbitrary, plausible base address for the stubbed device heap.
_HEAP_BASE = 0x1FFF9000


class MockProxy(AdcDmaMixin):
    """
    Stub of the generated Teensy RPC proxy.

    Every ``mem_*``/``update_*`` method that
    :class:`~teensy_minimal_rpc.adc_sampler.AdcSampler` invokes is recorded
    rather than transmitted.  :meth:`analog_reads_config` is deliberately *not*
    overridden -- it is the real :class:`AdcDmaMixin` implementation under
    test.
    """

    def __init__(self):
        self._next_address = _HEAP_BASE
        #: ``(address, bytes)`` pairs passed to `mem_cpy_host_to_device()`.
        self.host_to_device = []
        #: Addresses passed to `mem_free()` / `mem_aligned_free()`.
        self.freed = []
        #: ``(adc_num, registers)`` pairs passed to `update_adc_registers()`.
        self.adc_registers = []
        #: ``(reference, adc_num)`` pairs passed to `setReference()`.
        self.references = []
        self.averaging = []
        self.resolutions = []
        #: Arguments of the last `start_dma_adc()` call.
        self.dma_adc_starts = []

    # -- device memory ----------------------------------------------------
    def mem_alloc(self, size):
        address = self._next_address
        # Keep allocations well separated so address arithmetic in the tests
        # is unambiguous.
        self._next_address += max(int(size), 1) + 32
        return np.uint32(address)

    def mem_aligned_alloc(self, alignment, size):
        # Round the next address up to the requested alignment.
        remainder = self._next_address % alignment
        if remainder:
            self._next_address += alignment - remainder
        return self.mem_alloc(size)

    def mem_aligned_alloc_and_set(self, alignment, data):
        return self.mem_aligned_alloc(alignment, len(data))

    def mem_free(self, address):
        self.freed.append(int(address))

    def mem_aligned_free(self, address):
        self.freed.append(int(address))

    def mem_fill_uint8(self, address, value, size):
        pass

    def mem_cpy_host_to_device(self, address, data):
        self.host_to_device.append((int(address), bytes(data)))

    def mem_cpy_device_to_host(self, address, size):
        return np.zeros(int(size), dtype='uint8')

    # -- register / DMA plumbing -----------------------------------------
    def update_sim_SCGC6(self, message):
        pass

    def update_sim_SCGC7(self, message):
        pass

    def update_dma_TCD(self, dma_channel, message):
        pass

    def update_dma_mux_chcfg(self, dma_channel, message):
        pass

    def update_dma_registers(self, message):
        pass

    def enableDMA(self, adc_number):
        pass

    def attach_dma_interrupt(self, dma_channel):
        pass

    def start_dma_adc(self, pdb_config, address, size, stream_id):
        self.dma_adc_starts.append((int(pdb_config), int(address), int(size),
                                    int(stream_id)))
        return True

    def DMA_registers(self):
        # No DMA errors pending.
        return pd.DataFrame({'full_name': ['ERR'], 'value': [0]})

    # -- ADC settings applied by `analog_reads_config()` ------------------
    def update_adc_registers(self, adc_number, registers):
        self.adc_registers.append((adc_number, registers))

    def setReference(self, reference, adc_number):
        self.references.append((reference, adc_number))

    def setAveraging(self, average_count, adc_number):
        self.averaging.append((average_count, adc_number))

    def setResolution(self, resolution, adc_number):
        self.resolutions.append((resolution, adc_number))


@pytest.fixture
def proxy():
    return MockProxy()


# ---------------------------------------------------------------------------
# ADC configuration table (real, packaged CSV)
# ---------------------------------------------------------------------------
def test_adc_configs_loaded_from_packaged_csv():
    """
    Guards ``get_adc_configs()`` reading its table via ``pkgutil.get_data()``.

    ``pkgutil`` returns ``bytes``; feeding those straight to a text-mode
    ``pandas`` reader (or to ``jinja2``) is the classic Python 3 bytes/str
    break.  The table must load, be non-empty and be sorted by conversion time.
    """
    df_adc_configs = get_adc_configs()

    assert not df_adc_configs.empty
    assert set(df_adc_configs['Mode'].unique()) == {'single-ended',
                                                    'differential'}
    conversion_time = df_adc_configs['conversion_time'].values
    assert np.all(np.diff(conversion_time) >= 0)


@pytest.mark.parametrize('resolution', [8, 10, 12, 16])
def test_analog_reads_config_selects_config_for_resolution(proxy, resolution):
    """
    Guards ``analog_reads_config()`` finding a valid entry in the shipped ADC
    configuration table for every documented resolution (8/10/12/16 bits).

    The lookup uses ``idxmin()`` (an index *label*) rather than ``argmin()`` (a
    *positional* index) because the result feeds a label-based ``.loc[]``
    lookup; after the table is filtered and re-sorted those two differ, and
    ``argmin()`` silently selected the wrong row.
    """
    sampling_rate_hz, adc_settings, adc_sampler = \
        proxy.analog_reads_config(['A0'], sample_count=4,
                                  resolution=resolution)

    assert int(adc_settings['Bit-width']) == resolution
    assert int(adc_settings['resolution']) == resolution
    assert adc_settings['Mode'] == 'single-ended'
    assert adc_settings['differential'] is False
    assert adc_settings['reference_V'] == 3.3
    assert adc_settings['gain_power'] == 0
    assert sampling_rate_hz > 0
    assert isinstance(adc_sampler, AdcSampler)
    # The selected row really is the minimum-conversion-rate match.
    matches = DEFAULT_ADC_CONFIGS.loc[
        (DEFAULT_ADC_CONFIGS['Bit-width'] == resolution) &
        (DEFAULT_ADC_CONFIGS.Mode == 'single-ended') &
        (DEFAULT_ADC_CONFIGS.AverageNum == 1)]
    assert adc_settings['conversion_rate'] == \
        matches['conversion_rate'].min()
    # Settings were pushed to the device in the documented order.
    assert proxy.references == [(teensy.ADC_REF_3V3, teensy.ADC_0)]
    assert proxy.averaging == [(1, teensy.ADC_0)]
    assert proxy.resolutions == [(resolution, teensy.ADC_0)]


@pytest.mark.parametrize('resolution', [8, 10, 12, 16])
def test_analog_reads_config_differential_widens_even_resolutions(proxy,
                                                                  resolution):
    """
    Guards the differential-mode bit-width adjustment: sub-16-bit *even*
    resolutions gain one bit for the sign, and differential mode must select
    the 1.2 V reference (mandatory on the Teensy 3.2).
    """
    expected_width = resolution + 1 if resolution < 16 else resolution

    _, adc_settings, _ = proxy.analog_reads_config(['A0'], sample_count=4,
                                                   resolution=resolution,
                                                   differential=True)

    assert int(adc_settings['Bit-width']) == expected_width
    assert adc_settings['Mode'] == 'differential'
    assert adc_settings['reference_V'] == 1.2
    assert proxy.references == [(teensy.ADC_REF_1V2, teensy.ADC_0)]


def test_analog_reads_config_rejects_unmatched_settings(proxy):
    """
    Guards the explicit ``ValueError`` when no row of the ADC configuration
    table satisfies the request.  Without it, ``idxmin()`` on the empty match
    frame raised an opaque ``ValueError`` from deep inside ``pandas``.
    """
    with pytest.raises(ValueError, match='No ADC configuration matches'):
        proxy.analog_reads_config(['A0'], sample_count=4, resolution=7)


@pytest.mark.parametrize('gain_power', [-1, 8, 9, 100])
def test_analog_reads_config_rejects_out_of_range_gain_power(proxy,
                                                             gain_power):
    """
    Guards the ``gain_power`` range check being a real ``ValueError``.

    It used to be a bare ``assert``, which ``python -O`` strips -- so an
    out-of-range, user-supplied gain would have been written straight into the
    ADC ``PGA`` register.
    """
    with pytest.raises(ValueError, match=r'gain_power'):
        proxy.analog_reads_config(['A0'], sample_count=4, differential=True,
                                  gain_power=gain_power)


def test_analog_reads_config_rejects_gain_without_differential(proxy):
    """
    Guards programmable-gain amplification being rejected outside differential
    mode (the hardware PGA is only wired up for differential measurements).
    """
    with pytest.raises(ValueError, match='only.*differential'):
        proxy.analog_reads_config(['A0'], sample_count=4, differential=False,
                                  gain_power=2)


# ---------------------------------------------------------------------------
# `AdcSampler` construction / scatter TCD chain
# ---------------------------------------------------------------------------
def _scatter_tcds(proxy, sampler):
    """Decode the per-sample scatter TCDs copied to the stub device."""
    tcd_addrs = [int(address) for address in sampler.tcd_addrs]
    by_address = {address: np.frombuffer(data, dtype=TCD_RECORD_DTYPE)[0]
                  for address, data in proxy.host_to_device
                  if address in tcd_addrs}
    assert len(by_address) == len(tcd_addrs)
    return tcd_addrs, [by_address[address] for address in tcd_addrs]


@pytest.mark.parametrize('sample_count', [1, 10])
def test_adc_sampler_scatter_chain_wraps(proxy, sample_count):
    """
    Guards the scatter TCD chain wrapping back to the first descriptor.

    ``DLASTSGA`` of TCD *i* must point at TCD *(i + 1) % sample_count* -- the
    modulo matters: with ``sample_count == 1`` the single TCD has to link to
    *itself*.  Indexing ``tcd_addrs[i + 1]`` unguarded raised ``IndexError``
    for the last descriptor.
    """
    sampler = AdcSampler(proxy, ['A0', 'A1'], sample_count)

    assert sampler.sample_count == sample_count
    assert len(sampler.tcd_addrs) == sample_count

    tcd_addrs, tcds = _scatter_tcds(proxy, sampler)
    for i, tcd in enumerate(tcds):
        assert int(tcd['DLASTSGA']) == tcd_addrs[(i + 1) % sample_count], \
            f'TCD {i} does not link to TCD {(i + 1) % sample_count}'
    # The chain closes: the last descriptor links back to the first.
    assert int(tcds[-1]['DLASTSGA']) == tcd_addrs[0]
    # Only the final descriptor raises the major-loop interrupt (`INTMAJOR`).
    intmajor = [bool(int(tcd['CSR']) & (1 << 1)) for tcd in tcds]
    assert intmajor == [False] * (sample_count - 1) + [True]


def test_adc_sampler_single_channel_string_is_wrapped(proxy):
    """
    Guards a bare channel name (``'A0'``) being wrapped in a list rather than
    iterated character by character.
    """
    sampler = AdcSampler(proxy, 'A0', 4)

    assert sampler.channels == ['A0']
    assert sampler.channel_sc1as.size == 1


@pytest.mark.parametrize('sample_count', [0, -1])
def test_adc_sampler_rejects_non_positive_sample_count(proxy, sample_count):
    """
    Guards the explicit ``sample_count`` validation.  A zero/negative count
    previously produced an empty TCD chain and a zero-byte device allocation,
    surfacing much later as an unrelated DMA failure.
    """
    with pytest.raises(ValueError, match='sample_count'):
        AdcSampler(proxy, ['A0'], sample_count)


def test_adc_sampler_rejects_dma_channels_missing_roles(proxy):
    """
    Guards the ``dma_channels`` index validation.  The channels are looked up
    by attribute access (``dma_channels.scatter``); a ``Series`` missing a role
    otherwise failed with a bare ``AttributeError`` mid-configuration, leaving
    device memory allocated.
    """
    dma_channels = pd.Series([0, 1], index=['scatter', 'adc_conversion'])

    with pytest.raises(ValueError, match='adc_channel_configs'):
        AdcSampler(proxy, ['A0'], 4, dma_channels=dma_channels)


def test_adc_sampler_rejects_non_series_dma_channels(proxy):
    """
    Guards the ``dma_channels`` type check: a plain list has no ``.scatter``
    attribute, so it must be rejected up front.
    """
    with pytest.raises(TypeError, match='pandas.Series'):
        AdcSampler(proxy, ['A0'], 4, dma_channels=[0, 1, 2])


# ---------------------------------------------------------------------------
# `start_read()` sample-rate cache semantics
# ---------------------------------------------------------------------------
def test_start_read_requires_sample_rate_on_first_call(proxy):
    """
    Guards the explicit error when ``start_read()`` is called with no rate and
    no cached rate.  ``configure_timer(None)`` would otherwise fail with an
    unhelpful ``TypeError`` deep in the PDB arithmetic.
    """
    sampler = AdcSampler(proxy, ['A0'], 4)

    assert sampler.sample_rate_hz is None
    with pytest.raises(ValueError, match='No cached sampling rate'):
        sampler.start_read()


def test_start_read_reuses_cached_sample_rate(proxy):
    """
    Guards the documented "``sample_rate_hz`` can be omitted on subsequent
    calls" contract: passing ``None`` must reuse the cached rate (and its
    already-computed PDB configuration) instead of clobbering it.
    """
    sampler = AdcSampler(proxy, ['A0'], 4)

    sampler.start_read(sample_rate_hz=10000)
    cached_rate = sampler.sample_rate_hz
    cached_pdb_config = sampler.pdb_config
    assert cached_rate == 10000
    assert cached_pdb_config is not None

    result = sampler.start_read()

    assert result is sampler
    assert sampler.sample_rate_hz == cached_rate
    assert sampler.pdb_config == cached_pdb_config
    assert len(proxy.dma_adc_starts) == 2
    assert proxy.dma_adc_starts[0] == proxy.dma_adc_starts[1]


def test_sample_rate_hz_setter_rejects_none(proxy):
    """
    Guards the ``sample_rate_hz`` setter refusing ``None``, which would
    otherwise silently discard a previously cached (working) rate.
    """
    sampler = AdcSampler(proxy, ['A0'], 4)
    sampler.sample_rate_hz = 10000

    with pytest.raises(ValueError, match='must not be `None`'):
        sampler.sample_rate_hz = None

    assert sampler.sample_rate_hz == 10000


def test_start_read_raises_when_previous_operation_in_progress(proxy,
                                                              monkeypatch):
    """
    Guards the ``start_dma_adc()`` return value being checked: a falsy result
    means a DMA ADC read is already running, and silently ignoring it would
    clobber the in-flight transfer.
    """
    sampler = AdcSampler(proxy, ['A0'], 4)
    monkeypatch.setattr(proxy, 'start_dma_adc',
                        lambda *args, **kwargs: False)

    with pytest.raises(RuntimeError, match='in progress'):
        sampler.start_read(sample_rate_hz=10000)


# ---------------------------------------------------------------------------
# `__del__` with a dead proxy weakref
# ---------------------------------------------------------------------------
def test_del_with_dead_proxy_weakref_is_silent(capfd):
    """
    Guards ``AdcSampler.__del__()`` against a collected proxy.

    ``self.proxy`` is a ``weakref``; at interpreter shutdown (or once the
    caller drops its proxy reference) the referent is gone and
    ``self.proxy().mem_free(...)`` raised ``AttributeError: 'NoneType'``.
    Exceptions escaping ``__del__`` are unraisable, so the only symptom was a
    traceback printed to stderr.
    """
    sampler = AdcSampler(MockProxy(), ['A0'], 4)
    gc.collect()

    # The only strong reference to the proxy was the temporary above.
    assert sampler.proxy() is None

    sampler.__del__()

    del sampler
    gc.collect()

    out, err = capfd.readouterr()
    assert err == '', f'`__del__` wrote to stderr:\n{err}'


def test_del_frees_device_allocations(proxy):
    """
    Guards ``__del__()`` releasing every device allocation made by
    ``allocate_device_arrays()`` -- plain ``mem_free()`` for the unaligned
    buffers and ``mem_aligned_free()`` for the aligned ones.
    """
    sampler = AdcSampler(proxy, ['A0', 'A1'], 4)
    expected = sorted(int(sampler.allocs[name])
                      for name in ('scan_result', 'samples', 'sc1as', 'tcds'))

    sampler.__del__()

    assert sorted(proxy.freed) == expected


def test_del_is_idempotent(proxy):
    """
    Guards ``__del__()`` never raising when invoked twice (explicitly, then
    again by the garbage collector).
    """
    sampler = AdcSampler(proxy, ['A0'], 4)

    sampler.__del__()
    sampler.__del__()

    del sampler
    gc.collect()
