// MIT License
//
// Copyright (c) 2021 Lennart Braun
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

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <regex>
#include <stdexcept>

#include <boost/algorithm/string.hpp>
#include <boost/json/serialize.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/log/trivial.hpp>
#include <boost/program_options.hpp>

#include "algorithm/circuit_loader.h"
#include "base/gate_factory.h"
#include "base/two_party_backend.h"
#include "communication/communication_layer.h"
#include "communication/tcp_transport.h"
#include "statistics/analysis.h"
#include "utility/logger.h"

namespace po = boost::program_options;

struct Options {
  std::size_t threads;
  bool json;
  std::size_t num_repetitions;
  std::size_t num_simd;
  bool sync_between_setup_and_online;
  MOTION::MPCProtocol arithmetic_protocol;
  MOTION::MPCProtocol boolean_protocol;
  std::uint64_t input_value_1;
  std::uint64_t input_value_2;
  std::uint64_t input_value_3;
  //std::vector<std::uint64_t> input_values;
  //std::uint64_t num_elements;
  std::size_t my_id;
  MOTION::Communication::tcp_parties_config tcp_config;
  bool no_run = false;
};

std::optional<Options> parse_program_options(int argc, char* argv[]) {
  Options options;
  boost::program_options::options_description desc("Allowed options");
  // clang-format off
  desc.add_options()
    ("help,h", po::bool_switch()->default_value(false),"produce help message")
    ("config-file", po::value<std::string>(), "config file containing options")
    ("my-id", po::value<std::size_t>()->required(), "my party id")
    ("party", po::value<std::vector<std::string>>()->multitoken(),
     "(party id, IP, port), e.g., --party 1,127.0.0.1,7777")
    ("threads", po::value<std::size_t>()->default_value(0), "number of threads to use for gate evaluation")
    ("json", po::bool_switch()->default_value(false), "output data in JSON format")
    ("arithmetic-protocol", po::value<std::string>()->required(), "2PC protocol (GMW or BEAVY)")
    ("boolean-protocol", po::value<std::string>()->required(), "2PC protocol (Yao, GMW or BEAVY)")
    ("input-value-1", po::value<std::uint64_t>()->required(), "input value for Yao's Millionaires' Problem")
    ("input-value-2", po::value<std::uint64_t>()->required(), "input value for Yao's Millionaires' Problem")
    ("input-value-3", po::value<std::uint64_t>()->required(), "input value for Yao's Millionaires' Problem")
    //("input-values", po::value<std::vector<std::uint64_t> >()->multitoken(), "input values for Product")
    //("num-elements", po::value<std::uint64_t>()->required(), "number of elements in the array")
    ("repetitions", po::value<std::size_t>()->default_value(1), "number of repetitions")
    ("num-simd", po::value<std::size_t>()->default_value(1), "numbebr of SIMD values")
    ("sync-between-setup-and-online", po::bool_switch()->default_value(false),
     "run a synchronization protocol before the online phase starts")
    ("no-run", po::bool_switch()->default_value(false), "just build the circuit, but not execute it")
    ;
  // clang-format on

  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);
  bool help = vm["help"].as<bool>();
  if (help) {
    std::cerr << desc << "\n";
    return std::nullopt;
  }
  if (vm.count("config-file")) {
    std::ifstream ifs(vm["config-file"].as<std::string>().c_str());
    po::store(po::parse_config_file(ifs, desc), vm);
  }
  try {
    po::notify(vm);
  } catch (std::exception& e) {
    std::cerr << "error:" << e.what() << "\n\n";
    std::cerr << desc << "\n";
    return std::nullopt;
  }

  options.my_id = vm["my-id"].as<std::size_t>();
  options.threads = vm["threads"].as<std::size_t>();
  options.json = vm["json"].as<bool>();
  options.num_repetitions = vm["repetitions"].as<std::size_t>();
  options.num_simd = vm["num-simd"].as<std::size_t>();
  options.sync_between_setup_and_online = vm["sync-between-setup-and-online"].as<bool>();
  options.no_run = vm["no-run"].as<bool>();
  if (options.my_id > 1) {
    std::cerr << "my-id must be one of 0 and 1\n";
    return std::nullopt;
  }

  auto arithmetic_protocol = vm["arithmetic-protocol"].as<std::string>();
  boost::algorithm::to_lower(arithmetic_protocol);
  if (arithmetic_protocol == "gmw") {
    options.arithmetic_protocol = MOTION::MPCProtocol::ArithmeticGMW;
  } else if (arithmetic_protocol == "beavy") {
    options.arithmetic_protocol = MOTION::MPCProtocol::ArithmeticBEAVY;
  } else {
    std::cerr << "invalid protocol: " << arithmetic_protocol << "\n";
    return std::nullopt;
  }
  auto boolean_protocol = vm["boolean-protocol"].as<std::string>();
  boost::algorithm::to_lower(boolean_protocol);
  if (boolean_protocol == "yao") {
    options.boolean_protocol = MOTION::MPCProtocol::Yao;
  } else if (boolean_protocol == "gmw") {
    options.boolean_protocol = MOTION::MPCProtocol::BooleanGMW;
  } else if (boolean_protocol == "beavy") {
    options.boolean_protocol = MOTION::MPCProtocol::BooleanBEAVY;
  } else {
    std::cerr << "invalid protocol: " << boolean_protocol << "\n";
    return std::nullopt;
  }

  options.input_value_1 = vm["input-value-1"].as<std::uint64_t>();
  options.input_value_2 = vm["input-value-2"].as<std::uint64_t>();
  options.input_value_3 = vm["input-value-3"].as<std::uint64_t>();

  
  //options.input_values = vm["input-values"].as<std::vector<std::uint64_t>>();
  //options.num_elements = vm["num-elements"].as<std::uint64_t>();

  //if(options.num_elements != options.input_values.size()){
    //std::cerr << "The number of elements must be equal to size of the input array\n";
    //return std::nullopt;
  //}

  const auto parse_party_argument =
      [](const auto& s) -> std::pair<std::size_t, MOTION::Communication::tcp_connection_config> {
    const static std::regex party_argument_re("([01]),([^,]+),(\\d{1,5})");
    std::smatch match;
    if (!std::regex_match(s, match, party_argument_re)) {
      throw std::invalid_argument("invalid party argument");
    }
    auto id = boost::lexical_cast<std::size_t>(match[1]);
    auto host = match[2];
    auto port = boost::lexical_cast<std::uint16_t>(match[3]);
    return {id, {host, port}};
  };

  const std::vector<std::string> party_infos = vm["party"].as<std::vector<std::string>>();
  if (party_infos.size() != 2) {
    std::cerr << "expecting two --party options\n";
    return std::nullopt;
  }

  options.tcp_config.resize(2);
  std::size_t other_id = 2;

  const auto [id0, conn_info0] = parse_party_argument(party_infos[0]);
  const auto [id1, conn_info1] = parse_party_argument(party_infos[1]);
  if (id0 == id1) {
    std::cerr << "need party arguments for party 0 and 1\n";
    return std::nullopt;
  }
  options.tcp_config[id0] = conn_info0;
  options.tcp_config[id1] = conn_info1;

  return options;
}

