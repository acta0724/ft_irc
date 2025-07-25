#include <iostream>
#include <string>
#include <cstdlib>

#include "IrcServer.hpp"

int main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " <port> <password>" << std::endl;
    return 1;
  }

  int port = std::atoi(argv[1]);
  std::string password = argv[2];

  if (port <= 0 || port > 65535) {
    std::cerr << "Error: Invalid port number. Port must be between 1 and 65535." << std::endl;
    return 1;
  }

  IrcServer server = IrcServer(port, password);
  server.activate();
  return 0;
}
