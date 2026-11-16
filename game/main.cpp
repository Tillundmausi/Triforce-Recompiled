// SPDX-License-Identifier: GPL-2.0-or-later
// Triforce Recompiled - standalone PC version of Tri Force Heroes built on Azahar's 3DS core.
// The launcher window stays open for the whole session (setup assistant, online lobby,
// settings); the game runs in its own window next to it.
#include <algorithm>
#include <atomic>
#include <ctime>
#include <cstdio>
#include <functional>
#include <chrono>
#include <cstdlib>
#include <future>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "any_input.h"
#include "config.h"
#include "steam_net.h"
#include "ui.h"
#include "game_file.h"
#include "crash_handler.h"
#include "discord_rpc.h"
#include "romfs_watch.h"
#include "common/detached_tasks.h"
#include "common/file_util.h"
#include "common/logging/backend.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "common/string_util.h"
#include "core/hle/service/cfg/cfg.h"
#include "network/network.h"
#include "core/core.h"
#include "core/frontend/applets/default_applets.h"
#include "core/frontend/image_interface.h"
#include "core/hle/service/service.h"
#include "core/hle/service/am/am.h"
#include "core/hle/service/fs/archive.h"
#include "core/loader/loader.h"
#include "input_common/main.h"
#include "sdl_window.h"
#include "video_core/gpu.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/renderer_base.h"

namespace fs = std::filesystem;

