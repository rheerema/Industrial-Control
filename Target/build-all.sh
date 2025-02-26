#!/bin/bash

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
