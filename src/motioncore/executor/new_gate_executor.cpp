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

#include "new_gate_executor.h"

#include <iostream>

#include "base/gate_register.h"
#include "gate/new_gate.h"
#include "statistics/run_time_stats.h"
#include "utility/fiber_thread_pool/fiber_thread_pool.hpp"
#include "utility/logger.h"

namespace MOTION {

NewGateExecutor::NewGateExecutor(GateRegister &reg, std::function<void(void)> preprocessing_fctn,
                           std::shared_ptr<Logger> logger)
    : register_(reg),
      preprocessing_fctn_(std::move(preprocessing_fctn)),
      logger_(std::move(logger)) {}

void NewGateExecutor::evaluate_setup_online(Statistics::RunTimeStats &stats) {
  stats.record_start<Statistics::RunTimeStats::StatID::evaluate>();

  preprocessing_fctn_();

  if (logger_) {
    logger_->LogInfo(
        "Start evaluating the circuit gates sequentially (online after all finished setup)");
  }

  // create a pool with std::thread::hardware_concurrency() no. of threads
  // to execute fibers
  ENCRYPTO::FiberThreadPool fpool(0, 2 * register_.get_num_gates());

  // ------------------------------ setup phase ------------------------------
  stats.record_start<Statistics::RunTimeStats::StatID::gates_setup>();

  // evaluate the setup phase of all the gates
  for (auto &gate : register_.get_gates()) {
    if (gate->need_setup()) {
      fpool.post([&] {
        gate->evaluate_setup();
        register_.increment_gate_setup_counter();
      });
    }
  }
  register_.wait_setup();

  stats.record_end<Statistics::RunTimeStats::StatID::gates_setup>();

  if (logger_) {
    logger_->LogInfo(
        "Start with the online phase of the circuit gates");
  }

  // ------------------------------ online phase ------------------------------
  stats.record_start<Statistics::RunTimeStats::StatID::gates_online>();

  // evaluate the online phase of all the gates
  for (auto &gate : register_.get_gates()) {
    if (gate->need_online()) {
      fpool.post([&] {
        gate->evaluate_online();
        register_.increment_gate_online_counter();
      });
    }
  }
  register_.wait_online();

  stats.record_end<Statistics::RunTimeStats::StatID::gates_online>();

  // --------------------------------------------------------------------------

  if (logger_) {
    logger_->LogInfo(
        "Finished with the online phase of the circuit gates");
  }

  fpool.join();

  stats.record_end<Statistics::RunTimeStats::StatID::evaluate>();
}

void NewGateExecutor::evaluate(Statistics::RunTimeStats &stats) {
  throw std::logic_error("not implemented");
}

}  // namespace MOTION