namespace {

fs::path FromUtf8(const std::string& s) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

std::string ToUtf8(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

// Folder containing the executable. (SDL_GetBasePath is unavailable: Azahar
// builds SDL without its filesystem module, so it always returned nothing.)
fs::path ExeDirectory() {
#ifdef _WIN32
    return FromUtf8(FileUtil::GetExeDirectory()); // Azahar helper, UTF-8
#else
    std::error_code ec;
    const auto exe = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::current_path() : exe.parent_path();
#endif
}

constexpr const char* kVersion = "0.1.1";
constexpr u64 kTargetTitleId = 0x0004000000177000ull; // Tri Force Heroes EUR (project target)

void ShowError(const std::string& text) {
    LOG_CRITICAL(Frontend, "{}", text);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Triforce Recompiled", text.c_str(), nullptr);
}

bool IsDumpFile(const fs::path& p) {
    auto ext = p.extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".cia" || ext == ".3ds" || ext == ".cci" || ext == ".cxi";
}

bool IsCia(const std::string& path) {
    auto ext = FromUtf8(path).extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".cia";
}

// Search order: command line -> path chosen in the setup assistant -> "game" folder next
// to the exe -> exe folder -> up to 4 parent folders (a dev build finds the dump in the repo root).
std::string FindDump(int argc, char** argv, const TFH::AppConfig& cfg) {
    if (argc > 1) return argv[1];
    std::error_code ec;
    if (!cfg.game_path.empty() && fs::is_regular_file(FromUtf8(cfg.game_path), ec)) return cfg.game_path;
    const fs::path exeDir = ExeDirectory();
    std::vector<fs::path> dirs{exeDir / "game", exeDir};
    fs::path p = exeDir;
    for (int i = 0; i < 4 && p.has_parent_path() && p.parent_path() != p; ++i) {
        p = p.parent_path();
        dirs.push_back(p);
    }
    for (const auto& d : dirs) {
        if (!fs::is_directory(d, ec)) continue;
        for (const auto& entry : fs::directory_iterator(d, ec))
            if (entry.is_regular_file(ec) && IsDumpFile(entry.path())) return ToUtf8(entry.path());
    }
    return {};
}

// Path of the installed title content for a CIA (empty if it cannot be read).
std::string InstalledAppPath(const std::string& cia) {
    auto infos = Service::AM::GetCIAInfos(cia);
    if (infos.Failed()) return {};
    return Service::AM::GetTitleContentPath(Service::FS::MediaType::SDMC, infos.Unwrap().first.tid);
}

bool IsInstalled(const std::string& dump) {
    if (dump.empty()) return false;
    if (!IsCia(dump)) return true; // .3ds/.cxi are booted directly
    const std::string app = InstalledAppPath(dump);
    return !app.empty() && FileUtil::Exists(app);
}

void InitAzaharGlobals() {
    // Azahar looks up every system module in this map with .at() while booting;
    // normally its Qt/libretro frontends fill it. All HLE (false), like libretro.
    for (const auto& service_module : Service::service_module_map)
        Settings::values.lle_modules.emplace(service_module.name, false);
}

// The 3DS user name is at most 10 UTF-16 characters.
std::u16string ConsoleName(const std::string& utf8) {
    std::u16string n = Common::UTF8ToUTF16(utf8);
    if (n.size() > 10) n.resize(10);
    if (n.empty()) n = u"Link";
    return n;
}

std::string LoadErrorText(Core::System::ResultStatus r, const std::string& details) {
    using RS = Core::System::ResultStatus;
    switch (r) {
    case RS::ErrorGetLoader:
    case RS::ErrorLoader_ErrorInvalidFormat:
        return "The game file is not recognised. Supported: .cia, .3ds, .cci, .cxi";
    case RS::ErrorLoader_ErrorEncrypted:
        return "The game file is encrypted.\n\nDecrypt it on your own 3DS with GodMode9 (see the Setup tab).";
    case RS::ErrorSystemFiles:
        return "3DS system files are missing: " + details;
    default:
        return "The game could not be started (code " + std::to_string(static_cast<int>(r)) + ").\n" + details;
    }
}

void PrepareSteamAppId(const fs::path& exeDir) {
    // Uses Valve's public test app (Spacewar, 480) for lobbies and networking.
#ifdef _WIN32
    _putenv_s("SteamAppId", "480");
    _putenv_s("SteamGameId", "480");
#else
    setenv("SteamAppId", "480", 1);
    setenv("SteamGameId", "480", 1);
#endif
    std::error_code ec;
    if (!fs::exists(exeDir / "steam_appid.txt", ec)) std::ofstream(exeDir / "steam_appid.txt") << "480";
}

// ------------------------------------------------------------------------- Discord -----------
TFH::DiscordActivity BuildPresence(const TFH::AppConfig& cfg, const TFH::SteamNet& steam, bool running,
                                   std::int64_t start, const TFH::RomfsWatch::Snapshot& snap) {
    TFH::DiscordActivity a;
    a.large_image = "logo"; // art asset uploaded to the Discord application
    a.large_text = std::string("Triforce Recompiled ") + kVersion;
    a.button_label = "Get Triforce Recompiled";
    a.button_url = "https://github.com/Tillundmausi/Triforce-Recompiled";
    if (!running) {
        a.details = "In the launcher";
    } else {
        a.details = (cfg.discord_show_level && !snap.level.empty()) ? snap.level : "Playing";
        a.start = start;
    }
    if (cfg.discord_show_lobby && steam.InLobby()) {
        a.state = steam.IsHost() ? "Hosting an online lobby" : "In an online lobby";
        // Opaque ID (Discord only uses it to group party members); the lobby code is never sent.
        char id[32];
        std::snprintf(id, sizeof(id), "tfh-%016llx",
                      static_cast<unsigned long long>(std::hash<std::string>{}(steam.LobbyCode())));
        a.party_id = id;
        a.party_size = static_cast<int>(steam.Members().size());
        a.party_max = 3;
    } else if (running) {
        a.state = "Offline";
    }
    return a;
}

// ------------------------------------------------------------------------- CIA install job --
struct InstallJob {
    std::thread thread;
    std::atomic_int percent{0};
    std::atomic_bool done{false};
    Service::AM::InstallStatus status = Service::AM::InstallStatus::Success;

    bool Running() const { return thread.joinable() && !done; }
    void Start(const std::string& cia) {
        percent = 0;
        done = false;
        thread = std::thread([this, cia] {
            LOG_INFO(Frontend, "Installing {}", cia);
            status = Service::AM::InstallCIA(cia, [this](std::size_t d, std::size_t t) {
                percent = t ? static_cast<int>(d * 100 / t) : 0;
            });
            done = true;
        });
    }
    void Join() {
        if (thread.joinable()) thread.join();
    }
};

// ------------------------------------------------------------------------- game session ----
class GameSession {
public:
    bool Running() const { return window != nullptr; }
    TFH::SdlEmuWindow* Window() { return window.get(); }

    bool Start(const std::string& bootPath, const std::u16string& name, const TFH::AppConfig& cfg,
               const fs::path& logDir) {
        try {
            window = std::make_unique<TFH::SdlEmuWindow>(cfg.fullscreen);
        } catch (const std::exception& e) {
            ShowError(std::string("Could not create the game window: ") + e.what());
            window.reset();
            return false;
        }
        Core::System& system = Core::System::GetInstance();
        Frontend::RegisterDefaultApplets(system);
        system.RegisterImageInterface(std::make_shared<Frontend::ImageInterface>());

        TFH::RomfsWatch::Install(logDir);
        const auto result = system.Load(*window, bootPath);
        if (result != Core::System::ResultStatus::Success) {
            ShowError(LoadErrorText(result, system.GetStatusDetails()));
            system.Shutdown();
            TFH::RomfsWatch::Uninstall();
            window.reset();
            return false;
        }
        u64 programId = 0;
        system.GetAppLoader().ReadProgramId(programId);
        if (programId != kTargetTitleId)
            LOG_WARNING(Frontend, "Program ID {:016X} is not the tested target {:016X}", programId, kTargetTitleId);

        // Player name shown in game (3DS console user name).
        if (auto cfg_module = Service::CFG::GetModule(system)) {
            cfg_module->SetUsername(name);
            cfg_module->UpdateConfigNANDSavegame();
            LOG_INFO(Frontend, "Console user name set to '{}'", Common::UTF16ToUTF8(name));
        }
        system.ApplySettings();

        startTime = static_cast<std::int64_t>(std::time(nullptr));
        stop = false;
        failed = false;
        finished = false;
        failText.clear();
        thread = std::thread([this, programId] {
            Core::System& sys = Core::System::GetInstance();
            try {
                LOG_INFO(Frontend, "Emulation thread: loading disk resources");
                sys.GPU().ApplyPerProgramSettings(programId);
                sys.GPU().Renderer().Rasterizer()->LoadDefaultDiskResources(stop, nullptr);
                sys.RegisterCoreLoopThreadId();
                LOG_INFO(Frontend, "Emulation thread: entering run loop");
                while (!stop) {
                    const auto r = sys.RunLoop();
                    if (r == Core::System::ResultStatus::ShutdownRequested) break;
                    if (r != Core::System::ResultStatus::Success) {
                        failText = "Emulation error (code " + std::to_string(static_cast<int>(r)) +
                                   "): " + sys.GetStatusDetails();
                        failed = true;
                        break;
                    }
                }
            } catch (const std::exception& e) {
                failText = std::string("Internal error in the emulation thread: ") + e.what();
                failed = true;
            }
            sys.Shutdown();
            finished = true;
            window->RequestClose();
        });
        return true;
    }

    // True when the game ended on its own or the window was closed.
    bool ShouldStop() const { return Running() && (!window->IsOpen() || finished); }

    void Stop() {
        if (!Running()) return;
        stop = true;
        if (thread.joinable()) thread.join();
        TFH::RomfsWatch::Uninstall();
        window.reset();
        if (failed) ShowError(failText);
    }

    void ApplyLive(const TFH::AppConfig& cfg) {
        if (!Running()) return;
        Core::System::GetInstance().ApplySettings();
        if (window->IsFullscreen() != cfg.fullscreen) window->ToggleFullscreen();
        window->Relayout();
    }

    std::int64_t StartTime() const { return startTime; }

private:
    std::int64_t startTime = 0;
    std::unique_ptr<TFH::SdlEmuWindow> window;
    std::thread thread;
    std::atomic_bool stop{false};
    std::atomic_bool failed{false};
    std::atomic_bool finished{false};
    std::string failText;
};

} // namespace

int main(int argc, char** argv) {
    SDL_SetMainReady();
    Common::DetachedTasks detached_tasks;

    const fs::path exeDir = ExeDirectory();
    const fs::path userDir = exeDir / "user";
    const fs::path configFile = userDir / "config.ini";
    // Portable user folder next to the exe: saves, shader cache, logs, config.
    FileUtil::SetUserPath(ToUtf8(userDir) + "/");
    TFH::InstallCrashHandler(userDir);
    Common::Log::Initialize();
    Common::Log::Start();
    LOG_INFO(Frontend, "Triforce Recompiled {} starting", kVersion);

    TFH::AppConfig cfg = TFH::LoadConfig(configFile);
    auto save = [&] { TFH::SaveConfig(cfg, configFile); };

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        ShowError(std::string("SDL could not be started: ") + SDL_GetError());
        return 1;
    }
    // Keep controllers alive while the launcher window has the focus.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

