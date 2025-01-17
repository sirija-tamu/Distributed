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

struct zNode{
    int serverID;
    std::string hostname;
    std::string port;
    std::string type;
    std::string clusterdirectory;
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
std::vector<ServerInfo> syncServers;


//func declarations
int findServer(std::vector<zNode*> v, int id); 
std::time_t getTimeNow();
void checkHeartbeat();


// this function returns the index of the required server in its cluster array
int findServer(std::vector<zNode*> v, int id){
    v_mutex.lock();

    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i]->serverID == id) {
            v_mutex.unlock();
            return i; // Return the index of the zNode with the matching serverId
        }
    }
    v_mutex.unlock();

    // at this point no appropriate server was found
    return -1;  
}


bool zNode::isActive(){
    bool status = false;
    if(!missed_heartbeat){
        status = true;
    }else if(difftime(getTimeNow(),last_heartbeat) < 10){
        status = true;
    }
    return status;
}

bool syncExists(int clusterID) {
    for (const auto& sync : syncServers) { 
        if (sync.clusterid() == clusterID) {
            return true;
        }
    }
    return false;
}

class CoordServiceImpl final : public CoordService::Service {

    Status Heartbeat(ServerContext* context, const ServerInfo* serverinfo, Confirmation* confirmation) override {

        // using a multimap to extract custom metadata from the server's grpc to the coordinator
        const std::multimap<grpc::string_ref, grpc::string_ref>& metadata = context->client_metadata();

        std::string clusterid;
        int intClusterid;
        auto it = metadata.find("clusterid");
        if (it != metadata.end()) {
            // customValue is the clusterid from the metadata received in the server's rpc
            std::string customValue(it->second.data(), it->second.length());

            clusterid = customValue;
            intClusterid = std::stoi(clusterid);
        }

        // std::cout<<"Got Heartbeat! Serverid:"<<serverinfo->type()<<"("<<serverinfo->serverid()<<") and clusterid: (" << clusterid << ")\n";

        auto it2 = metadata.find("heartbeat");
        if (it2 != metadata.end()) { // HEARTBEAT RECEIVED - Server
            // customValue2 is the heartbeat from the metadata received from the server
            std::string customValue2(it2->second.data(), it2->second.length());

            // finding the server for which the heartbeat was received
            int curIndex = findServer(clusters[intClusterid-1], serverinfo->serverid());
            if (curIndex != -1){
                v_mutex.lock();

                zNode* curZ = clusters[intClusterid - 1][curIndex];
                curZ->last_heartbeat = getTimeNow();
                // Master is down, Slave is now master
                if (curZ->type == "slave" && clusters[intClusterid-1][0]->type == "down") {
                        curZ->type = "master";
                        confirmation->set_type("master");
                        clusters[intClusterid-1][1] = clusters[intClusterid-1][0]; // Move old master to slave
                        clusters[intClusterid-1][0] = curZ;
                        std::cout << "Slave replaced Master for cluster"<< intClusterid <<"/n";
                }

                v_mutex.unlock();

            }else { // if a heartbeat was received, that means that sometime in the past, the server was registered and stored in our data structure in memory
                std::cout << "server's znode was not found\n"; // THIS SHOULD NEVER HAPPEN
            }

            
        } else{ // NOT A HEARTBEAT, BUT INSTEAD INITIAL REGISTRATION - Synchronizer
            // checking if server already registered but just died and rejoined again
            if(serverinfo->type() == "server") { 
                int curIndex = findServer(clusters[intClusterid-1], serverinfo->serverid());

                // server is resurrected after it was killed in the past
                if (curIndex != -1){
                    v_mutex.lock();

                    zNode* curZ = clusters[intClusterid - 1][curIndex];
                    curZ->last_heartbeat = getTimeNow(); // updating the latest heartbeat value for the server

                    v_mutex.unlock();

                    std::cout << "an inactive server was resurrected" << "\n";
                }else { // first time the server contacts the coordinator and needs to be registered
                    std::cout << "new server registered\n";
                    zNode* z = new zNode();

                    z->hostname = serverinfo->hostname();
                    z->port = serverinfo->port();
                    z->serverID = serverinfo->serverid();
                    z->type = serverinfo->type(); 
                    z->last_heartbeat = getTimeNow();


                    v_mutex.lock();

                    // adding the newly created server to its relevant cluster
                    // If it's the first server in the cluster, make it master
                    if(clusters[intClusterid-1].size() == 0) {
                        z->type = "master";
                        z->clusterdirectory = "1";
                        confirmation->set_type("master");
                        confirmation->set_clusterdirectory("1");
                        clusters[intClusterid-1].push_back(z);
                        std::cout << "Master registered for cluster"<< intClusterid <<"\n";
                    } else {
                        // If master is down, make it master
                        if (clusters[intClusterid-1][0]->type == "down") {
                            z->type = "master";
                            z->clusterdirectory = "2";
                            confirmation->set_type("master");
                            confirmation->set_clusterdirectory("2");
                            clusters[intClusterid-1][0] = z; // Update master
                            std::cout << "Slave replaced Master for cluster"<< intClusterid <<"\n";
                        } else {
                            z->type = "slave";
                            confirmation->set_type("slave");
                            z->clusterdirectory = "2";
                            confirmation->set_clusterdirectory("2");
                            clusters[intClusterid-1].push_back(z); // Add new node as slave
                            std::cout << "Slave registered for cluster"<< intClusterid <<"\n";
                        }
                    }
                    
                    v_mutex.unlock();
                }
                } else {
                    // Registering Sync Servers
                    csce662::ServerInfo serverinfo_ = *serverinfo;
                    if(syncExists(serverinfo->clusterid())){
                        serverinfo_.set_type("slave");
                    } else {
                        serverinfo_.set_type("master");
                    }
                    syncServers.push_back(serverinfo_);
                    std::cout<<" Registered synchronizer on port="<<serverinfo->port() <<" clusterID= "<<serverinfo->clusterid()<<"\n";
                }
        }

        return Status::OK;
    }

