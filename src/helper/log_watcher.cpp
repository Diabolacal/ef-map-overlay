#include "log_watcher.hpp"

#include "log_parsers.hpp"

#include <windows.h>
#include <shlobj.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <cwchar>
#include <cwctype>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace helper::logs
{
    namespace
    {
        bool starts_with_case_insensitive(const std::wstring& value, const std::wstring& prefix)
        {
            if (value.size() < prefix.size())
            {
                return false;
            }

            for (std::size_t i = 0; i < prefix.size(); ++i)
            {
                if (std::towlower(value[i]) != std::towlower(prefix[i]))
                {
                    return false;
                }
            }
            return true;
        }

        std::wstring to_wstring(const std::filesystem::path& path)
        {
            return path.native();
        }

        std::string format_time_utc(const std::chrono::system_clock::time_point& tp)
        {
            const auto seconds = std::chrono::system_clock::to_time_t(tp);
            std::tm tm{};
            gmtime_s(&tm, &seconds);
            std::ostringstream oss;
            oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S UTC");
            return oss.str();
        }

        std::string sanitize(std::string value)
        {
            value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
            value.erase(std::remove(value.begin(), value.end(), '\n'), value.end());
            return value;
        }
    }

    LogWatcher::LogWatcher(Config config, const SystemResolver& resolver, PublishCallback publishCallback, StatusCallback statusCallback)
        : config_(std::move(config))
        , resolver_(resolver)
        , publishCallback_(std::move(publishCallback))
        , statusCallback_(std::move(statusCallback))
    {
    }

    LogWatcher::~LogWatcher()
    {
        stop();
    }

    void LogWatcher::start()
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (running_.load())
        {
            return;
        }

        stopRequested_.store(false);
        running_.store(true);
        worker_ = std::thread([this]() {
            run();
        });
    }

    void LogWatcher::stop()
    {
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (!running_.load())
            {
                return;
            }
            stopRequested_.store(true);
        }

        cv_.notify_all();
        if (worker_.joinable())
        {
            worker_.join();
        }
        running_.store(false);
    }

    LogWatcherStatus LogWatcher::status() const
    {
        std::lock_guard<std::mutex> guard(mutex_);
        return status_;
    }

    void LogWatcher::run()
    {
        spdlog::info("Log watcher thread starting");

        const auto pollInterval = std::max(std::chrono::milliseconds{250}, config_.pollInterval);

        while (!stopRequested_.load())
        {
            LogWatcherStatus snapshot;
            bool publish = false;
            bool forcePublish = false;

            {
                std::unique_lock<std::mutex> lock(mutex_);
                status_.running = true;

                discoverDirectories();
                forcePublish |= refreshChatFile();
                refreshCombatFile();

                publish |= processLocalChat();
                publish |= processCombat();

                snapshot = status_;
            }

            if (statusCallback_)
            {
                statusCallback_(snapshot);
            }

            publishStateIfNeeded(snapshot, publish || forcePublish);

            std::unique_lock<std::mutex> waitLock(mutex_);
            cv_.wait_for(waitLock, pollInterval, [this]() {
                return stopRequested_.load();
            });
        }

        spdlog::info("Log watcher thread stopping");
    }

    bool LogWatcher::discoverDirectories()
    {
        bool changed = false;

        if (config_.chatDirectoryOverride.has_value())
        {
            const auto desired = *config_.chatDirectoryOverride;
            if (status_.chatDirectory != desired)
            {
                status_.chatDirectory = desired;
                chatTail_.reset({});
                changed = true;
            }
        }
        else if (status_.chatDirectory.empty())
        {
            if (auto resolved = resolveDefaultDirectory(L"Chatlogs"))
            {
                status_.chatDirectory = *resolved;
                chatTail_.reset({});
                changed = true;
            }
        }

        if (config_.combatDirectoryOverride.has_value())
        {
            const auto desired = *config_.combatDirectoryOverride;
            if (status_.combatDirectory != desired)
            {
                status_.combatDirectory = desired;
                combatTail_.reset({});
            }
        }
        else if (status_.combatDirectory.empty())
        {
            if (auto resolved = resolveDefaultDirectory(L"Gamelogs"))
            {
                status_.combatDirectory = *resolved;
                combatTail_.reset({});
            }
        }

        if (!status_.chatDirectory.empty() && !status_.combatDirectory.empty())
        {
            status_.lastError.clear();
        }
        else if (status_.lastError.empty())
        {
            status_.lastError = "Waiting for Frontier log directories";
        }

        return changed;
    }

    bool LogWatcher::refreshChatFile()
    {
        if (status_.chatDirectory.empty())
        {
            chatTail_.path.clear();
            return false;
        }

        auto latest = latestChatLogPath(status_.chatDirectory);
        if (!latest.has_value())
        {
            chatTail_.path.clear();
            status_.chatFile.clear();
            return false;
        }

        if (chatTail_.path != *latest)
        {
            chatTail_.reset(*latest);
            status_.chatFile = *latest;
            lastPublishedSystemId_.reset();
            status_.lastError.clear();
            return true;
        }

        std::error_code ec;
        const auto writeTime = std::filesystem::last_write_time(*latest, ec);
        if (!ec)
        {
            chatWriteTime_ = writeTime;
        }

        return false;
    }

    bool LogWatcher::refreshCombatFile()
    {
        if (status_.combatDirectory.empty())
        {
            combatTail_.path.clear();
            return false;
        }

        auto latest = latestCombatLogPath(status_.combatDirectory);
        if (!latest.has_value())
        {
            combatTail_.path.clear();
            status_.combatFile.clear();
            status_.combat.reset();
            return false;
        }

        if (combatTail_.path != *latest)
        {
            combatTail_.reset(*latest);
            status_.combatFile = *latest;
            status_.combat.emplace();
            if (auto id = combat_log_character_id(latest->filename().string()))
            {
                status_.combat->characterId = *id;
            }
            else
            {
                status_.combat->characterId.clear();
            }
        }

        std::error_code ec;
        const auto writeTime = std::filesystem::last_write_time(*latest, ec);
        if (!ec)
        {
            combatWriteTime_ = writeTime;
        }

        return false;
    }

    bool LogWatcher::processLocalChat()
    {
        if (chatTail_.path.empty())
        {
            return false;
        }

        auto lines = readNewLines(chatTail_);
        bool updated = false;
        for (const auto& line : lines)
        {
            auto parsed = parse_local_chat_line(line);
            if (!parsed.has_value())
            {
                continue;
            }

            LocationSample sample;
            sample.systemName = parsed->systemName;
            sample.systemId = parsed->systemName;
            sample.observedAt = std::chrono::system_clock::now();

            if (const auto resolved = resolver_.resolve(sample.systemName))
            {
                sample.systemId = *resolved;
                status_.lastError.clear();
            }
            else
            {
                status_.lastError = "Unmapped system name: " + sample.systemName;
                spdlog::warn("LogWatcher unable to resolve system name '{}'", sample.systemName);
            }

            status_.location = std::move(sample);
            updated = true;
        }

        return updated;
    }

    bool LogWatcher::processCombat()
    {
        if (combatTail_.path.empty())
        {
            return false;
        }

        auto lines = readNewLines(combatTail_);
        if (lines.empty())
        {
            return false;
        }

        if (!status_.combat.has_value())
        {
            status_.combat.emplace();
            if (auto id = combat_log_character_id(combatTail_.path.filename().string()))
            {
                status_.combat->characterId = *id;
            }
        }

        bool updated = false;
        for (const auto& line : lines)
        {
            if (line.find("(combat)") != std::string::npos)
            {
                ++status_.combat->combatEventCount;
                status_.combat->lastCombatLine = sanitize(line);
                status_.combat->lastEventAt = std::chrono::system_clock::now();
                updated = true;
            }
            else if (line.find("(notify)") != std::string::npos)
            {
                ++status_.combat->notifyEventCount;
                status_.combat->lastEventAt = std::chrono::system_clock::now();
                updated = true;
            }
        }

        return updated;
    }

    std::vector<std::string> LogWatcher::readNewLines(FileTailState& state)
    {
        std::vector<std::string> lines;
        if (state.path.empty())
        {
            return lines;
        }

        const auto pathW = to_wstring(state.path);
        HANDLE handle = CreateFileW(pathW.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            status_.lastError = "Unable to open log file";
            return lines;
        }

        LARGE_INTEGER size{};
        if (!GetFileSizeEx(handle, &size))
        {
            CloseHandle(handle);
            return lines;
        }

        std::uint64_t fileSize = static_cast<std::uint64_t>(size.QuadPart);
        if (fileSize < state.offset)
        {
            state.offset = 0;
            state.encoding = TextEncoding::Unknown;
            state.consumedBom = false;
            state.pendingLine.clear();
            state.pendingBytes.clear();
        }

        if (fileSize == state.offset)
        {
            CloseHandle(handle);
            return lines;
        }

        std::uint64_t remaining = fileSize - state.offset;
        std::vector<char> buffer(static_cast<std::size_t>(remaining));

        LARGE_INTEGER seek{};
        seek.QuadPart = static_cast<LONGLONG>(state.offset);
        if (!SetFilePointerEx(handle, seek, nullptr, FILE_BEGIN))
        {
            CloseHandle(handle);
            return lines;
        }

        DWORD totalRead = 0;
        while (remaining > 0)
        {
            const DWORD chunk = static_cast<DWORD>(std::min<std::uint64_t>(remaining, static_cast<std::uint64_t>(64 * 1024)));
            DWORD read = 0;
            if (!ReadFile(handle, buffer.data() + totalRead, chunk, &read, nullptr) || read == 0)
            {
                break;
            }
            totalRead += read;
            remaining -= read;
        }
        CloseHandle(handle);

        if (totalRead == 0)
        {
            return lines;
        }

        buffer.resize(totalRead);
        state.offset += totalRead;

        if (!state.pendingBytes.empty())
        {
            buffer.insert(buffer.begin(), state.pendingBytes.begin(), state.pendingBytes.end());
            state.pendingBytes.clear();
        }

        const bool firstChunk = !state.consumedBom;
        auto converted = convertToUtf8(state, buffer, firstChunk);
        if (converted.empty())
        {
            return lines;
        }

        state.consumedBom = true;

        if (converted.size() >= 3 && static_cast<unsigned char>(converted[0]) == 0xEF && static_cast<unsigned char>(converted[1]) == 0xBB && static_cast<unsigned char>(converted[2]) == 0xBF)
        {
            converted.erase(0, 3);
        }

        std::string combined;
        combined.reserve(state.pendingLine.size() + converted.size());
        combined.append(state.pendingLine);
        combined.append(converted);
        state.pendingLine.clear();

        std::size_t position = 0;
        while (position < combined.size())
        {
            const auto newline = combined.find_first_of("\r\n", position);
            if (newline == std::string::npos)
            {
                break;
            }

            std::size_t next = newline + 1;
            if (combined[newline] == '\r' && next < combined.size() && combined[next] == '\n')
            {
                ++next;
            }

            lines.emplace_back(combined.substr(position, newline - position));
            position = next;
        }

        state.pendingLine = combined.substr(position);
        return lines;
    }

    bool LogWatcher::ensureUtf16Even(FileTailState& state, std::vector<char>& buffer)
    {
        if (buffer.empty())
        {
            return false;
        }

        if (buffer.size() % 2 == 0)
        {
            return true;
        }

        state.pendingBytes.push_back(buffer.back());
        buffer.pop_back();
        return !buffer.empty();
    }

    std::string LogWatcher::convertToUtf8(FileTailState& state, std::vector<char>& buffer, bool isFirstChunk)
    {
        if (buffer.empty())
        {
            return {};
        }

        if (state.encoding == TextEncoding::Unknown)
        {
            if (buffer.size() >= 2 && static_cast<unsigned char>(buffer[0]) == 0xFF && static_cast<unsigned char>(buffer[1]) == 0xFE)
            {
                state.encoding = TextEncoding::Utf16LE;
            }
            else
            {
                state.encoding = TextEncoding::Utf8;
            }
        }

        if (state.encoding == TextEncoding::Utf8)
        {
            std::size_t start = 0;
            if (isFirstChunk && buffer.size() >= 3 && static_cast<unsigned char>(buffer[0]) == 0xEF && static_cast<unsigned char>(buffer[1]) == 0xBB && static_cast<unsigned char>(buffer[2]) == 0xBF)
            {
                start = 3;
            }
            return std::string(buffer.data() + start, buffer.data() + buffer.size());
        }

        if (state.encoding == TextEncoding::Utf16LE)
        {
            std::size_t offset = 0;
            if (isFirstChunk && buffer.size() >= 2 && static_cast<unsigned char>(buffer[0]) == 0xFF && static_cast<unsigned char>(buffer[1]) == 0xFE)
            {
                offset = 2;
            }

            if (buffer.size() <= offset)
            {
                return {};
            }

            std::vector<char> slice(buffer.begin() + static_cast<std::ptrdiff_t>(offset), buffer.end());
            if (!ensureUtf16Even(state, slice))
            {
                return {};
            }

            const wchar_t* wide = reinterpret_cast<const wchar_t*>(slice.data());
            const int wcharCount = static_cast<int>(slice.size() / sizeof(wchar_t));
            if (wcharCount <= 0)
            {
                return {};
            }

            const int required = WideCharToMultiByte(CP_UTF8, 0, wide, wcharCount, nullptr, 0, nullptr, nullptr);
            if (required <= 0)
            {
                return {};
            }

            std::string utf8(static_cast<std::size_t>(required), '\0');
            WideCharToMultiByte(CP_UTF8, 0, wide, wcharCount, utf8.data(), required, nullptr, nullptr);
            return utf8;
        }

        return {};
    }

    std::optional<std::filesystem::path> LogWatcher::resolveDefaultDirectory(const wchar_t* subFolder) const
    {
        PWSTR rawPath = nullptr;
        std::filesystem::path documents;

        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &rawPath)) && rawPath != nullptr)
        {
            documents = std::filesystem::path(rawPath);
            CoTaskMemFree(rawPath);
        }

        if (documents.empty())
        {
            wchar_t* userProfile = nullptr;
            std::size_t length = 0;
            if (_wdupenv_s(&userProfile, &length, L"USERPROFILE") == 0 && userProfile != nullptr)
            {
                documents = std::filesystem::path(userProfile) / L"Documents";
                std::free(userProfile);
            }
        }

        if (documents.empty())
        {
            return std::nullopt;
        }

        documents /= L"Frontier";
        documents /= L"Logs";
        documents /= subFolder;

        if (!std::filesystem::exists(documents))
        {
            return std::nullopt;
        }

        return documents;
    }

    std::optional<std::filesystem::path> LogWatcher::latestChatLogPath(const std::filesystem::path& directory) const
    {
        std::optional<std::filesystem::path> best;
        std::filesystem::file_time_type bestTime{};

        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(directory, ec))
        {
            if (!entry.is_regular_file())
            {
                continue;
            }

            const auto filename = entry.path().filename().wstring();
            if (!starts_with_case_insensitive(filename, L"Local_"))
            {
                continue;
            }
            if (!entry.path().has_extension() || entry.path().extension() != ".txt")
            {
                continue;
            }

            const auto writeTime = entry.last_write_time(ec);
            if (ec)
            {
                continue;
            }

            if (!best.has_value() || writeTime > bestTime)
            {
                best = entry.path();
                bestTime = writeTime;
            }
        }

        return best;
    }

    std::optional<std::filesystem::path> LogWatcher::latestCombatLogPath(const std::filesystem::path& directory) const
    {
        std::optional<std::filesystem::path> best;
        std::filesystem::file_time_type bestTime{};

        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(directory, ec))
        {
            if (!entry.is_regular_file())
            {
                continue;
            }

            const auto filename = entry.path().filename().string();
            if (!is_combat_log_filename(filename))
            {
                continue;
            }

            const auto writeTime = entry.last_write_time(ec);
            if (ec)
            {
                continue;
            }

            if (!best.has_value() || writeTime > bestTime)
            {
                best = entry.path();
                bestTime = writeTime;
            }
        }

        return best;
    }

    void LogWatcher::publishStateIfNeeded(const LogWatcherStatus& snapshot, bool forcePublish)
    {
        if (!publishCallback_)
        {
            return;
        }

        if (!snapshot.location.has_value())
        {
            if (!forcePublish)
            {
                return;
            }
        }

        const auto now = std::chrono::system_clock::now();
        bool shouldPublish = forcePublish;

        if (snapshot.location.has_value())
        {
            if (!lastPublishedSystemId_.has_value() || *lastPublishedSystemId_ != snapshot.location->systemId)
            {
                shouldPublish = true;
                lastPublishedSystemId_ = snapshot.location->systemId;
            }
        }

        if (!shouldPublish)
        {
            if (lastPublishedAt_.time_since_epoch().count() == 0 || (now - lastPublishedAt_) > std::chrono::seconds(30))
            {
                shouldPublish = true;
            }
        }

        if (!shouldPublish)
        {
            return;
        }

        const auto state = buildOverlayState(snapshot);
        const auto payload = overlay::serialize_overlay_state(state).dump();
        publishCallback_(state, payload.size());
        lastPublishedAt_ = now;
    }

    overlay::OverlayState LogWatcher::buildOverlayState(const LogWatcherStatus& snapshot)
    {
        overlay::OverlayState state;
        state.generated_at_ms = now_ms();
    state.heartbeat_ms = state.generated_at_ms;
        state.follow_mode_enabled = true;
    state.source_online = true;

        if (snapshot.location.has_value())
        {
            overlay::RouteNode node;
            node.system_id = snapshot.location->systemId;
            node.display_name = snapshot.location->systemName;
            node.distance_ly = 0.0;
            node.via_gate = false;
            state.route.push_back(std::move(node));

            overlay::PlayerMarker marker;
            marker.system_id = snapshot.location->systemId;
            marker.display_name = snapshot.location->systemName;
            marker.is_docked = false;
            state.player_marker = marker;
        }
        else
        {
            overlay::RouteNode node;
            node.system_id = "LOG-WATCH";
            node.display_name = "Awaiting log data";
            node.distance_ly = 0.0;
            node.via_gate = false;
            state.route.push_back(std::move(node));
            state.notes = std::string{"Log watcher active, waiting for Local chat entry."};
        }

        if (!state.notes.has_value())
        {
            state.notes = buildStatusNotes(snapshot);
        }

        return state;
    }

    std::string LogWatcher::buildStatusNotes(const LogWatcherStatus& snapshot)
    {
        std::ostringstream oss;
        if (snapshot.location.has_value())
        {
            oss << "Location: " << snapshot.location->systemName;
            if (!snapshot.chatFile.empty())
            {
                oss << " (" << snapshot.chatFile.filename().string() << ")";
            }
            if (!snapshot.location->systemId.empty() && snapshot.location->systemId != snapshot.location->systemName)
            {
                oss << " [" << snapshot.location->systemId << "]";
            }
            if (snapshot.location->observedAt.time_since_epoch().count() != 0)
            {
                oss << " @ " << format_time_utc(snapshot.location->observedAt);
            }
        }
        else
        {
            oss << "Location pending";
        }

        if (snapshot.combat.has_value())
        {
            oss << "; Combat events: " << snapshot.combat->combatEventCount;
            if (!snapshot.combat->characterId.empty())
            {
                oss << " (" << snapshot.combat->characterId << ")";
            }
            if (!snapshot.combat->lastCombatLine.empty())
            {
                oss << " last=" << snapshot.combat->lastCombatLine.substr(0, 80);
            }
        }
        else if (!snapshot.combatFile.empty())
        {
            oss << "; Combat log armed";
        }

        return oss.str();
    }

    std::uint64_t LogWatcher::now_ms()
    {
        const auto now = std::chrono::system_clock::now();
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
    }
}