    // Steam is optional: without it the game still runs (single player).
    TFH::SteamNet steam;
    {
        PrepareSteamAppId(exeDir);
        std::string err;
        steam.Init(err);
    }

    InitAzaharGlobals();
    TFH::ApplyToAzahar(cfg); // before InputCommon::Init, which reads the input profile
    // Optional extra controller mappings (user/gamecontrollerdb.txt) for pads
    // SDL does not know by itself; must be loaded before the pads are opened.
    TFH::Input::LoadControllerMappings(userDir / "gamecontrollerdb.txt");
    InputCommon::Init();
    TFH::Input::RegisterAnyFactories();
    Network::Init();
    auto playerName = [&] {
        return ConsoleName((cfg.use_steam_name && steam.Available()) ? steam.PersonaName() : cfg.name);
    };
    steam.AttachToEmulator(Common::UTF16ToUTF8(playerName()));

    // ---- launcher model ------------------------------------------------------------------
    TFH::LauncherModel model;
    model.cfg = &cfg;
    model.steam = &steam;
    model.version = kVersion;
    model.user_dir = ToUtf8(userDir);
    std::future<TFH::DumpCheck> checkFuture;
    auto selectDump = [&](const std::string& path) {
        model.dump_path = path;
        model.installed = false;
        model.install_error.clear();
        model.check = {};
        if (path.empty()) return;
        model.checking = true;
        checkFuture = std::async(std::launch::async, [path] { return TFH::CheckDump(path); });
    };
    selectDump(FindDump(argc, argv, cfg));
    LOG_INFO(Frontend, "Game file: {}", model.dump_path.empty() ? "(none)" : model.dump_path);

