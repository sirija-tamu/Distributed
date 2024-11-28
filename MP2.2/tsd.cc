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

#include <cstddef>
#include <cstdlib>
#include <thread>
#include <cstdio>
#include <ctime>
#include <csignal>

#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>

#include <fstream>
#include <iostream>
#include <list>
#include <memory>
#include <string>
#include <stdlib.h>
#include <unistd.h>
#include <unordered_map>
#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>
#include<glog/logging.h>
#define log(severity, msg) LOG(severity) << msg; google::FlushLogFiles(google::severity); 

#include "sns.grpc.pb.h"
#include "coordinator.grpc.pb.h"
#include "coordinator.pb.h"
#include "client.h"
#include <filesystem>
namespace fs = std::filesystem;


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
using csce662::ServerInfo;

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;


struct Client {
    std::string username;
    bool connected = true;
    int following_file_size = 0;
    std::vector<Client*> client_followers;
    std::vector<Client*> client_following;
    ServerReaderWriter<Message, Message>* stream = 0;
    // adding these two new variables below to monitor client heartbeats
    std::time_t last_heartbeat;
    bool missed_heartbeat = false;
    bool operator==(const Client& c1) const{
        return (username == c1.username);
    }
};

void checkHeartbeat();
std::time_t getTimeNow();

std::unique_ptr<csce662::CoordService::Stub> coordinator_stub_;

// coordinator rpcs
IReply Heartbeat(std::string clusterId, std::string serverId, std::string hostname, std::string port);
ServerInfo serverInfo;
std::unordered_set<std::string> current_users = {};
std::unordered_map<std::string, std::vector<std::string>> user_timeline = {};

std::vector<std::string> get_lines_from_file(std::string filename) {
  std::vector<std::string> users;
  std::string user;
  std::ifstream file; 
  file.open(filename);
  if(file.peek() == std::ifstream::traits_type::eof()){
    file.close();
    return users;
  }
  while(file){
    getline(file,user);

    if(!user.empty())
      users.push_back(user);
  } 

  file.close();

  return users;
}


bool isincurrent(std::string username) {
  int cluster = ((atoi(username.c_str()))-1)%3+1;
  return cluster == serverInfo.clusterid();

}

std::string getfilename(std::string clientid = "current_cluster") {
    std::string folder = serverInfo.clusterdirectory();
    std::string path = "cluster_" + std::to_string(serverInfo.clusterid())+ "/" + folder +  "/";
    if(clientid == "current_cluster") {
      return path + "current_cluster_users.txt";
    }
    else if (clientid == "all") {
      return path + "all_users.txt";
    } 
    return path + clientid;
}

std::string getprimaryfilename(std::string clientid = "current_cluster", bool primary=false) {
    std::string folder = "2";
    if(primary){
      std::string folder = "1";
    }
    std::string path = "cluster_" + std::to_string(serverInfo.clusterid())+ "/" + folder +  "/";
    if(clientid == "current_cluster") {
      return path + "current_cluster_users.txt";
    }
    else if (clientid == "all") {
      return path + "all_users.txt";
    } 
    return path + clientid;
}


void copier(){
    std::vector<std::string> cusers;
    std::string tmp;
    std::vector<std::string> fnames = {"current_cluster", "all"};
    for (auto s : fnames) {
      std::ifstream source(getprimaryfilename(s, true), std::ios::binary);
      std::ofstream dest(getprimaryfilename(s, false), std::ios::binary);
      dest << source.rdbuf();
      source.close(); dest.close();
    }
    std::ifstream source(getprimaryfilename("current_cluster", true), std::ios::in);
    while(getline(source, tmp)) {
      cusers.push_back(tmp);
      current_users.insert(tmp);
    }
    source.close();
    fnames = {"_timeline.txt", "_followers.txt", "_follow_list.txt", ".txt"};
    for (auto s : cusers) {
      std::string base = getprimaryfilename(s,true);
      std::string dbase = getprimaryfilename(s, false);
      for (auto fn : fnames) {
        std::ifstream source (base  + fn,  std::ios::binary);
        std::ofstream dest   (dbase + fn,  std::ios::binary);
        dest << source.rdbuf();
        source.close(); dest.close();
      }
    }
}


