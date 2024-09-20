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

#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>

#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <stdlib.h>
#include <unistd.h>
#include <iomanip>
#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>
#include<glog/logging.h>
#include <cstdio>
#include <dirent.h>
#include <cstring>
#include <iostream>
#define log(severity, msg) LOG(severity) << msg; google::FlushLogFiles(google::severity); 

#include "sns.grpc.pb.h"


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


struct Client {
  std::string username;
  bool connected = true;
  int following_file_size = 0;
  std::vector<Client*> client_followers;
  std::vector<Client*> client_following;
  ServerReaderWriter<Message, Message>* stream = 0;
  bool hasStreamed = false;
  bool operator==(const Client& c1) const{
    return (username == c1.username);
  }
};

//Vector that stores every client that has been created
std::vector<Client*> client_db;


class SNSServiceImpl final : public SNSService::Service {
  
  Status List(ServerContext* context, const Request* request, ListReply* list_reply) override {
    // Find current user (for fetching followers)
    Client* curr_user = nullptr;
    // Add all users in client_db to list_reply's all_users
    for (Client* client : client_db) {
        list_reply->add_all_users(client->username);
        if (client->username == request->username()) {
            curr_user = client;
        }
    }

    // Add followers of current user to list_reply's followers
    for (Client* follower : curr_user->client_followers) {
        list_reply->add_followers(follower->username);
    }

    return Status::OK;
  }


  // Get client by username
  Client* getClient(const std::string& username) {
      for (Client* client : client_db) {
          if (client->username == username) {
              return client;
          }
      }
      return nullptr;
  }

  Status Follow(ServerContext* context, const Request* request, Reply* reply) override {
    // Get the current user and the username of the client the current user wants to follow,
    Client* curr_user = getClient(request->username());
    Client* wants_to_follow = getClient(request->arguments(0));

    // Check if the curr_user and the user the client wants to follow exists
    if (curr_user == nullptr) {
      reply->set_msg("FAILURE_INVALID_USERNAME: Current user doesn't exist in clientDB.");
      return Status::OK;
    }

    if (wants_to_follow == nullptr) {
      reply->set_msg("FAILURE_INVALID_USERNAME: The user name you want to follow doesn't exist in clientDB.");
      return Status::OK;
    }
    
    if (wants_to_follow->username == curr_user->username) {
      reply->set_msg("FAILURE_INVALID: You can't follow yourself");
      return Status::OK;
    }

    // Check if client already follows the prospective follower
    for (Client* following : curr_user->client_following) {
      if (following->username == wants_to_follow->username) {
          reply->set_msg("FAILURE_ALREADY_EXISTS: You are already following the user");
          return Status::OK;
      }
    }

    // Update the following and followers lists if its a new following.
    curr_user->client_following.push_back(wants_to_follow);
    wants_to_follow->client_followers.push_back(curr_user);

    reply->set_msg("SUCCESS: Successfully followed the user.");
    return Status::OK; 
  }

  Status UnFollow(ServerContext* context, const Request* request, Reply* reply) override {

    // Get the current user and the username of the client the current user wants to unfollow
    Client* curr_user = getClient(request->username());
    Client* to_unfollow = getClient(request->arguments(0));

    // Check if the current user and the user to be unfollowed exist
    if (curr_user == nullptr) {
        reply->set_msg("FAILURE_INVALID_USERNAME: Current user doesn't exist in clientDB.");
        return Status::OK;
    }

    if (to_unfollow == nullptr) {
        reply->set_msg("FAILURE_INVALID_USERNAME: The username you want to unfollow doesn't exist in clientDB.");
        return Status::OK;
    }

    if (to_unfollow->username == curr_user->username) {
        reply->set_msg("FAILURE_INVALID: You can't unfollow yourself.");
        return Status::OK;
    }

    std::vector<Client*>& following = curr_user->client_following;
    std::vector<Client*>& followers = to_unfollow->client_followers;

    for (int i = 0; i < following.size(); ++i) {
        if (following[i]->username == to_unfollow->username) {
            // Remove the Client* from curr_user's following
            following.erase(following.begin() + i);

            // Now remove curr_user from to_unfollow's followers
            for (int j = 0; j < followers.size(); ++j) {
                if (followers[j]->username == curr_user->username) {
                    followers.erase(followers.begin() + j);
                    break;
                }
            }
            reply->set_msg("SUCCESS: Successfully unfollowed the user.");
            return Status::OK;
        }
    }
    
    // If the current user is not following the user to unfollow
    reply->set_msg("FAILURE_NOT_A_FOLLOWER: You are not following this user.");
    return Status::OK;
  }

  // RPC Login
  Status Login(ServerContext* context, const Request* request, Reply* reply) override {

    // Find the client in the client_db using the helper function
    Client* curr_user = getClient(request->username());

    // Check if the client exists in client_db
    if (curr_user != nullptr) {
        // Check if the client is already logged in
        if (curr_user->connected) {
            reply->set_msg("FAILURE_ALREADY_EXISTS: The user is already logged in.");
            return Status::OK;
        }
        // Log the user in by setting the 'connected' status to true
        curr_user->connected = true;

    } else {
        curr_user = new Client();
        curr_user->username = request->username();
        curr_user->connected = true;
        // Create the user’s timeline file if it doesn't exist or is empty
        createTimeline(curr_user->username);
        client_db.push_back(curr_user);
    }

    // Set a success message
    reply->set_msg("SUCCESS: Logged in successfully.");
    return Status::OK;
  }

