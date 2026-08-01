#include "server.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    int port = 7070;
    if (argc > 1) port = std::stoi(argv[1]);

    ChatServer server(port);
    server.run();

    return 0;
}
