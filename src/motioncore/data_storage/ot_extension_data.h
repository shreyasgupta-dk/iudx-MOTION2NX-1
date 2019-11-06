// MIT License
//
// Copyright (c) 2019 Oleksandr Tkachenko, Lennart Braun
// Cryptography and Privacy Engineering Group (ENCRYPTO)
// TU Darmstadt, Germany
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

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "utility/bit_matrix.h"
#include "utility/bit_vector.h"

namespace ENCRYPTO {
class Condition;
}

namespace MOTION {

enum OTExtensionDataType : uint {
  rcv_masks = 0,
  rcv_corrections = 1,
  snd_messages = 2,
  OTExtension_invalid_data_type = 3
};

struct OTExtensionReceiverData {
  OTExtensionReceiverData();
  ~OTExtensionReceiverData() = default;

  std::shared_ptr<ENCRYPTO::BitMatrix> T_;

  // if many OTs are received in batches, it is not necessary to store all of the flags
  // for received messages but only for the first OT id in the batch. Thus, use a hash table.
  std::unordered_set<std::size_t> received_outputs_;
  std::vector<ENCRYPTO::BitVector<>> outputs_;
  std::unordered_map<std::size_t, std::unique_ptr<ENCRYPTO::Condition>> output_conds_;
  std::mutex received_outputs_mutex_;

  std::unordered_map<std::size_t, std::size_t> num_messages_;
  std::unordered_set<std::size_t> xor_correlation_;
  std::vector<std::size_t> bitlengths_;

  std::unique_ptr<ENCRYPTO::BitVector<>> real_choices_;
  std::unordered_map<std::size_t, std::unique_ptr<ENCRYPTO::Condition>> real_choices_cond_;
  std::unordered_set<std::size_t> set_real_choices_;
  std::mutex real_choices_mutex_;

  std::unique_ptr<ENCRYPTO::AlignedBitVector> random_choices_;

  std::unordered_map<std::size_t, std::size_t> num_ots_in_batch_;

  std::unique_ptr<ENCRYPTO::Condition> setup_finished_cond_;
  std::atomic<bool> setup_finished_{false};
};

struct OTExtensionSenderData {
  OTExtensionSenderData();
  ~OTExtensionSenderData() = default;

  std::size_t bit_size_{0};
  /// receiver's mask that are needed to construct matrix @param V_
  std::array<ENCRYPTO::AlignedBitVector, 128> u_;
  std::queue<std::size_t> received_u_ids_;
  std::size_t num_u_received_{0};
  std::unique_ptr<ENCRYPTO::Condition> received_u_condition_;

  std::shared_ptr<ENCRYPTO::BitMatrix> V_;

  // offset, num_ots
  std::unordered_map<std::size_t, std::size_t> num_ots_in_batch_;

  // corrections for GOTs, i.e., if random choice bit is not the real choice bit
  // send 1 to flip the messages before encoding or 0 otherwise for each GOT
  std::unordered_set<std::size_t> received_correction_offsets_;
  std::unordered_map<std::size_t, std::unique_ptr<ENCRYPTO::Condition>>
      received_correction_offsets_cond_;
  ENCRYPTO::BitVector<> corrections_;
  mutable std::mutex corrections_mutex_;

  // output buffer
  std::vector<ENCRYPTO::BitVector<>> y0_, y1_;
  std::vector<std::size_t> bitlengths_;

  std::unique_ptr<ENCRYPTO::Condition> setup_finished_cond_;
  std::atomic<bool> setup_finished_{false};
};

struct OTExtensionData {
  void MessageReceived(const std::uint8_t* message, const OTExtensionDataType type,
                       const std::size_t ot_id = 0);

  OTExtensionReceiverData& GetReceiverData() { return receiver_data_; }
  const OTExtensionReceiverData& GetReceiverData() const { return receiver_data_; }
  OTExtensionSenderData& GetSenderData() { return sender_data_; }
  const OTExtensionSenderData& GetSenderData() const { return sender_data_; }

  OTExtensionReceiverData receiver_data_;
  OTExtensionSenderData sender_data_;
};

}  // namespace MOTION