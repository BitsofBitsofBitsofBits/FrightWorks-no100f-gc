// F3 video recording (developer tools), see no100f_recorder.h.
//
// Video: aurora hands over every frame the game renders as the window's picture (aurora_set_video_capture), with
// the overlay chosen by aurora_set_capture_overlay: only the DEV stamp, or the menus too; never the REC light (its
// window is AURORA_CAPTURE_HIDDEN_WINDOW). A writer thread starts ffmpeg on the first frame and pipes raw frames to
// its stdin at a constant 60 fps on the wall clock: a frame lands at round((t - t0) * 60), gaps (loading, a stall,
// a 30 fps game) repeat the last frame, extra frames are dropped. x264 into a temporary .mkv (still playable if the
// game dies mid-recording).
// Audio ([game] record_audio): AudioBackend hands the recorder every block it sends to the output; from the first
// video frame on they go into a .wav whose first sample's Unix time is noted. A gap of more than 100 ms is filled
// with silence, so the sound stays on the clock.
// Stop: the pipe is closed, then a second ffmpeg run writes the .mp4 with the .wav placed at its offset from the first
// video frame (no audio: a plain remux). The .mkv/.wav and ffmpeg's log are deleted once the .mp4 is written, kept
// when something failed. ffmpeg runs in a job object that ends it with the game.

#include "no100f_recorder.h"

#include "audio_backend.h"
#include "build_info.h"
#include "no100f_devmenu.h"
#include "overlay_theme.h"
#include "runtime_config.h"
#include "runtime_log.h"

#include <aurora/aurora.h>
#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <cwchar>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace no100f_recorder {
namespace {

std::mutex g_noticeMutex;
std::string g_notice;
std::filesystem::path g_noticeFile;

void Notice(std::string text, std::filesystem::path file = {}) {
    std::lock_guard<std::mutex> lock(g_noticeMutex);
    g_notice = std::move(text);
    g_noticeFile = std::move(file);
}

} // namespace

std::string TakeNotice(std::filesystem::path& file) {
    std::lock_guard<std::mutex> lock(g_noticeMutex);
    file = std::exchange(g_noticeFile, std::filesystem::path());
    return std::exchange(g_notice, std::string());
}

#if defined(_WIN32)
namespace {

using Clock = std::chrono::steady_clock;
constexpr int kFps = 60;
constexpr uint32_t kMaxHeight = 1080;  // bigger windows are scaled down
constexpr size_t kMaxQueuedFrames = 8;
constexpr auto kMaxLength = std::chrono::minutes(30);
constexpr auto kFirstFrameTimeout = std::chrono::seconds(5);

int64_t UnixMicros() {
    FILETIME ft{};
    GetSystemTimePreciseAsFileTime(&ft);
    const uint64_t t = (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return static_cast<int64_t>(t / 10) - 11644473600000000LL;  // 100 ns since 1601 -> us since 1970
}

std::string Utf8(const std::filesystem::path& path) { return RuntimeConfigFile::PathToUtf8(path); }

std::filesystem::path WithSuffix(const std::filesystem::path& base, const wchar_t* suffix) {
    return std::filesystem::path(base.wstring() + suffix);
}

std::wstring Quoted(const std::filesystem::path& p) { return L"\"" + p.wstring() + L"\""; }

// ----- Audio: the .wav, fed by AudioBackend on the audio thread -----

struct AudioTrack {
    std::FILE* file = nullptr;  // set while recording audio
    uint32_t channels = 0;
    uint32_t rate = 0;          // 0 = no block yet
    uint64_t frames = 0;        // written, silence included
    uint64_t silence = 0;       // of those, silence put in for gaps
    int64_t startMicros = 0;    // Unix time of the first sample
};

std::mutex g_audioMutex;
AudioTrack g_audio;
std::atomic<bool> g_videoStarted{false};  // sound is kept from the first video frame on

void WriteWavHeader(std::FILE* f, uint32_t channels, uint32_t rate, uint64_t frames) {
    const uint32_t data = static_cast<uint32_t>(std::min<uint64_t>(frames * channels * 2, 0xFFFFFFFFull - 36));
    const auto u16 = [f](uint32_t v) {
        const uint8_t b[2] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8)};
        std::fwrite(b, 1, 2, f);
    };
    const auto u32 = [f](uint32_t v) {
        const uint8_t b[4] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v >> 16),
                              static_cast<uint8_t>(v >> 24)};
        std::fwrite(b, 1, 4, f);
    };
    std::fwrite("RIFF", 1, 4, f);
    u32(36 + data);
    std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16);
    u16(1);  // PCM
    u16(channels);
    u32(rate);
    u32(rate * channels * 2);
    u16(channels * 2);
    u16(16);
    std::fwrite("data", 1, 4, f);
    u32(data);
}

