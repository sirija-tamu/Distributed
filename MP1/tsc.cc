#include <iostream>
#include <memory>
#include <thread>
#include <vector>
#include <string>
#include <unistd.h>
#include <csignal>
#include <grpc++/grpc++.h>
#include "client.h"

#include "sns.grpc.pb.h"
using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;
using csce662::Message;
using csce662::ListReply;
using csce662::Request;
using csce662::Reply;
using csce662::SNSService;

void sig_ignore(int sig) {
  std::cout << "Signal caught " + sig;
}

Message MakeMessage(const std::string& username, const std::string& msg) {
    Message m;
    m.set_username(username);
    m.set_msg(msg);
    google::protobuf::Timestamp* timestamp = new google::protobuf::Timestamp();
    timestamp->set_seconds(time(NULL));
    timestamp->set_nanos(0);
    m.set_allocated_timestamp(timestamp);
    return m;
}


class Client : public IClient
{
public:
  Client(const std::string& hname,
	 const std::string& uname,
	 const std::string& p)
    :hostname(hname), username(uname), port(p) {}

  
protected:
  virtual int connectTo();
  virtual IReply processCommand(std::string& input);
  virtual void processTimeline();

private:
  std::string hostname;
  std::string username;
  std::string port;
  
  // You can have an instance of the client stub
  // as a member variable.
  std::unique_ptr<SNSService::Stub> stub_;
  
  IReply Login();
  IReply List();
  IReply Follow(const std::string &username);
  IReply UnFollow(const std::string &username);
  void   Timeline(const std::string &username);
};


///////////////////////////////////////////////////////////
//
//////////////////////////////////////////////////////////
int Client::connectTo()
{
  // ------------------------------------------------------------
  // In this function, you are supposed to create a stub so that
  // you call service methods in the processCommand/porcessTimeline
  // functions. That is, the stub should be accessible when you want
  // to call any service methods in those functions.
  // Please refer to gRpc tutorial how to create a stub.
  // ------------------------------------------------------------

  // Server address and port
  std::string server_address = hostname + ":" + port; 

  // Create a channel to the gRPC server
  auto channel = grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials());

  // Create a stub for the SNSService
  stub_ = csce662::SNSService::NewStub(channel);

  IReply ire = Client::Login();

  // Check if the stub is created successfully
  if (ire.comm_status != IStatus::SUCCESS) {
      std::cout << "Connected to gRPC server at " << server_address << std::endl;
      return 1;  // Success
  } else {
      std::cerr << "Failed to create gRPC stub." << std::endl;
      return -1; // Failure
  }
}

IReply Client::processCommand(std::string& input)
{
  // ------------------------------------------------------------
  // GUIDE 1:
  // In this function, you are supposed to parse the given input
  // command and create your own message so that you call an 
  // appropriate service method. The input command will be one
  // of the followings:
  //
  // FOLLOW <username>
  // UNFOLLOW <username>
  // LIST
  // TIMELINE
  // ------------------------------------------------------------
  
  // ------------------------------------------------------------
  // GUIDE 2:
  // Then, you should create a variable of IReply structure
  // provided by the client.h and initialize it according to
  // the result. Finally you can finish this function by returning
  // the IReply.
  // ------------------------------------------------------------
  
  
  // ------------------------------------------------------------
  // HINT: How to set the IReply?
  // Suppose you have "FOLLOW" service method for FOLLOW command,
  // IReply can be set as follow:
  // 
  //     // some codes for creating/initializing parameters for
  //     // service method
  //     IReply ire;
  //     grpc::Status status = stub_->FOLLOW(&context, /* some parameters */);
  //     ire.grpc_status = status;
  //     if (status.ok()) {
  //         ire.comm_status = SUCCESS;
  //     } else {
  //         ire.comm_status = FAILURE_NOT_EXISTS;
  //     }
  //      
  //      return ire;
  // 
  // IMPORTANT: 
  // For the command "LIST", you should set both "all_users" and 
  // "following_users" member variable of IReply.
  // ------------------------------------------------------------

    IReply ire;
    size_t space_pos = input.find(' ');
    std::string command = (space_pos == std::string::npos) ? input : input.substr(0, space_pos);
    if (command == "FOLLOW") {
        std::string username = (space_pos == std::string::npos) ? "" : input.substr(space_pos + 1);
        return Client::Follow(username);
    } else if (command == "UNFOLLOW") {
        std::string username = (space_pos == std::string::npos) ? "" : input.substr(space_pos + 1);
        return ire = Client::UnFollow(username);
    } else if (command == "LIST") {
        return  Client::List();
    } else if (command == "TIMELINE") {
        Client::processTimeline();
    } else {
        ire.comm_status = IStatus::FAILURE_INVALID;
    }
    return ire;
}



