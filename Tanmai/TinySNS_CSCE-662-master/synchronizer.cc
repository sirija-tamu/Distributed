// NOTE: This starter code contains a primitive implementation using the default RabbitMQ protocol.
// You are recommended to look into how to make the communication more efficient,
// for example, modifying the type of exchange that publishes to one or more queues, or
// throttling how often a process consumes messages from a queue so other consumers are not starved for messages
// All the functions in this implementation are just suggestions and you can make reasonable changes as long as
// you continue to use the communication methods that the assignment requires between different processes

#include <bits/fs_fwd.h>
#include <ctime>
#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>
#include <chrono>
#include <semaphore.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unordered_map>
#include <vector>
#include <unordered_set>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <mutex>
#include <stdlib.h>
#include <stdio.h>
#include <cstdlib>
#include <unistd.h>
#include <algorithm>
#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>
#include <glog/logging.h>
#include "sns.grpc.pb.h"
#include "sns.pb.h"
#include "coordinator.grpc.pb.h"
#include "coordinator.pb.h"

#include <amqp.h>
#include <amqp_tcp_socket.h>
#include <jsoncpp/json/json.h>

#define log(severity, msg) \
    LOG(severity) << msg;  \
    google::FlushLogFiles(google::severity);

namespace fs = std::filesystem;

using csce438::Confirmation;
using csce438::CoordService;
using csce438::ID;
using csce438::ServerInfo;
using csce438::AllSyncServers;
using csce438::SynchService;
using csce438::AllData;
using csce438::Empty;
using csce438::ID;
using google::protobuf::Duration;
using google::protobuf::Timestamp;
using grpc::ClientContext;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;
// tl = timeline, fl = follow list
using csce438::UserTLFL;

int synchID = 1;
int clusterID = 1;
bool isMaster = false;
int total_number_of_registered_synchronizers = 6; // update this by asking coordinator
std::string coordAddr;
std::string clusterSubdirectory;
std::vector<std::string> otherHosts;
std::unordered_map<std::string, int> timelineLengths;

std::vector<std::string> get_lines_from_file(std::string);
std::vector<std::string> get_all_users_func(int);
std::vector<std::string> get_tl_or_fl(int synchID, int clientID, bool tl);
std::vector<std::string> getFollowersOfUser(int);
bool file_contains_user(std::string filename, std::string user);
std::unordered_map<std::string, std::unordered_map<std::string, std::vector<std::string>>> others = {};
std::vector<std::string> appender(std::vector<std::string> &v, const google::protobuf::RepeatedPtrField<std::string>&  data);
void run_synchronizer(std::string coordIP, std::string coordPort, std::string port, int synchID);


void Heartbeat(std::string coordinatorIp, std::string coordinatorPort, ServerInfo serverInfo, int syncID);

std::unique_ptr<csce438::CoordService::Stub> coordinator_stub_;

class SynchServiceImpl final : public SynchService::Service {
    Status GetUserTLFL(ServerContext * context, const ID * id, AllData * alldata) override {
        log(INFO, " Serving REQ for GetUserTLFL");
        std::string master_path = "cluster_" + std::to_string(clusterID) + "/1/";
        std::vector<std::string> current_users = get_lines_from_file(master_path + "currentusers.txt");
        
        for(auto s :current_users) {
            UserTLFL usertlfl;
            usertlfl.set_user(s); 
            std::vector<std::string> tl  = get_tl_or_fl(synchID, std::stoi(s), true);
            std::vector<std::string> flr = get_tl_or_fl(synchID, std::stoi(s), false);
            for(auto timeline:tl){
                usertlfl.add_tl(timeline);
            }
            for (auto flr : flr) {
              usertlfl.add_flr(flr);
            }
            alldata->add_data()->CopyFrom(usertlfl);
        }
        return Status::OK;
    }
};

void RunServer(std::string coordIP, std::string coordPort, std::string port_no, int synchID){
  std::thread t1(run_synchronizer, coordIP, coordPort, port_no, synchID);

  //localhost = 127.0.0.1
  std::string server_address("127.0.0.1:"+port_no);
  SynchServiceImpl service;
  ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());

  std::cout << "Server listening on " << server_address << std::endl;
  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();
  t1.join();
}

int main(int argc, char **argv)
{
    int opt = 0;
    std::string coordIP;
    std::string coordPort;
    std::string port = "3029";

    while ((opt = getopt(argc, argv, "h:k:p:i:")) != -1)
    {
        switch (opt)
        {
        case 'h':
            coordIP = optarg;
            break;
        case 'k':
            coordPort = optarg;
            break;
        case 'p':
            port = optarg;
            break;
        case 'i':
            synchID = std::stoi(optarg);
            break;
        default:
            std::cerr << "Invalid Command Line Argument\n";
        }
    }

    std::string log_file_name = std::string("synchronizer-") + port;
    google::InitGoogleLogging(log_file_name.c_str());
    log(INFO, "Logging Initialized. Server starting...");

    coordAddr = coordIP + ":" + coordPort;
    clusterID = ((synchID - 1) % 3) + 1;
    ServerInfo serverInfo;
    serverInfo.set_hostname("localhost");
    serverInfo.set_port(port);
    serverInfo.set_type("synchronizer");
    serverInfo.set_serverid(synchID);
    serverInfo.set_clusterid(std::to_string(clusterID));
    Heartbeat(coordIP, coordPort, serverInfo, synchID);

    RunServer(coordIP, coordPort, port, synchID);
    return 0;
}