void CaptureAudio(const int16_t* samples, size_t frames, uint32_t channels, uint32_t rate) {
    if (!g_videoStarted.load(std::memory_order_acquire)) return;
    std::lock_guard<std::mutex> lock(g_audioMutex);
    AudioTrack& a = g_audio;
    if (!a.file || frames == 0 || channels == 0 || rate == 0) return;
    const int64_t now = UnixMicros();
    if (a.rate == 0) {
        a.rate = rate;
        a.channels = channels;
        a.startMicros = now;
    }
    if (rate != a.rate || channels != a.channels) return;  // the output was reopened differently: keep the first format
    const uint64_t due = static_cast<uint64_t>(std::max<int64_t>(0, now - a.startMicros)) * a.rate / 1000000;
    if (due > a.frames + frames + a.rate / 10) {
        static const std::vector<int16_t> zeros(4096, 0);
        for (uint64_t gap = (due - a.frames - frames) * a.channels; gap > 0;) {
            const size_t n = static_cast<size_t>(std::min<uint64_t>(gap, zeros.size()));
            std::fwrite(zeros.data(), sizeof(int16_t), n, a.file);
            gap -= n;
        }
        a.silence += due - a.frames - frames;
        a.frames = due - frames;
    }
    std::fwrite(samples, sizeof(int16_t), frames * channels, a.file);
    a.frames += frames;
}

// ----- Video: frames from aurora (render thread) to the writer thread -----

struct Frame {
    std::vector<uint8_t> pixels;
    int64_t micros = 0;
};

struct VideoQueue {
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<Frame> frames;
    std::vector<std::vector<uint8_t>> spare;  // reused pixel buffers
    bool started = false;                     // first frame seen: the fields below are set
    uint32_t width = 0;
    uint32_t height = 0;
    bool bgra = true;
    int64_t firstMicros = 0;
    bool closing = false;                     // no more frames: drain and finish
    uint64_t dropped = 0;                     // the writer (ffmpeg) was behind
    uint64_t resized = 0;                     // another size than the first frame's (the window was resized)
};

VideoQueue g_video;

void OnVideoFrame(const uint8_t* pixels, uint32_t width, uint32_t height, bool bgra, int64_t unixMicros, void*) {
    std::lock_guard<std::mutex> lock(g_video.mutex);
    if (g_video.closing) return;
    if (!g_video.started) {
        g_video.started = true;
        g_video.width = width;
        g_video.height = height;
        g_video.bgra = bgra;
        g_video.firstMicros = unixMicros;
        g_videoStarted.store(true, std::memory_order_release);
    } else if (width != g_video.width || height != g_video.height) {
        ++g_video.resized;
        return;
    }
    if (g_video.frames.size() >= kMaxQueuedFrames) {
        ++g_video.dropped;
        return;
    }
    Frame frame;
    if (!g_video.spare.empty()) {
        frame.pixels = std::move(g_video.spare.back());
        g_video.spare.pop_back();
    }
    frame.pixels.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
    frame.micros = unixMicros;
    g_video.frames.push_back(std::move(frame));
    g_video.cv.notify_one();
}

// ----- ffmpeg -----

