#ifndef ___NODE__H___
#define ___NODE__H___

#include <math.h>
#include <string.h>
#include <stdint.h>
#include <Arduino.h>
#include <NadaMQ.h>
#include <CArrayDefs.h>
#include "RPCBuffer.h"  // Define packet sizes
#include "TeensyMinimalRpc/Properties.h"  // Define package name, URL, etc.
#include <BaseNodeRpc/BaseNode.h>
#include <BaseNodeRpc/BaseNodeConfig.h>
#include <BaseNodeRpc/BaseNodeEeprom.h>
#include <BaseNodeRpc/BaseNodeI2c.h>
#include <BaseNodeRpc/BaseNodeI2cHandler.h>
#include <BaseNodeRpc/BaseNodeSerialHandler.h>
#include <BaseNodeRpc/BaseNodeState.h>
#include <BaseNodeRpc/SerialHandler.h>
#include <ADC.h>
#include <AnalogBufferDMA.h>
#include <DMAChannel.h>
#include <TeensyMinimalRpc/ADC.h>  // Analog to digital converter
#include <TeensyMinimalRpc/DMA.h>  // Direct Memory Access
#include <TeensyMinimalRpc/SIM.h>  // System integration module (clock gating)
#include <TeensyMinimalRpc/PIT.h>  // Programmable interrupt timer
#include <TeensyMinimalRpc/aligned_alloc.h>
#include <pb_eeprom.h>
#include <pb_validate.h>
#include <pb_cpp_api.h>
#include <LinkedList.h>
#include "teensy_minimal_rpc_config_validate.h"
#include "teensy_minimal_rpc_state_validate.h"
#include "TeensyMinimalRpc/config_pb.h"
#include "TeensyMinimalRpc/state_pb.h"

const uint32_t ADC_BUFFER_SIZE = 4096;

extern void dma_ch0_isr(void);
extern void dma_ch1_isr(void);
extern void dma_ch2_isr(void);
extern void dma_ch3_isr(void);
extern void dma_ch4_isr(void);
extern void dma_ch5_isr(void);
extern void dma_ch6_isr(void);
extern void dma_ch7_isr(void);
extern void dma_ch8_isr(void);
extern void dma_ch9_isr(void);
extern void dma_ch10_isr(void);
extern void dma_ch11_isr(void);
extern void dma_ch12_isr(void);
extern void dma_ch13_isr(void);
extern void dma_ch14_isr(void);
extern void dma_ch15_isr(void);

namespace teensy_minimal_rpc {

// Define the array that holds the conversions here.
// buffer_size must be a power of two.
// The buffer is stored with the correct alignment in the DMAMEM section
// the +0 in the aligned attribute is necessary b/c of a bug in gcc.
DMAMEM static volatile uint16_t __attribute__((aligned(ADC_BUFFER_SIZE+0))) adc_buffer[ADC_BUFFER_SIZE];


const size_t FRAME_SIZE = (3 * sizeof(uint8_t)  // Frame boundary
                           - sizeof(uint16_t)  // UUID
                           - sizeof(uint16_t)  // Payload length
                           - sizeof(uint16_t));  // CRC

class RingBufferDMA : public AnalogBufferDMA {
public:
    RingBufferDMA(volatile uint16_t* buffer1, uint16_t buffer1_count,
                  volatile uint16_t* buffer2 = nullptr, uint16_t buffer2_count = 0)
        : AnalogBufferDMA(buffer1, buffer1_count, buffer2, buffer2_count) {}

    volatile uint16_t *p_elems = bufferLastISRFilled();
    uint16_t b_size = _buffer1_count;


    // Function to check if the buffer is full
    bool isFull()  {
        return bufferCountLastISRFilled() == b_size;
    }

    // Function to check if the buffer is empty
    bool isEmpty()  {
        return bufferCountLastISRFilled() == 0;
    }

