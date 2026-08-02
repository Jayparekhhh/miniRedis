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
    bool set(  std::string& key,   std::string& value);
    std::string get(  std::string& key);
    bool del(  std::string& key);
    bool exists(  std::string& key)  ;
    std::vector<std::string> keys()  ;
    void clear();
    bool expire(  std::string& key, int seconds);
    bool isExpired(  Entry& entry)  ;
    long long ttl(  std::string& key);
    void removeExpiredKeys();
      std::unordered_map<std::string, Entry>& data()  ;
};

class TTLManager
{
private:
    Database& database_;
    std::thread worker_;
    std::atomic<bool> running_;

public:
       TTLManager(Database& database);
    ~TTLManager();
    void start();
    void stop();

private:
    void cleanupLoop();
};

class Serializer
{
public:
    bool save(  Database& database,   std::string& filename);
    bool load(Database& database,   std::string& filename);
};

class CommandParser
{
public:
    Command parse(  std::string& input)  ;
};

class RespParser
{
public:
    Command parse(  std::string& input)  ;
};

class CommandExecutor
{
private:
    Database& database_;
    Serializer serializer_;

public:
       CommandExecutor(Database& database);
    std::string execute(  Command& command);
};

class Socket
{
private:
    int fd_;

public:
    Socket();
    ~Socket();
    void create();
    int getFd()  ;
    void bind(int port);
    void listen(int backlog = SOMAXCONN);
    int accept();
    std::string receive(int clientFd);
    void sendMessage(int clientFd,   std::string& message);
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
       Server(int port);
    void start();
};
