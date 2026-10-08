#include "server.hpp"
#include <iostream>
#include <cstring>
#include <cerrno>
#include <csignal>
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

void ChatServer::updateEpoll(int fd, uint32_t events) {
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &ev);
}

void ChatServer::run() {
    signal(SIGPIPE, SIG_IGN);   // writes to closed sockets return EPIPE instead of killing us

    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) { std::cerr << "socket() failed\n"; return; }

    int opt = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(listenFd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "bind failed on port " << port_ << "\n"; return;
    }
    if (listen(listenFd_, 512) < 0) {
        std::cerr << "listen failed\n"; return;
    }
    setNonBlocking(listenFd_);

    epollFd_ = epoll_create1(0);
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = listenFd_;
    epoll_ctl(epollFd_, EPOLL_CTL_ADD, listenFd_, &ev);

    std::cout << "mini-chat listening on port " << port_ << std::endl;

    const int MAX_EVENTS = 256;
    epoll_event events[MAX_EVENTS];

    while (true) {
        int n = epoll_wait(epollFd_, events, MAX_EVENTS, -1);
        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            uint32_t evs = events[i].events;

            if (fd == listenFd_) {
                acceptNewConnection();
            } else if (evs & (EPOLLHUP | EPOLLERR)) {
                disconnectClient(fd);
            } else {
                if (evs & EPOLLOUT) handleClientWrite(fd);
                if (clients_.count(fd) && (evs & EPOLLIN)) handleClientRead(fd);
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
    clients_[clientFd] = std::move(c);
    rooms_[clients_[clientFd].room].insert(clientFd);

    std::string welcome = "Welcome! You are " + clients_[clientFd].nickname
                        + " in #lobby. Use /nick <name> and /join <room>.\n";
    clients_[clientFd].outbuf += welcome;
    tryFlush(clientFd);
}

void ChatServer::disconnectClient(int fd) {
    auto it = clients_.find(fd);
    if (it != clients_.end()) {
        std::string room = it->second.room;
        std::string nick = it->second.nickname;

        auto rit = rooms_.find(room);
        if (rit != rooms_.end()) {
            rit->second.erase(fd);
            if (rit->second.empty()) rooms_.erase(rit);
        }

        clients_.erase(it);
        broadcastToRoom(room, nick + " left the chat.\n", fd);
    }
    epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
}

void ChatServer::handleClientRead(int fd) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) return;

    char buf[4096];
    ssize_t n = read(fd, buf, sizeof(buf));
    if (n == 0) { disconnectClient(fd); return; }
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;
        disconnectClient(fd);
        return;
    }
    it->second.inbuf.append(buf, static_cast<size_t>(n));

    while (true) {
        auto cit = clients_.find(fd);
        if (cit == clients_.end()) return;

        size_t pos = cit->second.inbuf.find('\n');
        if (pos == std::string::npos) break;

        std::string line = cit->second.inbuf.substr(0, pos);
        cit->second.inbuf.erase(0, pos + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        processLine(cit->second, line);
    }
}

void ChatServer::handleClientWrite(int fd) {
    tryFlush(fd);
}

void ChatServer::tryFlush(int fd) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) return;
    Client& c = it->second;

    while (!c.outbuf.empty()) {
        ssize_t n = write(fd, c.outbuf.data(), c.outbuf.size());
        if (n > 0) {
            c.outbuf.erase(0, static_cast<size_t>(n));
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (!c.wantWrite) {
                c.wantWrite = true;
                updateEpoll(fd, EPOLLIN | EPOLLOUT);
            }
            return;
        } else {
            return;
        }
    }
    if (c.wantWrite) {
        c.wantWrite = false;
        updateEpoll(fd, EPOLLIN);
    }
}

void ChatServer::processLine(Client& client, const std::string& line) {
    if (line.rfind("/nick ", 0) == 0) {
        client.nickname = line.substr(6);
        client.outbuf += "You are now known as " + client.nickname + "\n";
        tryFlush(client.fd);
        return;
    }

    if (line.rfind("/join ", 0) == 0) {
        std::string newRoom = line.substr(6);
        std::string oldRoom = client.room;

        auto oldIt = rooms_.find(oldRoom);
        if (oldIt != rooms_.end()) {
            oldIt->second.erase(client.fd);
            if (oldIt->second.empty()) rooms_.erase(oldIt);
        }
        broadcastToRoom(oldRoom, client.nickname + " left the room.\n", client.fd);

        client.room = newRoom;
        rooms_[newRoom].insert(client.fd);
        broadcastToRoom(client.room, client.nickname + " joined the room.\n", client.fd);

        client.outbuf += "Joined #" + newRoom + "\n";
        tryFlush(client.fd);
        return;
    }

    if (line == "/list") {
        std::string msg = "Users in #" + client.room + ":\n";
        auto it = rooms_.find(client.room);
        if (it != rooms_.end()) {
            for (int fd : it->second) {
                auto cit = clients_.find(fd);
                if (cit != clients_.end()) msg += "  " + cit->second.nickname + "\n";
            }
        }
        client.outbuf += msg;
        tryFlush(client.fd);
        return;
    }

    std::string out = "[" + client.room + "] " + client.nickname + ": " + line + "\n";
    broadcastToRoom(client.room, out, -1);
}

void ChatServer::broadcastToRoom(const std::string& room, const std::string& message, int excludeFd) {
    auto it = rooms_.find(room);
    if (it == rooms_.end()) return;

    std::vector<int> targets(it->second.begin(), it->second.end());
    for (int fd : targets) {
        if (fd == excludeFd) continue;
        auto cit = clients_.find(fd);
        if (cit == clients_.end()) continue;
        cit->second.outbuf += message;
        tryFlush(fd);
    }
}