HANDLE Job() {
    static const HANDLE job = [] {
        HANDLE j = CreateJobObjectW(nullptr, nullptr);
        if (j) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
            info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(j, JobObjectExtendedLimitInformation, &info, sizeof info);
        }
        return j;
    }();
    return job;
}

// PATH (and the game's folder), then beside the launcher (where a portable launcher installs ffmpeg), the user data
// folder (where an installed launcher puts it), then the debug kit's copy.
std::optional<std::filesystem::path> FindFfmpeg() {
    wchar_t found[MAX_PATH * 2] = {};
    if (SearchPathW(nullptr, L"ffmpeg.exe", nullptr, static_cast<DWORD>(std::size(found)), found, nullptr) > 0) {
        return std::filesystem::path(found);
    }
    std::vector<std::filesystem::path> bases;
    if (const auto exeDir = RuntimeConfigFile::ExecutableDirectory()) {
        bases.push_back(*exeDir);
        bases.push_back(exeDir->parent_path());
    }
    bases.push_back(RuntimeConfigFile::ApplicationDataDirectory());
    for (const auto& base : bases) {
        for (const char* rel : {"ffmpeg.exe", "ffmpeg/bin/ffmpeg.exe", "tools/debug/local/ffmpeg/bin/ffmpeg.exe"}) {
            std::error_code ec;
            const std::filesystem::path p = base / rel;
            if (std::filesystem::is_regular_file(p, ec)) return p;
        }
    }
    return std::nullopt;
}

// Runs ffmpeg with stdout + stderr appended to log. stdinWrite: gets the write end of a pipe on its stdin; without
// it ffmpeg's stdin is a pipe that is closed at once.
bool RunFfmpeg(const std::filesystem::path& exe, const std::wstring& args, HANDLE log, HANDLE* stdinWrite,
               HANDLE& process) {
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE inRead = nullptr;
    HANDLE inWrite = nullptr;
    if (!CreatePipe(&inRead, &inWrite, &sa, 1 << 20)) return false;
    SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);
    // Only these two handles go to ffmpeg, not every inheritable handle the game has open.
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::vector<char> attrBuf(size);
    auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
    HANDLE inherit[2] = {inRead, log};
    bool ok = InitializeProcThreadAttributeList(attrs, 1, 0, &size) &&
              UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof inherit, nullptr,
                                        nullptr);
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof si;
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = inRead;
    si.StartupInfo.hStdOutput = log;
    si.StartupInfo.hStdError = log;
    si.lpAttributeList = attrs;
    std::wstring cmd = L"\"" + exe.wstring() + L"\" " + args;
    PROCESS_INFORMATION pi{};
    ok = ok && CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE,
                              EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr,
                              &si.StartupInfo, &pi);
    if (size > 0) DeleteProcThreadAttributeList(attrs);
    CloseHandle(inRead);
    if (!ok) {
        CloseHandle(inWrite);
        return false;
    }
    if (HANDLE job = Job()) AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    process = pi.hProcess;
    if (stdinWrite) {
        *stdinWrite = inWrite;
    } else {
        CloseHandle(inWrite);
    }
    return true;
}

bool WriteAll(HANDLE pipe, const uint8_t* data, size_t bytes) {
    while (bytes > 0) {
        DWORD written = 0;
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(bytes, 1u << 24));
        if (!WriteFile(pipe, data, chunk, &written, nullptr) || written == 0) return false;
        data += written;
        bytes -= written;
    }
    return true;
}

// ----- Sessions -----

struct Session {
    HANDLE log = nullptr;
    std::filesystem::path ffmpeg;
    std::filesystem::path base;   // Videos\FrightWorks_<date>_<time>, no extension
    Clock::time_point started;
    std::string startedText;
    std::string overlay;
    std::string whereStart;
    std::string whereEnd;
    std::string settings;
    std::string stopReason;
    double seconds = 0.0;
    bool audio = false;
};

