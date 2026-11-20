#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>
struct Module;
bool run_tdb_autorun(const Module &main,
                     const std::filesystem::path &directory);
size_t prepare_player_probe(const Module &main,
                            const std::filesystem::path &directory);
void poll_player_request();
void on_player_update(uintptr_t context, uintptr_t manager);
std::vector<size_t> lua_lifecycle_sites(const Module &main);
void on_lua_lifecycle(size_t offset, uint64_t *args, const void *caller_stack);
