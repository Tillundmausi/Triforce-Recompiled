// SPDX-License-Identifier: GPL-2.0-or-later
#include "steam_net.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <random>
#include <thread>
#include <unordered_map>

#include "common/logging/log.h"
#include "network/network.h"
#include "network/room_member.h"

#ifdef TFH_HAS_STEAM
#include <steam/steam_api.h>
#endif

namespace TFH {

namespace {
constexpr const char* kLobbyTag = "tfh_pc";        // marks our lobbies (App 480 is shared!)
constexpr const char* kProtocol = "1";             // bump when the packet format changes
constexpr int kMaxPlayers = 3;
constexpr int kChannel = 7;

// Locally administered unicast MAC derived from the Steam ID (same on every peer).
Network::MacAddress MacFor(std::uint64_t steam_id) {
    std::uint64_t h = steam_id * 0x9E3779B97F4A7C15ull;
    h ^= h >> 29;
    return {0x02, static_cast<u8>(h >> 32), static_cast<u8>(h >> 24), static_cast<u8>(h >> 16),
            static_cast<u8>(h >> 8), static_cast<u8>(h)};
}

std::string NewCode() {
    static constexpr char kAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; // no 0/O/1/I
    std::random_device rd;
    std::string code;
    for (int i = 0; i < 6; ++i) code += kAlphabet[rd() % (sizeof(kAlphabet) - 1)];
    return code;
}

std::string NormalizeCode(std::string code) {
    std::string out;
    for (char c : code) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) out += c;
    }
    return out;
}

// Wire format: 'T' ver type channel tx[6] dst[6] len(u32 LE) data
std::vector<u8> Serialize(const Network::WifiPacket& p) {
    std::vector<u8> out;
    out.reserve(20 + p.data.size());
    out.push_back('T');
    out.push_back(1);
    out.push_back(static_cast<u8>(p.type));
    out.push_back(p.channel);
    out.insert(out.end(), p.transmitter_address.begin(), p.transmitter_address.end());
    out.insert(out.end(), p.destination_address.begin(), p.destination_address.end());
    const auto n = static_cast<std::uint32_t>(p.data.size());
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<u8>(n >> (8 * i)));
    out.insert(out.end(), p.data.begin(), p.data.end());
    return out;
}

bool Deserialize(const u8* d, std::size_t n, Network::WifiPacket& p) {
    if (n < 20 || d[0] != 'T' || d[1] != 1) return false;
    p.type = static_cast<Network::WifiPacket::PacketType>(d[2]);
    p.channel = d[3];
    std::memcpy(p.transmitter_address.data(), d + 4, 6);
    std::memcpy(p.destination_address.data(), d + 10, 6);
    const std::uint32_t len = d[16] | (d[17] << 8) | (d[18] << 16) | (static_cast<std::uint32_t>(d[19]) << 24);
    if (len != n - 20) return false;
    p.data.assign(d + 20, d + n);
    return true;
}
} // namespace

#ifdef TFH_HAS_STEAM

struct SteamNet::Impl {
    bool available = false;
    CSteamID self;
    mutable std::mutex mutex; // guards everything below + member_macs

    CSteamID lobby;           // invalid when not in a lobby
    std::string status;
    bool busy = false;
    std::string pending_code; // code we are searching for (join by code)
    std::vector<LobbyListEntry> lobby_list;
    std::unordered_map<std::uint64_t, Network::MacAddress> member_macs; // other members

    // Emulator bridge
    std::atomic_bool attached{false};
    std::string nickname;
    std::thread recv_thread;
    std::atomic_bool recv_stop{false};

    CCallResult<Impl, LobbyCreated_t> on_created;
    CCallResult<Impl, LobbyEnter_t> on_entered;
    CCallResult<Impl, LobbyMatchList_t> on_list;
    STEAM_CALLBACK(Impl, OnLobbyChatUpdate, LobbyChatUpdate_t);
    STEAM_CALLBACK(Impl, OnJoinRequested, GameLobbyJoinRequested_t);
    STEAM_CALLBACK(Impl, OnSessionRequest, SteamNetworkingMessagesSessionRequest_t);
    STEAM_CALLBACK(Impl, OnSessionFailed, SteamNetworkingMessagesSessionFailed_t);

