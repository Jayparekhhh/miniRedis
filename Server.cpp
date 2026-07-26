#include "Database.h"

#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <iostream>
#include <cstring>
#include <netinet/in.h>
#include <sstream>

Socket::Socket()
    : fd_(-1)
{
}

Socket::~Socket()
{
    if (fd_ != -1)
    {
        close(fd_);
    }
}

void Socket::create()
{
    fd_ = socket(AF_INET, SOCK_STREAM, 0);

    if (fd_ == -1)
    {
        throw std::runtime_error(
            std::string("Failed to create socket: ") +
            std::strerror(errno)
        );
    }
    int opt = 1;

    if (setsockopt(
            fd_,
            SOL_SOCKET,
            SO_REUSEADDR,
            &opt,
            sizeof(opt)) == -1)
    {
        throw std::runtime_error(
            std::string("setsockopt failed: ") +
            std::strerror(errno)
        );
    }
}

int Socket::getFd() 
{
    return fd_;
}

void Socket::bind(int port)
{
    sockaddr_in serverAddress{};

    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(port);

    if (::bind(
            fd_,
            reinterpret_cast<sockaddr*>(&serverAddress),
            sizeof(serverAddress)) == -1)
    {
        throw std::runtime_error(
            std::string("Bind failed: ") +
            std::strerror(errno)
        );
    }
}

void Socket::listen(int backlog)
{
    if (::listen(fd_, backlog) == -1)
    {
        throw std::runtime_error(
            std::string("Listen failed: ") +
            std::strerror(errno)
        );
    }
}

int Socket::accept()
{
    int clientFd = ::accept(fd_, nullptr, nullptr);

    if (clientFd == -1)
    {
        throw std::runtime_error(
            std::string("Accept failed: ") +
            std::strerror(errno)
        );
    }

    return clientFd;
}

std::string Socket::receive(int clientFd)
{
    char buffer[1024];

    ssize_t bytesReceived =
        recv(clientFd,
             buffer,
             sizeof(buffer) - 1,
             0);

    if (bytesReceived == -1)
    {
        throw std::runtime_error(
            std::string("Receive failed: ") +
            std::strerror(errno)
        );
    }

    buffer[bytesReceived] = '\0';

    std::string msg(buffer);
    return msg;
}

void Socket::sendMessage(
    int clientFd,
     std::string& message)
{
    ssize_t bytesSent =
        send(clientFd,
             message.c_str(),
             message.size(),
             0);

    if (bytesSent == -1)
    {
        throw std::runtime_error(
            std::string("Send failed: ") +
            std::strerror(errno)
        );
    }
}

Command CommandParser::parse(
     std::string& input) 
{
    Command command;

    std::istringstream stream(input);

    stream >> command.name;

    std::string argument;

    while (stream >> argument)
    {
        command.arguments.push_back(argument);
    }

    return command;
}

Command RespParser::parse(
     std::string& input) 
{
    Command command;

    std::istringstream stream(input);

    std::vector<std::string> lines;

    std::string line;

    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        lines.push_back(line);
    }

    if (lines.empty())
    {
        return command;
    }

    if (lines[0].empty() || lines[0][0] != '*')
    {
        throw std::runtime_error("Invalid RESP array");
    }

    for (std::size_t i = 2; i < lines.size(); i += 2)
    {
        if (i == 2)
        {
            command.name = lines[i];
        }
        else
        {
            command.arguments.push_back(lines[i]);
        }
    }

    return command;
}

CommandExecutor::CommandExecutor(Database& database)
    : database_(database)
{
}