//Vector that stores every client that has been created
/* std::vector<Client*> client_db; */
// using an unordered map to store clients rather than a vector as this allows for O(1) accessing and O(1) insertion
std::unordered_map<std::string, Client*> client_db;

// util function for checking if a client exists in the client_db and fetching it if it does
Client* getClient(std::string username){
    auto it = client_db.find(username);

    if (it != client_db.end()) {
        return client_db[username];
    } else {
        return nullptr;
    }
}

class SNSServiceImpl final : public SNSService::Service {

    Status ClientHeartbeat(ServerContext* context, const Request* request, Reply* reply) override {

        std::string username = request->username();

        /*std::cout << "got a heartbeat from client: " << username << std::endl;*/
        Client* c = getClient(username);
        if (c != NULL){
            c->connected = true;
            c->last_heartbeat = getTimeNow();

        } else {
            std::cout << "client was not found, for some reason!\n";
            return Status::CANCELLED;
        }

        return Status::OK;
    }

    Status List(ServerContext* context, const Request* request, ListReply* list_reply) override {
        std::cout << " Serving List Request from: " << request->username() << "\n";
        std::ifstream in(getfilename("all"), std::ios::in | std::ios::out);
        std::string user;
        while(getline(in,user)) {
        list_reply->add_all_users(user);
        }
        in.close();
        in = std::ifstream(getfilename(request->username())+"_followers.txt");
        std::cout << "follower: \n";
        while(getline(in,user)) {
        std::cout << "\t" << user << "\n";
        list_reply->add_followers(user);
        }
        in.close();  
        return Status::OK;
    }

    Status Follow(ServerContext* context, const Request* request, Reply* reply) override {
        std::string username1 = request->username();
        std::string username2 = request->arguments(0);
        log(INFO,"Serving Follow Request from: " + username1 + " for: " + username2 + "\n");
        if(username1 == username2)
            reply->set_msg("Join Failed -- Invalid Username");
        else {
            Client *user1 = getClient(username1);
            Client *user2 = getClient(username2);
            if (user2 != nullptr) {
                user1->client_following.push_back(user2);
                user2->client_followers.push_back(user1);
                std::ofstream flr(getfilename(user2->username)+"_followers.txt", std::ios::app);
                flr << username1 << "\n";
                flr.close();
            }
            std::ofstream flw(getfilename(user1->username)+"_follow_list.txt", std::ios::app);
            flw << username2 << "\n";
            flw.close();
            reply->set_msg("Follow Successful");
        }
        return Status::OK;
    }

    Status UnFollow(ServerContext* context, const Request* request, Reply* reply) override {

        std::string username = request->username();
        // using a multimap to fetch the metadata out of the client's servercontext so we can check to see if a SIGINT was issued on the client's timeline
        const std::multimap<grpc::string_ref, grpc::string_ref>& metadata = context->client_metadata();

        auto it = metadata.find("terminated");
        if (it != metadata.end()) {
            std::string customValue(it->second.data(), it->second.length());

            std::string termStatus = customValue; // checking the value of the key "terminated" from the metadata in servercontext
            if (termStatus == "true"){

                Client* c = getClient(username);
                if (c != NULL){ // if the client exists, change its connection status and set its stream to null
                    c->last_heartbeat = getTimeNow();
                    c->connected = false;
                    c->stream = nullptr;
                }
                // DO NOT CONTINUE WITH UNFOLLOW AFTER THIS
                // Terminate here as this was not an actual unfollow request and just a makeshift way to handle SIGINTs on the client side
                return Status::OK;
            }

        }


        std::string u1 = request->username();
        std::string u2 = request->arguments(0);
        Client* c1 = getClient(u1);
        Client* c2 = getClient(u2);


        if (c1 == nullptr || c2 == nullptr) {
            return Status(grpc::CANCELLED, "invalid username");
        }

        if (c1 == c2){
            return Status(grpc::CANCELLED, "same client");
        }


        // Find and erase c2 from c1's following
        auto it1 = std::find(c1->client_following.begin(), c1->client_following.end(), c2);
        if (it1 != c1->client_following.end()) {
            c1->client_following.erase(it1);
        } else {
            return Status(grpc::CANCELLED, "not following");
        }

        // if it gets here, it means it was following the other client
        // Find and erase c1 from c2's followers
        auto it2 = std::find(c2->client_followers.begin(), c2->client_followers.end(), c1);
        if (it2 != c2->client_followers.end()) {
            c2->client_followers.erase(it2);
        }

        return Status::OK;
    }

