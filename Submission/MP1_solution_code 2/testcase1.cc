#include <iostream>
#include <string>
#include <cstdlib>
#include <thread>
#include <chrono>

// Function to kill a tmux session if it already exists
void kill_tmux_session(const std::string& session_name) {
    std::string kill_command = "tmux kill-session -t " + session_name + " 2>/dev/null";
    system(kill_command.c_str());
}

// Function to create a tmux session
void create_tmux_session(const std::string& session_name) {
    std::string tmux_command = "tmux new-session -d -s " + session_name;
    system(tmux_command.c_str());
}

// Function to open a terminal and attach it to the tmux session
void open_terminal_for_tmux(const std::string& session_name) {
    std::string terminal_command = "gnome-terminal -- tmux attach -t " + session_name;
    system(terminal_command.c_str());
}

// Function to send a command to a tmux session
void send_command_to_tmux(const std::string& session_name, const std::string& command) {
    std::string send_command = "tmux send-keys -t " + session_name + " \"" + command + "\" C-m";
    system(send_command.c_str());
}

int main() {
    // Step 1: Kill existing tmux sessions if they exist
    kill_tmux_session("COORDINATOR");
    kill_tmux_session("SERVER");
    kill_tmux_session("CLIENT");

    // Step 2: CREATE - Create a tmux session for the coordinator
    create_tmux_session("COORDINATOR");

    // Step 3: OPEN - Open a terminal and attach it to the COORDINATOR session
    open_terminal_for_tmux("COORDINATOR");

    // Step 4: SEND COMMAND - Execute the coordinator command
    send_command_to_tmux("COORDINATOR", "./coordinator -p 9090");

    // Step 5: CREATE - Create a tmux session for the server (TSD)
    create_tmux_session("SERVER");

    // Step 6: OPEN - Open a terminal and attach it to the SERVER session
    open_terminal_for_tmux("SERVER");

    // Step 7: SEND COMMAND - Execute the server command
    send_command_to_tmux("SERVER", "./tsd -c 1 -s 1 -h localhost -k 9090 -p 10000");

    // Wait a moment to allow the server to initialize
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Step 8: CREATE - Create a tmux session for the client (TSC)
    create_tmux_session("CLIENT");

    // Step 9: OPEN - Open a terminal and attach it to the CLIENT session
    open_terminal_for_tmux("CLIENT");

    // Step 10: SEND COMMAND - Execute the client command
    send_command_to_tmux("CLIENT", "./tsc -h localhost -k 9090 -u 1");

    // Step 11: SEND COMMAND - Send LIST command to the CLIENT session
    send_command_to_tmux("CLIENT", "LIST");

    // Step 12: SEND COMMAND - Send TIMELINE command to the CLIENT session
    send_command_to_tmux("CLIENT", "TIMELINE");

    return 0;
}