std::mutex g_sessionMutex;
std::optional<Session> g_session;      // a recording runs (game thread)
std::atomic<bool> g_recording{false};
std::atomic<bool> g_saving{false};     // the writer is still finishing the last one
std::atomic<bool> g_writerFailed{false};
HANDLE g_idle = CreateEventW(nullptr, TRUE, TRUE, nullptr);  // set while no writer runs
Clock::time_point g_recordingSince;

// The [video] and [game] sections of Config.toml, indented.
std::string SettingsText() {
    std::ifstream in(RuntimeConfigFile::ResolveConfigPath());
    if (!in) return "  (no Config.toml)\n";
    std::string out;
    bool keep = false;
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos) continue;
        if (line[first] == '[') keep = line.compare(first, 7, "[video]") == 0 || line.compare(first, 6, "[game]") == 0;
        if (keep) out += "  " + line + "\n";
    }
    return out;
}

std::string LocalTime(const char* format) {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    char text[64] = {};
    std::strftime(text, sizeof text, format, &local);
    return text;
}

// Writer thread: ffmpeg from the first frame on, frames at 60 fps until the queue is closed, then the .mp4 and .txt.
// `session` is filled in by Stop before it closes the queue (whereEnd, stopReason, seconds).
void Writer(std::shared_ptr<Session> session) {
    Session& s = *session;
    const std::filesystem::path video = WithSuffix(s.base, L".video.mkv");
    const std::filesystem::path wav = WithSuffix(s.base, L".audio.wav");
    const std::filesystem::path mp4 = WithSuffix(s.base, L".mp4");
    const std::filesystem::path logPath = WithSuffix(s.base, L".ffmpeg.log");
    HANDLE ffmpeg = nullptr;
    HANDLE pipe = nullptr;
    uint64_t written = 0;
    uint64_t repeated = 0;
    uint64_t skipped = 0;
    std::vector<uint8_t> last;
    std::string failure;
    uint32_t width = 0, height = 0;
    int64_t firstMicros = 0;
    for (;;) {
        Frame frame;
        {
            std::unique_lock<std::mutex> lock(g_video.mutex);
            g_video.cv.wait(lock, [] { return !g_video.frames.empty() || g_video.closing; });
            if (g_video.frames.empty()) break;  // closing and drained
            frame = std::move(g_video.frames.front());
            g_video.frames.pop_front();
            width = g_video.width;
            height = g_video.height;
            firstMicros = g_video.firstMicros;
            if (!pipe && failure.empty()) {
                const std::wstring args =
                    L"-hide_banner -loglevel warning -nostats -y -f rawvideo -pix_fmt " +
                    std::wstring(g_video.bgra ? L"bgra" : L"rgba") + L" -s " + std::to_wstring(width) + L"x" +
                    std::to_wstring(height) + L" -framerate " + std::to_wstring(kFps) +
                    L" -i pipe:0 -c:v libx264 -preset ultrafast -crf 20 -pix_fmt yuv420p " + Quoted(video);
                if (!RunFfmpeg(s.ffmpeg, args, s.log, &pipe, ffmpeg)) failure = "couldn't start ffmpeg";
            }
        }
        if (failure.empty()) {
            const auto index = static_cast<uint64_t>(std::llround(
                static_cast<double>(std::max<int64_t>(0, frame.micros - firstMicros)) * kFps / 1e6));
            if (index < written) {
                ++skipped;  // more than 60 fps
            } else {
                for (; written < index && !last.empty(); ++written, ++repeated) {
                    if (!WriteAll(pipe, last.data(), last.size())) break;
                }
                if (WriteAll(pipe, frame.pixels.data(), frame.pixels.size())) {
                    ++written;
                } else if (failure.empty()) {
                    failure = "ffmpeg stopped taking frames";
                    g_writerFailed = true;
                }
                std::swap(last, frame.pixels);
            }
        }
        std::lock_guard<std::mutex> lock(g_video.mutex);
        if (!frame.pixels.empty() && g_video.spare.size() < 4) g_video.spare.push_back(std::move(frame.pixels));
    }
    if (pipe) CloseHandle(pipe);  // end of input: ffmpeg finishes the file
    DWORD captureExit = 0;
    if (ffmpeg) {
        if (WaitForSingleObject(ffmpeg, 60000) != WAIT_OBJECT_0) TerminateProcess(ffmpeg, 1);
        GetExitCodeProcess(ffmpeg, &captureExit);
        CloseHandle(ffmpeg);
    }
    uint64_t dropped = 0, resized = 0;
    {
        std::lock_guard<std::mutex> lock(g_video.mutex);
        dropped = g_video.dropped;
        resized = g_video.resized;
    }

    AudioTrack track;
    {
        std::lock_guard<std::mutex> lock(g_audioMutex);
        track = std::exchange(g_audio, AudioTrack{});
    }
    if (track.file) {
        std::fseek(track.file, 0, SEEK_SET);
        WriteWavHeader(track.file, track.rate ? track.channels : 2, track.rate ? track.rate : 32000, track.frames);
        std::fclose(track.file);
    }
    const bool withAudio = s.audio && track.frames > 0;
    std::error_code ec;
    const auto videoBytes = std::filesystem::file_size(video, ec);
    bool saved = false;
    if (!ec && videoBytes > 0 && written > 0) {
        std::wstring args = L"-hide_banner -loglevel warning -nostats -nostdin -y -i " + Quoted(video) + L" ";
        if (withAudio) {
            const double offset = static_cast<double>(track.startMicros - firstMicros) / 1e6;
            wchar_t start[64] = {};
            std::swprintf(start, std::size(start), L"%.6f", offset);
            args += L"-itsoffset " + std::wstring(start) + L" -i " + Quoted(wav) +
                    L" -map 0:v -map 1:a -c:v copy -c:a aac -b:a 192k ";
        } else {
            args += L"-c copy ";
        }
        args += L"-movflags +faststart " + Quoted(mp4);
        HANDLE mux = nullptr;
        if (RunFfmpeg(s.ffmpeg, args, s.log, nullptr, mux)) {
            WaitForSingleObject(mux, 10 * 60 * 1000);
            DWORD muxExit = 1;
            GetExitCodeProcess(mux, &muxExit);
            CloseHandle(mux);
            saved = muxExit == 0 && std::filesystem::file_size(mp4, ec) > 0 && !ec;
        }
    } else if (failure.empty()) {
        failure = written == 0 ? "no frames came from the renderer" : "ffmpeg wrote no video";
    }
    CloseHandle(s.log);
    if (saved) {
        std::filesystem::remove(video, ec);
        std::filesystem::remove(wav, ec);
        std::filesystem::remove(logPath, ec);
    } else if (written == 0) {
        std::filesystem::remove(wav, ec);
    }

    std::string audioLine = "off";
    if (s.audio) {
        audioLine = track.rate == 0 ? "on, but the game sent no sound"
                                    : "game output, " + std::to_string(track.rate) + " Hz, before the volume and mute settings";
        if (track.rate != 0 && track.silence > 0) {
            audioLine += "; " + std::to_string(track.silence * 1000 / track.rate) + " ms of silence put in for gaps";
        }
    }
    std::ofstream txt(WithSuffix(s.base, L".txt"));
    txt << "video    "
        << (saved ? Utf8(mp4.filename()) : "not saved (" + (failure.empty() ? "see the .ffmpeg.log" : failure) + ")")
        << "\n"
        << "build    " << BuildInfo::Id() << "\n"
        << "started  " << s.startedText << ", " << static_cast<int>(s.seconds) << " s, stopped by " << s.stopReason << "\n"
        << "picture  " << width << "x" << height << " at " << kFps << " fps, " << s.overlay << "; " << written
        << " frames (" << repeated << " repeats for gaps, " << skipped << " over 60 fps skipped, " << dropped
        << " dropped while ffmpeg was behind" << (resized ? ", " + std::to_string(resized) + " after a window resize" : "")
        << "), ffmpeg exit " << captureExit << "\n"
        << "audio    " << audioLine << "\n"
        << "start    " << s.whereStart << "\n"
        << "end      " << s.whereEnd << "\n"
        << "settings at the start:\n"
        << s.settings;
    txt.close();
    RT_LOG(RT_TAG_RUNTIME) << "recording " << (saved ? "saved: " : "NOT saved: ") << Utf8(saved ? mp4 : video) << " ("
                           << written << " frames, " << dropped << " dropped)" << std::endl;
    const std::string folder = RuntimeConfigFile::CaptureDirectoryLabel(true);
    Notice(saved ? "Video saved: " + folder + Utf8(mp4.filename())
                 : "Video not saved: " + (failure.empty() ? "see " + folder + Utf8(logPath.filename()) : failure),
           saved ? mp4 : failure.empty() ? logPath : std::filesystem::path());
    g_saving = false;
    SetEvent(g_idle);
}

