//
// Copyright (c) 2024 Gary Huang (ghuang dot nctu at gmail dot com)
//

#ifndef GH_GAME_MINECRAFT_SERVER_HPP
#define GH_GAME_MINECRAFT_SERVER_HPP

#include <gh/system/shell.hpp>

#include <mutex>

namespace gh {
namespace game {
namespace minecraft {

namespace java {
} // namespace java

namespace bedrock {

class server : public system::shell
{
public:
    server(const char* command, boost::asio::io_service &ios)
    : shell(command, ios)
    { }

    auto update() -> int;
    auto backup() -> int;

private:
    std::mutex mutex;
};

} // namespace bedrock

} // namespace minecraft
} // namespace game
} // namespace gh

#include <gh/game/minecraft/impl/server.hpp>

#endif // GH_GAME_MINECRAFT_SERVER_HPP