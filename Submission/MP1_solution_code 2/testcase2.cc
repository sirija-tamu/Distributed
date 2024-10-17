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
    // Start coordinator
    open_terminal("COORDINATOR STARTUP", "./coordinator -p 9090");
    wait(0.2);

    // Start the server (tsd)
    open_terminal("SERVER STARTUP (TSD)", "./tsd -c 1 -s 1 -h localhost -k 9090 -p 10000");
    wait(5);

    // Kill the server (tsd)
    system("pkill -f './tsd'");
    wait(1);

    // Start the client (tsc)
    open_terminal("CLIENT STARTUP (TSC)", "./tsc -h localhost -k 9090 -u 1");
    wait(5);

    // Restart the client (tsc) after server is stopped
    open_terminal("CLIENT RESTART AFTER SERVER SHUTDOWN", "./tsc -h localhost -k 9090 -u 1");
    wait(0.2);

    // Restart the server (tsd)
    open_terminal("SERVER RESTART (TSD)", "./tsd -c 1 -s 1 -h localhost -k 9090 -p 10000");
    wait(5);

    // Start the client again after server restart
    open_terminal("CLIENT STARTUP AFTER SERVER RESTART", "./tsc -h localhost -k 9090 -u 1");

    return 0;
}
