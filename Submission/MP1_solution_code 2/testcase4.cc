#include <iostream>
#include <string>
#include <cstdlib>
#include <thread>
#include <chrono>

// Function to open a new terminal and execute a command
void open_terminal(const std::string& label, const std::string& command) {
    std::string terminal_command = "gnome-terminal -- bash -c 'echo \"" + label + "\" && " + command + "; exec bash'";
    system(terminal_command.c_str());
}

// Function to send additional commands to a terminal using tmux
void send_command_to_terminal(const std::string& terminal_name, const std::string& command) {
    std::string send_command = "tmux send-keys -t " + terminal_name + " \"" + command + "\" C-m";
    system(send_command.c_str());
}

int main() {
    // Step 1: Open coordinator in a new terminal
    open_terminal("COORDINATOR STARTUP", "./coordinator -p 9090");

    // Wait 1 second to ensure the coordinator starts properly
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Step 2: Open the server (TSD) in a new terminal
    open_terminal("SERVER STARTUP (TSD)", "./tsd -c 1 -s 1 -h localhost -k 9090 -p 10000");

    // Wait 5 seconds to allow the server to initialize
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Step 3: Open the client (TSC) in a new terminal and attach to tmux
    system("tmux new-session -d -s TSC"); // Create a tmux session for TSC
    open_terminal("CLIENT STARTUP (TSC)", "tmux attach -t TSC && ./tsc -h localhost -k 9090 -u 1");

    // Wait 2 seconds before sending additional commands to TSC terminal
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Step 4: Send LIST and FOLLOW commands to the TSC terminal
    send_command_to_terminal("TSC", "LIST");
    
    // Wait 1 second before sending the FOLLOW command
    std::this_thread::sleep_for(std::chrono::seconds(1));
    send_command_to_terminal("TSC", "FOLLOW");

    return 0;
}
