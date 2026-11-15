// SPDX-License-Identifier: GPL-2.0-or-later
// Steam integration: player name, lobbies (invite codes, friends, browser) and
// transport of the game's local-wireless (UDS) packets between lobby members.
//
// Routing mirrors Azahar's room server: broadcast frames go to every other
// member, unicast frames to the member whose (Steam-ID-derived) MAC matches.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace TFH {

struct LobbyMember {
    std::uint64_t id = 0;
    std::string name;
    int ping_ms = -1; // -1 = unknown / self
    bool is_owner = false;
    bool is_self = false;
};

struct LobbyListEntry {
    std::uint64_t id = 0;
    std::string host;
    std::string code;
    int members = 0;
    int max_members = 0;
};

class SteamNet {
public:
    SteamNet();
    ~SteamNet();
    SteamNet(const SteamNet&) = delete;
    SteamNet& operator=(const SteamNet&) = delete;

    // Returns false if Steam is not running / not available; error text in `error`.
    bool Init(std::string& error);
    void Shutdown();
    bool Available() const;

    // Must be called regularly from the main thread (dispatches Steam callbacks).
    void RunCallbacks();

    std::string PersonaName() const;

    // --- Lobby -------------------------------------------------------------------
    void CreateLobby(bool public_lobby);
    void JoinByCode(const std::string& code);
    void JoinLobby(std::uint64_t lobby_id);
    void LeaveLobby();
    void InviteFriends();
    void RefreshLobbyList();

    bool InLobby() const;
    bool IsHost() const;
    std::string LobbyCode() const;
    std::vector<LobbyMember> Members() const;
    std::vector<LobbyListEntry> LobbyList() const;
    std::string Status() const; // last human-readable status / error
    bool Busy() const;          // a create/join/search request is in flight

    // --- UDS bridge ----------------------------------------------------------------
    // Call after Network::Init(). While in a lobby, the emulated local wireless of
    // this console is connected to the other lobby members.
    void AttachToEmulator(const std::string& nickname);
    void DetachFromEmulator();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace TFH
