/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <ctime>
#include <thread>
#include <chrono>

#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>

#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <stdlib.h>
#include <unistd.h>
#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>
#include<glog/logging.h>
#define log(severity, msg) LOG(severity) << msg; google::FlushLogFiles(google::severity); 

#include "sns.grpc.pb.h"
#include "coordinator.grpc.pb.h"


using google::protobuf::Timestamp;
using google::protobuf::Duration;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;
using csce662::Message;
using csce662::ListReply;
using csce662::Request;
using csce662::Reply;
using csce662::SNSService;
using csce662::CoordService;
using csce662::Confirmation;
using csce662::ServerInfo;
using std::string;




struct Client {
  std::string username;
  bool connected = true;
  int following_file_size = 0;
  std::vector<Client*> client_followers;
  std::vector<Client*> client_following;
  ServerReaderWriter<Message, Message>* stream = 0;
  bool operator==(const Client& c1) const{
    return (username == c1.username);
  }
};

//Vector that stores every client that has been created
std::vector<Client*> client_db;
ServerInfo serverInfo;

//Helper function used to find a Client object given its username
int find_user(std::string username){
  int index = 0;
  for(Client* c : client_db){
    if(c->username == username)
      return index;
    index++;
  }
  return -1;
}

class SNSServiceImpl final : public SNSService::Service {
  
  Status List(ServerContext* context, const Request* request, ListReply* list_reply) override {
    log(INFO,"Serving List Request from: " + request->username()  + "\n");
     
    Client* user = client_db[find_user(request->username())];
 
    int index = 0;
    for(Client* c : client_db){
      list_reply->add_all_users(c->username);
    }
    std::vector<Client*>::const_iterator it;
    for(it = user->client_followers.begin(); it!=user->client_followers.end(); it++){
      list_reply->add_followers((*it)->username);
    }
    return Status::OK;
  }

  Status Follow(ServerContext* context, const Request* request, Reply* reply) override {

    std::string username1 = request->username();
    std::string username2 = request->arguments(0);
    log(INFO,"Serving Follow Request from: " + username1 + " for: " + username2 + "\n");

    int join_index = find_user(username2);
    if(join_index < 0 || username1 == username2)
      reply->set_msg("Join Failed -- Invalid Username");
    else{
      Client *user1 = client_db[find_user(username1)];
      Client *user2 = client_db[join_index];      
      if(std::find(user1->client_following.begin(), user1->client_following.end(), user2) != user1->client_following.end()){
	reply->set_msg("Join Failed -- Already Following User");
        return Status::OK;
      }
      user1->client_following.push_back(user2);
      user2->client_followers.push_back(user1);
      reply->set_msg("Follow Successful");
    }
    return Status::OK; 
  }

  Status UnFollow(ServerContext* context, const Request* request, Reply* reply) override {
    std::string username1 = request->username();
    std::string username2 = request->arguments(0);
    log(INFO,"Serving Unfollow Request from: " + username1 + " for: " + username2);
 
    int leave_index = find_user(username2);
    if(leave_index < 0 || username1 == username2) {
      reply->set_msg("Unknown follower");
    } else{
      Client *user1 = client_db[find_user(username1)];
      Client *user2 = client_db[leave_index];
      if(std::find(user1->client_following.begin(), user1->client_following.end(), user2) == user1->client_following.end()){
	reply->set_msg("You are not a follower");
        return Status::OK;
      }
      
      user1->client_following.erase(find(user1->client_following.begin(), user1->client_following.end(), user2)); 
      user2->client_followers.erase(find(user2->client_followers.begin(), user2->client_followers.end(), user1));
      reply->set_msg("UnFollow Successful");
    }
    return Status::OK;
  }

  // RPC Login
  Status Login(ServerContext* context, const Request* request, Reply* reply) override {
    Client* c = new Client();
    std::string username = request->username();
    log(INFO, "Serving Login Request: " + username + "\n");
    
    int user_index = find_user(username);
    if(user_index < 0){
      c->username = username;
      client_db.push_back(c);
      reply->set_msg("Login Successful!");
    }
    else{
      Client *user = client_db[user_index];
      if(user->connected) {
	log(WARNING, "User already logged on");
        reply->set_msg("you have already joined");
      }
      else{
        std::string msg = "Welcome Back " + user->username;
	reply->set_msg(msg);
        user->connected = true;
      }
    }
    return Status::OK;
  }

