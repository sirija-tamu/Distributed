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
std::vector<std::string> get_tl_or_fl(int synchID, int clientID, std::string name);
std::vector<std::string> getFollowersOfUser(int);
bool file_contains_user(std::string filename, std::string user);
std::unordered_map<std::string, std::unordered_map<std::string, std::vector<std::string>>> others = {};
std::vector<std::string> appender(std::vector<std::string> &v, const google::protobuf::RepeatedPtrField<std::string>&  data);
void run_synchronizer(std::string coordIP, std::string coordPort, std::string port, int synchID);


void Heartbeat(std::string coordinatorIp, std::string coordinatorPort, ServerInfo serverInfo, int syncID);

std::unique_ptr<csce438::CoordService::Stub> coordinator_stub_;

class SynchronizerRabbitMQ
{
private:
    amqp_connection_state_t conn;
    amqp_channel_t channel;
    std::string hostname;
    int port;
    int synchID;

    void setupRabbitMQ()
    {
        conn = amqp_new_connection();
        amqp_socket_t *socket = amqp_tcp_socket_new(conn);
        amqp_socket_open(socket, hostname.c_str(), port);
        amqp_login(conn, "/", 0, 131072, 0, AMQP_SASL_METHOD_PLAIN, "guest", "guest");
        amqp_channel_open(conn, channel);
    }

    void declareQueue(const std::string &queueName)
    {
        amqp_queue_declare(conn, channel, amqp_cstring_bytes(queueName.c_str()),
                           0, 0, 0, 0, amqp_empty_table);
    }

    void publishMessage(const std::string &queueName, const std::string &message)
    {
        amqp_basic_publish(conn, channel, amqp_empty_bytes, amqp_cstring_bytes(queueName.c_str()),
                           0, 0, NULL, amqp_cstring_bytes(message.c_str()));
    }

    std::string consumeMessage(const std::string &queueName, int timeout_ms = 5000)
    {
        amqp_basic_consume(conn, channel, amqp_cstring_bytes(queueName.c_str()),
                           amqp_empty_bytes, 0, 1, 0, amqp_empty_table);

        amqp_envelope_t envelope;
        amqp_maybe_release_buffers(conn);

        struct timeval timeout;
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_usec = (timeout_ms % 1000) * 1000;

        amqp_rpc_reply_t res = amqp_consume_message(conn, &envelope, &timeout, 0);

        if (res.reply_type != AMQP_RESPONSE_NORMAL)
        {
            return "";
        }

        std::string message(static_cast<char *>(envelope.message.body.bytes), envelope.message.body.len);
        amqp_destroy_envelope(&envelope);
        return message;
    }

public:
    SynchronizerRabbitMQ(const std::string &host, int p, int id) : hostname(host), port(p), channel(1), synchID(id)
    {
        setupRabbitMQ();
        declareQueue("synch" + std::to_string(synchID) + "_users_queue");
        declareQueue("synch" + std::to_string(synchID) + "_clients_relations_queue");
        declareQueue("synch" + std::to_string(synchID) + "_timeline_queue");
        // TODO: add or modify what kind of queues exist in your clusters based on your needs
    }

    void publishUserList()
    {
        std::vector<std::string> users = get_all_users_func(synchID);
        std::sort(users.begin(), users.end());
        Json::Value userList;
        for (const auto &user : users)
        {
            userList["users"].append(user);
        }
        Json::FastWriter writer;
        std::string message = writer.write(userList);
        publishMessage("synch" + std::to_string(synchID) + "_users_queue", message);
    }

    void consumeUserLists()
    {
        std::vector<std::string> allUsers;
        // YOUR CODE HERE

        // TODO: while the number of synchronizers is harcorded as 6 right now, you need to change this
        // to use the correct number of follower synchronizers that exist overall
        // accomplish this by making a gRPC request to the coordinator asking for the list of all follower synchronizers registered with it
        for (int i = 1; i <= 6; i++)
        {
            std::string queueName = "synch" + std::to_string(i) + "_users_queue";
            std::string message = consumeMessage(queueName, 1000); // 1 second timeout
            if (!message.empty())
            {
                Json::Value root;
                Json::Reader reader;
                if (reader.parse(message, root))
                {
                    for (const auto &user : root["users"])
                    {
                        allUsers.push_back(user.asString());
                    }
                }
            }
        }
        updateAllUsersFile(allUsers);
    }

    void publishClientRelations()
    {
        Json::Value relations;
        std::vector<std::string> users = get_all_users_func(synchID);

        for (const auto &client : users)
        {
            int clientId = std::stoi(client);
            std::vector<std::string> followers = getFollowersOfUser(clientId);

            Json::Value followerList(Json::arrayValue);
            for (const auto &follower : followers)
            {
                followerList.append(follower);
            }

            if (!followerList.empty())
            {
                relations[client] = followerList;
            }
        }

        Json::FastWriter writer;
        std::string message = writer.write(relations);
        publishMessage("synch" + std::to_string(synchID) + "_clients_relations_queue", message);
    }

