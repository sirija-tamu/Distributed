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


// Function to open a terminal and attach it to the tmux session
void open_terminal_for_tmux(const std::string& session_name) {
    std::string terminal_command = "gnome-terminal -- tmux attach -t " + session_name;
    system(terminal_command.c_str());
}

// Function to create a tmux session
void create_tmux_session(const std::string& session_name) {
    std::string tmux_command = "tmux new-session -d -s " + session_name;
    system(tmux_command.c_str());
    open_terminal_for_tmux(session_name);
}


// Function to send a command to a tmux session
void send_command_to_tmux(const std::string& session_name, const std::string& command) {
    std::string send_command = "tmux send-keys -t " + session_name + " \"" + command + "\" C-m";
    system(send_command.c_str());
}

// Function to wait for a specified amount of seconds
void wait(float seconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(seconds * 1000)));
}

int main() {
    // Kill all existing sessions
    kill_tmux_session("COORDINATOR");
    kill_tmux_session("MASTER1");
    kill_tmux_session("SLAVE1");
    kill_tmux_session("MASTER2");
    kill_tmux_session("SLAVE2");
    kill_tmux_session("MASTER3");
    kill_tmux_session("SLAVE3");
    kill_tmux_session("SYNC1");
    kill_tmux_session("SYNC2");
    kill_tmux_session("SYNC3");
    kill_tmux_session("CLIENT1");
    kill_tmux_session("CLIENT2");
    kill_tmux_session("CLIENT3");
    wait(2);

    // Start Coordinator
    create_tmux_session("COORDINATOR");
    wait(1);
    send_command_to_tmux("COORDINATOR", "./coordinator -p 9090");
    // Start 3 Master Servers
    create_tmux_session("MASTER1");
    wait(1);
    send_command_to_tmux("MASTER1", "./tsd -c 1 -s 1 -k 9090 -p 10000");

    create_tmux_session("MASTER2");
    wait(1);
    send_command_to_tmux("MASTER2", "./tsd -c 2 -s 2 -k 9090 -p 20000");

    create_tmux_session("MASTER3");
    wait(1);
    send_command_to_tmux("MASTER3", "./tsd -c 3 -s 3 -k 9090 -p 30000");

    // Start 3 Slave Servers
    create_tmux_session("SLAVE1");
    wait(1);
    send_command_to_tmux("SLAVE1", "./tsd -c 1 -s 4 -k 9090 -p 10001");

    create_tmux_session("SLAVE2");
    wait(1);
    send_command_to_tmux("SLAVE2", "./tsd -c 2 -s 5 -k 9090 -p 20001");

    create_tmux_session("SLAVE3");
    wait(1);
    send_command_to_tmux("SLAVE3", "./tsd -c 3 -s 6 -k 9090 -p 30001");

    // Start 3 Synchronizers
    create_tmux_session("SYNC1");
    wait(1);
    send_command_to_tmux("SYNC1", "./synchronizer -h localhost -k 9090 -p 1234 -i 1");

    create_tmux_session("SYNC2");
    wait(1);
    send_command_to_tmux("SYNC2", "./synchronizer  -h localhost -k 9090 -p 1235 -i 2");

    create_tmux_session("SYNC3");
    wait(1);
    send_command_to_tmux("SYNC3", "./synchronizer  -h localhost -k 9090 -p 1236 -i 3");

    // Start 3 Clients
    create_tmux_session("CLIENT1");
    wait(1);
    send_command_to_tmux("CLIENT1", "./tsc -k 9090 -u 1");

    create_tmux_session("CLIENT2");
    wait(1);
    send_command_to_tmux("CLIENT2", "./tsc -k 9090 -u 2");

    create_tmux_session("CLIENT3");
    wait(1);
    send_command_to_tmux("CLIENT3", "./tsc -k 9090 -u 3");
    
    // Simulate operations
    // Validate synchronization
    // Clients 1 and 2 retrieve messages in their timelines
    wait(30);
    send_command_to_tmux("CLIENT1", "LIST");
    send_command_to_tmux("CLIENT1", "FOLLOW 2");
    // wait for Sync
    wait(30);
    // post in timeline
    send_command_to_tmux("CLIENT2", "LIST");
    send_command_to_tmux("CLIENT2", "TIMELINE");
    wait(1);
    send_command_to_tmux("CLIENT2", "p1");
    wait(1);
    // open timeline
    send_command_to_tmux("CLIENT1", "TIMELINE");
    wait(30);
    std::cout << "Test case complete. Validate the output manually." << std::endl;
    wait(120);
    kill_tmux_session("COORDINATOR");
    kill_tmux_session("MASTER1");
    kill_tmux_session("SLAVE1");
    kill_tmux_session("MASTER2");
    kill_tmux_session("SLAVE2");
    kill_tmux_session("MASTER3");
    kill_tmux_session("SLAVE3");
    kill_tmux_session("SYNC1");
    kill_tmux_session("SYNC2");
    kill_tmux_session("SYNC3");
    kill_tmux_session("CLIENT1");
    kill_tmux_session("CLIENT2");
    kill_tmux_session("CLIENT3");
    return 0;
}