std::shared_ptr<Session> g_writerSession;  // the running writer's session (game thread)

void Start() {
    if (g_saving) {
        Notice("Still saving the last video, try again in a moment");
        return;
    }
    const auto ffmpeg = FindFfmpeg();
    if (!ffmpeg) {
        Notice("F3 needs ffmpeg: the launcher can download it (Extras > Developer tools)");
        return;
    }
    const std::filesystem::path folder = RuntimeConfigFile::CaptureDirectory(true);
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    const std::string stamp = "FrightWorks_" + LocalTime("%Y-%m-%d_%H-%M-%S");
    std::filesystem::path base = folder / stamp;
    for (int n = 2; std::filesystem::exists(WithSuffix(base, L".mp4"), ec) ||
                    std::filesystem::exists(WithSuffix(base, L".video.mkv"), ec);
         ++n) {
        base = folder / (stamp + "_" + std::to_string(n));
    }

    auto s = std::make_shared<Session>();
    s->ffmpeg = *ffmpeg;
    s->base = base;
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    s->log = CreateFileW(WithSuffix(base, L".ffmpeg.log").c_str(), FILE_APPEND_DATA,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &sa, OPEN_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
    if (s->log == INVALID_HANDLE_VALUE) {
        Notice("Recording failed: can't write to " + Utf8(folder));
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_video.mutex);
        g_video.frames.clear();
        g_video.started = false;
        g_video.closing = false;
        g_video.dropped = 0;
        g_video.resized = 0;
    }
    g_videoStarted = false;
    g_writerFailed = false;
    if (!aurora_set_video_capture(&OnVideoFrame, nullptr, kMaxHeight)) {
        CloseHandle(s->log);
        std::filesystem::remove(WithSuffix(base, L".ffmpeg.log"), ec);
        Notice("Recording failed: the picture can't be captured in this mode (HDR?)");
        return;
    }
    s->started = Clock::now();
    s->startedText = LocalTime("%Y-%m-%d %H:%M:%S");
    s->overlay = RuntimeConfigFile::CaptureOverlay() ? "with the menus" : "DEV stamp only";
    s->whereStart = no100f_devmenu::WhereText();
    s->settings = SettingsText();
    s->audio = RuntimeConfigFile::RecordAudio();
    if (s->audio) {
        std::FILE* wav = _wfopen(WithSuffix(base, L".audio.wav").c_str(), L"wb");
        if (wav) {
            WriteWavHeader(wav, 2, 32000, 0);  // placeholder, rewritten at the end
            {
                std::lock_guard<std::mutex> lock(g_audioMutex);
                g_audio = AudioTrack{};
                g_audio.file = wav;
            }
            AudioBackend::Instance().SetCapture(&CaptureAudio);
        } else {
            s->audio = false;
        }
    }
    RT_LOG(RT_TAG_RUNTIME) << "recording " << Utf8(WithSuffix(base, L".mp4")) << " (" << s->overlay
                           << (s->audio ? ", with sound" : "") << ") with " << Utf8(s->ffmpeg) << std::endl;
    g_saving = true;
    ResetEvent(g_idle);
    std::thread(Writer, s).detach();
    g_writerSession = s;
    g_session = *s;
    g_recording = true;
    g_recordingSince = s->started;
}