    void SetStatus(std::string s) {
        LOG_INFO(Frontend, "[Steam] {}", s);
        status = std::move(s);
    }

    void RebuildMembers() { // mutex held
        member_macs.clear();
        if (!lobby.IsValid()) return;
        const int n = SteamMatchmaking()->GetNumLobbyMembers(lobby);
        for (int i = 0; i < n; ++i) {
            const CSteamID m = SteamMatchmaking()->GetLobbyMemberByIndex(lobby, i);
            if (m != self) member_macs[m.ConvertToUint64()] = MacFor(m.ConvertToUint64());
        }
    }

    void OnCreated(LobbyCreated_t* r, bool io_failure) {
        std::lock_guard lock(mutex);
        busy = false;
        if (io_failure || r->m_eResult != k_EResultOK) {
            SetStatus("Could not create lobby (Steam error " + std::to_string(r->m_eResult) + ").");
            return;
        }
        lobby = CSteamID(r->m_ulSteamIDLobby);
        const std::string code = NewCode();
        SteamMatchmaking()->SetLobbyData(lobby, kLobbyTag, kProtocol);
        SteamMatchmaking()->SetLobbyData(lobby, "code", code.c_str());
        SteamMatchmaking()->SetLobbyData(lobby, "host", SteamFriends()->GetPersonaName());
        SteamMatchmaking()->SetLobbyJoinable(lobby, true);
        RebuildMembers();
        SetStatus("Lobby created. Code: " + code);
        ConnectBridgeLocked();
    }

    void OnEntered(LobbyEnter_t* r, bool io_failure) {
        std::lock_guard lock(mutex);
        busy = false;
        if (io_failure || r->m_EChatRoomEnterResponse != k_EChatRoomEnterResponseSuccess) {
            SetStatus("Could not join (lobby full or closed).");
            return;
        }
        const CSteamID id(r->m_ulSteamIDLobby);
        if (std::string(SteamMatchmaking()->GetLobbyData(id, kLobbyTag)) != kProtocol) {
            SteamMatchmaking()->LeaveLobby(id);
            SetStatus("This lobby is not a Triforce Recompiled lobby (or a different version).");
            return;
        }
        lobby = id;
        RebuildMembers();
        SetStatus("Joined lobby.");
        ConnectBridgeLocked();
    }

    void OnList(LobbyMatchList_t* r, bool io_failure) {
        std::lock_guard lock(mutex);
        busy = false;
        lobby_list.clear();
        if (io_failure) {
            SetStatus("Lobby search failed.");
            return;
        }
        for (uint32 i = 0; i < r->m_nLobbiesMatching; ++i) {
            const CSteamID id = SteamMatchmaking()->GetLobbyByIndex(static_cast<int>(i));
            LobbyListEntry e;
            e.id = id.ConvertToUint64();
            e.host = SteamMatchmaking()->GetLobbyData(id, "host");
            e.code = SteamMatchmaking()->GetLobbyData(id, "code");
            e.members = SteamMatchmaking()->GetNumLobbyMembers(id);
            e.max_members = SteamMatchmaking()->GetLobbyMemberLimit(id);
            lobby_list.push_back(std::move(e));
        }
        if (!pending_code.empty()) {
            const std::string code = pending_code;
            pending_code.clear();
            if (lobby_list.empty()) {
                SetStatus("No lobby found with code " + code + ".");
                return;
            }
            busy = true;
            SetStatus("Joining lobby " + code + " ...");
            on_entered.Set(SteamMatchmaking()->JoinLobby(CSteamID(static_cast<uint64>(lobby_list.front().id))), this, &Impl::OnEntered);
            return;
        }
        SetStatus("Found " + std::to_string(lobby_list.size()) + " open lobby(s).");
    }

    void ConnectBridgeLocked() {
        if (!attached || !lobby.IsValid()) return;
        if (auto member = Network::GetRoomMember().lock()) {
            member->ConnectExternal(nickname, MacFor(self.ConvertToUint64()),
                                    [this](const Network::WifiPacket& p) { Send(p); });
            LOG_INFO(Frontend, "[Steam] Local wireless bridged to lobby");
        }
    }