std::unique_ptr<MOTION::Communication::CommunicationLayer> setup_communication(
    const Options& options) {
  MOTION::Communication::TCPSetupHelper helper(options.my_id, options.tcp_config);
  return std::make_unique<MOTION::Communication::CommunicationLayer>(options.my_id,
                                                                     helper.setup_connections());
}

void print_stats(const Options& options,
                 const MOTION::Statistics::AccumulatedRunTimeStats& run_time_stats,
                 const MOTION::Statistics::AccumulatedCommunicationStats& comm_stats) {
  if (options.json) {
    auto obj = MOTION::Statistics::to_json("millionaires_problem", run_time_stats, comm_stats);
    obj.emplace("party_id", options.my_id);
    obj.emplace("arithmetic_protocol", MOTION::ToString(options.arithmetic_protocol));
    obj.emplace("boolean_protocol", MOTION::ToString(options.boolean_protocol));
    obj.emplace("simd", options.num_simd);
    obj.emplace("threads", options.threads);
    obj.emplace("sync_between_setup_and_online", options.sync_between_setup_and_online);
    std::cout << obj << "\n";
  } else {
    std::cout << MOTION::Statistics::print_stats("millionaires_problem", run_time_stats,
                                                 comm_stats);
  }
}

auto create_arith_add_circuit(const Options& options, MOTION::TwoPartyBackend& backend) {
  
 std::cout<<"Inside create";
  // retrieve the gate factories for the chosen protocols
  auto& gate_factory_arith = backend.get_gate_factory(options.arithmetic_protocol);//gmw
  auto& gate_factory_bool = backend.get_gate_factory(options.boolean_protocol);// beavy

  // share the inputs using the arithmetic protocol
  // NB: the inputs need to always be specified in the same order:
  // here we first specify the input of party 0, then that of party 1
  ENCRYPTO::ReusableFiberPromise<MOTION::IntegerValues<uint64_t>> input_promise;
  MOTION::WireVector input_0_arith, input_1_arith;
  if (options.my_id == 0) {
    auto pair = gate_factory_arith.make_arithmetic_64_input_gate_my(options.my_id, 1);
    input_promise = std::move(pair.first);
    input_0_arith = std::move(pair.second);
    input_1_arith = gate_factory_arith.make_arithmetic_64_input_gate_other(1 - options.my_id, 1);
  } else {
    input_0_arith = gate_factory_arith.make_arithmetic_64_input_gate_other(1 - options.my_id, 1);
    auto pair = gate_factory_arith.make_arithmetic_64_input_gate_my(options.my_id, 1);
    input_promise = std::move(pair.first);
    input_1_arith = std::move(pair.second);
  }

  // // create arithmetic gate to compute multiplication
  auto output = gate_factory_arith.make_binary_gate(
      ENCRYPTO::PrimitiveOperationType::ADD, input_0_arith, input_1_arith);
  // apply the circuit to the Boolean sahres
  // auto output = backend.make_circuit(ENCRYPTO::PrimitiveOperationType::ADD, input_0_arith, input_1_arith);

  // create an output gates of the result
  auto output_future = gate_factory_arith.make_arithmetic_64_output_gate_my(MOTION::ALL_PARTIES, output);
  // std  :: cout << "Before bvs";
  // auto bvs = output_future.get();
  // auto add_result = bvs.at(0);
  // if (!add_result){
  //   std :: cerr << "Error";
  //   //return std :: nullopt;
  //     }
  // return promise and future to allow setting inputs and retrieving outputs
  return std::make_pair(std::move(input_promise), std::move(output_future));
  

 
}

