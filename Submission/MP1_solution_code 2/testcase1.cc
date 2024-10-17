#include <iostream>
#include <string>
#include <cstdlib>
#include <thread>
#include <chrono>

void open_terminal(const std::string& label, const std::string& command) {
    std::string terminal_command = "gnome-terminal -- bash -c 'echo \"" + label + "\" && " + command + "; exec bash'";
    system(terminal_command.c_str());
}

int main() {
    // Open coordinator in a new terminal
    open_terminal("COORDINATOR STARTUP", "./coordinator -p 9090");

    // Wait 1 second to ensure the coordinator starts properly
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Open tsd (server) in a new terminal
    open_terminal("SERVER STARTUP (TSD)", "./tsd -c 1 -s 1 -h localhost -k 9090 -p 10000");

    // Wait 5 seconds to allow the server to initialize
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Open tsc (client) in a new terminal
    open_terminal("CLIENT STARTUP (TSC)", "./tsc -h localhost -k 9090 -u 1");

    return 0;
}
