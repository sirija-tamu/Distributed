#include <iostream>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

int main() {
    std::string coordinator_command = "gnome-terminal -- bash -c 'echo \"STEP 1 (COORDINATOR)\" && ./coordinator -p 9090; exec bash'";
    std::string tsd_command = "gnome-terminal -- bash -c 'echo \"STEP 2 (SERVER)\" && ./tsd -c 1 -s 1 -h localhost -k 9090 -p 1050; exec bash'";
    std::string tsc_command = "gnome-terminal -- bash -c 'echo \"STEP 3 (CLIENT)\" && ./tsc -h localhost -k 9090 -u 1; exec bash'";

    // Start coordinator in a new terminal
    system(coordinator_command.c_str());

    sleep(0.2);

    // Start tsd in a new terminal
    system(tsd_command.c_str());

    sleep(5);

    // Start tsc in a new terminal
    system(tsc_command.c_str());

    return 0;
}