    void DisconnectBridge() {
        if (auto member = Network::GetRoomMember().lock()) member->DisconnectExternal();
    }

    void Send(const Network::WifiPacket& p) {
        std::vector<std::uint64_t> targets;
        {
            std::lock_guard lock(mutex);
            if (!lobby.IsValid()) return;
            if (p.destination_address == Network::BroadcastMac) {
                for (const auto& [id, mac] : member_macs) targets.push_back(id);
            } else {
                for (const auto& [id, mac] : member_macs)
                    if (mac == p.destination_address) targets.push_back(id);
            }
        }
        if (targets.empty()) return;
        const auto bytes = Serialize(p);
        for (const auto id : targets) {
            SteamNetworkingIdentity ident;
            ident.SetSteamID64(id);
            SteamNetworkingMessages()->SendMessageToUser(
                ident, bytes.data(), static_cast<uint32>(bytes.size()),
                k_nSteamNetworkingSend_Reliable | k_nSteamNetworkingSend_AutoRestartBrokenSession, kChannel);
        }
    }

    void RecvLoop() {
        SteamNetworkingMessage_t* msgs[32];
        while (!recv_stop) {
            const int n = SteamNetworkingMessages()->ReceiveMessagesOnChannel(kChannel, msgs, 32);
            for (int i = 0; i < n; ++i) {
                Network::WifiPacket p;
                bool known = false;
                {
                    std::lock_guard lock(mutex);
                    known = member_macs.count(msgs[i]->m_identityPeer.GetSteamID64()) != 0;
                }
                if (known && Deserialize(static_cast<const u8*>(msgs[i]->m_pData),
                                         static_cast<std::size_t>(msgs[i]->m_cbSize), p) && attached) {
                    if (auto member = Network::GetRoomMember().lock()) member->InjectWifiPacket(p);
                }
                msgs[i]->Release();
            }
            if (n == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
};

void SteamNet::Impl::OnLobbyChatUpdate(LobbyChatUpdate_t* r) {
    std::lock_guard lock(mutex);
    if (!lobby.IsValid() || r->m_ulSteamIDLobby != lobby.ConvertToUint64()) return;
    RebuildMembers();
    const char* who = SteamFriends()->GetFriendPersonaName(CSteamID(r->m_ulSteamIDUserChanged));
    if (r->m_rgfChatMemberStateChange & k_EChatMemberStateChangeEntered)
        SetStatus(std::string(who) + " joined.");
    else
        SetStatus(std::string(who) + " left the lobby.");
}

void SteamNet::Impl::OnJoinRequested(GameLobbyJoinRequested_t* r) {
    // Accepted an invite via the Steam overlay / friends list while the game runs.
    std::lock_guard lock(mutex);
    if (lobby.IsValid()) {
        DisconnectBridge();
        SteamMatchmaking()->LeaveLobby(lobby);
        lobby = CSteamID();
    }
    busy = true;
    SetStatus("Invite accepted, joining ...");
    on_entered.Set(SteamMatchmaking()->JoinLobby(r->m_steamIDLobby), this, &Impl::OnEntered);
}

void SteamNet::Impl::OnSessionRequest(SteamNetworkingMessagesSessionRequest_t* r) {
    std::lock_guard lock(mutex);
    if (member_macs.count(r->m_identityRemote.GetSteamID64()))
        SteamNetworkingMessages()->AcceptSessionWithUser(r->m_identityRemote);
}

void SteamNet::Impl::OnSessionFailed(SteamNetworkingMessagesSessionFailed_t* r) {
    LOG_WARNING(Frontend, "[Steam] P2P session failed: {}", r->m_info.m_szEndDebug);
}

SteamNet::SteamNet() = default;
SteamNet::~SteamNet() { Shutdown(); }

bool SteamNet::Init(std::string& error) {
    SteamErrMsg msg{};
    if (SteamAPI_InitEx(&msg) != k_ESteamAPIInitResult_OK) {
        error = std::string("Steam not available: ") + msg;
        LOG_WARNING(Frontend, "[Steam] {}", error);
        return false;
    }
    impl = std::make_unique<Impl>();
    impl->available = true;
    impl->self = SteamUser()->GetSteamID();
    SteamNetworkingUtils()->InitRelayNetworkAccess();
    impl->recv_thread = std::thread([this] { impl->RecvLoop(); });
    LOG_INFO(Frontend, "[Steam] Logged in as {}", SteamFriends()->GetPersonaName());
    return true;
}

void SteamNet::Shutdown() {
    if (!impl) return;
    DetachFromEmulator();
    LeaveLobby();
    impl->recv_stop = true;
    if (impl->recv_thread.joinable()) impl->recv_thread.join();
    impl.reset();
    SteamAPI_Shutdown();
}

bool SteamNet::Available() const { return impl && impl->available; }
void SteamNet::RunCallbacks() { if (impl) SteamAPI_RunCallbacks(); }
std::string SteamNet::PersonaName() const { return impl ? SteamFriends()->GetPersonaName() : std::string{}; }

void SteamNet::CreateLobby(bool public_lobby) {
    if (!impl) return;
    LeaveLobby();
    std::lock_guard lock(impl->mutex);
    impl->busy = true;
    impl->SetStatus("Creating lobby ...");
    impl->on_created.Set(SteamMatchmaking()->CreateLobby(public_lobby ? k_ELobbyTypePublic : k_ELobbyTypeFriendsOnly,
                                                         kMaxPlayers),
                         impl.get(), &Impl::OnCreated);
}

void SteamNet::JoinByCode(const std::string& raw) {
    if (!impl) return;
    const std::string code = NormalizeCode(raw);
    if (code.size() != 6) {
        std::lock_guard lock(impl->mutex);
        impl->SetStatus("A lobby code has 6 characters.");
        return;
    }
    LeaveLobby();
    std::lock_guard lock(impl->mutex);
    impl->busy = true;
    impl->pending_code = code;
    impl->SetStatus("Searching lobby " + code + " ...");
    SteamMatchmaking()->AddRequestLobbyListStringFilter(kLobbyTag, kProtocol, k_ELobbyComparisonEqual);
    SteamMatchmaking()->AddRequestLobbyListStringFilter("code", code.c_str(), k_ELobbyComparisonEqual);
    SteamMatchmaking()->AddRequestLobbyListDistanceFilter(k_ELobbyDistanceFilterWorldwide);
    impl->on_list.Set(SteamMatchmaking()->RequestLobbyList(), impl.get(), &Impl::OnList);
}

void SteamNet::JoinLobby(std::uint64_t id) {
    if (!impl) return;
    LeaveLobby();
    std::lock_guard lock(impl->mutex);
    impl->busy = true;
    impl->SetStatus("Joining lobby ...");
    impl->on_entered.Set(SteamMatchmaking()->JoinLobby(CSteamID(static_cast<uint64>(id))), impl.get(), &Impl::OnEntered);
}

void SteamNet::LeaveLobby() {
    if (!impl) return;
    std::lock_guard lock(impl->mutex);
    if (!impl->lobby.IsValid()) return;
    impl->DisconnectBridge();
    SteamMatchmaking()->LeaveLobby(impl->lobby);
    impl->lobby = CSteamID();
    impl->member_macs.clear();
    impl->SetStatus("Left the lobby.");
}

void SteamNet::InviteFriends() {
    if (!impl) return;
    std::lock_guard lock(impl->mutex);
    if (impl->lobby.IsValid()) SteamFriends()->ActivateGameOverlayInviteDialog(impl->lobby);
}

void SteamNet::RefreshLobbyList() {
    if (!impl) return;
    std::lock_guard lock(impl->mutex);
    impl->busy = true;
    impl->pending_code.clear();
    impl->SetStatus("Searching open lobbies ...");
    SteamMatchmaking()->AddRequestLobbyListStringFilter(kLobbyTag, kProtocol, k_ELobbyComparisonEqual);
    SteamMatchmaking()->AddRequestLobbyListFilterSlotsAvailable(1);
    SteamMatchmaking()->AddRequestLobbyListDistanceFilter(k_ELobbyDistanceFilterWorldwide);
    impl->on_list.Set(SteamMatchmaking()->RequestLobbyList(), impl.get(), &Impl::OnList);
}

bool SteamNet::InLobby() const {
    if (!impl) return false;
    std::lock_guard lock(impl->mutex);
    return impl->lobby.IsValid();
}

bool SteamNet::IsHost() const {
    if (!impl) return false;
    std::lock_guard lock(impl->mutex);
    return impl->lobby.IsValid() && SteamMatchmaking()->GetLobbyOwner(impl->lobby) == impl->self;
}

std::string SteamNet::LobbyCode() const {
    if (!impl) return {};
    std::lock_guard lock(impl->mutex);
    return impl->lobby.IsValid() ? SteamMatchmaking()->GetLobbyData(impl->lobby, "code") : "";
}

std::vector<LobbyMember> SteamNet::Members() const {
    std::vector<LobbyMember> out;
    if (!impl) return out;
    std::lock_guard lock(impl->mutex);
    if (!impl->lobby.IsValid()) return out;
    const CSteamID owner = SteamMatchmaking()->GetLobbyOwner(impl->lobby);
    const int n = SteamMatchmaking()->GetNumLobbyMembers(impl->lobby);
    for (int i = 0; i < n; ++i) {
        const CSteamID id = SteamMatchmaking()->GetLobbyMemberByIndex(impl->lobby, i);
        LobbyMember m;
        m.id = id.ConvertToUint64();
        m.name = SteamFriends()->GetFriendPersonaName(id);
        m.is_owner = id == owner;
        m.is_self = id == impl->self;
        if (!m.is_self) {
            SteamNetworkingIdentity ident;
            ident.SetSteamID(id);
            SteamNetConnectionRealTimeStatus_t st{};
            if (SteamNetworkingMessages()->GetSessionConnectionInfo(ident, nullptr, &st) ==
                k_ESteamNetworkingConnectionState_Connected)
                m.ping_ms = st.m_nPing;
        }
        out.push_back(std::move(m));
    }
    return out;
}

std::vector<LobbyListEntry> SteamNet::LobbyList() const {
    if (!impl) return {};
    std::lock_guard lock(impl->mutex);
    return impl->lobby_list;
}

std::string SteamNet::Status() const {
    if (!impl) return {};
    std::lock_guard lock(impl->mutex);
    return impl->status;
}

bool SteamNet::Busy() const {
    if (!impl) return false;
    std::lock_guard lock(impl->mutex);
    return impl->busy;
}

void SteamNet::AttachToEmulator(const std::string& nickname) {
    if (!impl) return;
    std::lock_guard lock(impl->mutex);
    impl->nickname = nickname;
    impl->attached = true;
    impl->ConnectBridgeLocked();
}

void SteamNet::DetachFromEmulator() {
    if (!impl) return;
    std::lock_guard lock(impl->mutex);
    impl->attached = false;
    impl->DisconnectBridge();
}

#else // !TFH_HAS_STEAM ------------------------------------------------------------

struct SteamNet::Impl {};
SteamNet::SteamNet() = default;
SteamNet::~SteamNet() = default;
bool SteamNet::Init(std::string& error) {
    error = "Built without the Steamworks SDK - online play is not available.";
    return false;
}
void SteamNet::Shutdown() {}
bool SteamNet::Available() const { return false; }
void SteamNet::RunCallbacks() {}
std::string SteamNet::PersonaName() const { return {}; }
void SteamNet::CreateLobby(bool) {}
void SteamNet::JoinByCode(const std::string&) {}
void SteamNet::JoinLobby(std::uint64_t) {}
void SteamNet::LeaveLobby() {}
void SteamNet::InviteFriends() {}
void SteamNet::RefreshLobbyList() {}
bool SteamNet::InLobby() const { return false; }
bool SteamNet::IsHost() const { return false; }
std::string SteamNet::LobbyCode() const { return {}; }
std::vector<LobbyMember> SteamNet::Members() const { return {}; }
std::vector<LobbyListEntry> SteamNet::LobbyList() const { return {}; }
std::string SteamNet::Status() const { return {}; }
bool SteamNet::Busy() const { return false; }
void SteamNet::AttachToEmulator(const std::string&) {}
void SteamNet::DetachFromEmulator() {}

#endif

} // namespace TFH