    model.game_files_log = ToUtf8(userDir / "log" / "game_files.txt");

    // ---- Discord Rich Presence -----------------------------------------------------------
    TFH::DiscordRpc discord;
    auto lastPresence = std::chrono::steady_clock::now() - std::chrono::seconds(10);

    TFH::Launcher launcher;
    if (!launcher.Create("Triforce Recompiled")) {
        ShowError(std::string("Could not open the launcher window: ") + SDL_GetError());
        return 1;
    }
    if (!std::getenv("TFH_UI_TAB")) launcher.SelectTab(cfg.setup_done ? "Play" : "Setup");

    GameSession session;
    InstallJob install;
    bool quit = false;
    auto lastLauncherFrame = std::chrono::steady_clock::now() - std::chrono::seconds(1);

    auto cycleLayout = [&] {
        static constexpr int kOrder[] = {0, 3, 2, 1, 5};
        std::size_t next = 0;
        for (std::size_t i = 0; i < std::size(kOrder); ++i)
            if (kOrder[i] == cfg.layout) next = (i + 1) % std::size(kOrder);
        cfg.layout = kOrder[next];
    };

    // -- Doppel switch (single player) ------------------------------------------------------
    // The game switches characters when a Doppel panel on the touch screen is
    // tapped, so the shortcut taps the three panels in turn.
    int doppelNext = 0;
    auto tapDoppel = [&](int index) {
        if (!session.Running()) return;
        index = std::clamp(index, 0, 2);
        const TFH::TouchPoint p{cfg.doppel_x[index], cfg.doppel_y[index]};
        session.Window()->TapTouch(p);
        LOG_INFO(Frontend, "Doppel switch: tapped panel {} at ({}, {})", index + 1, p.x, p.y);
    };
    auto cycleDoppel = [&] {
        if (!cfg.doppel_switch_enabled || !session.Running()) return;
        tapDoppel(doppelNext);
        doppelNext = (doppelNext + 1) % 3;
    };