void run_add_circuit(const Options& options, MOTION::TwoPartyBackend& backend){
  // build the circuit and gets promise/future for the input/output
  auto [input_promise, output_future] = create_arith_add_circuit(options, backend);

  if (options.no_run) {
    return;
  }

  // set the promise with our input value
  input_promise.set_value({options.input_value_1});
  //input_promise.set_value({options.input_value_2});

  // execute the protocol
  backend.run();

  // retrieve the result from the future
  auto bvs = output_future.get();
  auto add_result = bvs.at(0);

  if (!options.json) {
    std::cout << "The addition result is:- " << add_result << std::endl;
  }
}

auto create_composite_circuit(const Options& options, MOTION::TwoPartyBackend& backend) 
{ 
   MOTION::CircuitLoader circuit_loader;
  
  // retrieve the gate factories for the chosen protocols
  auto& gate_factory_arith = backend.get_gate_factory(options.arithmetic_protocol);
  auto& gate_factory_bool = backend.get_gate_factory(options.boolean_protocol);
  

  std::vector<ENCRYPTO::ReusableFiberPromise<MOTION::IntegerValues<uint64_t>>> input_promises;
  std::vector<MOTION::WireVector> input_0_arith, input_1_arith;
  // share the inputs using the arithmetic protocol
  // NB: the inputs need to always be specified in the same order:
  // here we first specify the input of party 0, then that of party 1
   ENCRYPTO::ReusableFiberPromise<MOTION::IntegerValues<uint64_t>> input_promise_0,input_promise_1,input_promise_2;
  MOTION::WireVector input_a_arith, input_b_arith, input_c_arith, input_d_arith,input_e_arith,input_f_arith;
  if (options.my_id == 0) {
    auto pair = gate_factory_arith.make_arithmetic_64_input_gate_my(options.my_id, 1);
    input_promise_0 = std::move(pair.first);
    input_a_arith = std::move(pair.second);
    input_b_arith = gate_factory_arith.make_arithmetic_64_input_gate_other(1 - options.my_id, 1);

    input_promises.push_back(std::move(input_promise_0));
    input_0_arith.push_back(input_a_arith);
    input_1_arith.push_back(input_b_arith);

    auto pair2 = gate_factory_arith.make_arithmetic_64_input_gate_my(options.my_id, 1);
    input_promise_1 = std::move(pair2.first);
    input_c_arith = std::move(pair2.second);
    input_d_arith = gate_factory_arith.make_arithmetic_64_input_gate_other(1 - options.my_id, 1);

    input_promises.push_back(std::move(input_promise_1));
    input_0_arith.push_back(input_c_arith);
    input_1_arith.push_back(input_d_arith);

    auto pair3 = gate_factory_arith.make_arithmetic_64_input_gate_my(options.my_id, 1);
    input_promise_2 = std::move(pair3.first);
    input_e_arith = std::move(pair3.second);
    input_f_arith = gate_factory_arith.make_arithmetic_64_input_gate_other(1 - options.my_id, 1);
    
    input_promises.push_back(std::move(input_promise_2));
    input_0_arith.push_back(input_e_arith);
    input_1_arith.push_back(input_f_arith);

    
    } else {
    input_a_arith = gate_factory_arith.make_arithmetic_64_input_gate_other(1 - options.my_id, 1);
    auto pair = gate_factory_arith.make_arithmetic_64_input_gate_my(options.my_id, 1);
    input_promise_0 = std::move(pair.first);
    input_b_arith = std::move(pair.second);
    
    input_promises.push_back(std::move(input_promise_0));
    input_0_arith.push_back(input_a_arith);
    input_1_arith.push_back(input_b_arith);

    input_c_arith = gate_factory_arith.make_arithmetic_64_input_gate_other(1 - options.my_id, 1);
    auto pair2 = gate_factory_arith.make_arithmetic_64_input_gate_my(options.my_id, 1);
    input_promise_1 = std::move(pair2.first);
    input_d_arith = std::move(pair2.second);
    
    input_promises.push_back(std::move(input_promise_1));
    input_0_arith.push_back(input_c_arith);
    input_1_arith.push_back(input_d_arith);

    input_e_arith = gate_factory_arith.make_arithmetic_64_input_gate_other(1 - options.my_id, 1);
    auto pair3 = gate_factory_arith.make_arithmetic_64_input_gate_my(options.my_id, 1);
    input_promise_2 = std::move(pair3.first);
    input_f_arith = std::move(pair3.second);

    input_promises.push_back(std::move(input_promise_2));
    input_0_arith.push_back(input_e_arith);
    input_1_arith.push_back(input_f_arith);
    } 

    std::vector< MOTION::WireVector > mult_output;
    
   // create arithmetic gate to compute addition
  auto wires_out_1 = gate_factory_arith.make_binary_gate(
    ENCRYPTO::PrimitiveOperationType::ADD, input_0_arith[0], input_1_arith[0]);


  auto wires_out_2= gate_factory_arith.make_binary_gate(
     ENCRYPTO::PrimitiveOperationType::ADD, input_0_arith[1], input_1_arith[1]);

  auto wires_out_3= gate_factory_arith.make_binary_gate(
     ENCRYPTO::PrimitiveOperationType::ADD, input_0_arith[2], input_1_arith[2]);
  
  //auto output = gate_factory_arith.make_binary_gate(
    //ENCRYPTO::PrimitiveOperationType::ADD, wires_out_1, wires_out_2);

  // create an output gates of the result
  //auto output_future = gate_factory_arith.make_arithmetic_64_output_gate_my(MOTION::ALL_PARTIES, wires_out_1);
  //auto output_future_0 = gate_factory_arith.make_arithmetic_64_output_gate_my(MOTION::ALL_PARTIES,wires_out_1);
  //auto output_future_1 = gate_factory_arith.make_arithmetic_64_output_gate_my(MOTION::ALL_PARTIES,wires_out_2);
  
   auto& gt_circuit =
     circuit_loader.load_gtmux_circuit(64, options.boolean_protocol != MOTION::MPCProtocol::Yao);

  
    auto output_0_bool = backend.convert(options.boolean_protocol, wires_out_1);
    auto output_1_bool = backend.convert(options.boolean_protocol, wires_out_2);
    auto output_2_bool = backend.convert(options.boolean_protocol, wires_out_3);

    auto output0 = backend.make_circuit(gt_circuit, output_0_bool, output_1_bool);
    auto output1 =  backend.make_circuit(gt_circuit, output0, output_2_bool);
    auto output_new = backend.convert(options.arithmetic_protocol , output1);
    auto output_future = gate_factory_arith.make_arithmetic_64_output_gate_my(MOTION::ALL_PARTIES, output_new);


    //auto output_future = gate_factory_bool.make_boolean_output_gate_my(MOTION::ALL_PARTIES, output_1);

  // return promise and future to allow setting inputs and retrieving outputs
  return std::make_pair(std::move(output_future),std::move(input_promises));
    
}




 

