from path_helpers import path
try:
    from .node import (Proxy as _Proxy, I2cProxy as _I2cProxy,
                       SerialProxy as _SerialProxy)

    HW_TCDS_ADDR = 0x40009000
    TCD_RECORD_DTYPE = [('SADDR', 'uint32'),
                        ('SOFF', 'uint16'),
                        ('ATTR', 'uint16'),
                        ('NBYTES', 'uint32'),
                        ('SLAST', 'uint32'),
                        ('DADDR', 'uint32'),
                        ('DOFF', 'uint16'),
                        ('CITER', 'uint16'),
                        ('DLASTSGA', 'uint32'),
                        ('CSR', 'uint16'),
                        ('BITER', 'uint16')]


    class ProxyMixin(object):
        '''
        Mixin class to add convenience wrappers around methods of the generated
        `node.Proxy` class.

        For example, expose config and state getters/setters as attributes.
        '''
        host_package_name = str(path(__file__).parent.name.replace('_', '-'))

        def __init__(self, *args, **kwargs):
            super(ProxyMixin, self).__init__(*args, **kwargs)
            self.init_dma()

        def init_dma(self):
            '''
            Initialize eDMA engine.  This includes:

             - Enabling clock gating for DMA and DMA mux.
             - Resetting all DMA channel transfer control descriptors.

            See the following sections in [K20P64M72SF1RM][1] for more information:

             - (12.2.13/256) System Clock Gating Control Register 6 (SIM_SCGC6)
             - (12.2.14/259) System Clock Gating Control Register 7 (SIM_SCGC7)
             - (21.3.17/415) TCD registers
            '''
            from .SIM import R_SCGC6, R_SCGC7

            # Enable DMA-related clocks in clock-gating configuration
            # registers.
            # SIM_SCGC6 |= SIM_SCGC6_DMAMUX;
            self.update_sim_SCGC6(R_SCGC6(DMAMUX=True))
            # SIM_SCGC7 |= SIM_SCGC7_DMA;
            self.update_sim_SCGC7(R_SCGC7(DMA=True))

            # Reset all DMA transfer control descriptor registers (i.e., set to
            # 0).
            for i in xrange(self.dma_channel_count()):
                self.reset_dma_TCD(i)

        def DMA_TCD(self, dma_channel):
            from arduino_rpc.protobuf import resolve_field_values
            import arduino_helpers.hardware.teensy.dma as dma
            from .DMA import TCD

            tcd = TCD.FromString(self.read_dma_TCD(dma_channel).tostring())
            df_tcd = resolve_field_values(tcd)
            return (df_tcd[['full_name', 'value']].dropna()
                    .join(dma.TCD_DESCRIPTIONS, on='full_name')
                    [['full_name', 'value', 'short_description', 'page']]
                    .sort_values(['page','full_name']))

        def DMA_registers(self):
            from arduino_rpc.protobuf import resolve_field_values
            import arduino_helpers.hardware.teensy.dma as dma
            from .DMA import Registers

            dma_registers = (Registers
                             .FromString(self.read_dma_registers().tostring()))
            df_dma = resolve_field_values(dma_registers)
            return (df_dma.dropna(subset=['value'])
                    .join(dma.REGISTERS_DESCRIPTIONS, on='full_name')
                    [['full_name', 'value', 'short_description', 'page']])

        def tcd_msg_to_struct(self, tcd_msg):
            '''
            Convert Transfer Control Descriptor from Protocol Buffer message
            encoding to raw structure, i.e., 32 bytes starting from `SADDR`
            field.

            See 21.3.17/415 in the [manual][1] for more details.

            [1]: https://www.pjrc.com/teensy/K20P64M72SF1RM.pdf
            '''
            # Copy TCD to device so that we can extract the raw bytes from
            # device memory (raw TCD bytes are read into `tcd0`.
            #
            # __TODO__:
            #  - Modify `TeensyMinimalRpc/DMA.h::serialize_TCD` and
            #  `TeensyMinimalRpc/DMA.h::update_TCD` functions to work offline.
            #      * Operate on variable by reference, on-device use actual register.
            #  - Add `arduino_helpers.hardware.teensy` function to convert
            #    between TCD protobuf message and binary TCD struct.

            self.update_dma_TCD(0, tcd_msg)
            return (self.mem_cpy_device_to_host(HW_TCDS_ADDR, 32)
                    .view(TCD_RECORD_DTYPE)[0])

        def tcd_struct_to_msg(self, tcd_struct):
            '''
            Convert Transfer Control Descriptor from raw structure (i.e., 32
            bytes starting from `SADDR` field) to Protocol Buffer message
            encoding.

            See 21.3.17/415 in the [manual][1] for more details.

            [1]: https://www.pjrc.com/teensy/K20P64M72SF1RM.pdf
            '''
            from .DMA import TCD

            # Copy TCD structure to device so that we can extract the serialized protocol
            # buffer representation from the device.
            #
            # __TODO__:
            #  - Modify `TeensyMinimalRpc/DMA.h::serialize_TCD` and
            #    `TeensyMinimalRpc/DMA.h::update_TCD` functions to work offline.
            #      * Operate on variable by reference, on-device use actual register.
            #  - Add `arduino_helpers.hardware.teensy` function to convert between TCD
            #    protobuf message and binary TCD struct.
            self.mem_cpy_host_to_device(HW_TCDS_ADDR, tcd_struct.tostring())
            return TCD.FromString(self.read_dma_TCD(0).tostring())

        @property
        def config(self):
            from .config import Config

            return Config.FromString(self.serialize_config().tostring())

        @config.setter
        def config(self, value):
            return self.update_config(value)

        @property
        def state(self):
            from .config import State

            return State.FromString(self.serialize_state().tostring())

        @state.setter
        def state(self, value):
            return self.update_state(value)

        def update_config(self, **kwargs):
            '''
            Update fields in the config object based on keyword arguments.

            By default, these values will be saved to EEPROM. To prevent this
            (e.g., to verify system behavior before committing the changes),
            you can pass the special keyword argument 'save=False'. In this case,
            you will need to call the method save_config() to make your changes
            persistent.
            '''

            from .config import Config

            save = True
            if 'save' in kwargs.keys() and not kwargs.pop('save'):
                save = False

            config = Config(**kwargs)
            return_code = super(ProxyMixin, self).update_config(config)

            if save:
                super(ProxyMixin, self).save_config()

            return return_code

        def update_state(self, **kwargs):
            from .config import State

            state = State(**kwargs)
            return super(ProxyMixin, self).update_state(state)


    class Proxy(ProxyMixin, _Proxy):
        pass

    class I2cProxy(ProxyMixin, _I2cProxy):
        pass

    class SerialProxy(ProxyMixin, _SerialProxy):
        pass

except (ImportError, TypeError):
    Proxy = None
    I2cProxy = None
    SerialProxy = None