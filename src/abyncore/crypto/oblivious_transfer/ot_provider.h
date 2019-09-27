// MIT License
//
// Copyright (c) 2019 Oleksandr Tkachenko
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
#include <memory>
#include <unordered_map>

#include "flatbuffers/flatbuffers.h"

#include "utility/bit_vector.h"

namespace ABYN {
class DataStorage;
}

namespace ENCRYPTO {

namespace ObliviousTransfer {

enum OTProtocol : uint {
  GOT = 0,   // general OT
  ROT = 1,   // random OT
  XCOT = 2,  // XOR-correlated OT
  ACOT = 3,  // additively-correlated OT
  invalid_OT = 4
};

class OTVector {
 public:
  OTVector() = delete;

  std::size_t GetOtId() const noexcept { return ot_id_; }
  std::size_t GetNumOTs() const noexcept { return num_ots_; }
  std::size_t GetBitlen() const noexcept { return bitlen_; }
  OTProtocol GetProtocol() const noexcept { return p_; }

 protected:
  OTVector(const std::size_t ot_id, const std::size_t vector_id, const std::size_t num_ots,
           const std::size_t bitlen, const OTProtocol p,
           const std::shared_ptr<ABYN::DataStorage> &data_storage,
           const std::function<void(flatbuffers::FlatBufferBuilder &&)> &Send);

  const std::size_t ot_id_, vector_id_, num_ots_, bitlen_;
  const OTProtocol p_;

  std::shared_ptr<ABYN::DataStorage> data_storage_;
  std::function<void(flatbuffers::FlatBufferBuilder &&)> Send_;
};

class OTVectorSender : public OTVector {
 public:
  const std::vector<BitVector<>> &GetInputs() const { return inputs_; };
  virtual const std::vector<BitVector<>> &GetOutputs();

  virtual void SetInputs(const std::vector<BitVector<>> &v) = 0;
  virtual void SetInputs(std::vector<BitVector<>> &&v) = 0;

  virtual void SendMessages() = 0;

  void WaitSetup();

 protected:
  OTVectorSender(const std::size_t ot_id, const std::size_t vector_id, const std::size_t num_ots,
                 const std::size_t bitlen, const OTProtocol p,
                 const std::shared_ptr<ABYN::DataStorage> &data_storage,
                 const std::function<void(flatbuffers::FlatBufferBuilder &&)> &Send);

  void Reserve(const std::size_t id, const std::size_t num_ots, const std::size_t bitlen);

  std::vector<BitVector<>> inputs_, outputs_;
};

class GOTVectorSender final : public OTVectorSender {
 public:
  GOTVectorSender(const std::size_t ot_id, const std::size_t vector_id, const std::size_t num_ots,
                  const std::size_t bitlen, const std::shared_ptr<ABYN::DataStorage> &data_storage,
                  const std::function<void(flatbuffers::FlatBufferBuilder &&)> &Send);

  void SetInputs(std::vector<BitVector<>> &&v) final;

  void SetInputs(const std::vector<BitVector<>> &v) final;

  // blocking wait for correction bits
  void SendMessages() final;
};

class COTVectorSender final : public OTVectorSender {
 public:
  COTVectorSender(const std::size_t id, const std::size_t vector_id, const std::size_t num_ots,
                  const std::size_t bitlen, OTProtocol p,
                  const std::shared_ptr<ABYN::DataStorage> &data_storage,
                  const std::function<void(flatbuffers::FlatBufferBuilder &&)> &Send);

  void SetInputs(std::vector<BitVector<>> &&v) final;

  void SetInputs(const std::vector<BitVector<>> &v) final;

  const std::vector<BitVector<>> &GetOutputs() final;

  void SendMessages() final;
};

class ROTVectorSender final : public OTVectorSender {
 public:
  ROTVectorSender(const std::size_t ot_id, const std::size_t vector_id, const std::size_t num_ots,
                  const std::size_t bitlen, const std::shared_ptr<ABYN::DataStorage> &data_storage,
                  const std::function<void(flatbuffers::FlatBufferBuilder &&)> &Send);

  void SetInputs(std::vector<BitVector<>> &&v) final;

  void SetInputs(const std::vector<BitVector<>> &v) final;