    //function returns the server information for requested client id
    //this function assumes there are always 3 clusters and has math
    //hardcoded to represent this.
    Status GetServer(ServerContext* context, const ID* id, ServerInfo* serverinfo) override {
        std::cout<<"Got GetServer for clientID: "<<id->id()<<std::endl;
        int clusterId = ((id->id() - 1) % 3) + 1;

        // Your code here
        // If server is active, return serverinfo

        // finding a server to assign to the new client
        int curIndex = -1;
        
        // this function returns the index of the required server in its cluster array
        v_mutex.lock();
        
        for (size_t i = 0; i < clusters[clusterId-1].size(); ++i) {
            if (clusters[clusterId-1][i]->isActive() && clusters[clusterId-1][i]->type == "master") {
                v_mutex.unlock();
                curIndex = i;
                break;
            }
        }
        v_mutex.unlock();

    // at this point no appropriate server was found

        if (curIndex != -1){
            v_mutex.lock();
            zNode* curZ = clusters[clusterId - 1][curIndex];
            v_mutex.unlock();
            if (curZ->isActive()){ // setting the ServerInfo values to return to the client if its server is active
                serverinfo->set_hostname(curZ->hostname);
                serverinfo->set_port(curZ->port);
            } else {
                std::cout << "The server is not active!\n";
            }
        }else { 
            std::cout << "the server that is supposed to serve the client is down!\n";
        }

        return Status::OK;
    }

    Status GetFollowerServer(ServerContext* context, const ID* id, ServerInfo* serverinfo) override {
        for (const ServerInfo& sync : syncServers) {  
            if (sync.clusterid() == id->id() && sync.type() == "master") {
                *serverinfo = sync;  
                return Status::OK;
            }
        }
        return Status::OK; 
    }

    Status GetAllFollowerServers(ServerContext* context, const ID* id, ServerList* serverList) override {
        for (const ServerInfo& sync : syncServers) { 
            serverList->add_hostname(sync.hostname());  // Add hostname
            serverList->add_port(sync.port());          // Add port
            serverList->add_serverid(sync.serverid());  // Add server ID
        }
        int clusterId = ((id->id() - 1) % 3) + 1;
        serverList->set_clusterdirectory(clusters[clusterId-1][0]->clusterdirectory);
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
                        std::cout << " marking this server as down: " << s->type << "\n";
                        s->type = "down";
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

