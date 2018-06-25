
#include <chrono>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <thread>

#include <grpcpp/grpcpp.h>

#ifdef BAZEL_BUILD
#include "examples/protos/omr.grpc.pb.h"
#else
#include "omr.grpc.pb.h"
#endif

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;
using std::chrono::system_clock;

using omr::FileString;
using omr::ExecBytes;
using omr::OMRSaveReplay;

FileString MakeFileString(const std::string& message, int version)
   {
   omr::FileString fileString;

   fileString.set_file(message);
   fileString.set_version(version);
   return fileString;
   }

class OMRClient {

public:
   OMRClient(std::shared_ptr<Channel> channel)
     : stub_(OMRSaveReplay::NewStub(channel)) {}

   void SendOutFile()
     {
     // With the ClientContext we can set deadlines for a particular response
     ClientContext context;
     omr::FileString fileString; // SEND
     omr::ExecBytes returnedBytes; // receive

     std::string clientStr = "This is a string that will be substituted by a file\n";
     fileString.set_file(clientStr);
     fileString.set_version(34);

     Status status = stub_->SendOutFile(&context, fileString, &returnedBytes);

     std::cout << "Client received the following message from the server:" << '\n';
     std::cout << "Byte stream: " << returnedBytes.bytestream() << '\n';
     std::cout << "Size: " << returnedBytes.size() << '\n';

     if(status.ok())
        {
          std::cout << "Client finished successfully." << '\n';
        }
     else
        {
          std::cout << status.error_code() << ": " << status.error_message()
                    << std::endl;
          std::cout << "RPC failed" << '\n';
        }
     }

   // Implement SendOutFiles bidirectional streaming for client
   void SendOutFiles()
      {
      ClientContext context;
      system_clock::time_point deadline = std::chrono::system_clock::now() +
         std::chrono::milliseconds(5500); // 5 seconds
      // Deadline for all the responses to reach the client
      context.set_deadline(deadline);

      std::shared_ptr<ClientReaderWriter<FileString, ExecBytes> > stream(
          stub_->SendOutFiles(&context));

      std::thread writer([stream]() {
        std::vector<FileString> files{
          MakeFileString("This is the FIRST message.", 3241),
          MakeFileString("Looks like I am the SECOND message.", 9991),
          MakeFileString("Wow, another one!! I'm the THIRD message.", 33),
          MakeFileString("This is it!! I am the LAST and FOURTH message.", 385)};
        for (const FileString& ff : files) {
          std::cout << "Sending file: " << ff.file() << std::endl;
          std::cout << "With version: " << ff.version() << std::endl;

          stream->Write(ff);
          std::this_thread::sleep_for (std::chrono::milliseconds(500));
          }
        stream->WritesDone();
      });

     ExecBytes retBytes;
     while (stream->Read(&retBytes))
        {
        std::cout << "Client received: " << retBytes.bytestream()
        << ", with size: " << retBytes.size() << '\n';
        }

     writer.join();
     Status status = stream->Finish();

     if (!status.ok())
       {
       std::cout << "OMR SendOutFiles rpc failed." << std::endl;
       std::cout << status.error_message() << '\n';
       std::cout << "Error details: " << status.error_details() << '\n';
       std::cout << "Status error code: " << status.error_code() << '\n';
       }
     }

   private:
      std::unique_ptr<OMRSaveReplay::Stub> stub_;
};

int main(int argc, char** argv) {
  // Instantiate the client. It requires a channel, out of which the actual RPCs
  // are created. This channel models a connection to an endpoint (in this case,
  // localhost at port 50051). We indicate that the channel isn't authenticated
  // (use of InsecureChannelCredentials()).
  OMRClient client(grpc::CreateChannel(
      "localhost:50055", grpc::InsecureChannelCredentials()));

  std::cout << "-------------- Client Running --------------" << std::endl;
  client.SendOutFile();
  std::cout << "-------------- Client Finished --------------" << std::endl;

  std::cout << "____----____----____ BiDirectional SendOutFiles ____----____----____" << '\n';
  client.SendOutFiles();
  return 0;
}