    // RPC Login
    Status Login(ServerContext* context, const Request* request, Reply* reply) override {
        std::cout<<"Serving Login Request: "<< request->username() << "\n";
        Client* c = new Client();
        std::string username = request->username();
        
        if(getClient(username) == nullptr){
          c->username = username;
          client_db[username] = c;
          reply->set_msg("Login Successful!");
          for (auto c : current_users) {
              std::cout << " prev current " << c << "\n";
          }
          c->stream = nullptr;
          c->connected = false;
          c->missed_heartbeat = false;
          c->last_heartbeat = getTimeNow();
            if(current_users.find(username) == current_users.end()) {
              std::ofstream current(getfilename("current_cluster"),std::ios::app|std::ios::out|std::ios::in);
              current << username << "\n";
              current.close();
              current_users.insert(username);
              std::ofstream all(getfilename("all"),std::ios::app|std::ios::out|std::ios::in);
              all << username << "\n";
              all.close();
          }
        } else {
          std::cout << " User found in getClient " << c << "\n";
          Client *user = getClient(username);
          current_users.insert(user->username);
          user->stream = nullptr;
          if(user->connected) {
              log(WARNING, "User already logged on");
              std::string msg = "Welcome Back " + user->username;
              reply->set_msg(msg);
          } else{
              std::string msg = "Welcome Back " + user->username;
              reply->set_msg(msg);
              user->connected = true;
          }
        }
        
        return Status::OK;
    }

    const int MAX_MESSAGES = 20;

  Status Timeline(ServerContext* context, ServerReaderWriter<Message, Message>* stream) override {
    log(INFO,"Serving Timeline Request");
    Message message;
    Client *c;
        while(stream->Read(&message)) {
      std::string username = message.username();
c = getClient(username);
      c->stream = stream;
      std::string filename = getfilename(username) + "_timeline.txt";
      std::ofstream user_file(filename, std::ios::app|std::ios::out|std::ios::in);
      google::protobuf::Timestamp temptime = message.timestamp();
      // std::string time = google::protobuf::util::TimeUtil::ToString(temptime);
      std::time_t time = temptime.seconds();
      std::string tstr = std::ctime(&time);
      tstr.pop_back();
      std::string fileinput = message.username() + std::string(" (") + tstr + std::string(") >> ") + message.msg() + "\n";
      //"Set Stream" is the default message from the client to initialize the stream
      if(message.msg() != "Set Stream") {
        user_file << fileinput;
        user_file.close();
      }
      //If message = "Set Stream", print the first 20 chats from the people you follow
      else {
        std::string line;
        std::vector<std::string> newest_twenty;
        std::ifstream in(getfilename(username)+"_timeline.txt");
        int count = 0;
        //Read the last up-to-20 lines (newest 20 messages) from userfollowing.txt
        while(getline(in, line)){
          newest_twenty.push_back(line);
        }
        Message new_msg; 
 	      //Send the newest messages to the client to be displayed
 	      if(newest_twenty.size() >= 40) {
          for(int i = newest_twenty.size()-40; i<newest_twenty.size(); i+=2){
            new_msg.set_msg(newest_twenty[i]);
            stream->Write(new_msg);
          }
        } else{
          for(int i = 0; i < newest_twenty.size(); i += 2) {
            new_msg.set_msg(newest_twenty[i]);
            stream->Write(new_msg);
	        }
        }
        continue;
      }
      //Send the message to each follower's stream in current_cluster cluster
      std::vector<Client*>::const_iterator it;
      std::ifstream infile(getfilename(c->username)+"_followers.txt");
      std::string follower;
      while (getline(infile, follower)) {
        Client * temp_client = getClient(follower);
        if (temp_client != nullptr) {
          if(temp_client->stream) {
            Message new_msg;
            new_msg.set_msg(fileinput);
            temp_client->stream->Write(new_msg);
          }
          std::string temp_username = temp_client->username;
          if (isincurrent(temp_username)) {
              std::string temp_file = getfilename(temp_username) + "_timeline.txt";
              std::ofstream user_file(temp_file,std::ios::app|std::ios::out|std::ios::in);
              user_file << fileinput;
              user_file.close();
          }
        }
      }
    }
    //If the client disconnected from Chat Mode, set connected to false
    c->connected = false;
    return Status::OK;
  }
  
};

