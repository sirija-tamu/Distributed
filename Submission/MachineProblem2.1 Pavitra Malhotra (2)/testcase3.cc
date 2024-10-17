#include <iostream>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

std::string executeCommand(const std::string& command) {
    std::string result;
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return "Error";
    }
    char buffer[128];
    while (!feof(pipe)) {
        if (fgets(buffer, sizeof(buffer), pipe) != NULL) {
            result += buffer;
        }
    }
    pclose(pipe);
    return result;
}


int main() {
    std::string coordinator_command = "gnome-terminal -- bash -c 'echo \"STEP 1 (COORDINATOR)\" && ./coordinator -p 9090; exec bash'";
    std::string tsd1_command = "gnome-terminal -- bash -c 'echo \"STEP 2 (SERVER 1)\" && ./tsd -c 1 -s 1 -h localhost -k 9090 -p 1050; exec bash'";
    std::string tsd2_command = "gnome-terminal -- bash -c 'echo \"STEP 3 (SERVER 2)\" && ./tsd -c 2 -s 2 -h localhost -k 9090 -p 1051; exec bash'";
    std::string tsc1_command = "gnome-terminal -- bash -c 'echo \"STEP 4 (CLIENT 1)\" && ./tsc -h localhost -k 9090 -u 1; exec bash'";
    std::string tsc2_command = "gnome-terminal -- bash -c 'echo \"STEP 5 (CLIENT 2)\" && ./tsc -h localhost -k 9090 -u 2; exec bash'";

    // Start coordinator in a new terminal
    system(coordinator_command.c_str());

    sleep(1);

    // Start tsd in a new terminal
    system(tsd1_command.c_str());

    system(tsd2_command.c_str());

    sleep(5);

    system(tsc1_command.c_str());

    system(tsc2_command.c_str());

    sleep(0.2);

    // Kill tsd
    system("pkill -f './tsd -c 2 -s 2 -h localhost -k 9090 -p 1051'");

    sleep(1);

    sleep(5);

    //list timeline c1

    //list timeline c2

    tsd2_command = "gnome-terminal -- bash -c 'echo \"STEP 6 (SERVER 2 - RESTARTING)\" && ./tsd -c 2 -s 2 -h localhost -k 9090 -p 1051; exec bash'";

    system(tsd2_command.c_str());

    sleep(5);

    return 0;
}
