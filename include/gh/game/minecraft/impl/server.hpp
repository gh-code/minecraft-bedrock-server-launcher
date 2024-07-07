//
// Copyright (c) 2024 Gary Huang (ghuang dot nctu at gmail dot com)
//

#ifndef GH_GAME_MINECRAFT_IMPL_SERVER_HPP
#define GH_GAME_MINECRAFT_IMPL_SERVER_HPP

#include <cstdlib>

namespace gh {
namespace game {
namespace minecraft {

namespace bedrock {

inline
auto server::update() -> int
{
    prompt() << "update" << std::endl;
}

inline
auto server::backup() -> int
{
    if (mutex.try_lock()) {
        std::cout << "backup started\n";
        std::system("backup_world.bat > last_backup.log");
        // std::this_thread::sleep_for(std::chrono::seconds(10));
        std::cout << "backup done\n";
        mutex.unlock();
        return 0;
    }
    std::cout << "already under backup\n";
    return 255;
}

} // namespace bedrock

} // namespace minecraft
} // namespace game
} // namespace gh

#endif // GH_GAME_MINECRAFT_IMPL_SERVER_HPP