    void consumeClientRelations()
    {
        std::vector<std::string> allUsers = get_all_users_func(synchID);

        // YOUR CODE HERE

        // TODO: hardcoding 6 here, but you need to get list of all synchronizers from coordinator as before
        for (int i = 1; i <= 6; i++)
        {

            std::string queueName = "synch" + std::to_string(i) + "_clients_relations_queue";
            std::string message = consumeMessage(queueName, 1000); // 1 second timeout

            if (!message.empty())
            {
                Json::Value root;
                Json::Reader reader;
                if (reader.parse(message, root))
                {
                    for (const auto &client : allUsers)
                    {
                        std::string followerFile = "./cluster_" + std::to_string(clusterID) + "/" + clusterSubdirectory + "/" + client + "_followers.txt";
                        std::string semName = "/" + std::to_string(clusterID) + "_" + clusterSubdirectory + "_" + client + "_followers.txt";
                        sem_t *fileSem = sem_open(semName.c_str(), O_CREAT);

                        std::ofstream followerStream(followerFile, std::ios::app | std::ios::out | std::ios::in);
                        if (root.isMember(client))
                        {
                            for (const auto &follower : root[client])
                            {
                                if (!file_contains_user(followerFile, follower.asString()))
                                {
                                    followerStream << follower.asString() << std::endl;
                                }
                            }
                        }
                        sem_close(fileSem);
                    }
                }
            }
        }
    }

    // for every client in your cluster, update all their followers' timeline files
    // by publishing your user's timeline file (or just the new updates in them)
    //  periodically to the message queue of the synchronizer responsible for that client
    void publishTimelines()
    {
        std::vector<std::string> users = get_all_users_func(synchID);

        for (const auto &client : users)
        {
            int clientId = std::stoi(client);
            int client_cluster = ((clientId - 1) % 3) + 1;
            // only do this for clients in your own cluster
            if (client_cluster != clusterID)
            {
                continue;
            }

            std::vector<std::string> timeline = get_tl_or_fl(synchID, clientId,"tl");
            std::vector<std::string> followers = getFollowersOfUser(clientId);

            for (const auto &follower : followers)
            {
                // send the timeline updates of your current user to all its followers

                // YOUR CODE HERE
            }
        }
    }

    // For each client in your cluster, consume messages from your timeline queue and modify your client's timeline files based on what the users they follow posted to their timeline
    void consumeTimelines()
    {
        std::string queueName = "synch" + std::to_string(synchID) + "_timeline_queue";
        std::string message = consumeMessage(queueName, 1000); // 1 second timeout

        if (!message.empty())
        {
            // consume the message from the queue and update the timeline file of the appropriate client with
            // the new updates to the timeline of the user it follows

            // YOUR CODE HERE
        }
    }

private:
    void updateAllUsersFile(const std::vector<std::string> &users)
    {

        std::string usersFile = "./cluster_" + std::to_string(clusterID) + "/" + clusterSubdirectory + "/all_users.txt";
        std::string semName = "/" + std::to_string(clusterID) + "_" + clusterSubdirectory + "_all_users.txt";
        sem_t *fileSem = sem_open(semName.c_str(), O_CREAT);

        std::ofstream userStream(usersFile, std::ios::app | std::ios::out | std::ios::in);
        for (std::string user : users)
        {
            if (!file_contains_user(usersFile, user))
            {
                userStream << user << std::endl;
            }
        }
        sem_close(fileSem);
    }
};

class SynchServiceImpl final : public SynchService::Service {
    Status GetUserTLFL(ServerContext * context, const ID * id, AllData * alldata) override {
        log(INFO, " Serving REQ for GetUserTLFL");
        std::string master_path = "cluster_" + std::to_string(clusterID) + "/1/";
        std::vector<std::string> current_users = get_lines_from_file(master_path + "currentusers.txt");
        
        for(auto s :current_users) {
            UserTLFL usertlfl;
            usertlfl.set_user(s); 
            std::vector<std::string> tl  = get_tl_or_fl(synchID,  std::stoi(s), "tl");
            std::vector<std::string> flw = get_tl_or_fl(synchID,  std::stoi(s), "flw");
            std::vector<std::string> flr = get_tl_or_fl(synchID,  std::stoi(s), "flr");
            for(auto timeline:tl){
                usertlfl.add_tl(timeline);
            }
            for(auto follow:flw){
                usertlfl.add_flw(follow);
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
                std::string fname = master_path +currentUserBeingFollowed+"_followers.txt";
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
                  std::string tfile = master_path + currentuser + "_timeline.txt";
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

std::vector<std::string> get_tl_or_fl(int synchID, int clientID, std::string name){
    // std::string master_fn = "./master"+std::to_string(synchID)+"/"+std::to_string(clientID);
    // std::string slave_fn = "./slave"+std::to_string(synchID)+"/" + std::to_string(clientID);
    std::string master_fn = "cluster_" + std::to_string(clusterID) + "/1/" + std::to_string(clientID);
    std::string slave_fn = "cluster_" + std::to_string(clusterID) + "/2/" + std::to_string(clientID);
    if(name == "tl") {
        master_fn.append("_timeline.txt");
        slave_fn.append("_timeline.txt");
    }else if (name == "flw") {
        master_fn.append("_following.txt");
        slave_fn.append("_following.txt");
    } else if (name == "flr") {
        master_fn.append("_followers.txt");
        slave_fn.append("_followers.txt");
    } else if (name == "current") {
        master_fn.append("_currentuser.txt");
        slave_fn.append("_currentuser.txt");
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
