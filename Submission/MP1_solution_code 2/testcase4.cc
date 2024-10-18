#include <iostream>
#include <string>
#include <cstdlib>
#include <thread>
#include <chrono>

// Function to create a tmux session and execute a command
void create_tmux_session(const std::string& session_name, const std::string& command) {
    std::string tmux_command = "tmux new-session -d -s " + session_name + " '" + command + "'";
    system(tmux_command.c_str());
}

// Function to send additional commands to a tmux session
void send_command_to_tmux(const std::string& session_name, const std::string& command) {
    std::string send_command = "tmux send-keys -t " + session_name + " \"" + command + "\" C-m";
    system(send_command.c_str());
}

int main() {
    // Step 1: Create a tmux session for the coordinator and start it
    create_tmux_session("COORDINATOR", "./coordinator -p 9090");
    
    // Wait 1 second to ensure the coordinator starts properly
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Step 2: Create a tmux session for the server (TSD) and start it
    create_tmux_session("SERVER", "./tsd -c 1 -s 1 -h localhost -k 9090 -p 10000");

    // Wait 5 seconds to allow the server to initialize
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Step 3: Create a tmux session for the client (TSC) and start it
    create_tmux_session("CLIENT", "./tsc -h localhost -k 9090 -u 1");

    // Wait 2 seconds before sending additional commands to the CLIENT terminal
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Step 4: Send LIST and FOLLOW commands to the CLIENT terminal
    send_command_to_tmux("CLIENT", "LIST");

    // Wait 1 second before sending the FOLLOW command
    std::this_thread::sleep_for(std::chrono::seconds(1));
    send_command_to_tmux("CLIENT", "FOLLOW");

    return 0;
}
