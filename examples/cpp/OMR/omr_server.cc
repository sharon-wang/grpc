
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include <grpc/grpc.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/security/server_credentials.h>

#ifdef BAZEL_BUILD
#include "examples/protos/omr.grpc.pb.h"
#else
#include "omr.grpc.pb.h"
#endif

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReaderWriter;
using grpc::Status;
using grpc::StatusCode;

using omr::FileString;
using omr::ExecBytes;
using omr::OMRSaveReplay;

class OMRServerImpl final : public OMRSaveReplay::Service {

   public:

      Status SendOutFile(ServerContext* context, const FileString* fileString,
                        ExecBytes* execBytes) override {

         std::cout << "File version from client: " << fileString->version() << '\n';
         std::cout << "File:" << '\n';
         std::cout << fileString->file() << '\n';

         execBytes->set_size(512);
         execBytes->set_bytestream("0001101011010");

         return Status::OK;
         }

     // Implement SendOutFiles bidirectional streaming for server
     Status SendOutFiles(ServerContext* context,
                       ServerReaderWriter<ExecBytes, FileString>* stream) override {

        std::string serverStr[4] = {"0000011111", "01010101", "11110000", "000111000"};
        int count = 0;

        FileString ff;
        while (stream->Read(&ff))
           {
              std::cout << "Server received file: " << ff.file() << '\n';
              std::cout << "Server file version: " << ff.version() << '\n';
              std::cout << "Count: " << count << '\n';

              ExecBytes e;
              e.set_bytestream(serverStr[count]);
              e.set_size((count + 2) * 64);

              std::cout << "Sending to client: " << serverStr[count] << '\n';
              count++;
              stream->Write(e);
              // Sleeps for 1 second
              std::this_thread::sleep_for (std::chrono::seconds(1));
           }

        if (context->IsCancelled()) {
          return Status(StatusCode::CANCELLED, "Deadline exceeded or Client cancelled, abandoning.");
        }

        return Status::OK;
        }

};

void RunServer() {
  std::string server_address("localhost:50055");
  OMRServerImpl service;

  ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;
  server->Wait();
}

int main(int argc, char** argv) {
  RunServer();

  return 0;
}
