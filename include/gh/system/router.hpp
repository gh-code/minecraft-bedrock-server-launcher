//
// Copyright (c) 2024 Gary Huang (ghuang dot nctu at gmail dot com)
//

#ifndef GH_GAME_MINECRAFT_ROUTER_HPP
#define GH_GAME_MINECRAFT_ROUTER_HPP

#include <unordered_map>
#include <vector>
#include <string>
#include <functional>

namespace gh {
namespace system {

class router
{
public:
    using Matches = std::vector<std::string>;
    using Callback = std::function<void(Matches)>;
    using Table = std::unordered_map<std::string,Callback>;

public:
    /// Construct
    router() = default;

    /// Copy constructor
    router(const router&) = delete;

    /// Copy assignment
    router& operator=(const router&) = delete;

    /// Destructor
    ~router() = default;

    template<class Callable>
    auto get(const char* expr, Callable&& callback) -> void
    { table[expr] = std::move(callback); }

    auto get_table() const -> const Table&
    { return table; }

private:
    Table table;
};

} // namespace system
} // namespace gh

#endif // GH_GAME_MINECRAFT_ROUTER_HPP