void run_synchronizer(std::string coordIP, std::string coordPort, std::string port, int synchID) {
    std::string target_str = coordIP + ":" + coordPort;
    std::unique_ptr<CoordService::Stub> coord_stub_;
    coord_stub_ = std::unique_ptr<CoordService::Stub>(CoordService::NewStub(grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials())));

    ServerInfo msg;
    Confirmation c;
    ClientContext context;

    msg.set_serverid(synchID);
    msg.set_hostname("127.0.0.1");
    msg.set_port(port);
    msg.set_clusterid(std::to_string(synchID));
    Empty empty;
    Confirmation confirmation;
    ID id;

    grpc::Status status = coord_stub_->RegisterSyncServer(&context, msg, &confirmation);
    if (!status.ok()) {
      log(INFO, "Coord down");
      exit(-1);
    }
    log(INFO, " Registered with coord " + confirmation.status());
    AllSyncServers allsyncservers;
    sleep(10);

    try {
      ClientContext cc;
      coord_stub_->GetSyncServers(&cc, empty, &allsyncservers);
    } catch(...) {
        std::exception_ptr p = std::current_exception();
        std::cout <<(p ? p.__cxa_exception_type()->name() : "null") << std::endl;
    }

    std::vector<std::unique_ptr<SynchService::Stub>> syncstubs;
    for (const ServerInfo serverInfo : allsyncservers.servers()) {
      if (serverInfo.clusterid() != std::to_string(synchID)) {
        syncstubs.push_back(std::move(
          SynchService::NewStub(
              grpc::CreateChannel("127.0.0.1:" + serverInfo.port(), grpc::InsecureChannelCredentials())
            )));
      }
    }
    
    std::string master_path = "cluster_" + std::to_string(synchID) + "/1/";

    while(true) {
        AllData all;
        std::vector<std::string> myusers = get_lines_from_file(master_path+"currentusers.txt");
        std::vector<std::string> allusers = get_lines_from_file(master_path+"allusers.txt");
        for (auto & stub : syncstubs) {
          ClientContext ctx;
          stub->GetUserTLFL(&ctx, id, &all);
          for(const UserTLFL &d : all.data()) {
            // std::cout << "\t\tgot data from " << d.user() << "\n";
            if (find(allusers.begin(),allusers.end(),d.user()) == allusers.end()) {
              allusers.push_back(d.user());
            }
            std::unordered_map<std::string, std::vector<std::string>> tmp = {};
            tmp["tl"] = std::vector<std::string>();
            tmp["flr"] = std::vector<std::string>();
            tmp["flw"] = std::vector<std::string>();
            if (others.find(d.user()) == others.end()) {
              others[d.user()] = tmp;
            }
            auto &otherClusterUser = others[d.user()];
            std::vector<std::string> diff = appender(otherClusterUser["flr"], d.flr());
            diff = appender(otherClusterUser["flw"],d.flw());
            for (auto &currentUserBeingFollowed : diff) {              
              if (find(myusers.begin(),myusers.end(), currentUserBeingFollowed) != myusers.end()) {
                std::string fname = master_path + "_"+currentUserBeingFollowed+"_follower.txt";
                std::ofstream oflr(fname, std::ios::app);
                oflr << d.user() << "\n";
                oflr.close();
              }
            }
            diff = appender(otherClusterUser["tl"],d.tl());
            // all current users following otherClusterUser, update their timeline
            // update the timeline messages for all followers of this user
            // same for follower & following
            std::vector<std::string> &vec = otherClusterUser["flr"];
            for (auto currentuser : myusers) {
              for (auto &msg : diff) {
                if (find(vec.begin(), vec.end(), currentuser) != vec.end()) {
                  std::string tfile = master_path +"_timeline.txt";
                  std::ofstream timeline(tfile, std::ios::app);
                  timeline << msg+"\n" << "\n";
                  timeline.close();
                }
              }
            }
          }
        }
      std::ofstream allwrite( master_path + "allusers.txt", std::ios::out);
      for (auto &al : allusers) {
        allwrite << al << "\n";
      }
      allwrite.close();
      sleep(20);
    }
    return ;
}

std::vector<std::string> appender(std::vector<std::string> &v, const google::protobuf::RepeatedPtrField<std::string>& data) {
  std::vector<std::string> diff = {};
  for (auto d : data) {
    if (find(v.begin(), v.end(), d) == v.end()) {
      v.push_back(d);
      diff.push_back(d);
    }
  }
  return diff;
}