    while (!quit) {
        // -- events -------------------------------------------------------------------------
        SDL_Event e;
        bool have = SDL_WaitEventTimeout(&e, session.Running() ? 4 : 16) != 0;
        bool settingsFromKeys = false;
        while (have) {
            if (e.type == SDL_QUIT) {
                quit = true;
            } else if (launcher.HandleEvent(e, cfg)) {
                // handled by the launcher UI
            } else if (session.Running() && e.type == SDL_KEYDOWN && !e.key.repeat &&
                       e.key.windowID == session.Window()->WindowId() &&
                       (e.key.keysym.scancode == SDL_SCANCODE_ESCAPE || e.key.keysym.scancode == SDL_SCANCODE_F9 ||
                        e.key.keysym.scancode == SDL_SCANCODE_F10 || e.key.keysym.scancode == SDL_SCANCODE_F11)) {
                switch (e.key.keysym.scancode) {
                case SDL_SCANCODE_ESCAPE:
                    session.Window()->ReleaseAllInput();
                    launcher.SelectTab("Settings");
                    launcher.BringToFront();
                    break;
                case SDL_SCANCODE_F9:
                    cfg.swap_screens = !cfg.swap_screens;
                    settingsFromKeys = true;
                    break;
                case SDL_SCANCODE_F10:
                    cycleLayout();
                    settingsFromKeys = true;
                    break;
                default:
                    cfg.fullscreen = !session.Window()->IsFullscreen();
                    settingsFromKeys = true;
                    break;
                }
            } else if (session.Running() && cfg.doppel_switch_enabled && cfg.doppel_key > 0 &&
                       e.type == SDL_KEYDOWN && !e.key.repeat &&
                       e.key.windowID == session.Window()->WindowId() &&
                       e.key.keysym.scancode == cfg.doppel_key) {
                cycleDoppel();
            } else if (session.Running() && cfg.doppel_switch_enabled && cfg.doppel_button >= 0 &&
                       e.type == SDL_CONTROLLERBUTTONDOWN && e.cbutton.button == cfg.doppel_button) {
                cycleDoppel();
            } else if (session.Running()) {
                session.Window()->HandleEvent(e);
            }
            have = SDL_PollEvent(&e) != 0;
        }

        if (launcher.CloseRequested()) {
            if (session.Running())
                launcher.Minimize(); // keep the process (and the online connection) alive
            else
                quit = true;
        }

        // -- background work ------------------------------------------------------------------
        steam.RunCallbacks();
        TFH::Input::RefreshPads(); // pads SDL only sees as a plain joystick
        if (session.Running()) {
            session.Window()->UpdateTouch(); // release a frontend tap (Doppel switch)
            // Generic pads are not SDL game controllers, so their button events
            // never arrive - poll the configured shortcut button instead.
            if (cfg.doppel_switch_enabled && cfg.doppel_button >= 0 && session.Window()->HasFocus() &&
                TFH::Input::RawPadButtonPressed(cfg.doppel_button))
                cycleDoppel();
            const TFH::TouchPoint t = session.Window()->LastTouch();
            model.last_touch = {t.x, t.y};
            model.last_touch_valid = session.Window()->HasLastTouch();
        }
        if (model.checking && checkFuture.valid() &&
            checkFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            model.check = checkFuture.get();
            model.checking = false;
            model.installed = model.check.state == TFH::DumpCheck::State::Ok && IsInstalled(model.dump_path);
            LOG_INFO(Frontend, "Game check: {}", model.check.message);
        }
        if (model.installing) {
            model.install_percent = install.percent;
            if (install.done) {
                install.Join();
                model.installing = false;
                using IS = Service::AM::InstallStatus;
                if (install.status == IS::Success && IsInstalled(model.dump_path)) {
                    model.installed = true;
                } else if (install.status == IS::ErrorEncrypted) {
                    model.install_error = "Installation failed: the file is encrypted.";
                } else {
                    model.install_error = "Installation failed (code " +
                                          std::to_string(static_cast<int>(install.status)) + ").";
                }
            }
        }
        if (session.ShouldStop()) {
            session.Stop();
            model.game_running = false;
            launcher.BringToFront();
        }

        // -- Discord Rich Presence (once per second) -----------------------------------------
        if (std::chrono::steady_clock::now() - lastPresence >= std::chrono::seconds(1)) {
            lastPresence = std::chrono::steady_clock::now();
            const auto snap = TFH::RomfsWatch::Get();
            model.current_level = snap.level;
            model.last_game_file = snap.last_file;
            model.game_files_seen = snap.files_seen;
            if (cfg.discord_enabled) {
                discord.Start(cfg.discord_app_id.empty() ? std::string(TFH_DISCORD_APP_ID) : cfg.discord_app_id);
                discord.SetActivity(BuildPresence(cfg, steam, session.Running(), session.StartTime(), snap));
            } else {
                discord.Stop();
            }
            model.discord_status = cfg.discord_enabled ? discord.Status() : "Off";
        }

        // -- launcher (throttled while the game runs) ---------------------------------------
        const auto now = std::chrono::steady_clock::now();
        TFH::LauncherActions a;
        if (!session.Running() || now - lastLauncherFrame > std::chrono::milliseconds(33)) {
            lastLauncherFrame = now;
            a = launcher.Frame(model);
        }
        a.settings_changed |= settingsFromKeys;

        if (a.browse) {
            const std::string picked = TFH::BrowseForGameFile();
            if (!picked.empty()) {
                cfg.game_path = picked;
                save();
                selectDump(picked);
            }
        }
        if (a.use_path && !a.path_input.empty()) {
            cfg.game_path = a.path_input;
            save();
            selectDump(a.path_input);
        }
        if (a.install && !model.installing && IsCia(model.dump_path)) {
            model.installing = true;
            model.install_error.clear();
            install.Start(model.dump_path);
        }
        if (a.settings_changed) {
            TFH::ApplyToAzahar(cfg);
            session.ApplyLive(cfg);
            save();
        }
        if (a.stop_game) {
            session.Stop();
            model.game_running = false;
        }
        if (a.doppel_test >= 0) tapDoppel(a.doppel_test);
        if (a.play && !session.Running()) {
            const std::string bootPath = IsCia(model.dump_path) ? InstalledAppPath(model.dump_path) : model.dump_path;
            steam.AttachToEmulator(Common::UTF16ToUTF8(playerName()));
            if (!bootPath.empty() && session.Start(bootPath, playerName(), cfg, userDir / "log")) {
                model.game_running = true;
            }
        }
        if (a.quit) quit = true;
    }

    session.Stop();
    discord.Stop();
    install.Join();
    save();
    launcher.Destroy();
    steam.DetachFromEmulator();
    Network::Shutdown();
    steam.Shutdown();
    TFH::Input::UnregisterAnyFactories();
    InputCommon::Shutdown();
    SDL_Quit();
    LOG_INFO(Frontend, "Triforce Recompiled exiting");
    return 0;
}
