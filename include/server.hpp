#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstdint>

struct Client {
    int fd = -1;
    std::string nickname;
    std::string room = "lobby";
    std::string inbuf;    // accumulates until '\n'
    std::string outbuf;   // pending bytes to send
    bool wantWrite = false;
};

class ChatServer {
public:
    explicit ChatServer(int port);
    void run();

private:
    void acceptNewConnection();
    void handleClientRead(int fd);
    void handleClientWrite(int fd);
    void disconnectClient(int fd);
    void broadcastToRoom(const std::string& room, const std::string& message, int excludeFd = -1);
    void processLine(Client& client, const std::string& line);
    void tryFlush(int fd);
    void updateEpoll(int fd, uint32_t events);

    int port_;
    int listenFd_ = -1;
    int epollFd_ = -1;
    std::unordered_map<int, Client> clients_;
    std::unordered_map<std::string, std::unordered_set<int>> rooms_;
};
