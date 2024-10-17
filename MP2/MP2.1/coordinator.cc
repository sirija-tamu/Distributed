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
#define log(severity, msg) LOG(severity) << msg; google::FlushLogFiles(google::severity); 

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
        // Convert cluster ID from string to integer
        int clusterID = std::stoi(serverinfo->clusterid());

        // Check if the cluster ID exists in the routing table
        if (clusters.find(clusterID) == clusters.end()) {
            confirmation->set_status(false);
            log(ERROR, "Invalid cluster ID: " + std::to_string(clusterID));
            
            return grpc::Status(grpc::StatusCode::NOT_FOUND, 
                                "Cluster ID " + std::to_string(serverinfo->serverid()) + " not found");
        }

        // Get a reference to the first zNode in the cluster
        zNode& znode = clusters[clusterID][0];

        // Update server information in the zNode
        znode.serverID = serverinfo->serverid();
        znode.port = serverinfo->port();
        znode.type = serverinfo->type();

        // Simulate a delay for heartbeat processing
        std::this_thread::sleep_for(std::chrono::seconds(5));

        // Update the last heartbeat time
        znode.last_heartbeat = getTimeNow();
        
        log(INFO, "Received Heartbeat from ClusterID: " + std::to_string(clusterID) +
                  ", ServerID: " + std::to_string(serverinfo->serverid()));
        
        // Acknowledge successful heartbeat processing
        confirmation->set_status(true);

        return Status::OK;
    }

    //function returns the server information for requested client id
    //this function assumes there are always 3 clusters and has math
    //hardcoded to represent this.
    Status GetServer(ServerContext* context, const ID* id, ServerInfo* serverinfo) override {
        // Your code here
        std::cout << "Received request for clientID: " << id->id() << std::endl;

        // Calculate the clusterID based on the client ID
        int clusterID = (id->id() - 1) % 3 + 1;

        // Check if the clusterID exists in the routing table (clusters map)
        if (clusters.find(clusterID) != clusters.end()) {
            const std::vector<zNode>& cluster = clusters[clusterID];  // Get the cluster nodes

            // Look for the first active server in the cluster
            for (const auto& node : cluster) {
                if (node.isActive()) {
                    // Populate server information for the active server
                    serverInfo->set_serverid(node.serverID);
                    serverInfo->set_hostname(node.hostname);
                    serverInfo->set_port(node.port);
                    serverInfo->set_type(node.type);

                    std::cout << "Assigned server: " << node.serverID << " from cluster " << clusterID << std::endl;
                    return Status::OK;  // Return success if an active server is found
                }
            }

            // No active server was found in the cluster
            return grpc::Status(grpc::StatusCode::UNAVAILABLE, "No active server available in the cluster");
        } else {
            // Cluster information not found in the routing table
            return grpc::Status(grpc::StatusCode::NOT_FOUND, "Cluster information not found in routing table");
        } 
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
    std::cout << "Coordinator listening on " << server_address << std::endl;

    // Wait for the server to shutdown. Note that some other thread must be
    // responsible for shutting down the server for this call to ever return.
    server->Wait();
}

int main(int argc, char** argv) {

    std::string port = "9090";
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
    std::string log_file_name = std::string("coordinator-port-") + port;  
    google::InitGoogleLogging(log_file_name.c_str());

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