// function that sends a heartbeat to the coordinator
IReply Heartbeat(std::string clusterId, std::string serverId, std::string hostname, std::string port, bool isHeartbeat) {

    IReply ire;

    // creating arguments and utils to make the gRPC
    ClientContext context;
    csce662::ServerInfo serverinfo;
    csce662::Confirmation confirmation;


    if (isHeartbeat){
        context.AddMetadata("heartbeat", "Hello"); // adding the server's clusterId in the metadata so the coordinator can know
    } else {
        serverinfo.set_type("server");
    }

    context.AddMetadata("clusterid", clusterId); // adding the server's clusterId in the metadata so the coordinator can know

    int intServerId = std::stoi(serverId);

    serverinfo.set_serverid(intServerId);
    serverinfo.set_hostname(hostname);
    serverinfo.set_port(port);
    serverinfo.set_clusterid(std::atoi(clusterId.c_str()));
    grpc::Status status = coordinator_stub_->Heartbeat(&context, serverinfo, &confirmation);
    if (status.ok()){
        ire.grpc_status = status;
        serverInfo.set_type(confirmation.type());
          if (!isHeartbeat){
              serverInfo.set_clusterdirectory(confirmation.clusterdirectory());
              std::cout << "Set the clusterdirectory"<<confirmation.clusterdirectory()<<"\n";
          }
        if (confirmation.type() == "master" && serverInfo.clusterdirectory() == "2") {
            copier();
            std::cout << "Master Server is down, wait for copying\n";
        }
        log(INFO, "Got confirmation from cooridinator  now type=" + confirmation.type());
    }else { // professor said in class that since the servers cannot be run without a coordinator, you should exit

        ire.grpc_status = status;
        std::cout << "coordinator not found! exiting now...\n";
    }

    return ire;
}

// function that runs inside a detached thread that calls the heartbeat function
void sendHeartbeat(std::string clusterId, std::string serverId, std::string hostname, std::string port) {
    while (true){

        sleep(3);

        IReply reply = Heartbeat(clusterId, serverId, "localhost", port, true);

        if (!reply.grpc_status.ok()){
                        exit(1);
        }
    }

}

void updateTimelineStream() {
  while (true) {
    std::vector<std::string> current_user_list = get_lines_from_file(getfilename("current_cluster"));
    for (auto &c : current_user_list) {
      std::string file_name = getfilename(c) + "_timeline.txt";
      std::ifstream ifs((file_name), std::ios::in);
      std::string tmp;
      std::vector<std::string> tl;
      while (getline(ifs, tmp))
        tl.push_back(tmp);
      ifs.close();

      std::vector<std::string> msg = {};
      if (user_timeline.find(c) == user_timeline.end())
        user_timeline[c] = {};
      for (int i = 0; i < tl.size(); i+=1) {
        std::string msg = tl[i];
        if(!msg.empty() && msg.size() > 2){
          std::string name = msg.substr(0, 1);
          if ((find(user_timeline[c].begin(), user_timeline[c].end(), msg) == user_timeline[c].end()) &&
              find(current_user_list.begin(), current_user_list.end(), name) == current_user_list.end()
              ) {
                      user_timeline[c].push_back(tl[i]);
            Client* tmp = getClient(c);
            if (tmp != nullptr) {
              if(tmp->connected && tmp->stream) {
                try {
                  Message new_msg;
                  new_msg.set_msg(msg);
                  ServerReaderWriter<Message, Message>* tt = tmp->stream;
                  std::cout<<"new_msg\n";
                  tmp->stream->Write(new_msg);
                } catch(...) {
                  std::cout << "Error in Write\n";
                }
              }
            }
          }
        }
      }
    }
    sleep(15);
  }
}

