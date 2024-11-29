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

using csce662::AllUsers;
using csce662::Confirmation;
using csce662::CoordService;
using csce662::ID;
using csce662::ServerInfo;
using csce662::ServerList;
using csce662::SynchronizerListReply;
using csce662::SynchService;
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
using csce662::TLFL;

int synchID = 1;
int clusterID = 1;
bool isMaster = false;
int total_number_of_registered_synchronizers = 3; // update this by asking coordinator
std::string coordAddr;
std::string clusterSubdirectory = "1";
std::string name = "Finalist";
std::vector<std::string> otherHosts;
std::unordered_map<std::string, int> timelineLengths;

std::vector<std::string> get_lines_from_file(std::string);
std::vector<std::string> get_all_users_func(int synchID, bool all_users = true);
std::vector<std::string> get_tl_or_fl(int, std::string);
std::vector<std::string> getFollowersOfUser(int);
bool file_contains_user(std::string filename, std::string user);

void Heartbeat(std::string coordinatorIp, std::string coordinatorPort, ServerInfo serverInfo, int syncID);

std::unique_ptr<csce662::CoordService::Stub> coordinator_stub_;

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
        declareQueue("synch" + name + std::to_string(synchID) + "_users_queue");
        declareQueue("synch" + name + std::to_string(synchID) + "_clients_relations_queue");
        declareQueue("synch" + name + std::to_string(synchID) + "_timeline_queue");
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
        publishMessage("synch" + name + std::to_string(synchID) + "_users_queue", message);
    }

    void consumeUserLists()
    {
        std::vector<std::string> allUsers;
        // YOUR CODE HERE

        // TODO: while the number of synchronizers is harcorded as 6 right now, you need to change this
        // to use the correct number of follower synchronizers that exist overall
        // accomplish this by making a gRPC request to the coordinator asking for the list of all follower synchronizers registered with it
        if (total_number_of_registered_synchronizers > 0) {
          for (int i = 1; i <= total_number_of_registered_synchronizers; i++)
          {
              std::string queueName = "synch" + name + std::to_string(i) + "_users_queue";
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
        }
        updateAllUsersFile(allUsers);
    }

    void publishClientRelations()
    {
        Json::Value relations;
        std::vector<std::string> cluster_users = get_all_users_func(synchID, false);
        
        for (const auto &client : cluster_users)
        {
            int clientId = std::stoi(client);
            std::vector<std::string> follow_list = get_tl_or_fl(clientId, "fl");

            Json::Value followList(Json::arrayValue);
            for (const auto &following : follow_list)
            {
                followList.append(following);
            }

            if (!followList.empty())
            {
                relations[client] = followList;
            }
        }

        Json::FastWriter writer;
        std::string message = writer.write(relations);
        publishMessage("synch" + name + std::to_string(synchID) + "_clients_relations_queue", message);
    }

    void consumeClientRelations()
    {
        std::vector<std::string> allUsers = get_all_users_func(synchID);

        // YOUR CODE HERE

        // If user in cluster_users
        std::vector<std::string> clusterUsers = get_all_users_func(synchID, false);
        
        // TODO: hardcoding 6 here, but you need to get list of all synchronizers from coordinator as before
        if (total_number_of_registered_synchronizers > 0) {
          for (int i = 1; i <= total_number_of_registered_synchronizers; i++)
          {
              // Consume Follow List
              std::string queueName = "synch" + name + std::to_string(i) + "_clients_relations_queue";
              std::string message = consumeMessage(queueName, 1000); // 1 second timeout

              if (!message.empty())
              {
                  Json::Value root;
                  Json::Reader reader;
                  if (reader.parse(message, root))
                  {
                      // for all users
                      for (const auto &client : allUsers)
                      {
                          // if the client has a message to post
                          if (root.isMember(client))
                          {
                              // for user in client's follow_list
                              for (const auto &user : root[client])
                              {
                                    if (std::find(clusterUsers.begin(), clusterUsers.end(), user.asString()) != clusterUsers.end()) {
                                        // check users followers list
                                        std::string user_followers_file = "cluster_" + std::to_string(clusterID) + "/" + clusterSubdirectory + "/" + user.asString() + "_followers.txt";
                                        std::string semName = "/" + std::to_string(clusterID) + "_" + clusterSubdirectory + "_" + user.asString() + "_followers.txt";
                                        sem_t *fileSem = sem_open(semName.c_str(), O_CREAT);
                                        std::ofstream followerStream(user_followers_file, std::ios::app | std::ios::out | std::ios::in);
                                        if (!file_contains_user(user_followers_file, client))
                                        {
                                            followerStream << client << std::endl;
                                        }
                                        sem_close(fileSem);
                                    }
                                     
                              }
                          }
                      }
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
        std::vector<std::string> cluster_users = get_all_users_func(synchID, false);
        std::unordered_map<int, std::vector<std::string>> updates;
        
        for (const auto &client : cluster_users)
        {
            int clientId = std::stoi(client);
            std::vector<std::string> timeline = get_tl_or_fl(clientId, "tl");
            std::vector<std::string> followers = get_tl_or_fl(clientId, "flw");
            std::vector<std::string> usersPosts;
            
            for (const auto& line : timeline) {
                // Check if the line starts with the clientId
                if (!line.empty() && line.rfind(client, 0) == 0) { // rfind returns 0 if prefix matches
                    usersPosts.push_back(line);
                }
            }
            
            // Append user posts to each follower's updates
            for (const auto& follower : followers) {
                int followerId = std::stoi(follower);
                updates[followerId].insert(updates[followerId].end(), usersPosts.begin(), usersPosts.end());
            }
        }
        
        Json::Value posts;
        for (const auto& [clientId, userPosts] : updates) {
            Json::Value postArray(Json::arrayValue);
            for (const auto& post : userPosts) {
                postArray.append(post);
            }
            posts[std::to_string(clientId)+"tl"] = postArray;
        }
    
        Json::FastWriter writer;
        std::string message = writer.write(posts);
        publishMessage("synch" + name + std::to_string(synchID) + "_timeline_queue", message);
    }

    // For each client in your cluster, consume messages from your timeline queue and modify your client's timeline files based on what the users they follow posted to their timeline
    void consumeTimelines()
    {
        std::vector<std::string> cluster_users = get_all_users_func(synchID, false);
        if (total_number_of_registered_synchronizers > 0) {
          for (int i = 1; i <= total_number_of_registered_synchronizers; i++)
          {
              std::string queueName = "synch" + name + std::to_string(i) + "_timeline_queue";
              std::string message = consumeMessage(queueName, 1000); // 1 second timeout
              if (!message.empty())
              {
                  Json::Value root;
                  Json::Reader reader;
                  if (reader.parse(message, root))
                  {
                      for (const auto& user : cluster_users) {
                        // Check if the client has posts in the message
                        if (root.isMember(user+"tl")) {
                            // Open the timeline file only once per client
                            std::string timelineFile = "cluster_" + std::to_string(clusterID) + "/" + clusterSubdirectory + "/" + user + "_timeline.txt";
                            std::string semName = "/" + std::to_string(clusterID) + "_" + clusterSubdirectory + "_" + user + "_timeline.txt";
                            sem_t *fileSem = sem_open(semName.c_str(), O_CREAT);
                            std::ofstream timelineStream(timelineFile, std::ios::app | std::ios::out | std::ios::in);
                            // Process each post in the client's timeline
                            for (const auto& line : root[user+"tl"]) {
                                // If the post doesn't already exist in the file, write it
                                if (!file_contains_user(timelineFile, line.asString())) {
                                    timelineStream << line.asString() << std::endl;
                                }
                            }
                            // Close the file after processing the client
                            sem_close(fileSem);
                            timelineStream.close();
                          }
                      }
                  }
              }
          }
        }
    }

private:
    void updateAllUsersFile(const std::vector<std::string> &users)
    {

        std::string usersFile = "cluster_" + std::to_string(clusterID) + "/" + clusterSubdirectory + "/all_users.txt";
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

void run_synchronizer(std::string coordIP, std::string coordPort, std::string port, int synchID, SynchronizerRabbitMQ &rabbitMQ);

class SynchServiceImpl final : public SynchService::Service
{
    // You do not need to modify this in any way
};

void RunServer(std::string coordIP, std::string coordPort, std::string port_no, int synchID)
{
    // localhost = 127.0.0.1
    std::string server_address("127.0.0.1:" + port_no);
    log(INFO, "Starting synchronizer server at " + server_address);
    SynchServiceImpl service;
    // grpc::EnableDefaultHealthCheckService(true);
    // grpc::reflection::InitProtoReflectionServerBuilderPlugin();
    ServerBuilder builder;
    // Listen on the given address without any authentication mechanism.
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    // Register "service" as the instance through which we'll communicate with
    // clients. In this case it corresponds to an *synchronous* service.
    builder.RegisterService(&service);
    // Finally assemble the server.
    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Server listening on " << server_address << std::endl;

    // Initialize RabbitMQ connection
    SynchronizerRabbitMQ rabbitMQ("localhost", 5672, synchID);

    std::thread t1(run_synchronizer, coordIP, coordPort, port_no, synchID, std::ref(rabbitMQ));

    // Create a consumer thread
    std::thread consumerThread([&rabbitMQ]()
                               {
        while (true) {
            rabbitMQ.consumeUserLists();
            rabbitMQ.consumeClientRelations();
            rabbitMQ.consumeTimelines();
            std::this_thread::sleep_for(std::chrono::seconds(5));
            // you can modify this sleep period as per your choice
        } });

    server->Wait();
    //   t1.join();
    //   consumerThread.join();
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
    serverInfo.set_clusterid(clusterID);
    Heartbeat(coordIP, coordPort, serverInfo, synchID);

    RunServer(coordIP, coordPort, port, synchID);
    return 0;
}

void run_synchronizer(std::string coordIP, std::string coordPort, std::string port, int synchID, SynchronizerRabbitMQ &rabbitMQ)
{
    // setup coordinator stub
    std::string target_str = coordIP + ":" + coordPort;
    std::unique_ptr<CoordService::Stub> coord_stub_;
    coord_stub_ = std::unique_ptr<CoordService::Stub>(CoordService::NewStub(grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials())));

    ServerInfo msg;
    Confirmation c;

    msg.set_serverid(synchID);
    msg.set_hostname("127.0.0.1");
    msg.set_port(port);
    msg.set_type("follower");

    // TODO: begin synchronization process
    while (true)
    {
        // the synchronizers sync files every 5 seconds
        grpc::ClientContext context;
        ServerList followerServers;
        ID id;
        id.set_id(synchID);

        // making a request to the coordinator to see count of follower synchronizers
        coord_stub_->GetAllFollowerServers(&context, id, &followerServers);
        total_number_of_registered_synchronizers = followerServers.serverid_size();
        clusterSubdirectory = followerServers.clusterdirectory();
        sleep(5);
        std::vector<int> server_ids;
        std::vector<std::string> hosts, ports;
        for (std::string host : followerServers.hostname())
        {
            hosts.push_back(host);
        }
        for (std::string port : followerServers.port())
        {
            ports.push_back(port);
        }
        for (int serverid : followerServers.serverid())
        {
            server_ids.push_back(serverid);
        }

        // update the count of how many follower sychronizer processes the coordinator has registered

        // below here, you run all the update functions that synchronize the state across all the clusters
        // make any modifications as necessary to satisfy the assignments requirements
        // Publish user list
        rabbitMQ.publishUserList();

        // Publish client relations
        rabbitMQ.publishClientRelations();

        // Publish timelines
        rabbitMQ.publishTimelines();
    }
    return;
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
    ClientContext context;
    Confirmation confirmation;
    grpc::Status status = stub->Heartbeat(&context, serverInfo, &confirmation);

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
        if (user == users[i])
        {
            sem_close(fileSem);
            return true;
        }
    }
    sem_close(fileSem);
    return false;
}

std::vector<std::string> get_all_users_func(int synchID, bool all_users)
{
    std::string user_file_name = "all_users";
    if (!all_users) {
      user_file_name = "current_cluster_users";
    }
    // read all_users file master and client for correct serverID
    // std::string master_users_file = "./master"+std::to_string(synchID)+"/all_users";
    // std::string slave_users_file = "./slave"+std::to_string(synchID)+"/all_users";
    std::string clusterID = std::to_string(((synchID - 1) % 3) + 1);
    std::string master_users_file = "cluster_" + clusterID + "/1/" + user_file_name + ".txt";
    std::string slave_users_file = "cluster_" + clusterID + "/2/" + user_file_name + ".txt";
    // take longest list and package into AllUsers message
    std::vector<std::string> master_user_list = get_lines_from_file(master_users_file);
    std::vector<std::string> slave_user_list = get_lines_from_file(slave_users_file);
    
    if (master_user_list.size() >= slave_user_list.size())
        return master_user_list;
    else
        return slave_user_list;
}

std::vector<std::string> get_tl_or_fl(int clientID, std::string fileName)
{
    // std::string master_fn = "./master"+std::to_string(synchID)+"/"+std::to_string(clientID);
    // std::string slave_fn = "./slave"+std::to_string(synchID)+"/" + std::to_string(clientID);
    std::string master_fn = "cluster_" + std::to_string(clusterID) + "/1/" + std::to_string(clientID);
    std::string slave_fn = "cluster_" + std::to_string(clusterID) + "/2/" + std::to_string(clientID);
    if (fileName == "tl")
    {
        master_fn.append("_timeline.txt");
        slave_fn.append("_timeline.txt");
    }
    else if (fileName == "fl")
    {
        master_fn.append("_follow_list.txt");
        slave_fn.append("_follow_list.txt");
    } 
    else { 
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