  void SendMessages() final;
};

class OTVectorReceiver : public OTVector {
 public:
  virtual void SetChoices(const BitVector<> &v) = 0;

  virtual void SetChoices(BitVector<> &&v) = 0;

  const virtual BitVector<> &GetChoices() = 0;

  const virtual std::vector<BitVector<>> &GetOutputs() = 0;

  virtual void SendCorrections() = 0;

  void WaitSetup();

 protected:
  OTVectorReceiver(const std::size_t ot_id, const std::size_t vector_id, const std::size_t num_ots,
                   const std::size_t bitlen, const OTProtocol p,
                   const std::shared_ptr<ABYN::DataStorage> &data_storage,
                   std::function<void(flatbuffers::FlatBufferBuilder &&)> Send);

  void Reserve(const std::size_t id, const std::size_t num_ots, const std::size_t bitlen);

  BitVector<> choices_;
  std::vector<BitVector<>> messages_;
};

class GOTVectorReceiver final : public OTVectorReceiver {
 public:
  GOTVectorReceiver(const std::size_t ot_id, const std::size_t vector_id, const std::size_t num_ots,
                    const std::size_t bitlen,
                    const std::shared_ptr<ABYN::DataStorage> &data_storage,
                    const std::function<void(flatbuffers::FlatBufferBuilder &&)> &Send);

  void SetChoices(BitVector<> &&v) final;

  void SetChoices(const BitVector<> &v) final;

  const BitVector<> &GetChoices() final { return choices_; }

  void SendCorrections() final;

  const std::vector<BitVector<>> &GetOutputs() final;

 private:
  bool corrections_sent_ = false;
};

class COTVectorReceiver final : public OTVectorReceiver {
 public:
  COTVectorReceiver(const std::size_t ot_id, const std::size_t vector_id, const std::size_t num_ots,
                    const std::size_t bitlen, OTProtocol p,
                    const std::shared_ptr<ABYN::DataStorage> &data_storage,
                    const std::function<void(flatbuffers::FlatBufferBuilder &&)> &Send);

  void SendCorrections() final;

  void SetChoices(BitVector<> &&v);

  void SetChoices(const BitVector<> &v);

  const BitVector<> &GetChoices() final { return choices_; }

  const std::vector<BitVector<>> &GetOutputs() final;

 private:
  bool corrections_sent_ = false;
};

class ROTVectorReceiver final : public OTVectorReceiver {
 public:
  ROTVectorReceiver(const std::size_t ot_id, const std::size_t vector_id, const std::size_t num_ots,
                    const std::size_t bitlen,
                    const std::shared_ptr<ABYN::DataStorage> &data_storage,
                    const std::function<void(flatbuffers::FlatBufferBuilder &&)> &Send);

  void SetChoices(const BitVector<> &v) final;

  void SetChoices(BitVector<> &&v) final;

  void SendCorrections() final;

  const BitVector<> &GetChoices() final;

  const std::vector<BitVector<>> &GetOutputs() final;
};

class OTProviderSender {
 public:
  OTProviderSender() = default;

  OTProviderSender(const std::shared_ptr<ABYN::DataStorage> &data_storage)
      : data_storage_(data_storage) {}

  ~OTProviderSender() = default;

  OTProviderSender(const OTProviderSender &) = delete;

  std::shared_ptr<OTVectorSender> &GetOTs(std::size_t offset);

  std::shared_ptr<OTVectorSender> &RegisterOTs(
      const std::size_t bitlen, const std::size_t num_ots, const OTProtocol p,
      const std::function<void(flatbuffers::FlatBufferBuilder &&)> &Send);

  auto GetNumOTs() const { return total_ots_count_; }

  void Clear();

  void Reset();

 private:
  std::unordered_map<std::size_t, std::shared_ptr<OTVectorSender>> sender_data_;

  std::size_t total_ots_count_{0}, next_vector_id_{0};

  std::shared_ptr<ABYN::DataStorage> data_storage_;
};

class OTProviderReceiver {
 public:
  OTProviderReceiver() = default;

  OTProviderReceiver(const std::shared_ptr<ABYN::DataStorage> &data_storage)
      : data_storage_(data_storage) {}