void Client::processTimeline()
{
    Timeline(username);
}

// List Command
IReply Client::List() {

    IReply ire;
    // Prepare the request object
    Request request;
    // Setting the username
    request.set_username(username); 
    // Create a ListReply response object
    ListReply list_reply;
    // Create a ClientContext for managing the gRPC call
    grpc::ClientContext context;
    // Make the gRPC call
    grpc::Status status = stub_->List(&context, request, &list_reply);

    // Check if the gRPC call was successful
    if (status.ok()) {
        // Process the response
        ire.all_users = std::vector<std::string>(list_reply.all_users().begin(), list_reply.all_users().end());
        ire.followers = std::vector<std::string>(list_reply.followers().begin(), list_reply.followers().end());
    } 

    ire.comm_status = IStatus::SUCCESS;
    ire.grpc_status = status;
    return ire;
}

// Follow Command        
IReply Client::Follow(const std::string& username2) {

    IReply ire; 

    // Prepare the request object
    Request request;
    request.set_username(username); // The user initiating the follow
    request.add_arguments(username2); // The user to be followed

    // Create a FollowReply response object
    Reply follow_reply;
    // Create a ClientContext for managing the gRPC call
    grpc::ClientContext context;

    // Make the gRPC call
    grpc::Status status = stub_->Follow(&context, request, &follow_reply);

    if (status.ok()) {
          const std::string& msg = follow_reply.msg();

          if (msg.find("FAILURE_INVALID_USERNAME") != std::string::npos) {
              ire.comm_status = IStatus::FAILURE_INVALID_USERNAME;
          } else if (msg.find("FAILURE_ALREADY_EXISTS") != std::string::npos) {
              ire.comm_status = IStatus::FAILURE_ALREADY_EXISTS;
          } else if (msg.find("SUCCESS") != std::string::npos) {
              ire.comm_status = IStatus::SUCCESS;
          } else {
              ire.comm_status = IStatus::FAILURE_UNKNOWN;
          }
    } else {
          ire.comm_status = IStatus::FAILURE_UNKNOWN;
    }
    ire.grpc_status = status;
    return ire;
}

// UNFollow Command  
IReply Client::UnFollow(const std::string& username2) {

    IReply ire;

    /***
    YOUR CODE HERE
    ***/
    std::cout << "Unfollow not implemented yet ";
    return ire;
}

// Login Command  
IReply Client::Login() {

    IReply ire;
  
        // Prepare the request object
    Request request;
    request.set_username(username); // The user initiating the follow

    // Create a LoginReply response object
    Reply login_reply;
    // Create a ClientContext for managing the gRPC call
    grpc::ClientContext context;

    // Make the gRPC call
    grpc::Status status = stub_->Login(&context, request, &login_reply);

    if (status.ok()) {
          const std::string& msg = login_reply.msg();
          if (msg.find("FAILURE_ALREADY_EXISTS") != std::string::npos) {
              ire.comm_status = IStatus::FAILURE_ALREADY_EXISTS;
          } else if (msg.find("SUCCESS") != std::string::npos) {
              ire.comm_status = IStatus::SUCCESS;
          } else {
              ire.comm_status = IStatus::FAILURE_UNKNOWN;
          }
    } else {
          ire.comm_status = IStatus::FAILURE_UNKNOWN;
    }
    ire.grpc_status = status;
    return ire;
}

// Timeline Command
void Client::Timeline(const std::string& username) {

    // ------------------------------------------------------------
    // In this function, you are supposed to get into timeline mode.
    // You may need to call a service method to communicate with
    // the server. Use getPostMessage/displayPostMessage functions 
    // in client.cc file for both getting and displaying messages 
    // in timeline mode.
    // ------------------------------------------------------------

    // ------------------------------------------------------------
    // IMPORTANT NOTICE:
    //
    // Once a user enter to timeline mode , there is no way
    // to command mode. You don't have to worry about this situation,
    // and you can terminate the client program by pressing
    // CTRL-C (SIGINT)
    // ------------------------------------------------------------
  
    /***
    YOUR CODE HERE
    ***/
   std::cout << "Timeline not implemented yet ";



}



//////////////////////////////////////////////
// Main Function
/////////////////////////////////////////////
int main(int argc, char** argv) {

  std::string hostname = "localhost";
  std::string username = "default";
  std::string port = "3010";
    
  int opt = 0;
  while ((opt = getopt(argc, argv, "h:u:p:")) != -1){
    switch(opt) {
    case 'h':
      hostname = optarg;break;
    case 'u':
      username = optarg;break;
    case 'p':
      port = optarg;break;
    default:
      std::cout << "Invalid Command Line Argument\n";
    }
  }
      
  std::cout << "Logging Initialized. Client starting...";
  
  Client myc(hostname, username, port);
  
  myc.run();
  
  return 0;
}