void RunServer(std::string clusterId, std::string serverId, std::string coordinatorIP, std::string coordinatorPort, std::string port_no) {
    std::string server_address = "0.0.0.0:"+port_no;
    SNSServiceImpl service;

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Server listening on " << server_address << std::endl;
    log(INFO, "Server listening on "+server_address);


    // FROM WHAT I UNDERSTAND, THIS IS THE BEST PLACE TO REGISTER WITH THE COORDINATOR

    // need to first create a stub to communicate with the coordinator to get the info of the server to connect to
    std::string coordinator_address = coordinatorIP + ":" + coordinatorPort;
    grpc::ChannelArguments channel_args;
    std::shared_ptr<grpc::Channel> channel = grpc::CreateCustomChannel(
            coordinator_address, grpc::InsecureChannelCredentials(), channel_args);

    // Instantiate the coordinator stub
    coordinator_stub_ = csce662::CoordService::NewStub(channel);
    IReply reply = Heartbeat(clusterId, serverId, "localhost", port_no, false);
    if (!reply.grpc_status.ok()){
        // EXITING AS COORDINATOR WAS NOT REACHABLE
        exit(0);
    }

    std::vector<std::string> current = get_lines_from_file(getfilename("current_cluster"));
    for (auto s : current) {
        current_users.insert(s);
    }
    // running the heartbeat function to monitor heartbeats from the clients
    std::thread hb(checkHeartbeat);
    // running a thread to periodically send a heartbeat to the coordinator
    std::thread myhb(sendHeartbeat, clusterId, serverId, "localhost", port_no);
    std::thread timelineThread(updateTimelineStream);

    myhb.detach();
    timelineThread.detach();


    server->Wait();
}


void checkHeartbeat(){
    while(true){
        //check clients for heartbeat > 3s

        for (const auto& pair : client_db){
            if(difftime(getTimeNow(),pair.second->last_heartbeat) > 5){
                std::cout << "missed heartbeat from client with id " << pair.first << std::endl;
                if(!pair.second->missed_heartbeat){
                    Client* current = getClient(pair.first);
                    if (current != NULL){
                        std::cout << "setting the client's values in the DB to show that it is down!\n";
                        current->connected = false;
                        current->stream = nullptr;
                        current->missed_heartbeat = true;
                        current->last_heartbeat = getTimeNow();
                    } else{
                        std::cout << "SUDDENLY, THE CLIENT CANNOT BE FOUND?!\n";
                    }
                }
            }
        }

        sleep(3);
    }
}

void createClusterDirectories(std::string clusterID) {
    // Construct the base path
    std::string basePath = "cluster_" + clusterID;

    // Construct paths for /1/ and /2/
    std::string path1 = basePath + "/1/";
    std::string path2 = basePath + "/2/";

    try {
        // Create /1/ directory
        if (!fs::exists(path1)) {
            fs::create_directories(path1);
            std::cout << "Directory created: " << path1 << std::endl;
        } else {
            std::cout << "Directory already exists: " << path1 << std::endl;
        }

        // Create /2/ directory
        if (!fs::exists(path2)) {
            fs::create_directories(path2);
            std::cout << "Directory created: " << path2 << std::endl;
        } else {
            std::cout << "Directory already exists: " << path2 << std::endl;
        }
    } catch (const std::exception &e) {
        std::cerr << "Error creating directories: " << e.what() << std::endl;
    }
}


int main(int argc, char** argv) {

    std::string clusterId = "1";
    std::string serverId = "1";
    std::string coordinatorIP = "localhost";
    std::string coordinatorPort = "9090";
    std::string port = "1000";

    int opt = 0;
    while ((opt = getopt(argc, argv, "c:s:h:k:p:")) != -1){
        switch(opt) {
            case 'c':
                clusterId = optarg;break;
            case 's':
                serverId = optarg;break;
            case 'h':
                coordinatorIP = optarg;break;
            case 'k':
                coordinatorPort = optarg;break;
            case 'p':
                port = optarg;break;
            default:
                std::cout << "Invalid Command Line Argument\n";
        }
    }

    createClusterDirectories(clusterId);
    std::string log_file_name = std::string("server-") + port;
    google::InitGoogleLogging(log_file_name.c_str());
    log(INFO, "Logging Initialized. Server starting...");
    
    serverInfo.set_serverid(std::atoi(serverId.c_str()));
    serverInfo.set_hostname("localhost");
    serverInfo.set_port(port);
    serverInfo.set_clusterid(std::stoi(clusterId)); // Convert string to int using std::stoi
    std::cout << serverInfo.clusterid() << "\n";
    /* RunServer(port); */
    // changing this call so i can pass other auxilliary variables to be able to communicate with the server
    RunServer(clusterId, serverId, coordinatorIP, coordinatorPort, port);

    return 0;
}

std::time_t getTimeNow(){
    return std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
}
