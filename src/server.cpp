#include "server.hpp"
#include <iostream>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>

namespace {
void setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
}

ChatServer::ChatServer(int port) : port_(port) {}

void ChatServer::run() {
    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        std::cerr << "Failed to create socket\n";
        return;
    }

    int opt = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(listenFd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Bind failed on port " << port_ << "\n";
        return;
    }

    listen(listenFd_, 128);
    setNonBlocking(listenFd_);

    epollFd_ = epoll_create1(0);
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = listenFd_;
    epoll_ctl(epollFd_, EPOLL_CTL_ADD, listenFd_, &ev);

    std::cout << "mini-chat listening on port " << port_ << "...\n";

    const int MAX_EVENTS = 64;
    epoll_event events[MAX_EVENTS];

    while (true) {
        int n = epoll_wait(epollFd_, events, MAX_EVENTS, -1);
        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            if (fd == listenFd_) {
                acceptNewConnection();
            } else if (events[i].events & (EPOLLHUP | EPOLLERR)) {
                disconnectClient(fd);
            } else if (events[i].events & EPOLLIN) {
                handleClientData(fd);
            }
        }
    }
}

void ChatServer::acceptNewConnection() {
    sockaddr_in clientAddr{};
    socklen_t clientLen = sizeof(clientAddr);
    int clientFd = accept(listenFd_, (sockaddr*)&clientAddr, &clientLen);
    if (clientFd < 0) return;

    setNonBlocking(clientFd);

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = clientFd;
    epoll_ctl(epollFd_, EPOLL_CTL_ADD, clientFd, &ev);

    Client c;
    c.fd = clientFd;
    c.nickname = "guest" + std::to_string(clientFd);
    clients_[clientFd] = c;

    std::string welcome = "Welcome! You are " + c.nickname + " in #lobby. Use /nick <name> and /join <room>.\n";
    write(clientFd, welcome.c_str(), welcome.size());
}

void ChatServer::disconnectClient(int fd) {
    epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
    auto it = clients_.find(fd);
    if (it != clients_.end()) {
        broadcastToRoom(it->second.room, it->second.nickname + " left the chat.\n", fd);
        clients_.erase(it);
    }
}

void ChatServer::handleClientData(int fd) {
    char buffer[4096];
    ssize_t bytesRead = read(fd, buffer, sizeof(buffer) - 1);
    if (bytesRead <= 0) {
        disconnectClient(fd);
        return;
    }
    buffer[bytesRead] = '\0';

    std::istringstream stream(buffer);
    std::string line;
    while (std::getline(stream, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();
        if (line.empty()) continue;
        processLine(clients_[fd], line);
    }
}

void ChatServer::processLine(Client& client, const std::string& line) {
    if (line.rfind("/nick ", 0) == 0) {
        client.nickname = line.substr(6);
        std::string msg = "You are now known as " + client.nickname + "\n";
        write(client.fd, msg.c_str(), msg.size());
        return;
    }

    if (line.rfind("/join ", 0) == 0) {
        std::string newRoom = line.substr(6);
        broadcastToRoom(client.room, client.nickname + " left the room.\n", client.fd);
        client.room = newRoom;
        broadcastToRoom(client.room, client.nickname + " joined the room.\n", client.fd);
        std::string msg = "Joined #" + newRoom + "\n";
        write(client.fd, msg.c_str(), msg.size());
        return;
    }

    if (line == "/list") {
        std::string msg = "Users in #" + client.room + ":\n";
        for (auto& [fd, c] : clients_) {
            if (c.room == client.room) msg += "  " + c.nickname + "\n";
        }
        write(client.fd, msg.c_str(), msg.size());
        return;
    }

    // default: broadcast as a chat message
    std::string out = "[" + client.room + "] " + client.nickname + ": " + line + "\n";
    broadcastToRoom(client.room, out, -1); // include sender, so they see their own message too
}

void ChatServer::broadcastToRoom(const std::string& room, const std::string& message, int excludeFd) {
    for (auto& [fd, c] : clients_) {
        if (c.room == room && fd != excludeFd) {
            write(fd, message.c_str(), message.size());
        }
    }
}