void Stop(const char* reason) {
    aurora_set_video_capture(nullptr, nullptr, 0);  // no frame callback after this returns
    if (g_session->audio) AudioBackend::Instance().SetCapture(nullptr);
    std::shared_ptr<Session> s = std::move(g_writerSession);
    // The writer reads these only after the queue is closed below.
    s->stopReason = reason;
    s->seconds = std::chrono::duration<double>(Clock::now() - g_session->started).count();
    s->whereEnd = no100f_devmenu::WhereText();
    g_session.reset();
    g_recording = false;
    {
        std::lock_guard<std::mutex> lock(g_video.mutex);
        g_video.closing = true;
    }
    g_video.cv.notify_all();
    Notice("Saving the video...");
}

} // namespace

void Toggle() noexcept {
    std::lock_guard<std::mutex> lock(g_sessionMutex);
    if (g_session) {
        Stop("F3");
    } else {
        Start();
    }
}

void Update() noexcept {
    std::lock_guard<std::mutex> lock(g_sessionMutex);
    if (!g_session) return;
    const auto elapsed = Clock::now() - g_session->started;
    if (g_writerFailed) {
        Stop("ffmpeg, which stopped taking frames (see the log)");
    } else if (elapsed > kMaxLength) {
        Stop("the 30 minute limit");
    } else if (elapsed > kFirstFrameTimeout && !g_videoStarted) {
        Stop("no frames from the renderer");
    }
}