std::vector<std::string> get_lines_from_file(std::string filename)
{
    std::vector<std::string> users;
    std::string user;
    std::ifstream file;
    std::string semName = "/" + std::to_string(clusterID) + "_" + clusterSubdirectory + "_" + filename;
    sem_t *fileSem = sem_open(semName.c_str(), O_CREAT);
    file.open(filename);
    if (file.peek() == std::ifstream::traits_type::eof())
    {
        // return empty vector if empty file
        // std::cout<<"returned empty vector bc empty file"<<std::endl;
        file.close();
        sem_close(fileSem);
        return users;
    }
    while (file)
    {
        getline(file, user);

        if (!user.empty())
            users.push_back(user);
    }

    file.close();
    sem_close(fileSem);

    return users;
}

void Heartbeat(std::string coordinatorIp, std::string coordinatorPort, ServerInfo serverInfo, int syncID)
{
    // For the synchronizer, a single initial heartbeat RPC acts as an initialization method which
    // servers to register the synchronizer with the coordinator and determine whether it is a master

    log(INFO, "Sending initial heartbeat to coordinator");
    std::string coordinatorInfo = coordinatorIp + ":" + coordinatorPort;
    std::unique_ptr<CoordService::Stub> stub = std::unique_ptr<CoordService::Stub>(CoordService::NewStub(grpc::CreateChannel(coordinatorInfo, grpc::InsecureChannelCredentials())));

    // send a heartbeat to the coordinator, which registers your follower synchronizer as either a master or a slave

    // YOUR CODE HERE
}

bool file_contains_user(std::string filename, std::string user)
{
    std::vector<std::string> users;
    // check username is valid
    std::string semName = "/" + std::to_string(clusterID) + "_" + clusterSubdirectory + "_" + filename;
    sem_t *fileSem = sem_open(semName.c_str(), O_CREAT);
    users = get_lines_from_file(filename);
    for (int i = 0; i < users.size(); i++)
    {
        // std::cout<<"Checking if "<<user<<" = "<<users[i]<<std::endl;
        if (user == users[i])
        {
            // std::cout<<"found"<<std::endl;
            sem_close(fileSem);
            return true;
        }
    }
    // std::cout<<"not found"<<std::endl;
    sem_close(fileSem);
    return false;
}

std::vector<std::string> get_all_users_func(int synchID)
{
    // read all_users file master and client for correct serverID
    // std::string master_users_file = "./master"+std::to_string(synchID)+"/all_users";
    // std::string slave_users_file = "./slave"+std::to_string(synchID)+"/all_users";
    std::string clusterID = std::to_string(((synchID - 1) % 3) + 1);
    std::string master_users_file = "./cluster_" + clusterID + "/1/all_users.txt";
    std::string slave_users_file = "./cluster_" + clusterID + "/2/all_users.txt";
    // take longest list and package into AllUsers message
    std::vector<std::string> master_user_list = get_lines_from_file(master_users_file);
    std::vector<std::string> slave_user_list = get_lines_from_file(slave_users_file);

    if (master_user_list.size() >= slave_user_list.size())
        return master_user_list;
    else
        return slave_user_list;
}

std::vector<std::string> get_tl_or_fl(int synchID, int clientID, bool tl)
{
    // std::string master_fn = "./master"+std::to_string(synchID)+"/"+std::to_string(clientID);
    // std::string slave_fn = "./slave"+std::to_string(synchID)+"/" + std::to_string(clientID);
    std::string master_fn = "cluster_" + std::to_string(clusterID) + "/1/" + std::to_string(clientID);
    std::string slave_fn = "cluster_" + std::to_string(clusterID) + "/2/" + std::to_string(clientID);
    if (tl)
    {
        master_fn.append("_timeline.txt");
        slave_fn.append("_timeline.txt");
    }
    else
    {
        master_fn.append("_followers.txt");
        slave_fn.append("_followers.txt");
    }

    std::vector<std::string> m = get_lines_from_file(master_fn);
    std::vector<std::string> s = get_lines_from_file(slave_fn);

    if (m.size() >= s.size())
    {
        return m;
    }
    else
    {
        return s;
    }
}

std::vector<std::string> getFollowersOfUser(int ID)
{
    std::vector<std::string> followers;
    std::string clientID = std::to_string(ID);
    std::vector<std::string> usersInCluster = get_all_users_func(synchID);

    for (auto userID : usersInCluster)
    { // Examine each user's following file
        std::string file = "cluster_" + std::to_string(clusterID) + "/" + clusterSubdirectory + "/" + userID + "_follow_list.txt";
        std::string semName = "/" + std::to_string(clusterID) + "_" + clusterSubdirectory + "_" + userID + "_follow_list.txt";
        sem_t *fileSem = sem_open(semName.c_str(), O_CREAT);
        // std::cout << "Reading file " << file << std::endl;
        if (file_contains_user(file, clientID))
        {
            followers.push_back(userID);
        }
        sem_close(fileSem);
    }

    return followers;
}