  Status Timeline(ServerContext* context, 
		ServerReaderWriter<Message, Message>* stream) override {
    log(INFO,"Serving Timeline Request");
    Message message;
    Client *c;
    while(stream->Read(&message)) {
      std::string username = message.username();
      int user_index = find_user(username);
      c = client_db[user_index];
 
      //Write the current message to "username.txt"
      std::string filename = username+".txt";
      std::ofstream user_file(filename,std::ios::app|std::ios::out|std::ios::in);
      google::protobuf::Timestamp temptime = message.timestamp();
      std::string time = google::protobuf::util::TimeUtil::ToString(temptime);
      std::string fileinput = time+" :: "+message.username()+":"+message.msg()+"\n";
      //"Set Stream" is the default message from the client to initialize the stream
      if(message.msg() != "Set Stream")
        user_file << fileinput;
      //If message = "Set Stream", print the first 20 chats from the people you follow
      else{
        if(c->stream==0)
      	  c->stream = stream;
        std::string line;
        std::vector<std::string> newest_twenty;
        std::ifstream in(username+"following.txt");
        int count = 0;
        //Read the last up-to-20 lines (newest 20 messages) from userfollowing.txt
        while(getline(in, line)){
//          count++;
//          if(c->following_file_size > 20){
//	    if(count < c->following_file_size-20){
//	      continue;
//            }
//          }
          newest_twenty.push_back(line);
        }
        Message new_msg; 
 	//Send the newest messages to the client to be displayed
 	if(newest_twenty.size() >= 40){ 	
	    for(int i = newest_twenty.size()-40; i<newest_twenty.size(); i+=2){
	       new_msg.set_msg(newest_twenty[i]);
	       stream->Write(new_msg);
	    }
        }else{
	    for(int i = 0; i<newest_twenty.size(); i+=2){
	       new_msg.set_msg(newest_twenty[i]);
	       stream->Write(new_msg);
	    }
        }
        //std::cout << "newest_twenty.size() " << newest_twenty.size() << std::endl; 
        continue;
      }
      //Send the message to each follower's stream
      std::vector<Client*>::const_iterator it;
      for(it = c->client_followers.begin(); it!=c->client_followers.end(); it++){
        Client *temp_client = *it;
      	if(temp_client->stream!=0 && temp_client->connected)
	  temp_client->stream->Write(message);
        //For each of the current user's followers, put the message in their following.txt file
        std::string temp_username = temp_client->username;
        std::string temp_file = temp_username + "following.txt";
	std::ofstream following_file(temp_file,std::ios::app|std::ios::out|std::ios::in);
	following_file << fileinput;
        temp_client->following_file_size++;
	std::ofstream user_file(temp_username + ".txt",std::ios::app|std::ios::out|std::ios::in);
        user_file << fileinput;
      }
    }
    //If the client disconnected from Chat Mode, set connected to false
    c->connected = false;
    return Status::OK;
  }

};
// Function to send repeated heartbeats to the coordinator
void sendHeartbeat(const std::string& coordinatorAddress) {
    bool isFirstTime = true;

    while (true) {
        // Establish gRPC channel to the coordinator
        std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel(coordinatorAddress, grpc::InsecureChannelCredentials());
        std::unique_ptr<csce662::CoordService::Stub> stub = csce662::CoordService::NewStub(channel);

        // Prepare the RPC call to the Heartbeat function
        grpc::ClientContext context;
        csce662::Confirmation confirmation;
        grpc::Status status = stub->Heartbeat(&context, serverInfo, &confirmation);

        if (status.ok()) {
            log(INFO, "Heartbeat sent to coordinator.");
        } else {
            log(INFO, "Couldn't send heartbeat to coordinator");
        }

        // Delay for the first heartbeat (5s) and subsequent heartbeats (10s)
        sleep(isFirstTime ? 5 : 10);
        isFirstTime = false;
    }
}

// Function to check if zNode exists in the coordinator cluster
bool exists(const std::string coordinatorAddress) {
    std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel(coordinatorAddress, grpc::InsecureChannelCredentials());
    std::unique_ptr<csce662::CoordService::Stub> stub = csce662::CoordService::NewStub(channel);

    grpc::ClientContext context;
    csce662::Status status;
    grpc::Status rpcStatus = stub->exists(&context, serverInfo, &status);

    if (status.status()) {
        log(INFO, "Server exists");
    } 

    return status.status();
}

// Function to create zNode in the coordinator cluster
void create(const std::string& coordinatorAddress) {
    std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel(coordinatorAddress, grpc::InsecureChannelCredentials());
    std::unique_ptr<csce662::CoordService::Stub> stub = csce662::CoordService::NewStub(channel);

    grpc::ClientContext context;
    csce662::Status status;
    grpc::Status rpcStatus = stub->create(&context, serverInfo, &status);

    log(INFO, status.status() ? "Server creation successful." : "Server creation failed.");
}

// Function to run the server
void RunServer(int clusterID, int serverID, const std::string coordHostname, const std::string coordPort, const std::string portNo) {
    // Initialize server information
    serverInfo.set_hostname("0.0.0.0");
    serverInfo.set_port(portNo);
    serverInfo.set_serverid(serverID);
    serverInfo.set_clusterid(clusterID);

    std::string serverAddress = "0.0.0.0:" + portNo;
    SNSServiceImpl service;

    ServerBuilder builder;
    builder.AddListeningPort(serverAddress, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<Server> server(builder.BuildAndStart());
    log(INFO, "Server listening on " + serverAddress);

    std::string coordAddress = coordHostname + ":" + coordPort;

    // Check existence and create if necessary
    if (!exists(coordAddress)) {
        log(INFO, "Creating new Server as it doesn't exist");
        create(coordAddress);
    }

    // Start heartbeat thread
    std::thread heartbeatThread(sendHeartbeat, coordAddress);

    server->Wait();
    heartbeatThread.join();
}

int main(int argc, char** argv) {
    int clusterID = 0, serverID = 0;
    std::string hostname, coordPort, portNo;

    // Parsing command-line arguments using getopt
    int opt;
    while ((opt = getopt(argc, argv, "c:s:h:k:p:")) != -1) {
        switch (opt) {
            case 'c':
                clusterID = std::atoi(optarg);
                break;
            case 's':
                serverID = std::atoi(optarg);
                break;
            case 'h':
                hostname = optarg;
                break;
            case 'k':
                coordPort = optarg;
                break;
            case 'p':
                portNo = optarg;
                break;
            default:
                std::cerr << "Invalid Command Line Argument\n";
                return 1;
        }
    }

    log(INFO, "Logging Initialized. Server: " + std::to_string(serverID) + " starting...");
    RunServer(clusterID, serverID, hostname, coordPort, portNo);

    return 0;
}
