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

// Function to simulate Ctrl+C to stop the server
void kill_server(const std::string& session_name) {
    std::string send_signal = "tmux send-keys -t " + session_name + " C-c";
    system(send_signal.c_str());
}

// Function to wait for a specified amount of seconds
void wait(float seconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(seconds * 1000)));
}

int main() {
    // Kill existing tmux sessions if they exist
    kill_tmux_session("COORDINATOR");
    kill_tmux_session("SERVER");
    kill_tmux_session("CLIENT");

    // Start COORDINATOR
    create_tmux_session("COORDINATOR");
    open_terminal_for_tmux("COORDINATOR");
    send_command_to_tmux("COORDINATOR", "./coordinator -p 9090");
    wait(1);

    // Start SERVER
    create_tmux_session("SERVER");
    open_terminal_for_tmux("SERVER");
    send_command_to_tmux("SERVER", "./tsd -c 1 -s 1 -h localhost -k 9090 -p 10000");

    // 5 seconds heartbeat for server registration
    wait(5);

    // kill SERVER
    kill_server("SERVER");

    // wait for 1 second before starting the client
    wait(1);
    create_tmux_session("CLIENT");
    open_terminal_for_tmux("CLIENT");
    send_command_to_tmux("CLIENT", "./tsc -h localhost -k 9090 -u 1");

    //wait for 5 seconds and try to start the client
    wait(5);
    send_command_to_tmux("CLIENT", "./tsc -h localhost -k 9090 -u 1");
    
    // Start Server again
    send_command_to_tmux("SERVER", "./tsd -c 1 -s 1 -h localhost -k 9090 -p 10000");

    // 5 seconds heartbeat for server registration
    wait(5);
    send_command_to_tmux("CLIENT", "./tsc -h localhost -k 9090 -u 1");
    // execute commands
    wait(1);
    send_command_to_tmux("CLIENT", "LIST");
    wait(1);
    send_command_to_tmux("CLIENT", "TIMELINE");

    return 0;
}
