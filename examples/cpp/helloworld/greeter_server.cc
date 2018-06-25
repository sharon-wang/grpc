/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <iostream>
#include <memory>
#include <string>

#include "Jit.hpp"
#include "ilgen/TypeDictionary.hpp"
#include "ilgen/JitBuilderRecorderTextFile.hpp"
#include "ilgen/JitBuilderReplayTextFile.hpp"
#include "ilgen/MethodBuilderReplay.hpp"

#include <grpcpp/grpcpp.h>

#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "helloworld.grpc.pb.h"
#endif

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using helloworld::HelloRequest;
using helloworld::HelloReply;
using helloworld::Greeter;

// Logic and data behind the server's behavior.
class GreeterServiceImpl final : public Greeter::Service {
  Status SayHello(ServerContext* context, const HelloRequest* request,
                  HelloReply* reply) override {
    std::cout << "I am the server. Printing request message..." << '\n';
    std::string prefix("Hello ");
    reply->set_greetings(prefix + request->somename());

    bool initialized = initializeJit();
    if (!initialized)
    {
      std::cerr << "FAIL: could not initialize JIT\n";
      exit(-1);
    }
    else
    {
      std::cout << ">>> JIT INITIALIZED" << '\n';
    }

    std::cout << request->testfile(); // Prints "file" received from client

    TR::JitBuilderReplayTextFile replay(request->testfile());
    TR::JitBuilderRecorderTextFile recorder(NULL, "simple2.out");

    TR::TypeDictionary types;
    uint8_t *entry = 0;

    std::cout << "Step 1: verify output file\n";
    TR::MethodBuilderReplay mb(&types, &replay, &recorder); // Process Constructor
    int32_t rc = compileMethodBuilder(&mb, &entry); // Process buildIL

    typedef int32_t (SimpleMethodFunction)(int32_t);
    SimpleMethodFunction *increment = (SimpleMethodFunction *) entry;

    int32_t v;
    v=0; std::cout << "increment(" << v << ") == " << increment(v) << "\n";
    v=1; std::cout << "increment(" << v << ") == " << increment(v) << "\n";
    v=10; std::cout << "increment(" << v << ") == " << increment(v) << "\n";
    v=-15; std::cout << "increment(" << v << ") == " << increment(v) << "\n";

    shutdownJit();

    return Status::OK;
  }

  Status SayHelloAgain(ServerContext* context, const HelloRequest* request,
                  HelloReply* reply) override {
    std::string prefix("Hello Again and Again ");
    reply->set_luckynumber(951268);
    reply->set_greetings(prefix + request->somename());

    return Status::OK;
  }
};

void RunServer() {
  std::string server_address("0.0.0.0:50053");
  GreeterServiceImpl service;

  ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to an *synchronous* service.
  builder.RegisterService(&service);
  // Finally assemble the server.
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;

  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();
}

int main(int argc, char** argv) {
  RunServer();

  return 0;
}