void run_composite_circuit(const Options& options, MOTION::TwoPartyBackend& backend)
{
  
     auto& gate_factory_bool = backend.get_gate_factory(options.boolean_protocol);
     auto& gate_factory_arith = backend.get_gate_factory(options.arithmetic_protocol);

     
     auto [output_futures,input_final] = create_composite_circuit(options,backend);
  
      auto input_0=std::move(input_final[0]); 
      input_0.set_value({options.input_value_1});
      auto input_1=std::move(input_final[1]);
      input_1.set_value({options.input_value_2});
      auto input_2 =std::move(input_final[2]);
      input_2.set_value({options.input_value_3});

     /*auto[output_bool_0]=std::move(output_futures[0]);
     auto[output_bool_1]=std::move(output_futures[1]);
     auto[output_bool_2]=std::move(output_futures[2]);
     */
       
      //auto output_future_0 = gate_factory_arith.make_arithmetic_64_output_gate_my(MOTION::ALL_PARTIES,output_bool_0);
      //auto output_future_1 = gate_factory_arith.make_arithmetic_64_output_gate_my(MOTION::ALL_PARTIES,output_bool_1);
      //auto output_future_2 = gate_factory_arith.make_arithmetic_64_output_gate_my(MOTION::ALL_PARTIES,output_bool_2);

     // create an output gates of the result
      
    //auto max_result = 0;

  // execute the protocol
  backend.run();

  // retrieve the result from the future
  //auto bvs0 = output_future_0.get();
  //auto bvs1 = output_future_1.get();
  //auto bvs2 =  output_future_2.get();
    auto bvs = output_futures.get();
    auto result = bvs.at(0);

   // Conversion of result to readable arithmetic format
  /*for (int i = 63; i >= 0; i--) {
    max_result = max_result + bvs.at(i).Get(0);
    // left shift is easier to implement
    max_result = max_result << 1;
  }
  max_result = max_result >> 1;
  */
  if (options.no_run) {
    return;
  }
  // auto add_result0 = bvs0.at();
   //auto add_result1 = bvs1.at();
   //auto add_result2 = bvs2.at();
   //bool gt_result= bvs.at(0).Get(0);
  if (!options.json) {
    std::cout << "The maximum of the composite function is:- " << result << std::endl;
    //std::cout << "The result of the composite function is:- " << add_result1 << std::endl;
    //std::cout << "The result of the composite function is:- " << add_result2 << std::endl;
    /*if (gt_result) {
      std::cout << "Party 0 has more money than Party 1" << std::endl;
     } else {
        std::cout << "Party 1 has at least the same amount of money as Party 0" << std::endl;
      }  */
  }
}



int main(int argc, char* argv[]) 
{
  auto options = parse_program_options(argc, argv);
  if (!options.has_value()) {
    return EXIT_FAILURE;
  }

  try {
    auto comm_layer = setup_communication(*options);
    auto logger = std::make_shared<MOTION::Logger>(options->my_id,
                                                   boost::log::trivial::severity_level::trace);
    comm_layer->set_logger(logger);
    MOTION::Statistics::AccumulatedRunTimeStats run_time_stats;
    MOTION::Statistics::AccumulatedCommunicationStats comm_stats;
    for (std::size_t i = 0; i < options->num_repetitions; ++i) {
      MOTION::TwoPartyBackend backend(*comm_layer, options->threads,
                                      options->sync_between_setup_and_online, logger);
      run_add_circuit(*options, backend);

     // run_composite_circuit(*options, backend);

      comm_layer->sync();
      comm_stats.add(comm_layer->get_transport_statistics());
      comm_layer->reset_transport_statistics();
      run_time_stats.add(backend.get_run_time_stats());
    }
    comm_layer->shutdown();
    print_stats(*options, run_time_stats, comm_stats);
  } catch (std::runtime_error& e) {
    std::cerr << "ERROR OCCURRED: " << e.what() << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}