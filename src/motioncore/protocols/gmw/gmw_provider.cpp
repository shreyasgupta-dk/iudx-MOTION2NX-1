// MIT License
//
// Copyright (c) 2020 Lennart Braun
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <cstdint>
#include <unordered_map>
#include "gmw_provider.h"

#include "base/gate_register.h"
#include "communication/communication_layer.h"
#include "communication/fbs_headers/gmw_message_generated.h"
#include "communication/message.h"
#include "communication/message_handler.h"
#include "conversion.h"
#include "crypto/motion_base_provider.h"
#include "crypto/multiplication_triple/mt_provider.h"
#include "crypto/multiplication_triple/sp_provider.h"
#include "gate.h"
#include "utility/constants.h"
#include "utility/logger.h"
#include "utility/meta.hpp"
#include "wire.h"

namespace MOTION::proto::gmw {

GMWProvider::GMWProvider(Communication::CommunicationLayer& communication_layer,
                         GateRegister& gate_register,
                         Crypto::MotionBaseProvider& motion_base_provider, MTProvider& mt_provider,
                         SPProvider& sp_provider, SBProvider& sb_provider,
                         std::shared_ptr<Logger> logger)
    : CommMixin(communication_layer, Communication::MessageType::GMWGate, logger),
      communication_layer_(communication_layer),
      gate_register_(gate_register),
      motion_base_provider_(motion_base_provider),
      mt_provider_(mt_provider),
      sp_provider_(sp_provider),
      sb_provider_(sb_provider),
      my_id_(communication_layer_.get_my_id()),
      num_parties_(communication_layer_.get_num_parties()),
      next_input_id_(0),
      logger_(std::move(logger)) {}

GMWProvider::~GMWProvider() = default;

void GMWProvider::setup() {
  motion_base_provider_.wait_for_setup();
  mt_provider_.WaitFinished();
  sp_provider_.WaitFinished();
  set_setup_ready();
}

bool GMWProvider::is_my_job(std::size_t gate_id) const noexcept {
  return my_id_ == (gate_id % num_parties_);
}

std::size_t GMWProvider::get_next_input_id(std::size_t num_inputs) noexcept {
  auto next_id = next_input_id_;
  next_input_id_ += num_inputs;
  return next_id;
}

static BooleanGMWWireVector cast_wires(std::vector<std::shared_ptr<NewWire>> wires) {
  BooleanGMWWireVector result(wires.size());
  std::transform(std::begin(wires), std::end(wires), std::begin(result),
                 [](auto& w) { return std::dynamic_pointer_cast<BooleanGMWWire>(w); });
  return result;
}

static std::vector<std::shared_ptr<NewWire>> cast_wires(BooleanGMWWireVector&& wires) {
  return std::vector<std::shared_ptr<NewWire>>(std::begin(wires), std::end(wires));
}

template <typename T>
static ArithmeticGMWWireP<T> cast_arith_wire(std::shared_ptr<NewWire> wire) {
  auto ptr = std::dynamic_pointer_cast<ArithmeticGMWWire<T>>(wire);
  assert(ptr);
  return ptr;
}

template <typename T>
static std::shared_ptr<NewWire> cast_arith_wire(ArithmeticGMWWireP<T> wire) {
  return std::shared_ptr<NewWire>(wire);
}

// Boolean inputs/outputs

std::pair<ENCRYPTO::ReusableFiberPromise<BitValues>, WireVector>
GMWProvider::make_boolean_input_gate_my(std::size_t input_owner, std::size_t num_wires,
                                        std::size_t num_simd) {
  if (input_owner != my_id_) {
    throw std::logic_error("trying to create input gate for wrong party");
  }
  BooleanGMWWireVector output;
  ENCRYPTO::ReusableFiberPromise<std::vector<ENCRYPTO::BitVector<>>> promise;
  auto gate_id = gate_register_.get_next_gate_id();
  auto gate = std::make_unique<BooleanGMWInputGateSender>(gate_id, *this, num_wires, num_simd,
                                                          promise.get_future());
  output = gate->get_output_wires();
  gate_register_.register_gate(std::move(gate));
  return {std::move(promise), cast_wires(std::move(output))};
}

WireVector GMWProvider::make_boolean_input_gate_other(std::size_t input_owner,
                                                      std::size_t num_wires, std::size_t num_simd) {
  if (input_owner == my_id_) {
    throw std::logic_error("trying to create input gate for wrong party");
  }
  BooleanGMWWireVector output;
  auto gate_id = gate_register_.get_next_gate_id();
  auto gate = std::make_unique<BooleanGMWInputGateReceiver>(gate_id, *this, num_wires, num_simd,
                                                            input_owner);
  output = gate->get_output_wires();
  gate_register_.register_gate(std::move(gate));
  return cast_wires(std::move(output));
}

ENCRYPTO::ReusableFiberFuture<BitValues> GMWProvider::make_boolean_output_gate_my(
    std::size_t output_owner, const WireVector& in) {
  if (output_owner != ALL_PARTIES && output_owner != my_id_) {
    throw std::logic_error("trying to create output gate for wrong party");
  }
  auto gate_id = gate_register_.get_next_gate_id();
  auto input = cast_wires(in);
  auto gate =
      std::make_unique<BooleanGMWOutputGate>(gate_id, *this, std::move(input), output_owner);
  auto future = gate->get_output_future();
  gate_register_.register_gate(std::move(gate));
  return future;
}

void GMWProvider::make_boolean_output_gate_other(std::size_t output_owner, const WireVector& in) {
  if (output_owner == ALL_PARTIES || output_owner == my_id_) {
    throw std::logic_error("trying to create output gate for wrong party");
  }
  auto gate_id = gate_register_.get_next_gate_id();
  auto input = cast_wires(in);
  auto gate =
      std::make_unique<BooleanGMWOutputGate>(gate_id, *this, std::move(input), output_owner);
  gate_register_.register_gate(std::move(gate));
}

// arithmetic inputs/outputs

template <typename T>
std::pair<ENCRYPTO::ReusableFiberPromise<IntegerValues<T>>, WireVector>
GMWProvider::basic_make_arithmetic_input_gate_my(std::size_t input_owner, std::size_t num_simd) {
  if (input_owner != my_id_) {
    throw std::logic_error("trying to create input gate for wrong party");
  }
  ENCRYPTO::ReusableFiberPromise<std::vector<T>> promise;
  auto gate_id = gate_register_.get_next_gate_id();
  auto gate = std::make_unique<ArithmeticGMWInputGateSender<T>>(gate_id, *this, num_simd,
                                                                promise.get_future());
  auto output = gate->get_output_wire();
  gate_register_.register_gate(std::move(gate));
  return {std::move(promise), {cast_arith_wire(std::move(output))}};
}

std::pair<ENCRYPTO::ReusableFiberPromise<IntegerValues<std::uint8_t>>, WireVector>
GMWProvider::make_arithmetic_8_input_gate_my(std::size_t input_owner, std::size_t num_simd) {
  return basic_make_arithmetic_input_gate_my<std::uint8_t>(input_owner, num_simd);
}
std::pair<ENCRYPTO::ReusableFiberPromise<IntegerValues<std::uint16_t>>, WireVector>
GMWProvider::make_arithmetic_16_input_gate_my(std::size_t input_owner, std::size_t num_simd) {
  return basic_make_arithmetic_input_gate_my<std::uint16_t>(input_owner, num_simd);
}
std::pair<ENCRYPTO::ReusableFiberPromise<IntegerValues<std::uint32_t>>, WireVector>
GMWProvider::make_arithmetic_32_input_gate_my(std::size_t input_owner, std::size_t num_simd) {
  return basic_make_arithmetic_input_gate_my<std::uint32_t>(input_owner, num_simd);
}
std::pair<ENCRYPTO::ReusableFiberPromise<IntegerValues<std::uint64_t>>, WireVector>
GMWProvider::make_arithmetic_64_input_gate_my(std::size_t input_owner, std::size_t num_simd) {
  return basic_make_arithmetic_input_gate_my<std::uint64_t>(input_owner, num_simd);
}

template <typename T>
WireVector GMWProvider::basic_make_arithmetic_input_gate_other(std::size_t input_owner,
                                                               std::size_t num_simd) {
  if (input_owner == my_id_) {
    throw std::logic_error("trying to create input gate for wrong party");
  }
  auto gate_id = gate_register_.get_next_gate_id();
  auto gate =
      std::make_unique<ArithmeticGMWInputGateReceiver<T>>(gate_id, *this, num_simd, input_owner);
  auto output = gate->get_output_wire();
  gate_register_.register_gate(std::move(gate));
  return {cast_arith_wire(std::move(output))};
}

WireVector GMWProvider::make_arithmetic_8_input_gate_other(std::size_t input_owner,
                                                           std::size_t num_simd) {
  return basic_make_arithmetic_input_gate_other<std::uint8_t>(input_owner, num_simd);
}
WireVector GMWProvider::make_arithmetic_16_input_gate_other(std::size_t input_owner,
                                                            std::size_t num_simd) {
  return basic_make_arithmetic_input_gate_other<std::uint16_t>(input_owner, num_simd);
}
WireVector GMWProvider::make_arithmetic_32_input_gate_other(std::size_t input_owner,
                                                            std::size_t num_simd) {
  return basic_make_arithmetic_input_gate_other<std::uint32_t>(input_owner, num_simd);
}
WireVector GMWProvider::make_arithmetic_64_input_gate_other(std::size_t input_owner,
                                                            std::size_t num_simd) {
  return basic_make_arithmetic_input_gate_other<std::uint64_t>(input_owner, num_simd);
}

template <typename T>
ENCRYPTO::ReusableFiberFuture<IntegerValues<T>> GMWProvider::basic_make_arithmetic_output_gate_my(
    std::size_t output_owner, const WireVector& in) {
  if (output_owner != ALL_PARTIES && output_owner != my_id_) {
    throw std::logic_error("trying to create output gate for wrong party");
  }
  if (in.size() != 1) {
    throw std::logic_error("invalid number of wires for arithmetic gate");
  }
  auto input = cast_arith_wire<T>(in[0]);
  if (input == nullptr) {
    throw std::logic_error("wrong wire type");
  }
  auto gate_id = gate_register_.get_next_gate_id();
  auto gate =
      std::make_unique<ArithmeticGMWOutputGate<T>>(gate_id, *this, std::move(input), output_owner);
  auto future = gate->get_output_future();
  gate_register_.register_gate(std::move(gate));
  return future;
}

ENCRYPTO::ReusableFiberFuture<IntegerValues<std::uint8_t>>
GMWProvider::make_arithmetic_8_output_gate_my(std::size_t output_owner, const WireVector& in) {
  return basic_make_arithmetic_output_gate_my<std::uint8_t>(output_owner, in);
}
ENCRYPTO::ReusableFiberFuture<IntegerValues<std::uint16_t>>
GMWProvider::make_arithmetic_16_output_gate_my(std::size_t output_owner, const WireVector& in) {
  return basic_make_arithmetic_output_gate_my<std::uint16_t>(output_owner, in);
}
ENCRYPTO::ReusableFiberFuture<IntegerValues<std::uint32_t>>
GMWProvider::make_arithmetic_32_output_gate_my(std::size_t output_owner, const WireVector& in) {
  return basic_make_arithmetic_output_gate_my<std::uint32_t>(output_owner, in);
}
ENCRYPTO::ReusableFiberFuture<IntegerValues<std::uint64_t>>
GMWProvider::make_arithmetic_64_output_gate_my(std::size_t output_owner, const WireVector& in) {
  return basic_make_arithmetic_output_gate_my<std::uint64_t>(output_owner, in);
}

void GMWProvider::make_arithmetic_output_gate_other(std::size_t output_owner,
                                                    const WireVector& in) {
  if (output_owner == ALL_PARTIES || output_owner == my_id_) {
    throw std::logic_error("trying to create output gate for wrong party");
  }
  if (in.size() != 1) {
    throw std::logic_error("invalid number of wires for arithmetic gate");
  }
  std::unique_ptr<NewGate> gate;
  auto gate_id = gate_register_.get_next_gate_id();
  switch (in[0]->get_bit_size()) {
    case 8: {
      gate = std::make_unique<ArithmeticGMWOutputGate<std::uint8_t>>(
          gate_id, *this, cast_arith_wire<std::uint8_t>(in[0]), output_owner);
      break;
    }
    case 16: {
      gate = std::make_unique<ArithmeticGMWOutputGate<std::uint16_t>>(
          gate_id, *this, cast_arith_wire<std::uint16_t>(in[0]), output_owner);
      break;
    }
    case 32: {
      gate = std::make_unique<ArithmeticGMWOutputGate<std::uint32_t>>(
          gate_id, *this, cast_arith_wire<std::uint32_t>(in[0]), output_owner);
      break;
    }
    case 64: {
      gate = std::make_unique<ArithmeticGMWOutputGate<std::uint64_t>>(
          gate_id, *this, cast_arith_wire<std::uint64_t>(in[0]), output_owner);
      break;
    }
    default: {
      throw std::logic_error("unsupprted bit size");
    }
  }
  gate_register_.register_gate(std::move(gate));
}

std::vector<std::shared_ptr<NewWire>> GMWProvider::make_unary_gate(
    ENCRYPTO::PrimitiveOperationType op, const std::vector<std::shared_ptr<NewWire>>& in_a) {
  switch (op) {
    case ENCRYPTO::PrimitiveOperationType::INV:
      return make_inv_gate(in_a);
    case ENCRYPTO::PrimitiveOperationType::NEG:
      return make_neg_gate(in_a);
    case ENCRYPTO::PrimitiveOperationType::SQR:
      return make_sqr_gate(in_a);
    default:
      throw std::logic_error(fmt::format("GMW does not support the unary operation {}", op));
  }
}

std::vector<std::shared_ptr<NewWire>> GMWProvider::make_binary_gate(
    ENCRYPTO::PrimitiveOperationType op, const std::vector<std::shared_ptr<NewWire>>& in_a,
    const std::vector<std::shared_ptr<NewWire>>& in_b) {
  switch (op) {
    case ENCRYPTO::PrimitiveOperationType::XOR:
      return make_xor_gate(in_a, in_b);
    case ENCRYPTO::PrimitiveOperationType::AND:
      return make_and_gate(in_a, in_b);
    case ENCRYPTO::PrimitiveOperationType::ADD:
      return make_add_gate(in_a, in_b);
    case ENCRYPTO::PrimitiveOperationType::MUL:
      return make_mul_gate(in_a, in_b);
    default:
      throw std::logic_error(fmt::format("GMW does not support the binary operation {}", op));
  }
}

WireVector GMWProvider::make_inv_gate(const WireVector& in_a) {
  auto gate_id = gate_register_.get_next_gate_id();
  auto gate = std::make_unique<BooleanGMWINVGate>(gate_id, *this, cast_wires(in_a));
  auto output = gate->get_output_wires();
  gate_register_.register_gate(std::move(gate));
  return cast_wires(std::move(output));
}

WireVector GMWProvider::make_xor_gate(const WireVector&in_a,
                                                const WireVector& in_b) {
  BooleanGMWWireVector output;
  auto gate_id = gate_register_.get_next_gate_id();
  auto gate = std::make_unique<BooleanGMWXORGate>(gate_id, cast_wires(in_a), cast_wires(in_b));
  output = gate->get_output_wires();
  gate_register_.register_gate(std::move(gate));
  return cast_wires(std::move(output));
}

WireVector GMWProvider::make_and_gate(const WireVector& in_a,
                                                const WireVector& in_b) {
  BooleanGMWWireVector output;
  auto gate_id = gate_register_.get_next_gate_id();
  auto gate = std::make_unique<BooleanGMWANDGate>(gate_id, *this, cast_wires(in_a), cast_wires(in_b));
  output = gate->get_output_wires();
  gate_register_.register_gate(std::move(gate));
  return cast_wires(std::move(output));
}

static std::size_t check_arithmetic_wire(const WireVector& in) {
  if (in.size() != 1) {
    throw std::logic_error("arithmetic operations support single wires only");
  }
  return in[0]->get_bit_size();
}

template <template <typename> class UnaryGate, typename T>
WireVector GMWProvider::make_arithmetic_unary_gate(const NewWireP& in_a) {
  auto gate_id = gate_register_.get_next_gate_id();
  auto gate = std::make_unique<UnaryGate<T>>(
      gate_id, *this, cast_arith_wire<T>(in_a));
  auto output = {cast_arith_wire(gate->get_output_wire())};
  gate_register_.register_gate(std::move(gate));
  return output;
}

template <template <typename> class UnaryGate>
WireVector GMWProvider::make_arithmetic_unary_gate(const WireVector& in_a) {
  auto bit_size = check_arithmetic_wire(in_a);
  switch (bit_size) {
    case 8:
      return make_arithmetic_unary_gate<UnaryGate, std::uint8_t>(in_a[0]);
    case 16:
      return make_arithmetic_unary_gate<UnaryGate, std::uint16_t>(in_a[0]);
    case 32:
      return make_arithmetic_unary_gate<UnaryGate, std::uint32_t>(in_a[0]);
    case 64:
      return make_arithmetic_unary_gate<UnaryGate, std::uint64_t>(in_a[0]);
    default:
      throw std::logic_error(fmt::format("unexpected bit size {}", bit_size));
  }
}

static std::size_t check_arithmetic_wires(const WireVector& in_a, const WireVector& in_b) {
  if (in_a.size() != 1 || in_b.size() != 1) {
    throw std::logic_error("arithmetic operations support single wires only");
  }
  auto bit_size = in_a[0]->get_bit_size();
  if (bit_size != in_b[0]->get_bit_size()) {
    throw std::logic_error("different bit sizes on wires");
  }
  return bit_size;
}

template <template <typename> class BinaryGate, typename T>
WireVector GMWProvider::make_arithmetic_binary_gate(const NewWireP& in_a, const NewWireP& in_b) {
  auto gate_id = gate_register_.get_next_gate_id();
  auto gate = std::make_unique<BinaryGate<T>>(
      gate_id, *this, cast_arith_wire<T>(in_a), cast_arith_wire<T>(in_b));
  auto output = {cast_arith_wire(gate->get_output_wire())};
  gate_register_.register_gate(std::move(gate));
  return output;
}

template <template <typename> class BinaryGate>
WireVector GMWProvider::make_arithmetic_binary_gate(const WireVector& in_a,
                                                    const WireVector& in_b) {
  auto bit_size = check_arithmetic_wires(in_a, in_b);
  switch (bit_size) {
    case 8:
      return make_arithmetic_binary_gate<BinaryGate, std::uint8_t>(in_a[0], in_b[0]);
    case 16:
      return make_arithmetic_binary_gate<BinaryGate, std::uint16_t>(in_a[0], in_b[0]);
    case 32:
      return make_arithmetic_binary_gate<BinaryGate, std::uint32_t>(in_a[0], in_b[0]);
    case 64:
      return make_arithmetic_binary_gate<BinaryGate, std::uint64_t>(in_a[0], in_b[0]);
    default:
      throw std::logic_error(fmt::format("unexpected bit size {}", bit_size));
  }
}

WireVector GMWProvider::make_neg_gate(const WireVector& in) {
  return make_arithmetic_unary_gate<ArithmeticGMWNEGGate>(in);
}

WireVector GMWProvider::make_add_gate(const WireVector& in_a, const WireVector& in_b) {
  return make_arithmetic_binary_gate<ArithmeticGMWADDGate>(in_a, in_b);
}

WireVector GMWProvider::make_mul_gate(const WireVector& in_a, const WireVector& in_b) {
  return make_arithmetic_binary_gate<ArithmeticGMWMULGate>(in_a, in_b);
}

WireVector GMWProvider::make_sqr_gate(const WireVector& in) {
  return make_arithmetic_unary_gate<ArithmeticGMWSQRGate>(in);
}

template <typename T>
WireVector GMWProvider::basic_make_convert_to_arithmetic_gmw_gate(BooleanGMWWireVector&& in_a) {
  [[maybe_unused]] auto num_wires = in_a.size();
  assert(num_wires == ENCRYPTO::bit_size_v<T>);
  auto gate_id = gate_register_.get_next_gate_id();
  auto gate = std::make_unique<BooleanToArithmeticGMWGate<T>>(gate_id, *this, std::move(in_a));
  auto output = gate->get_output_wire();
  gate_register_.register_gate(std::move(gate));
  return {std::dynamic_pointer_cast<NewWire>(output)};
}

WireVector GMWProvider::make_convert_to_arithmetic_gmw_gate(BooleanGMWWireVector &&in_a) {
  auto bit_size = in_a.size();
  switch (bit_size) {
    case 8:
      return basic_make_convert_to_arithmetic_gmw_gate<std::uint8_t>(std::move(in_a));
    case 16:
      return basic_make_convert_to_arithmetic_gmw_gate<std::uint16_t>(std::move(in_a));
    case 32:
      return basic_make_convert_to_arithmetic_gmw_gate<std::uint32_t>(std::move(in_a));
    case 64:
      return basic_make_convert_to_arithmetic_gmw_gate<std::uint64_t>(std::move(in_a));
    default:
      throw std::logic_error(fmt::format("unsupported bit size {} for Boolean to Arithmetic GMW conversion\n", bit_size));
  }
}

WireVector GMWProvider::convert_boolean(MPCProtocol proto, const WireVector &in) {
  auto input = cast_wires(in);

  switch (proto) {
    case MPCProtocol::ArithmeticGMW:
      return make_convert_to_arithmetic_gmw_gate(std::move(input));
    default:
      throw std::logic_error(fmt::format("GMW does not support conversion to {}", ToString(proto)));
  }
}

WireVector GMWProvider::convert(MPCProtocol proto, const WireVector &in) {
  auto input = cast_wires(in);
  assert(input.size() > 0);
  auto src_proto = input.at(0)->get_protocol();

  switch (src_proto) {
    case MPCProtocol::ArithmeticGMW:
      throw std::logic_error("not yet implemented");
    case MPCProtocol::BooleanGMW:
      return convert_boolean(proto, in);
    default:
      throw std::logic_error("expected GMW protocol");
  }
}

}  // namespace MOTION::proto::gmw