#include <algorithm>
#include <cstdio>
#include <ctime>

#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>
#include <chrono>
#include <sys/stat.h>
#include <sys/types.h>
#include <utility>
#include <vector>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <mutex>
#include <stdlib.h>
#include <unistd.h>
#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>
#include<glog/logging.h>
#include <map>

#define log(severity, msg) LOG(severity) << msg; google::FlushLogFiles(google::severity); 

#include "coordinator.grpc.pb.h"
#include "coordinator.pb.h"

using google::protobuf::Timestamp;
using google::protobuf::Duration;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;
using csce662::CoordService;
using csce662::ServerInfo;
using csce662::Confirmation;
using csce662::ID;
using csce662::ServerList;
using csce662::SynchService;
using csce662::Path;
using csce662::PathAndData;

struct zNode{
    int serverID;
    std::string hostname;
    std::string port;
    std::string type;
    std::time_t last_heartbeat;
    bool missed_heartbeat;
    bool isActive();

};

//potentially thread safe 
std::mutex v_mutex;
std::vector<zNode*> cluster1;
std::vector<zNode*> cluster2;
std::vector<zNode*> cluster3;

// creating a vector of vectors containing znodes
std::vector<std::vector<zNode*>> clusters = {cluster1, cluster2, cluster3};


//func declarations
int findServer(std::vector<zNode*> v, int id); 
std::time_t getTimeNow();
void checkHeartbeat();


bool zNode::isActive(){
    bool status = false;
    if(!missed_heartbeat){
        status = true;
    }else if(difftime(getTimeNow(),last_heartbeat) < 10){
        status = true;
    }
    return status;
}


class CoordServiceImpl final : public CoordService::Service {

    Status Heartbeat(ServerContext* context, const ServerInfo* serverinfo, Confirmation* confirmation) override {
        // Your code here
        int clusterID = serverinfo->clusterid();
        int serverID = serverinfo->serverid();
        
        log(INFO, "Heartbeat received from Server " + serverinfo->hostname() + " at port " + serverinfo->port() + ".\n");
        confirmation->set_status(true);  // Acknowledge receipt of the heartbeat
        for (zNode* node : clusters[clusterID - 1]) {
            // If server ID present
            if (node->serverID == serverID) {
                // Update the node's last heartbeat timestamp
                node->last_heartbeat = getTimeNow();
                node->missed_heartbeat = false; 
                log(INFO, "Updated heartbeat for Server " + std::to_string(serverID));
                return Status::OK;
            }
        }
        return Status::OK;
    }

    //function returns the server information for requested client id
    //this function assumes there are always 3 clusters and has math
    //hardcoded to represent this.
    Status GetServer(ServerContext* context, const ID* id, ServerInfo* serverinfo) override {
        // Your code here
        log(INFO, "Got GetServer for clientID: " + std::to_string(id->id()) + "\n");
        // Determine the cluster ID based on the client ID
        int clusterID = ((id->id() - 1) % 3) + 1;
        bool serverFound = false; // Track if an active server is found
        if (!clusters[clusterID - 1].empty()) {
            for (zNode* node : clusters[clusterID - 1]) {
                if (node->isActive()) {
                    // If the server is active, set the hostname and port
                    serverinfo->set_hostname(node->hostname);
                    serverinfo->set_port(node->port);
                    serverFound = true; // Mark that an active server has been found
                    break; // Exit loop after finding the first active server
                }
            }
        } 
        // If no active server was found, throw error!
        if (!serverFound) {
            return Status(grpc::StatusCode::NOT_FOUND, "Server not found");
        }
        return Status::OK;
    }
    
Status exists(ServerContext* context, const ServerInfo* serverinfo, csce662::Status* status) {
    // Get the cluster ID and server ID from the provided ServerInfo object
    int clusterID = serverinfo->clusterid();
    int serverID = serverinfo->serverid();

    // Default status to false
    status->set_status(false);

    // Iterate through the nodes in the specified cluster
    for (zNode* node : clusters[clusterID - 1]) {
        // Check if the current node's server ID found, set status to true
        if (node->serverID == serverID) {
            status->set_status(true); // Update status to true
            break; 
        }
    }

    return Status::OK;
}

Status create(ServerContext* context, const ServerInfo* serverinfo, csce662::Status* status) {
    // Get cluster and server IDs
    int clusterID = serverinfo->clusterid();
    int serverID = serverinfo->serverid();

    // Create a new zNode
    zNode* znode = new zNode();

    // Set zNode properties
    znode->hostname = serverinfo->hostname();
    znode->port = serverinfo->port();
    znode->last_heartbeat = getTimeNow(); // Record the current time as last heartbeat
    znode->missed_heartbeat = false; // Initialize missed heartbeat flag
    znode->serverID = serverID; // Set server ID

    log(INFO, "Updated heartbeat time for Server "); // Log update
    
    // Check if a zNode with the given serverID exists
    bool serverFound = false;
    for (zNode* node : clusters[clusterID - 1]) { // Iterate over nodes in the cluster
        if (node->serverID == serverID) { // If server ID matches
            serverFound = true; // Server found
            status->set_status(true); // Set status to true
            return Status::OK; // Exit early if found
        }
    }
    // Add new server if there is no server
    log(INFO, "Adding New Server to cluster" + std::to_string(clusterID) + "\n" ); // Log new server initialization
    clusters[clusterID - 1].push_back(znode); // Add new zNode to the cluster
    status->set_status(true); 
    return Status::OK; 
}

  

};

void RunServer(std::string port_no){
    //start thread to check heartbeats
    std::thread hb(checkHeartbeat);
    //localhost = 127.0.0.1
    std::string server_address("127.0.0.1:"+port_no);
    CoordServiceImpl service;
    //grpc::EnableDefaultHealthCheckService(true);
    //grpc::reflection::InitProtoReflectionServerBuilderPlugin();
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

    std::string port = "3010";
    int opt = 0;
    while ((opt = getopt(argc, argv, "p:")) != -1){
        switch(opt) {
            case 'p':
                port = optarg;
                break;
            default:
                std::cerr << "Invalid Command Line Argument\n";
        }
    }
    RunServer(port);
    return 0;
}



void checkHeartbeat(){
    while(true){
        //check servers for heartbeat > 10
        //if true turn missed heartbeat = true
        // Your code below

        v_mutex.lock();

        // iterating through the clusters vector of vectors of znodes
        for (auto& c : clusters){
            for(auto& s : c){
                if(difftime(getTimeNow(),s->last_heartbeat)>10){
                    std::cout << "missed heartbeat from server " << s->serverID << std::endl;
                    if(!s->missed_heartbeat){
                        s->missed_heartbeat = true;
                        s->last_heartbeat = getTimeNow();
                    }
                }
            }
        }

        v_mutex.unlock();

        sleep(3);
    }
}


std::time_t getTimeNow(){
    return std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
}

