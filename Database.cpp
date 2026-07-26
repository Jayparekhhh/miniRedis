#include "Database.h"

#include <chrono>
#include <fstream>
#include <iostream>

bool Database::set(std::string& key, std::string& value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    data_[key].value = value;
    return true;
}

std::string Database::get(std::string& key) 
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = data_.find(key);

    if (it == data_.end())
    {
        return "(nil)";
    }

    if (isExpired(it->second))
    {
        data_.erase(it);
        return "(nil)";
    }

    return it->second.value;
}

bool Database::del(std::string& key)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return data_.erase(key) > 0;
}

bool Database::exists(std::string& key)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto itr = data_.find(key);

    if (itr == data_.end())
        return false;

    if (itr->second.hasExpiry &&
        std::chrono::steady_clock::now() >= itr->second.expiryTime)
    {
        data_.erase(itr);
        return false;
    }

    return true;
}

std::vector<std::string> Database::keys() 
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> result;

    result.reserve(data_.size());

    for (auto& [key, value] : data_)
    {
        result.push_back(key);
    }

    return result;
}

void Database::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    data_.clear();
}

bool Database::expire(std::string& key, int seconds)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = data_.find(key);

    if (it == data_.end())
    {
        return false;
    }

    it->second.hasExpiry = true;

    it->second.expiryTime =
        std::chrono::steady_clock::now() +
        std::chrono::seconds(seconds);

    return true;
}

bool Database::isExpired(Entry& entry) 
{
    // No lock needed here: this is an internal helper function
    // called by get/ttl/removeExpiredKeys which already hold the lock.
    if (!entry.hasExpiry)
    {
        return false;
    }

    return std::chrono::steady_clock::now() >= entry.expiryTime;
}

long long Database::ttl(std::string& key)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = data_.find(key);

    if (it == data_.end())
    {
        return -2;
    }

    if (isExpired(it->second))
    {
        data_.erase(it);
        return -2;
    }

    if (!it->second.hasExpiry)
    {
        return -1;
    }

    auto remaining =
        std::chrono::duration_cast<std::chrono::seconds>(
            it->second.expiryTime -
            std::chrono::steady_clock::now());

    return remaining.count();
}

void Database::removeExpiredKeys()
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = data_.begin();

    while (it != data_.end())
    {
        if (isExpired(it->second))
        {
            it = data_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

std::unordered_map<std::string, Entry>& Database::data() 
{
    std::lock_guard<std::mutex> lock(mutex_);
    return data_;
}

TTLManager::TTLManager(Database& database)
    : database_(database),
      running_(false)
{
}

TTLManager::~TTLManager()
{
    stop();
}

void TTLManager::start()
{
    if (running_)
    {
        return;
    }

    running_ = true;

    worker_ = std::thread(
        &TTLManager::cleanupLoop,
        this
    );
}

void TTLManager::stop()
{
    if (!running_)
    {
        return;
    }

    running_ = false;

    if (worker_.joinable())
    {
        worker_.join();
    }
}

void TTLManager::cleanupLoop()
{
    while (running_)
    {
        database_.removeExpiredKeys();

        std::this_thread::sleep_for(
            std::chrono::seconds(1));
    }
}

bool Serializer::save(Database& database, std::string& filename)
{
    std::ofstream out(
        filename,
        std::ios::binary
    );

    if (!out)
    {
        return false;
    }

    std::size_t count = database.data().size();

    out.write(
        reinterpret_cast<char*>(&count),
        sizeof(count)
    );

    for (auto& [key, entry] : database.data())
    {
        std::size_t keyLength = key.size();

        out.write(
            reinterpret_cast<char*>(&keyLength),
            sizeof(keyLength)
        );

        out.write(
            key.data(),
            keyLength
        );

        std::size_t valueLength = entry.value.size();

        out.write(
            reinterpret_cast<char*>(&valueLength),
            sizeof(valueLength)
        );

        out.write(
            entry.value.data(),
            valueLength
        );
    }
    return true;
}

bool Serializer::load(Database& database, std::string& filename)
{
    std::ifstream in(
        filename,
        std::ios::binary
    );

    if (!in)
    {
        return false;
    }

    std::size_t count;

    in.read(
        reinterpret_cast<char*>(&count),
        sizeof(count)
    );

    for (std::size_t i = 0; i < count; i++)
    {
        std::size_t keyLength;

        in.read(
            reinterpret_cast<char*>(&keyLength),
            sizeof(keyLength)
        );

        std::string key(keyLength, '\0');

        in.read(
            key.data(),
            keyLength
        );

        std::size_t valueLength;

        in.read(
            reinterpret_cast<char*>(&valueLength),
            sizeof(valueLength)
        );

        std::string value(valueLength, '\0');

        in.read(
            value.data(),
            valueLength
        );

        database.set(
            key,
            value
        );
    }

    return true;
}
