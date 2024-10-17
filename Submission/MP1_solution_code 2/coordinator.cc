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
using csce662::Status;

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

//Map to store vector of zNodes according to ClusterID
std::map<int,std::vector<zNode>> cluster;


//func declarations
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
    //std::cout<<"Got Heartbeat! "<<serverinfo->type()<<"("<<serverinfo->serverid()<<")"<<std::endl;

    // Your code here

    int clusterID = serverinfo->clusterid();
    int serverID = serverinfo->serverid();
    log(INFO, "Received Heartbeat from Server "+serverinfo->hostname() + ":" + serverinfo->port() + "\n");
    //std::cout <<  << serverID << " (" << serverinfo->hostname() + ":" + serverinfo->port() << ")" << std::endl;

    // Your code here to handle the heartbeat.

    // Check if the server is already in the list.
    auto cluster_it = cluster.find(clusterID);

    confirmation->set_status(true);

    if (cluster_it != cluster.end()) {
        // The clusterID exists in the map
        std::vector<zNode>& nodes = cluster_it->second;

        // Now, check if a zNode with the given serverID exists within this cluster
        for (zNode& node : nodes) {
            if (node.serverID == serverID) {
                node.last_heartbeat = getTimeNow();
                node.missed_heartbeat = false;

                std::tm* timeInfo = std::localtime(&node.last_heartbeat);
                std::ostringstream oss;
                oss << std::put_time(timeInfo, "%Y-%m-%d %H:%M:%S");

                log(INFO, "Updated heartbeat time for Server "+ std::to_string(serverID) + " Received at time = " + oss.str() + "\n");
                //std::cout << "Updated heartbeat time for Server " << serverID <<" Received at time = "<<oss.str()<< std::endl<< std::endl;

                return Status::OK;
            }
        }
    }
    return Status::OK;
  }
  
  //function returns the server information for requested client id
  //this function assumes there are always 3 clusters and has math
  //hardcoded to represent this.
  Status GetServer(ServerContext* context, const ID* id, ServerInfo* serverinfo) override {
    
    log(INFO,"Got GetServer for clientID: " + std::to_string(id->id()) + "\n");
    //std::cout<<"Got GetServer for clientID: "<<id->id()<<std::endl;
    int clusterID = ((id->id()-1)%3)+1;

    // Your code here
    // If server is active, return serverinfo

    // Check if the clusterID exists in the map
    if (cluster.find(clusterID) != cluster.end()) {
        // Check if the cluster contains a non-empty vector of zNodes
        if (!cluster[clusterID].empty()) {
            // Iterate through the vector to find an active zNode
            for (zNode& node : cluster[clusterID]) {
                if (node.isActive()) {

                    serverinfo->set_hostname(node.hostname);
                    serverinfo->set_port(node.port);
                }
                else {
                    serverinfo->set_hostname("Server Inactive");
                }
            }
        } else {

              serverinfo->set_hostname("Failure");
        }
    } else {
        // The clusterID does not exist in the map
        serverinfo->set_hostname("Failure");
    }

     
    return Status::OK;
  }

  //Function to check if zNode exists in the cluster
  Status exists(ServerContext* context, const ServerInfo* serverinfo, Status* status){

    int clusterID = serverinfo->clusterid();
    int serverID = serverinfo->serverid();

    zNode znode;

    znode.hostname = serverinfo->hostname();
    znode.port = serverinfo->port();
    znode.last_heartbeat = getTimeNow();
    znode.missed_heartbeat = false;
    znode.serverID = serverID;


    auto cluster_it = cluster.find(clusterID);

    status->set_status(false);
    
    if (cluster_it != cluster.end()) {
        // The clusterID exists in the map
        std::vector<zNode>& nodes = cluster_it->second;

        // Now, check if a zNode with the given serverID exists within this cluster
        bool serverFound = false;
        for (const zNode& node : nodes) {
            if (node.serverID == serverID) {
                serverFound = true;
                status->set_status(true);
                return Status::OK;
            }
        }

    }

    return Status::OK;

  }

  //Creating a zNode in the cluster
  Status create(ServerContext* context, const ServerInfo* serverinfo, Status* status){
    
    int clusterID = serverinfo->clusterid();
    int serverID = serverinfo->serverid();

    zNode znode;

    znode.hostname = serverinfo->hostname();
    znode.port = serverinfo->port();
    znode.last_heartbeat = getTimeNow();
    znode.missed_heartbeat = false;
    znode.serverID = serverID;


    auto cluster_it = cluster.find(clusterID);

    if (cluster_it != cluster.end()) {
        // The clusterID exists in the map
        std::vector<zNode>& nodes = cluster_it->second;

        // Now, check if a zNode with the given serverID exists within this cluster
        bool serverFound = false;
        for (const zNode& node : nodes) {
            if (node.serverID == serverID) {
                serverFound = true;
                status->set_status(true);
                return Status::OK;
                break; // You can stop searching once you find a matching serverID
            }
        }

        if (serverFound) {

            log(INFO,"Server = " + std::to_string(serverID) + " exists in Cluster " + std::to_string(clusterID) + "\n");

            //std::cout << "Server " << serverID << " exists in Cluster " << clusterID << "." << std::endl;
        } else {
            status->set_status(false);
            nodes.push_back(znode);
        }
    } else {
        // The clusterID does not exist in the map

        log(INFO,"Cluster " + std::to_string(clusterID) + " does not exist in the map. Adding it...\n");

        //std::cout << "Cluster " << clusterID << " does not exist in the map. Adding it..." << std::endl;
        
        // Create a new cluster vector and push the zNode into it
        std::vector<zNode> newCluster;
        newCluster.push_back(znode);        

        // Insert the new cluster into the map
        cluster.insert({clusterID, newCluster});

        log(INFO," Server " + std::to_string(serverID) + " added to Cluster " + std::to_string(clusterID) + "\n");
        
        //std::cout << "Server " << serverID << " added to Cluster " << clusterID << "." << std::endl;
    }

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

  log(INFO,"Server listening on " + server_address + "\n");

  //std::cout << "Server listening on " << server_address << std::endl;

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

//function to check if zNode has missed heartbeat or not
void checkHeartbeat(){
  
  while (true) {
      // Loop through the integers (cluster IDs) in the map
      for (auto& pair : cluster) {
          int clusterID = pair.first; // Get the cluster ID
          std::vector<zNode>& nodes = pair.second; // Get the vector of zNodes for this cluster

          // Loop through the zNodes in the vector
          for (zNode& node : nodes) {
              // Check if the last heartbeat is more than 10 seconds ago
              if (difftime(getTimeNow(), node.last_heartbeat) > 10) {
                  // Update the missed_heartbeat flag
                  node.missed_heartbeat = true;
              }
          }
      }

      sleep(2);
  }

}


std::time_t getTimeNow(){
    return std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
}