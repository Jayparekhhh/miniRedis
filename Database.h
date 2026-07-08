#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <atomic>
#include <thread>
#include <stdexcept>


struct Entry
{
    std::string value;
    bool hasExpiry = false;
    std::chrono::steady_clock::time_point expiryTime;
};

struct Command
{
    std::string name;
    std::vector<std::string> arguments;
};

class Database
{
private:
    std::unordered_map<std::string, Entry> data_;
    mutable std::mutex mutex_;

public:
    bool set(const std::string& key, const std::string& value);
    std::string get(const std::string& key);
    bool del(const std::string& key);
    bool exists(const std::string& key) const;
    std::vector<std::string> keys() const;
    void clear();
    bool expire(const std::string& key, int seconds);
    bool isExpired(const Entry& entry) const;
    long long ttl(const std::string& key);
    void removeExpiredKeys();
    const std::unordered_map<std::string, Entry>& data() const;
};

class TTLManager
{
private:
    Database& database_;
    std::thread worker_;
    std::atomic<bool> running_;

public:
    explicit TTLManager(Database& database);
    ~TTLManager();
    void start();
    void stop();

private:
    void cleanupLoop();
};

class Serializer
{
public:
    bool save(const Database& database, const std::string& filename);
    bool load(Database& database, const std::string& filename);
};

class CommandParser
{
public:
    Command parse(const std::string& input) const;
};

class RespParser
{
public:
    Command parse(const std::string& input) const;
};

class CommandExecutor
{
private:
    Database& database_;
    Serializer serializer_;

public:
    explicit CommandExecutor(Database& database);
    std::string execute(const Command& command);
};

class Socket
{
private:
    int fd_;

public:
    Socket();
    ~Socket();
    void create();
    int getFd() const;
    void bind(int port);
    void listen(int backlog = SOMAXCONN);
    int accept();
    std::string receive(int clientFd);
    void sendMessage(int clientFd, const std::string& message);
};

class ClientHandler
{
private:
    Socket& socket_;
    Database& database_;
    CommandExecutor executor_;
    int clientFd_;

public:
    ClientHandler(Socket& socket, Database& database, int clientFd);
    void handle();
};

class Server
{
private:
    Socket socket_;
    Database database_;
    TTLManager ttlManager_;
    int port_;
    std::vector<std::thread> clientThreads_;
    Serializer serializer_;

public:
    explicit Server(int port);
    void start();
};
