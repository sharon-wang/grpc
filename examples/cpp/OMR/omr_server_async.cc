
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include <grpc/grpc.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/security/server_credentials.h>
#include <grpc/support/log.h>

#ifdef BAZEL_BUILD
#include "examples/protos/omr.grpc.pb.h"
#else
#include "omr.grpc.pb.h"
#endif

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReaderWriter;
using grpc::StatusCode;

using grpc::ServerAsyncResponseWriter;
using grpc::ServerCompletionQueue;
using grpc::Status;

using omr::FileString;
using omr::ExecBytes;
using omr::OMRSaveReplay;

class OMRServerImpl final : public OMRSaveReplay::Service {

   public:
      ~OMRServerImpl() {
        server_->Shutdown();
        // Always shutdown the completion queue after the server.
        completionQueue_->Shutdown();
      }

      void RunServer() {
        std::string server_address("localhost:50055");
        OMRServerImpl service;

        ServerBuilder builder;
        builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
        builder.RegisterService(&service_);

        completionQueue_ = builder.AddCompletionQueue();
        // std::unique_ptr<Server> server(builder.BuildAndStart());
        server_ = builder.BuildAndStart();
        std::cout << "Server listening on " << server_address << std::endl;
        // completionQueueWait();
        HandleRpcs();

      }

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

     private:
     // Class encompasing the state and logic needed to serve a request.
     class CallData {
      public:
       // Take in the "service" instance (in this case representing an asynchronous
       // server) and the completion queue "completionQueue" used for asynchronous communication
       // with the gRPC runtime.
       CallData(OMRSaveReplay::AsyncService* service, ServerCompletionQueue* completionQueue)
           : service_(service), completionQueue_(completionQueue), responder_(&ctx_), status_(CREATE) {
         // Invoke the serving logic right away.
         Proceed();
       }

       void Proceed() {
         if (status_ == CREATE) {
            std::cout << "*** Server CREATE state ***" << std::endl;

           // Make this instance progress to the PROCESS state.
           status_ = PROCESS;

           // As part of the initial CREATE state, we *request* that the system
           // start processing SayHello requests. In this request, "this" acts are
           // the tag uniquely identifying the request (so that different CallData
           // instances can serve different requests concurrently), in this case
           // the memory address of this CallData instance.
           service_->RequestSendOutFile(&ctx_, &request_, &responder_, completionQueue_, completionQueue_,
                                     this);
           std::cout << "*** Server RequestSendOutFile ***" << std::endl;

         } else if (status_ == PROCESS) {
            std::cout << "*** Server PROCESS state ***" << std::endl;

           // Spawn a new CallData instance to serve new clients while we process
           // the one for this CallData. The instance will deallocate itself as
           // part of its FINISH state.
           new CallData(service_, completionQueue_);

           // The actual processing.
           reply_.set_bytestream(request_.file());
           reply_.set_size(666);

           // And we are done! Let the gRPC runtime know we've finished, using the
           // memory address of this instance as the uniquely identifying tag for
           // the event.
           status_ = FINISH;
           responder_.Finish(reply_, Status::OK, this);
           std::cout << "*** Request received from client: " << request_.file() << " ***" << std::endl;
         } else {
           GPR_ASSERT(status_ == FINISH);
           std::cout << "*** Server FINISH state ***" << std::endl;
           // Once in the FINISH state, deallocate ourselves (CallData).
           delete this;
         }
       }

      private:
       // The means of communication with the gRPC runtime for an asynchronous
       // server.
       OMRSaveReplay::AsyncService* service_;
       // The producer-consumer queue where for asynchronous server notifications.
       ServerCompletionQueue* completionQueue_;
       // Context for the rpc, allowing to tweak aspects of it such as the use
       // of compression, authentication, as well as to send metadata back to the
       // client.
       ServerContext ctx_;

       // What we get from the client.
       FileString request_;
       // What we send back to the client.
       ExecBytes reply_;

       // The means to get back to the client.
       ServerAsyncResponseWriter<ExecBytes> responder_;

       // Let's implement a tiny state machine with the following states.
       enum CallStatus { CREATE, PROCESS, FINISH };
       CallStatus status_;  // The current serving state.
     };

     // This can be run in multiple threads if needed.
     void HandleRpcs() {
       // Spawn a new CallData instance to serve new clients.
       new CallData(&service_, completionQueue_.get());
       void* tag;  // uniquely identifies a request.
       bool ok;
       while (true) {
         // Block waiting to read the next event from the completion queue. The
         // event is uniquely identified by its tag, which in this case is the
         // memory address of a CallData instance.
         // The return value of Next should always be checked. This return value
         // tells us whether there is any kind of event or completionQueue_ is shutting down.
         GPR_ASSERT(completionQueue_->Next(&tag, &ok));
         GPR_ASSERT(ok);
         static_cast<CallData*>(tag)->Proceed();
       }
     }

     std::unique_ptr<ServerCompletionQueue> completionQueue_;
     OMRSaveReplay::AsyncService service_;
     std::unique_ptr<Server> server_;
};

int main(int argc, char** argv) {
  OMRServerImpl server;
  server.RunServer();

  return 0;
}
