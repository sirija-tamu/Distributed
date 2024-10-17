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

    // Kill tsd
    system("pkill -f './tsd'");

    sleep(1);

    // Start tsc in a new terminal
    system(tsc_command.c_str());

    sleep(5);

    //system("pkill -f 'tsc'");

    tsc_command = "gnome-terminal -- bash -c 'echo \"STEP 4 (CLIENT - RUNNING AGAIN)\" && ./tsc -h localhost -k 9090 -u 1; exec bash'";

    system(tsc_command.c_str());

    sleep(0.2);

    tsd_command = "gnome-terminal -- bash -c 'echo \"STEP 5 (SERVER - RESTART)\" && ./tsd -c 1 -s 1 -h localhost -k 9090 -p 1050; exec bash'";

    system(tsd_command.c_str());

    sleep(5);

    //system("pkill -f 'tsc'");

    tsc_command = "gnome-terminal -- bash -c 'echo \"STEP 6 (CLIENT - RUNNING AGAIN AFTER SERVER RESTART)\" && ./tsc -h localhost -k 9090 -u 1; exec bash'";

    system(tsc_command.c_str());

    return 0;
}
