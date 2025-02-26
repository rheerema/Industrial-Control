#!/bin/bash

# Function to run commands with sudo
run_with_sudo() {
  sudo "$@"
}

# Clean up any previous installation.  Begin by stopping and disabling
run_with_sudo systemctl stop printem.service
run_with_sudo systemctl disable printem.service
run_with_sudo systemctl daemon-reload

# Remove previous copies
[ -f "/usr/local/bin/printem" ] && run_with_sudo rm -f printem
[ -f "/etc/systemd/system/printem.service" ] && run_with_sudo rm -f /etc/systemd/system/printem.service

# Build each component
# Console is an interactive command shell
cd ../PE-Console
make clean
make
cd -

# Control allows a program to call it for services
cd ../PE-Control
make clean
make
cd -

# The Printer Emulator protocol engine runs as a service under systemd
cd ../Printer-Emulator
make clean
make
cd -

# Copy the Printer Emulator protocol service to systemd
run_with_sudo cp ../Printer-Emulator/printem /usr/local/bin
run_with_sudo cp ./printem.service /etc/systemd/system/

# Reload systemd
run_with_sudo systemctl daemon-reload

# Enable the service to start at boot
run_with_sudo systemctl enable printem.service

# Start the service immediately
run_with_sudo systemctl start printem.service


