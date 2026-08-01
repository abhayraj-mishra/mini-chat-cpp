#pragma once
#include <string>
#include <unordered_map>
#include <vector>

// A single-threaded, event-driven (epoll-based) multi-room chat server.
// Deliberately built differently from a thread-per-client model to show
// understanding of I/O multiplexing -- a common systems interview topic.
//
// Client commands (one per line):
//   /nick <name>          set your display name
//   /join <room>          join or switch to a room (default room: "lobby")
//   /list                 list users in your current room
//   anything else         broadcast the line to everyone in your room
//
// TODO(day2): add a message-rate limiter per client to prevent flooding.
// TODO(day3): persist chat history per room to disk, replay last N lines
//             to a client when they /join a room.
// TODO(day4): add private messaging: /msg <user> <text>.
// TODO(day5): add basic auth (token in first line) before allowing /nick.
struct Client {
    int fd;
    std::string nickname;
    std::string room = "lobby";
};

class ChatServer {
public:
    explicit ChatServer(int port);
    void run();

private:
    void acceptNewConnection();
    void handleClientData(int fd);
    void disconnectClient(int fd);
    void broadcastToRoom(const std::string& room, const std::string& message, int excludeFd = -1);
    void processLine(Client& client, const std::string& line);

    int port_;
    int listenFd_ = -1;
    int epollFd_ = -1;
    std::unordered_map<int, Client> clients_; // fd -> client state
};
