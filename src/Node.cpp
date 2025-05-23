#include "Node.h"

namespace teensy_minimal_rpc {

void Node::begin() {
  config_.set_buffer(get_buffer());
  config_.validator_.set_node(*this);
  config_.reset();
  config_.load();
  state_.set_buffer(get_buffer());
  state_.validator_.set_node(*this);
  state_.reset();
#if !defined(DISABLE_SERIAL)
  // Start Serial after loading config to set baud rate.
  Serial.begin(115200);
#endif  // #ifndef DISABLE_SERIAL
  adc_ = new ADC();
  if (config_._.i2c_address > 0) { Wire.setClock(400000); }
}


void Node::set_i2c_address(uint8_t value) {
  // Convert to uint32_t as required by the method
  uint32_t address = value;
  
  // Use the inherited method to validate and apply the I2C address
  if (on_config_i2c_address_changed(address)) {
    // If address is valid, update the configuration
    config_._.i2c_address = address;
  }
}

}  // namespace teensy_minimal_rpc
