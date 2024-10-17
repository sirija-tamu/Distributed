#include <iostream>
#include <string>
#include <cstdlib>
#include <thread>
#include <chrono>

// Function to open a new terminal and execute the given command
void open_terminal(const std::string& description, const std::string& command) {
    std::string terminal_command = "gnome-terminal -- bash -c 'echo \"" + description + "\" && " + command + "; exec bash'";
    system(terminal_command.c_str());
}

// Function to wait for a specified amount of seconds
void wait(float seconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(seconds * 1000)));
}

int main() {
    // Step 1: Start the coordinator
    open_terminal("STARTING COORDINATOR", "./coordinator -p 9090");
    wait(1);

    // Step 2: Start Server 1
    open_terminal("STARTING SERVER 1", "./tsd -c 1 -s 1 -h localhost -k 9090 -p 10000");

    // Step 3: Start Server 2
    open_terminal("STARTING SERVER 2", "./tsd -c 2 -s 2 -h localhost -k 9090 -p 10001");
    wait(5);

    // Step 4: Start Client 1
    open_terminal("STARTING CLIENT 1", "./tsc -h localhost -k 9090 -u 1");

    // Step 5: Start Client 2
    open_terminal("STARTING CLIENT 2", "./tsc -h localhost -k 9090 -u 2");
    wait(0.2);

    // Step 6: Kill Server 2
    system("pkill -f './tsd -c 2 -s 2 -h localhost -k 9090 -p 10001'");
    wait(1);
    //list timeline c2
    wait(5);
    //list timeline c1
    //list timeline c2

    // Step 7: Restart Server 2
    open_terminal("RESTARTING SERVER 2", "./tsd -c 2 -s 2 -h localhost -k 9090 -p 1051");
    wait(5);

    // Step 8: Kill Server 2
    system("pkill -f './tsc -h localhost -k 9090 -u 2'");
    // Restart
    open_terminal("STARTING CLIENT 2", "./tsc -h localhost -k 9090 -u 2");
    return 0;
}