bool Recording() noexcept { return g_recording; }

// The camcorder light: a blinking red dot, REC and the time in the top-right corner, in the window aurora never
// captures. Grey "SAVING" while the last video is written.
void DrawIndicator() noexcept {
    const bool recording = g_recording;
    if (!recording && !g_saving) return;
    using namespace OverlayTheme;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                                        ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (ImGui::Begin(AURORA_CAPTURE_HIDDEN_WINDOW, nullptr, kFlags)) {
        ImDrawList* list = ImGui::GetWindowDrawList();
        list->PushClipRectFullScreen();
        ImFont* font = Heading();
        char text[32];
        if (recording) {
            const auto secs = std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - g_recordingSince).count();
            std::snprintf(text, sizeof text, "REC %02lld:%02lld", static_cast<long long>(secs / 60),
                          static_cast<long long>(secs % 60));
        } else {
            std::snprintf(text, sizeof text, "SAVING");
        }
        const ImVec2 size = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, text);
        const float radius = font->FontSize * 0.28f;
        const float margin = Px(14);
        const ImVec2 textPos(viewport->Pos.x + viewport->Size.x - margin - size.x, viewport->Pos.y + margin);
        const ImVec2 dot(textPos.x - Px(8) - radius, textPos.y + size.y * 0.5f);
        const bool lit = !recording || (std::chrono::duration_cast<std::chrono::milliseconds>(
                                            Clock::now().time_since_epoch()).count() / 500) % 2 == 0;
        const ImU32 colour = recording ? IM_COL32(230, 30, 30, 255) : IM_COL32(170, 170, 170, 255);
        if (lit) {
            list->AddCircleFilled(dot, radius + Px(2), IM_COL32(0, 0, 0, 140));
            list->AddCircleFilled(dot, radius, colour);
        }
        OutlinedText(list, font, textPos, recording ? IM_COL32(255, 255, 255, 255) : colour, Col(Colour::Ink), Px(2),
                     text);
        list->PopClipRect();
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void FinishForExit() noexcept {
    {
        std::lock_guard<std::mutex> lock(g_sessionMutex);
        if (g_session) Stop("closing the game");
    }
    WaitForSingleObject(g_idle, 10 * 60 * 1000);
}

#else

void Toggle() noexcept { Notice("Video recording (F3) is Windows-only for now"); }
void Update() noexcept {}
bool Recording() noexcept { return false; }
void DrawIndicator() noexcept {}
void FinishForExit() noexcept {}

#endif

} // namespace no100f_recorder