  ~OTProviderReceiver() = default;

  OTProviderReceiver(const OTProviderReceiver &) = delete;

  std::shared_ptr<OTVectorReceiver> &GetOTs(const std::size_t offset);

  std::shared_ptr<OTVectorReceiver> &RegisterOTs(
      const std::size_t bitlen, const std::size_t num_ots, const OTProtocol p,
      const std::function<void(flatbuffers::FlatBufferBuilder &&)> &Send);

  std::size_t GetNumOTs() const { return total_ots_count_; }

  void Clear();
  void Reset();

 private:
  std::unordered_map<std::size_t, std::shared_ptr<OTVectorReceiver>> receiver_data_;

  std::size_t total_ots_count_{0}, next_vector_id_{0};

  std::shared_ptr<ABYN::DataStorage> data_storage_;
};

// OTProvider encapsulates both sender and receiver interfaces for simplicity
class OTProvider {
 public:
  virtual ~OTProvider() = default;

  OTProvider(const OTProvider &) = delete;

  /// @param bitlen Bit-length of the messages
  /// @param num_ots Number of OTs
  /// @param p OT protocol from {General OT (GOT), Correlated OT (COT), Random OT (ROT)}
  /// @return Offset to the OT that can be used to set input messages
  std::shared_ptr<OTVectorSender> &RegisterSend(const std::size_t bitlen = 1,
                                                const std::size_t num_ots = 1,
                                                const OTProtocol p = GOT) {
    return sender_provider_.RegisterOTs(bitlen, num_ots, p, Send_);
  }

  /// @param bitlen Bit-length of the messages
  /// @param num_ots Number of OTs
  /// @param p OT protocol from {General OT (GOT), Correlated OT (COT), Random OT (ROT)}
  /// @return Offset to the OT that can be used to retrieve the output of the OT
  std::shared_ptr<OTVectorReceiver> &RegisterReceive(const std::size_t bitlen = 1,
                                                     const std::size_t num_ots = 1,
                                                     const OTProtocol p = GOT) {
    return receiver_provider_.RegisterOTs(bitlen, num_ots, p, Send_);
  }

  std::shared_ptr<OTVectorSender> &GetSent(const size_t id) { return sender_provider_.GetOTs(id); }

  std::shared_ptr<OTVectorReceiver> &GetReceiver(const size_t id) {
    return receiver_provider_.GetOTs(id);
  }

  std::size_t GetNumOTsReceiver() const { return receiver_provider_.GetNumOTs(); }

  std::size_t GetNumOTsSender() const { return sender_provider_.GetNumOTs(); }

  virtual void SendSetup() = 0;
  virtual void ReceiveSetup() = 0;

  void Clear() {
    receiver_provider_.Clear();
    sender_provider_.Clear();
  }

  void Reset() {
    receiver_provider_.Reset();
    sender_provider_.Reset();
  }

 protected:
  OTProvider(const std::shared_ptr<ABYN::DataStorage> &data_storage,
             std::function<void(flatbuffers::FlatBufferBuilder &&)> Send)
      : data_storage_(data_storage),
        Send_(Send),
        receiver_provider_(OTProviderReceiver(data_storage_)),
        sender_provider_(OTProviderSender(data_storage_)) {}

  std::shared_ptr<ABYN::DataStorage> data_storage_;
  std::function<void(flatbuffers::FlatBufferBuilder &&)> Send_;
  OTProviderReceiver receiver_provider_;
  OTProviderSender sender_provider_;
};

class OTProviderFromFile : public OTProvider {
  // TODO
};

class OTProviderFromBaseOTs : public OTProvider {
  // TODO
};

class OTProviderFromOTExtension final : public OTProvider {
 public:
  void SendSetup() final;

  void ReceiveSetup() final;

  OTProviderFromOTExtension(std::function<void(flatbuffers::FlatBufferBuilder &&)> Send,
                            const std::shared_ptr<ABYN::DataStorage> &data_storage);
};

class OTProviderFromThirdParty : public OTProvider {
  // TODO
};

class OTProviderFromMultipleThirdParties : public OTProvider {
  // TODO
};

}  // namespace ObliviousTransfer
}