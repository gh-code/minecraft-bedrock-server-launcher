//
// Copyright (c) 2026 Gary Huang (ghuang dot nctu at gmail dot com)
//

#ifndef GH_UTILITY_LOGGER_HPP
#define GH_UTILITY_LOGGER_HPP

#include <iostream>
#include <sstream>
#include <mutex>

namespace gh::utility {

class logger
{
public:
    explicit logger(std::ostream &os)
    : os_(&os)
    { }

    logger()
    : os_(nullptr)
    { }

    logger(const logger &other) = default;
    logger &operator=(const logger &other) = default;

    logger(logger &&other) noexcept
    : os_(other.os_)
    {
        other.os_ = nullptr;
    }

    virtual ~logger() = default;

    logger &operator=(logger &&other) noexcept
    {
        if (this != &other) {
            os_ = other.os_;
            other.os_ = nullptr;
        }
        return *this;
    }

    class message
    {
    public:
        explicit message(logger &log)
        : logger_(log)
        { }

        message(const message &) = delete;
        message &operator=(const message &) = delete;
        message(message &&) = default;
        message &operator=(message &&) = default;

        template <typename T>
        message &operator<<(const T &msg)
        {
            buffer_ << msg;
            return *this;
        }

        message &operator<<(std::ostream &(*manip)(std::ostream &))
        {
            buffer_ << manip;
            flush();
            return *this;
        }

        ~message()
        {
            flush();
        }

    private:
        void flush()
        {
            if (flushed_) {
                return;
            }

            const std::string output = buffer_.str();
            if (output.empty()) {
                flushed_ = true;
                return;
            }

            logger_.flush(output);
            flushed_ = true;
        }

        logger &logger_;
        std::ostringstream buffer_;
        bool flushed_ = false;
    };

    auto stream() -> message
    {
        return message(*this);
    }

    virtual auto flush(const std::string &message) -> void
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (os_) {
            *os_ << message;
            os_->flush();
        }
    }

private:
    std::ostream *os_;
    std::mutex mutex_;
};

class loggable
{
public:
    static logger empty_logger;

public:
    loggable()
        : logger_(&empty_logger)
    { }

    virtual ~loggable()
    { }

    auto log() -> logger::message
    {
        return logger_->stream();
    }

    auto set_logger(logger &log) -> void
    {
        logger_ = &log;
    }

private:
    logger *logger_;
};

} // namespace gh::utility

#endif // GH_UTILITY_LOGGER_HPP