std::string CommandExecutor::execute( Command& command)
{
    if (command.name == "SET")
    {
        if (command.arguments.size() != 2)
        {
            return "ERR invalid SET command\n";
        }

        database_.set(
            command.arguments[0],
            command.arguments[1]);

        return "OK\n";
    }

    else if (command.name == "GET")
    {
        if (command.arguments.size() != 1)
        {
            return "ERR invalid GET command\n";
        }

        return database_.get(command.arguments[0]) + "\n";
    }

    else if (command.name == "DEL")
    {
        if (command.arguments.size() != 1)
        {
            return "ERR invalid DEL command\n";
        }

        return database_.del(command.arguments[0])
               ? "1\n"
               : "0\n";
    }

    else if (command.name == "EXISTS")
    {
        if (command.arguments.size() != 1)
        {
            return "ERR invalid EXISTS command\n";
        }

        return database_.exists(command.arguments[0])
               ? "1\n"
               : "0\n";
    }

    else if (command.name == "KEYS")
    {
        if (!command.arguments.empty())
        {
            return "ERR KEYS takes no arguments\n";
        }

        auto keys = database_.keys();

        if (keys.empty())
        {
            return "(empty)\n";
        }

        std::string response;

        for ( auto& key : keys)
        {
            response += key;
            response += '\n';
        }

        return response;
    }

    else if (command.name == "CLEAR")
    {
        if (!command.arguments.empty())
        {
            return "ERR CLEAR takes no arguments\n";
        }

        database_.clear();

        return "OK\n";
    }

    else if (command.name == "EXPIRE")
    {
        if (command.arguments.size() != 2)
        {
            return "ERR invalid EXPIRE command\n";
        }

        int seconds =
            std::stoi(command.arguments[1]);

        return database_.expire(
                command.arguments[0],
                seconds)
            ? "1\n"
            : "0\n";
    }
    else if (command.name == "TTL")
    {
        if (command.arguments.size() != 1)
        {
            return "ERR invalid TTL command\n";
        }

        return std::to_string(
            database_.ttl(command.arguments[0])
        ) + "\n";
    }
    else if (command.name == "SAVE")
    {
        if (!command.arguments.empty())
        {
            return "ERR SAVE takes no arguments\n";
        }

        bool success =
            serializer_.save(
                database_,
                "cachecore.rdb"
            );

        return success
            ? "OK\n"
            : "ERR failed to save\n";
    }
    else if (command.name == "LOAD")
    {
        if (!command.arguments.empty())
        {
            return "ERR LOAD takes no arguments\n";
        }

        bool success =
            serializer_.load(
                database_,
                "cachecore.rdb"
            );

        return success
            ? "OK\n"
            : "ERR failed to load\n";
    }
    else if (command.name == "PING")
    {
        if (!command.arguments.empty())
        {
            return "ERR PING takes no arguments\n";
        }

        return "PONG\n";
    }
    return "ERR unknown command\n";
}

ClientHandler::ClientHandler(
    Socket& socket,
    Database& database,
    int clientFd)
    : socket_(socket),
      database_(database),
      executor_(database),
      clientFd_(clientFd)
{
}

void ClientHandler::handle()
{
    CommandParser commandParser;
    RespParser respParser;
    while (true)
    {
        std::string message =
            socket_.receive(clientFd_);

        if (message.empty())
        {
            std::cout
                << "Client disconnected\n";
            break;
        }

        Command command;
        if (!message.empty() && message[0] == '*')
        {
            command = respParser.parse(message);
        }
        else
        {
            command = commandParser.parse(message);
        }
        
        std::string response =
            executor_.execute(command);

        socket_.sendMessage(
            clientFd_,
            response
        );
        
    }
}

Server::Server(int port)
    : socket_(),
      database_(),
      ttlManager_(database_),
      port_(port)
{
}

void Server::start()
{
    socket_.create();
    serializer_.load(
        database_,
        "cachecore.rdb"
    );
    std::cout
        << "Socket created. FD = "
        << socket_.getFd()
        << '\n';

    socket_.bind(port_);

    std::cout
        << "Bound to port "
        << port_
        << '\n';

    socket_.listen(128);
    ttlManager_.start();

    std::cout
        << "Listening on port "
        << port_
        << '\n';

    while (true)
    {
        try{
            int clientFd = socket_.accept();

            std::cout
                << "Client connected. FD = "
                << clientFd
                << '\n';

            std::thread(
                [this, clientFd]()
                {
                    ClientHandler handler(
                        socket_,
                        database_,
                        clientFd);

                    handler.handle();
                }
            ).detach();
        }
        catch ( std::exception& e)
        {
            std::cerr
                << "Accept error: "
                << e.what()
                << '\n';
        }
    }
}
