# HTTP Proxy Server

A concurrent web proxy server that accepts incoming connections, reads and parses HTTP/1.0 GET requests, forwards requests to web servers, reads the servers’ responses, and forwards the responses to the corresponding clients. The `socket` libray is used to communicate over network connections. Uses `POSIX` threads to deal with multiple clients concurrently. Tested on `64-bit Ubuntu 22.04.1 LTS (Linux kernel 5.15.0)`.

# Project Structure

- `proxy.c` : the main implementation file
- `csapp.{h,c}` : The CS:APP package which includes the robust I/O (RIO) package
- `http_parser.h : A small HTTP string parsing library
- `Makefile`: This is the makefile that builds the proxy program. Type `make`
  to build your solution, or `make clean` followed by `make` for a
  fresh build.
- `port-for-user.pl`: Generates a random port for a particular user
  - usage: `./port-for-user.pl <AndrewID>`
- `driver.sh`: The autograder code used by Autolab
  - usage: `./driver.sh check` for the checkpoint, or `./driver.sh` for the final submission
- `pxy` : PxyDrive testing framework
- `tests/`: Test files used by Pxydrive
- `tiny`: Tiny Web server from the CS:APP text