    // Function to read data from the buffer
    int16_t read() {
        if (isEmpty()) {
            return 0;
        }

        int16_t result;
        volatile uint16_t *lastBuffer = bufferLastISRFilled();
        uint16_t lastIndex = bufferCountLastISRFilled() - 1;
        result = lastBuffer[lastIndex];
        return result;
    }
};

class Node;

typedef nanopb::EepromMessage<teensy_minimal_rpc_Config,
                              config_validate::Validator<Node> > config_t;
typedef nanopb::Message<teensy_minimal_rpc_State,
                        state_validate::Validator<Node> > state_t;

class Node :
  public BaseNode,
  public BaseNodeEeprom,
  public BaseNodeI2c,
  public BaseNodeConfig<config_t>,
  public BaseNodeState<state_t>,
#ifndef DISABLE_SERIAL
  public BaseNodeSerialHandler,
#endif  // #ifndef DISABLE_SERIAL
  public BaseNodeI2cHandler<base_node_rpc::i2c_handler_t> {
public:
  typedef PacketParser<FixedPacket> parser_t;

  static const uint32_t BUFFER_SIZE = 8192;  // >= longest property string

  // use dma with ADC0
  RingBufferDMA *dmaBuffer_;

  uint8_t buffer_[BUFFER_SIZE];
  ADC *adc_;
  uint32_t adc_period_us_;
  uint32_t adc_timestamp_us_;
  bool adc_tick_tock_;
  uint32_t adc_millis_;
  uint32_t adc_SYST_CVR_;
  uint32_t adc_millis_prev_;
  uint32_t adc_SYST_CVR_prev_;
  uint32_t adc_count_;
  int8_t dma_channel_done_;
  int8_t last_dma_channel_done_;
  bool adc_read_active_;
  LinkedList<uint32_t> allocations_;
  LinkedList<uint32_t> aligned_allocations_;
  UInt8Array dma_data_;
  uint16_t dma_stream_id_;

  Node()
    : BaseNode(),
      BaseNodeConfig<config_t>(teensy_minimal_rpc_Config_fields),
      BaseNodeState<state_t>(teensy_minimal_rpc_State_fields),
      dmaBuffer_(NULL),
      adc_period_us_(0),
      adc_timestamp_us_(0),
      adc_tick_tock_(false),
      adc_count_(0),
      dma_channel_done_(-1),
      last_dma_channel_done_(-1),
      adc_read_active_(false),
      dma_stream_id_(0) {
    pinMode(LED_BUILTIN, OUTPUT);
    dma_data_ = UInt8Array_init_default();
  }

  void begin();
  UInt8Array get_buffer() {
    /* This is a required method to provide a temporary buffer to the
     * `BaseNode...` classes. */
    return UInt8Array_init(sizeof(buffer_), buffer_);
  }
  /****************************************************************************
   * # User-defined methods #
   *
   * Add new methods below.  When Python package is generated using the
   * command, `paver sdist` from the project root directory, the signatures of
   * the methods below will be scanned and code will automatically be generated
   * to support calling the methods from Python over a serial connection.
   *
   * e.g.
   *
   *     bool less_than(float a, float b) { return a < b; }
   *
   * See [`arduino_rpc`][1] and [`base_node_rpc`][2] for more details.
   *
   * [1]: https://github.com/wheeler-microfluidics/arduino_rpc
   * [2]: https://github.com/wheeler-microfluidics/base_node_rpc
   */

  // ##########################################################################
  // # Callback methods
  void on_tick() {
    if (adc_read_active_) return;
    uint8_t channel;
    switch(adc_count_ & 0x01) {
      case(0): channel = A0; break;
      case(1): channel = A1; break;
      //case(2): channel = A2;
      //case(3): channel = A3;
      default: channel = A0;
    }
    adc_->adc0->startSingleRead(channel);
  }
  void on_adc_done() {
    if (adc_read_active_) return;
    adc_count_++;
    //adc_tick_tock_ = !adc_tick_tock_;
    //digitalWriteFast(LED_BUILTIN, adc_tick_tock_);
    //adc_SYST_CVR_prev_ = adc_SYST_CVR_;
    //adc_millis_ = millis();
    //adc_SYST_CVR_ = SYST_CVR;
  }
  /** Start ADC DMA transfers and copy the result as a stream packet to the
   * serial port when transfer has completed.
   *
   * \param pdb_config Programmable delay block status and control register
   *                   configuration.
   * \param addr Address to copy from after DMA transfer operations are
   *             complete.
   * \param size Number of bytes to copy to stream.
   * \param stream_id Identifier for stream packet.
   *
   * \see #loop
   */
  void start_dma_adc(uint32_t pdb_config, uint32_t addr, uint32_t size,
                     uint16_t stream_id) {
    dma_data_ = UInt8Array_init(size, reinterpret_cast<uint8_t*>(addr));
    dma_stream_id_ = stream_id;
    /*
     * Load configuration to Programmable Delay Block to start periodic ADC
     * reads.
     */
    PDB0_SC = pdb_config;
  }
  /** Called periodically from the main program loop. */
  void loop() {
    if (dma_channel_done_ >= 0) {
      // DMA channel has completed.
      last_dma_channel_done_ = dma_channel_done_;
      dma_channel_done_ = -1;

      // Copy DMA ADC data to serial port as a `STREAM` packet.
      if (dma_data_.length > 0) {
        serial_handler_.receiver_.write_f_(dma_data_,
                                           Packet::packet_type::STREAM,
                                           dma_stream_id_);
      }
    }
  }
  /** Returns current contents of DMA result buffer. */
  UInt8Array dma_data() const { return dma_data_; }

  // ##########################################################################
  // # Accessor methods
  uint32_t D__F_CPU() { return F_CPU; }
  uint32_t D__F_BUS() { return F_BUS; }
  uint32_t V__SYST_CVR() { return SYST_CVR; }
  uint32_t V__SCB_ICSR() { return SCB_ICSR; }
  UInt16Array adc_buffer() {
    UInt8Array byte_buffer = get_buffer();
    UInt16Array result;
    result.data = reinterpret_cast<uint16_t *>(byte_buffer.data);
    result.length = dmaBuffer_->b_size;

    uint16_t i = 0;
    for (i = 0; i < dmaBuffer_->b_size; i++) {
      result.data[i] = dmaBuffer_->p_elems[i];
    }
    return result;
  }
  float adc_period_us() const {
    uint32_t _SYST_CVR = ((adc_SYST_CVR_ < adc_SYST_CVR_prev_)
                          ? adc_SYST_CVR_ + 1000
                          : adc_SYST_CVR_);
    return (compute_timestamp_us(_SYST_CVR, 0) -
            compute_timestamp_us(adc_SYST_CVR_prev_, 0));
  }
  float adc_timestamp_us() const {
    return compute_timestamp_us(adc_SYST_CVR_, adc_millis_);
  }
  uint16_t analog_input_to_digital_pin(uint16_t pin) { return analogInputToDigitalPin(pin); }
  uint32_t benchmark_flops_us(uint32_t N) {
    /*
     * Parameters
     * ----------
     * N : uint32_t
     *     Number of floating point operations to perform.
     *
     * Returns
     * -------
     * uint32_t
     *     Number of microseconds to perform ``N`` floating point operations.
     */
    float a = 1e6;
    float b = 1e7;
    uint32_t start = micros();
    for (uint32_t i = 0; i < N; i++) {
      a /= b;
    }
    return (micros() - start);
  }
  uint32_t benchmark_iops_us(uint32_t N) {
    /*
     * Parameters
     * ----------
     * N : uint32_t
     *     Number of integer operations to perform.
     *
     * Returns
     * -------
     * uint32_t
     *     Number of microseconds to perform ``N`` integer operations.
     */
    uint32_t a = 1e6;
    uint32_t b = 1e7;
    uint32_t start = micros();
    for (uint32_t i = 0; i < N; i++) {
      a /= b;
    }
    return (micros() - start);
  }
  float compute_timestamp_us(uint32_t _SYST_CVR, uint32_t _millis) const {
    uint32_t current = ((F_CPU / 1000) - 1) - _SYST_CVR;
#if defined(KINETISL) && F_CPU == 48000000
    return _millis * 1000 + ((current * (uint32_t)87381) >> 22);
#elif defined(KINETISL) && F_CPU == 24000000
    return _millis * 1000 + ((current * (uint32_t)174763) >> 22);
#endif
    return 1000 * (_millis + current * (1000. / F_CPU));
  }

  uint16_t digital_pin_has_pwm(uint16_t pin) { return digitalPinHasPWM(pin); }
  uint16_t digital_pin_to_interrupt(uint16_t pin) { return digitalPinToInterrupt(pin); }
  uint16_t dma_channel_count() { return DMA_NUM_CHANNELS; }
  bool dma_empty() { return (dmaBuffer_ == NULL) ? 0 : dmaBuffer_->isEmpty(); }
  bool dma_full() { return (dmaBuffer_ == NULL) ? 0 : dmaBuffer_->isFull(); }
  int16_t dma_read() { return (dmaBuffer_ == NULL) ? 0 : dmaBuffer_->read(); }
  UInt8Array dma_tcd() {
    /* Return serialized "Transfer control descriptor" of DMA channel. */
    UInt8Array result = get_buffer();
    if (dmaBuffer_ == NULL) {
      result.length = 0;
      return result;
    }
    typedef typename DMABaseClass::TCD_t tcd_t;
    tcd_t &tcd = *reinterpret_cast<tcd_t *>(result.data);
    result.length = sizeof(tcd_t);
    tcd = *(dmaBuffer_->_dmachannel_adc.TCD);
    return result;
  }
  int8_t last_dma_channel_done() const { return last_dma_channel_done_; }
  UInt8Array mem_cpy_device_to_host(uint32_t address, uint32_t size) {
    UInt8Array output;
    output.length = size;
    output.data = (uint8_t *)address;
    return output;
  }
  UInt8Array read_adc_registers(uint8_t adc_num) {
    return teensy::adc::serialize_registers(adc_num, get_buffer());
  }
  UInt8Array read_dma_TCD(uint8_t channel_num) {
    return teensy::dma::serialize_TCD(channel_num, get_buffer());
  }
  void reset_dma_TCD(uint8_t channel_num) {
    teensy::dma::reset_TCD(channel_num);
  }
  UInt8Array read_dma_mux_chcfg(uint8_t channel_num) {
    return teensy::dma::serialize_mux_chcfg(channel_num, get_buffer());
  }
  UInt8Array read_dma_priority(uint8_t channel_num) {
    return teensy::dma::serialize_dchpri(channel_num, get_buffer());
  }
  UInt8Array read_dma_registers() {
    return teensy::dma::serialize_registers(get_buffer());
  }
  UInt8Array read_pit_registers() {
    return teensy::pit::serialize_registers(get_buffer());
  }
  UInt8Array read_pit_timer_config(uint8_t timer_index) {
    return teensy::pit::serialize_timer_config(timer_index, get_buffer());
  }
  UInt8Array read_sim_SCGC6() { return teensy::sim::serialize_SCGC6(get_buffer()); }
  UInt8Array read_sim_SCGC7() { return teensy::sim::serialize_SCGC7(get_buffer()); }
  UInt8Array _uuid() {
    /* Read unique chip identifier. */
    UInt8Array result = get_buffer();
    result.length = 4 * sizeof(uint32_t);
    memcpy(&result.data[0], &SIM_UIDH, result.length);
    return result;
  }

  // ##########################################################################
  // # Mutator methods
  void attach_dma_interrupt(uint8_t dma_channel) {
    void (*isr)(void);
    switch(dma_channel) {
      case 0: isr = &dma_ch0_isr; break;
      case 1: isr = &dma_ch1_isr; break;
      case 2: isr = &dma_ch2_isr; break;
      case 3: isr = &dma_ch3_isr; break;
      case 4: isr = &dma_ch4_isr; break;
      case 5: isr = &dma_ch5_isr; break;
      case 6: isr = &dma_ch6_isr; break;
      case 7: isr = &dma_ch7_isr; break;
      case 8: isr = &dma_ch8_isr; break;
      case 9: isr = &dma_ch9_isr; break;
      case 10: isr = &dma_ch10_isr; break;
      case 11: isr = &dma_ch11_isr; break;
      case 12: isr = &dma_ch12_isr; break;
      case 13: isr = &dma_ch13_isr; break;
      case 14: isr = &dma_ch14_isr; break;
      case 15: isr = &dma_ch15_isr; break;
      default: return;
    }
    _VectorsRam[dma_channel + IRQ_DMA_CH0 + 16] = isr;
    NVIC_ENABLE_IRQ(IRQ_DMA_CH0 + dma_channel);
  }
  void clear_dma_errors() {
    DMA_CERR = DMA_CERR_CAEI;  // Clear All Error Indicators
  }
  void detach_dma_interrupt(uint8_t dma_channel) {
      NVIC_DISABLE_IRQ(IRQ_DMA_CH0 + dma_channel);
  }
  bool dma_start(uint32_t buffer_size) {
    const bool power_of_two = (buffer_size &&
                               !(buffer_size & (buffer_size - 1)));
    if ((buffer_size > ADC_BUFFER_SIZE) || !power_of_two) { return false; }
    dma_stop();
    dmaBuffer_ = new RingBufferDMA(teensy_minimal_rpc::adc_buffer, buffer_size);
    dmaBuffer_->init(adc_, ADC_0);
    dmaBuffer_->userData(2048); // save initial starting average
    return true;
  }
  void dma_stop() {
    if (dmaBuffer_ != NULL) { delete dmaBuffer_; }
  }
  void free_all() {
    while (allocations_.size() > 0) { free((void *)allocations_.shift()); }
    while (aligned_allocations_.size() > 0) {
      aligned_free((void *)aligned_allocations_.shift());
    }
  }
  uint32_t mem_alloc(uint32_t size) {
    uint32_t address = (uint32_t)malloc(size);
    // Save to list of allocations for memory management.
    allocations_.add(address);
    return address;
  }
  uint32_t mem_aligned_alloc(uint32_t alignment, uint32_t size) {
    uint32_t address = (uint32_t)aligned_malloc(alignment, size);
    // Save to list of allocations for memory management.
    aligned_allocations_.add(address);
    return address;
  }
  uint32_t mem_aligned_alloc_and_set(uint32_t alignment, UInt8Array data) {
    // Allocate aligned memory.
    const uint32_t address = mem_aligned_alloc(alignment, data.length);
    if (!address) { return 0; }
    // Copy data to allocated memory.
    mem_cpy_host_to_device(address, data);
    return address;
  }
  void mem_aligned_free(uint32_t address) {
    for (int i = 0; i < aligned_allocations_.size(); i++) {
      if (aligned_allocations_.get(i) == address) {
        aligned_allocations_.remove(i);
      }
    }
    aligned_free((void *)address);
  }
  void mem_cpy_host_to_device(uint32_t address, UInt8Array data) {
    memcpy((uint8_t *)address, data.data, data.length);
  }
  void mem_fill_uint8(uint32_t address, uint8_t value, uint32_t size) {
    mem_fill((uint8_t *)address, value, size);
  }
  void mem_fill_uint16(uint32_t address, uint16_t value, uint32_t size) {
    mem_fill((uint16_t *)address, value, size);
  }
  void mem_fill_uint32(uint32_t address, uint32_t value, uint32_t size) {
    mem_fill((uint32_t *)address, value, size);
  }
  void mem_fill_float(uint32_t address, float value, uint32_t size) {
    mem_fill((float *)address, value, size);
  }
  void mem_free(uint32_t address) {
    for (int i = 0; i < allocations_.size(); i++) {
      if (allocations_.get(i) == address) { allocations_.remove(i); }
    }
    free((void *)address);
  }
  void reset_last_dma_channel_done() { last_dma_channel_done_ = -1; }
  void set_i2c_address(uint8_t value);  // Override to validate i2c address
  int8_t update_adc_registers(uint8_t adc_num, UInt8Array serialized_adc_msg) {
    return teensy::adc::update_registers(adc_num, serialized_adc_msg);
  }
  int8_t update_dma_mux_chcfg(uint8_t channel_num, UInt8Array serialized_mux) {
    return teensy::dma::update_mux_chcfg(channel_num, serialized_mux);
  }
  int8_t update_dma_registers(UInt8Array serialized_dma_msg) {
    return teensy::dma::update_registers(serialized_dma_msg);
  }
  int8_t update_dma_TCD(uint8_t channel_num, UInt8Array serialized_tcd) {
    return teensy::dma::update_TCD(channel_num, serialized_tcd);
  }
  int8_t update_pit_registers(UInt8Array serialized_pit_msg) {
    return teensy::pit::update_registers(serialized_pit_msg);
  }
  int8_t update_pit_timer_config(uint32_t index,
                                 UInt8Array serialized_config) {
    return teensy::pit::update_timer_config(index, serialized_config);
  }
  int8_t update_sim_SCGC6(UInt8Array serialized_scgc6) {
    return teensy::sim::update_SCGC6(serialized_scgc6);
  }
  int8_t update_sim_SCGC7(UInt8Array serialized_scgc7) {
    return teensy::sim::update_SCGC7(serialized_scgc7);
  }

  // ##########################################################################
  // # Teensy library mutator methods
  int analogRead(uint8_t pin, int8_t adc_num) {
  //! Returns the analog value of the pin.
  /** It waits until the value is read and then returns the result.
  * If a comparison has been set up and fails, it will return ADC_ERROR_VALUE.
  * This function is interrupt safe, so it will restore the adc to the state it was before being called
  * If more than one ADC exists, it will select the module with less workload, you can force a selection using
  * adc_num. If you select ADC1 in Teensy 3.0 it will return ADC_ERROR_VALUE.
  */
    if (adc_num == 0){return adc_->adc0->analogRead(pin);}
    else{return adc_->adc1->analogRead(pin);}
  }
  int analogReadContinuous(int8_t adc_num) {
  //! Reads the analog value of a continuous conversion.
  /** Set the continuous conversion with with analogStartContinuous(pin) or startContinuousDifferential(pinP, pinN).
  *   \return the last converted value.
  *   If single-ended and 16 bits it's necessary to typecast it to an unsigned type (like uint16_t),
  *   otherwise values larger than 3.3/2 V are interpreted as negative!
  */
    if (adc_num == 0){return adc_->adc0->analogReadContinuous();}
    else{return adc_->adc1->analogReadContinuous();}
  }
  int analogReadDifferential(uint8_t pinP, uint8_t pinN, int8_t adc_num) {
  //! Reads the differential analog value of two pins (pinP - pinN).
  /** It waits until the value is read and then returns the result.
  * If a comparison has been set up and fails, it will return ADC_ERROR_VALUE.
  * \param pinP must be A10 or A12.
  * \param pinN must be A11 (if pinP=A10) or A13 (if pinP=A12).
  * Other pins will return ADC_ERROR_VALUE.
  *
  * This function is interrupt safe, so it will restore the adc to the state it was before being called
  * If more than one ADC exists, it will select the module with less workload, you can force a selection using
  * adc_num
  */
    if (adc_num == 0){return adc_->adc0->analogReadDifferential(pinP, pinN);}
    else{return adc_->adc1->analogReadDifferential(pinP, pinN);}
  }
  void setAveraging(uint8_t num, int8_t adc_num) {
    //! Set the number of averages
    /*!
     * \param num can be 0, 4, 8, 16 or 32.
     */
    if (adc_num == 0){adc_->adc0->setAveraging(num);}
    else{adc_->adc1->setAveraging(num);}
  }
  void setConversionSpeed(uint8_t speed, int8_t adc_num) {
    //! Sets the conversion speed (changes the ADC clock, ADCK)
    /**
     * \param speed can be ADC_VERY_LOW_SPEED, ADC_LOW_SPEED, ADC_MED_SPEED, ADC_HIGH_SPEED_16BITS, ADC_HIGH_SPEED or ADC_VERY_HIGH_SPEED.
     *
     * ADC_VERY_LOW_SPEED is guaranteed to be the lowest possible speed within specs for resolutions less than 16 bits (higher than 1 MHz),
     * it's different from ADC_LOW_SPEED only for 24, 4 or 2 MHz bus frequency.
     * ADC_LOW_SPEED is guaranteed to be the lowest possible speed within specs for all resolutions (higher than 2 MHz).
     * ADC_MED_SPEED is always >= ADC_LOW_SPEED and <= ADC_HIGH_SPEED.
     * ADC_HIGH_SPEED_16BITS is guaranteed to be the highest possible speed within specs for all resolutions (lower or eq than 12 MHz).
     * ADC_HIGH_SPEED is guaranteed to be the highest possible speed within specs for resolutions less than 16 bits (lower or eq than 18 MHz).
     * ADC_VERY_HIGH_SPEED may be out of specs, it's different from ADC_HIGH_SPEED only for 48, 40 or 24 MHz bus frequency.
     *
     * Additionally the conversion speed can also be ADC_ADACK_2_4, ADC_ADACK_4_0, ADC_ADACK_5_2 and ADC_ADACK_6_2,
     * where the numbers are the frequency of the ADC clock (ADCK) in MHz and are independent on the bus speed.
     * This is useful if you are using the Teensy at a very low clock frequency but want faster conversions,
     * but if F_BUS<F_ADCK, you can't use ADC_VERY_HIGH_SPEED for sampling speed.
     *
     */
    ADC_CONVERSION_SPEED speed_;
    switch(speed) {
      case(0): speed_ = ADC_CONVERSION_SPEED::VERY_LOW_SPEED; break;
      case(1): speed_ = ADC_CONVERSION_SPEED::LOW_SPEED; break;
      case(2): speed_ = ADC_CONVERSION_SPEED::MED_SPEED; break;
      case(3): speed_ = ADC_CONVERSION_SPEED::HIGH_SPEED; break;
      case(4): speed_ = ADC_CONVERSION_SPEED::VERY_HIGH_SPEED; break;
      case(16): speed_ = ADC_CONVERSION_SPEED::ADACK_2_4; break;
      case(17): speed_ = ADC_CONVERSION_SPEED::ADACK_4_0; break;
      case(18): speed_ = ADC_CONVERSION_SPEED::ADACK_5_2; break;
      case(19): speed_ = ADC_CONVERSION_SPEED::ADACK_6_2; break;
      default: speed_ = ADC_CONVERSION_SPEED::VERY_LOW_SPEED;
    }
    if (adc_num == 0){adc_->adc0->setConversionSpeed(speed_);}
    else{adc_->adc1->setConversionSpeed(speed_);}
  }
  void setReference(uint8_t type, int8_t adc_num) {
    //! Set the voltage reference you prefer, default is 3.3 V (VCC)
    /*!
     * \param type can be ADC_REF_3V3, ADC_REF_1V2 (not for Teensy LC) or ADC_REF_EXT.
     *
     *  It recalibrates at the end.
     */
    ADC_REFERENCE type_;
    switch(type) {
      case(0): type_ = ADC_REFERENCE::REF_3V3; break;
      case(1): type_ = ADC_REFERENCE::REF_1V2; break;
      case(2): type_ = ADC_REFERENCE::REF_EXT; break;
      default: type_ = ADC_REFERENCE::REF_3V3;
    }
    if (adc_num == 0){adc_->adc0->setReference(type_);}
    else{adc_->adc1->setReference(type_);}
  }
  void setResolution(uint8_t bits, int8_t adc_num) {
    //! Change the resolution of the measurement.
    /*
     *  \param bits is the number of bits of resolution.
     *  For single-ended measurements: 8, 10, 12 or 16 bits.
     *  For differential measurements: 9, 11, 13 or 16 bits.
     *  If you want something in between (11 bits single-ended for example) select the inmediate higher
     *  and shift the result one to the right.
     *
     *  Whenever you change the resolution, change also the comparison values (if you use them).
     */
    if (adc_num == 0){adc_->adc0->setResolution(bits);}
    else{adc_->adc1->setResolution(bits);}
  }
  void setSamplingSpeed(uint8_t speed, int8_t adc_num) {
    //! Sets the sampling speed
    /** Increase the sampling speed for low impedance sources, decrease it for higher impedance ones.
     * \param speed can be ADC_VERY_LOW_SPEED, ADC_LOW_SPEED, ADC_MED_SPEED, ADC_HIGH_SPEED or ADC_VERY_HIGH_SPEED.
     *
     * ADC_VERY_LOW_SPEED is the lowest possible sampling speed (+24 ADCK).
     * ADC_LOW_SPEED adds +16 ADCK.
     * ADC_MED_SPEED adds +10 ADCK.
     * ADC_HIGH_SPEED (or ADC_HIGH_SPEED_16BITS) adds +6 ADCK.
     * ADC_VERY_HIGH_SPEED is the highest possible sampling speed (0 ADCK added).
     */
    ADC_SAMPLING_SPEED speed_;
    switch(speed) {
      case(0): speed_ = ADC_SAMPLING_SPEED::VERY_LOW_SPEED; break;
      case(1): speed_ = ADC_SAMPLING_SPEED::LOW_SPEED; break;
      case(2): speed_ = ADC_SAMPLING_SPEED::MED_SPEED; break;
      case(3): speed_ = ADC_SAMPLING_SPEED::HIGH_SPEED; break;
      case(4): speed_ = ADC_SAMPLING_SPEED::VERY_HIGH_SPEED; break;
      default: speed_ = ADC_SAMPLING_SPEED::VERY_LOW_SPEED;
    }
    if (adc_num == 0){adc_->adc0->setSamplingSpeed(speed_);}
    else{adc_->adc1->setSamplingSpeed(speed_);}
  }
  void disableCompare(int8_t adc_num) {
  //! Disable the compare function
    if (adc_num == 0){adc_->adc0->disableCompare();}
    else{adc_->adc1->disableCompare();}
  }
  void disableDMA(int8_t adc_num) {
  //! Disable ADC DMA request
    if (adc_num == 0){adc_->adc0->disableDMA();}
    else{adc_->adc1->disableDMA();}
  }
  void disableInterrupts(int8_t adc_num) {
  //! Disable interrupts
    if (adc_num == 0){adc_->adc0->disableInterrupts();}
    else{adc_->adc1->disableInterrupts();}
  }
  void disablePGA(int8_t adc_num) {
  //! Disable PGA
    if (adc_num == 0){adc_->adc0->disablePGA();}
    else{adc_->adc1->disablePGA();}
  }
  void enableCompare(int16_t compValue, bool greaterThan, int8_t adc_num) {
  //! Enable the compare function to a single value
  /** A conversion will be completed only when the ADC value
  *  is >= compValue (greaterThan=1) or < compValue (greaterThan=0)
  *  Call it after changing the resolution
  *  Use with interrupts or poll conversion completion with isComplete()
  */
    if (adc_num == 0){adc_->adc0->enableCompare(compValue, greaterThan);}
    else{adc_->adc1->enableCompare(compValue, greaterThan);}
  }
  void enableCompareRange(int16_t lowerLimit, int16_t upperLimit, bool insideRange, bool inclusive, int8_t adc_num) {
  //! Enable the compare function to a range
  /** A conversion will be completed only when the ADC value is inside (insideRange=1) or outside (=0)
  *  the range given by (lowerLimit, upperLimit),including (inclusive=1) the limits or not (inclusive=0).
  *  See Table 31-78, p. 617 of the freescale manual.
  *  Call it after changing the resolution
  *  Use with interrupts or poll conversion completion with isComplete()
  */
    if (adc_num == 0){adc_->adc0->enableCompareRange(lowerLimit, upperLimit, insideRange, inclusive);}
    else{adc_->adc1->enableCompareRange(lowerLimit, upperLimit, insideRange, inclusive);}
  }
  void enableDMA(int8_t adc_num) {
  //! Enable DMA request
  /** An ADC DMA request will be raised when the conversion is completed
  *  (including hardware averages and if the comparison (if any) is true).
  */
    if (adc_num == 0){adc_->adc0->enableDMA();}
    else{adc_->adc1->enableDMA();}
  }
  void enableInterrupts(int8_t adc_num, uint8_t priority = 255) {
  //! Enable interrupts
  /** An IRQ_ADC0 Interrupt will be raised when the conversion is completed
  *  (including hardware averages and if the comparison (if any) is true).
  * @param isr function (returns void and accepts no arguments) that will be executed after an interrupt.
  * @param priority Interrupt priority, highest is 0, lowest is 255.
  */
    if (adc_num == 0){adc_->adc0->enableInterrupts(nullptr, priority);}
    else{adc_->adc1->enableInterrupts(nullptr, priority);}
  }
  void enablePGA(uint8_t gain, int8_t adc_num) {
  //! Enable and set PGA
  /** Enables the PGA and sets the gain
  *   Use only for signals lower than 1.2 V
  *   \param gain can be 1, 2, 4, 8, 16, 32 or 64
  *
  */
    if (adc_num == 0){adc_->adc0->enablePGA(gain);}
    else{adc_->adc1->enablePGA(gain);}
  }
  int readSingle(int8_t adc_num) {
  //! Reads the analog value of a single conversion.
  /** Set the conversion with with startSingleRead(pin) or startSingleDifferential(pinP, pinN).
  *   \return the converted value.
  */
    if (adc_num == 0){return adc_->adc0->readSingle();}
    else{return adc_->adc1->readSingle();}
  }
  bool startContinuous(uint8_t pin, int8_t adc_num) {
  //! Starts continuous conversion on the pin.
  /** It returns as soon as the ADC is set, use analogReadContinuous() to read the value.
  */
    if (adc_num == 0){return adc_->adc0->startContinuous(pin);}
    else{return adc_->adc1->startContinuous(pin);}
  }
  bool startContinuousDifferential(uint8_t pinP, uint8_t pinN, int8_t adc_num) {
  //! Starts continuous conversion between the pins (pinP-pinN).
  /** It returns as soon as the ADC is set, use analogReadContinuous() to read the value.
  * \param pinP must be A10 or A12.
  * \param pinN must be A11 (if pinP=A10) or A13 (if pinP=A12).
  * Other pins will return ADC_ERROR_DIFF_VALUE.
  */
    if (adc_num == 0){return adc_->adc0->startContinuousDifferential(pinP, pinN);}
    else{return adc_->adc1->startContinuousDifferential(pinP, pinN);}
  }
  bool startSingleDifferential(uint8_t pinP, uint8_t pinN, int8_t adc_num) {
  //! Start a differential conversion between two pins (pinP - pinN) and enables interrupts.
  /** It returns inmediately, get value with readSingle().
  *   \param pinP must be A10 or A12.
  *   \param pinN must be A11 (if pinP=A10) or A13 (if pinP=A12).
  *
  *   Other pins will return ADC_ERROR_DIFF_VALUE.
  *   If this function interrupts a measurement, it stores the settings in adc_config
  */
    if (adc_num == 0){return adc_->adc0->startSingleDifferential(pinP, pinN);}
    else{return adc_->adc1->startSingleDifferential(pinP, pinN);}
  }
  bool startSingleRead(uint8_t pin, int8_t adc_num) {
  //! Starts an analog measurement on the pin and enables interrupts.
  /** It returns inmediately, get value with readSingle().
  *   If the pin is incorrect it returns ADC_ERROR_VALUE
  *   If this function interrupts a measurement, it stores the settings in adc_config
  */
    if (adc_num == 0){return adc_->adc0->startSingleRead(pin);}
    else{return adc_->adc1->startSingleRead(pin);}
  }
  void stopContinuous(int8_t adc_num) {
  //! Stops continuous conversion
    if (adc_num == 0){adc_->adc0->stopContinuous();}
    else{adc_->adc1->stopContinuous();}
  }

  // ##########################################################################
  // # Teensy library accessor methods
  uint32_t getMaxValue(int8_t adc_num) {
  //! Returns the maximum value for a measurement: 2^res-1.
    if (adc_num == 0){return adc_->adc0->getMaxValue();}
    else{return adc_->adc1->getMaxValue();}
  }
  uint8_t getPGA(int8_t adc_num) {
  //! Returns the PGA level
  /** PGA level = from 1 to 64
  */
    if (adc_num == 0){return adc_->adc0->getPGA();}
    else{return adc_->adc1->getPGA();}
  }
  uint8_t getResolution(int8_t adc_num) {
  //! Returns the resolution of the ADC_Module.
    if (adc_num == 0){return adc_->adc0->getResolution();}
    else{return adc_->adc1->getResolution();}
  }
  bool isComplete(int8_t adc_num) {
  //! Is an ADC conversion ready?
  /**
  *  \return 1 if yes, 0 if not.
  *  When a value is read this function returns 0 until a new value exists
  *  So it only makes sense to call it with continuous or non-blocking methods
  */
    if (adc_num == 0){return adc_->adc0->isComplete();}
    else{return adc_->adc1->isComplete();}
  }
  bool isContinuous(int8_t adc_num) {
  //! Is the ADC in continuous mode?
    if (adc_num == 0){return adc_->adc0->isContinuous();}
    else{return adc_->adc1->isContinuous();}
  }
  bool isConverting(int8_t adc_num) {
  //! Is the ADC converting at the moment?
    if (adc_num == 0){return adc_->adc0->isConverting();}
    else{return adc_->adc1->isConverting();}
  }
  bool isDifferential(int8_t adc_num) {
  //! Is the ADC in differential mode?
    if (adc_num == 0){return adc_->adc0->isDifferential();}
    else{return adc_->adc1->isDifferential();}
  }
};

}  // namespace teensy_minimal_rpc


#endif  // #ifndef ___NODE__H___
