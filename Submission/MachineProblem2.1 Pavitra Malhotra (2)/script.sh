#!/bin/bash

# Start coordinator in a new terminal
gnome-terminal -- bash -c './coordinator -p 9090; exec bash'

# Sleep for 1 second
sleep 1

# Start tsd in a new terminal
gnome-terminal -- bash -c './tsd -c 1 -s 1 -h localhost -k 9090 -p 1050; exec bash'

# Sleep for 5 seconds
sleep 5

# Kill tsd
pkill -f "./tsd"

# Sleep for 1 second
sleep 1

# Start tsc in a new terminal
gnome-terminal -- bash -c './tsc -h localhost -k 9090 -u 1; exec bash'

# Sleep for 5 seconds
sleep 5

# Start tsc again in the same terminal (CCC)
gnome-terminal -- bash -c './tsc -h localhost -k 9090 -u 1; exec bash'