  Status Timeline(ServerContext* context, 
		ServerReaderWriter<Message, Message>* stream) override {
    Message m;
    while (stream->Read(&m)) {
        std::string username = m.username();
        Client *c = getClient(username);
        if (!c->hasStreamed) {
          c->hasStreamed = true;
          c->stream = stream;
          // read 20 latest massages from file currentuser_timeline
          std::vector<std::vector<std::string>> msgs = get_last_20_messages(username);
          // write messages into the stream
          writeMessagesToStream(msgs, stream);
        } else {
          // if current user started posting
          for (const auto& follower : c->client_followers) {
            // If follower is active, live stream the content
            if (follower->stream != nullptr){ 
              follower->stream->Write(m);
            }
            // Add it to their timeline.txt
            writeToTimeline(follower->username, c->username, m)
          }
        }
    }
    return Status::OK;
  }

    // Function to write into the follower's timeline
  void writeToTimeline(const std::string& followerUsername, const std::string& messageSender, const std::string& messageContent) {
      // Maintain their timeline in the follower->username._timeline file
      std::string filename = followerUsername + "_timeline.txt";
      
      // Get the current time for the timestamp
      auto now = std::chrono::system_clock::now();
      std::time_t now_time = std::chrono::system_clock::to_time_t(now);
      std::tm* local_tm = std::localtime(&now_time);

      // Format the timestamp
      std::ostringstream oss;
      oss << std::put_time(local_tm, "%Y-%m-%d %H:%M:%S");
      std::string timestamp = oss.str();

      // Format the output string
      std::string formatted_message = messageSender + " (" + timestamp + ") >> " + messageContent + "\n";
      
      // Append the formatted message to the follower's timeline file
      std::ofstream outFile(filename, std::ios::app);
      if (outFile.is_open()) {
          outFile << formatted_message;
          outFile.close();
      } else {
          std::cerr << "Error: Unable to open file " << filename << std::endl;
      }
  }

std::vector<std::vector<std::string>> get_last_20_messages(std::string username) {
    // File containing the user's timeline
    std::string filename = username + "_timeline.txt"; 
    std::ifstream inFile(filename);
    std::vector<std::vector<std::string>> messages; // To store the messages
    std::string line;
    // TODO: change Logic
    while (std::getline(inFile, line)) {
      // Add the message directly to the vector
      std::cout << " line " + line << std::endl;
      messages.push_back({line}); // Store the entire line as a single element vector
      // Keep only the last 20 messages
      if (messages.size() > 20) {
        messages.erase(messages.begin()); // Remove the oldest message
      }
    }
    inFile.close();
    return messages; 
  }

  void writeMessagesToStream(const std::vector<std::vector<std::string>>& msgs, 
                            ServerReaderWriter<Message, Message> *stream) {
    for (const auto& msg : msgs) {
        Message m1; // Assuming Message is the correct type for your stream
        std::string full_message = msg[0]; // Assuming the first part is the full formatted message

        // Find the position of " >> "
        std::size_t pos = full_message.find(" >> ");
        if (pos != std::string::npos) {
            std::string user_info = full_message.substr(0, pos); // Get the user info
            std::string message_text = full_message.substr(pos + 4); // Get the actual message
            
            // Extract the username from user_info
            std::size_t username_end = user_info.find(" (");
            std::string username = user_info.substr(0, username_end);
            
            m1.set_username(username); // Set the username
            m1.set_msg(message_text); // Set the message text
            stream->Write(m1); // Write the message to the stream
        }
    }
  }
  
  void createTimeline(const std::string& username) {
    // Open the file in append mode
    std::ofstream outfile(username + "_timeline.txt", std::ios::app);
    if (outfile.is_open()) {
        // Close the file when done
        outfile.close();
        std::cout << "Success: Timeline file created for user '" << username << "'." << std::endl;
    } else {
        std::cerr << "Error: Failed to open timeline file for user '" << username << "'." << std::endl;
    }
  }

void RunServer(std::string port_no) {
  std::string server_address = "0.0.0.0:"+port_no;
  SNSServiceImpl service;

  ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;
  log(INFO, "Server listening on "+server_address);

  server->Wait();
}

void clearTimelineFilesInCurrentDirectory() {
    DIR* dir = opendir(".");
    if (dir == nullptr) {
        std::cerr << "Could not clear Timeline, please restart." << std::endl;
        return;
    }
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;

        // Check if the file ends with "_timeline.txt"
        if (filename.size() >= 13 && filename.compare(filename.size() - 13, 13, "_timeline.txt") == 0) {
            if (std::remove(filename.c_str()) == 0) {
                std::cout << "Cleared file: " << filename << std::endl;
            } else {
                std::cerr << "Failed to remove file: " << filename << std::endl;
            }
        }
    }

    closedir(dir);
}


int main(int argc, char** argv) {

  std::string port = "3010";
  
  int opt = 0;
  while ((opt = getopt(argc, argv, "p:")) != -1){
    switch(opt) {
      case 'p':
          port = optarg;break;
      default:
	  std::cerr << "Invalid Command Line Argument\n";
    }
  }
  clearTimelineFilesInCurrentDirectory();
  std::string log_file_name = std::string("server-") + port;
  google::InitGoogleLogging(log_file_name.c_str());
  log(INFO, "Logging Initialized. Server starting...");
  RunServer(port);

  return 0;
}
