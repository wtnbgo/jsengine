// ============================================================
// REPL 実装 (console + file channel)
// ============================================================
#ifdef JSENGINE_USE_REPL

#include "repl.h"
#include "jsengine.hpp"

#include <SDL3/SDL.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

// ============================================================
// 引数ラッチ
// ============================================================
static bool g_arg_console = false;
static bool g_arg_file = false;
static std::string g_arg_file_dir;
static JsEngine *g_engine = nullptr;

namespace JsRepl {

void parseArgs(int argc, char *argv[]) {
    g_arg_console = false;
    g_arg_file = false;
    g_arg_file_dir.clear();
    for (int i = 1; i < argc; i++) {
        // -repl: 単独で有効。 -repl=off などの否定指定は受け付けない (krkrz と簡略化)。
        if (SDL_strcmp(argv[i], "-repl") == 0) {
            g_arg_console = true;
        } else if (SDL_strcmp(argv[i], "-replfile") == 0 && i + 1 < argc) {
            g_arg_file = true;
            g_arg_file_dir = argv[i + 1];
        }
    }
}

bool wantsConsole() { return g_arg_console; }
bool wantsFile() { return g_arg_file && !g_arg_file_dir.empty(); }

} // namespace JsRepl

// ============================================================
// メインスレッド実行キュー
//   ワーカー (console / file) は 1 件の式をここに submit して、 結果が
//   返るまで待つ。 メインスレッドは drain で 1 件処理する。
// ============================================================
namespace {

std::mutex g_submit_mtx;     // 同時 in-flight を 1 件に制限

std::mutex g_req_mtx;
std::string g_req_script;
bool g_req_pending = false;

std::mutex g_resp_mtx;
std::condition_variable g_resp_cv;
bool g_resp_ok = false;
std::string g_resp_text;
bool g_resp_ready = false;

std::atomic<bool> g_terminating{false};

// ワーカーから呼ぶ: スクリプトを 1 件 submit してレスポンスを待つ。
// 戻り値 ok=true なら text に評価結果、 false ならエラーメッセージ。
// 戻り値の戻り型は (bool ok, std::string text)。
struct ReplResult { bool ok; std::string text; };

ReplResult repl_submit_and_wait(const std::string &script) {
    std::lock_guard<std::mutex> submit_lk(g_submit_mtx);
    if (g_terminating.load(std::memory_order_acquire)) return {false, "terminating"};

    {
        std::lock_guard<std::mutex> lk(g_req_mtx);
        g_req_script = script;
        g_req_pending = true;
    }

    std::unique_lock<std::mutex> lk(g_resp_mtx);
    g_resp_cv.wait(lk, []{
        return g_resp_ready || g_terminating.load(std::memory_order_acquire);
    });
    if (!g_resp_ready) return {false, "terminating"};

    ReplResult out{g_resp_ok, std::move(g_resp_text)};
    g_resp_ready = false;
    g_resp_ok = false;
    g_resp_text.clear();
    return out;
}

void repl_shutdown_queue() {
    g_terminating.store(true, std::memory_order_release);
    g_resp_cv.notify_all();
}

void repl_reset_queue() {
    g_terminating.store(false, std::memory_order_release);
    std::lock_guard<std::mutex> lk1(g_req_mtx);
    std::lock_guard<std::mutex> lk2(g_resp_mtx);
    g_req_pending = false;
    g_req_script.clear();
    g_resp_ready = false;
    g_resp_ok = false;
    g_resp_text.clear();
}

} // anonymous

// ============================================================
// console REPL ワーカー (stdin 行入力)
// ============================================================
namespace {

class ConsoleReplThread {
public:
    ConsoleReplThread() : terminating_(false) {
        thread_ = std::thread([this]{ this->run(); });
    }
    ~ConsoleReplThread() {
        terminating_.store(true, std::memory_order_release);
        // stdin は OS 終了で kick されるのを期待。 join しないと未定義動作の恐れが
        // あるが、 stdin で fgets ブロック中の thread を綺麗に止める手段は portable
        // に存在しないので detach する (プロセス終了時に OS が回収する)。
        if (thread_.joinable()) thread_.detach();
    }

private:
    std::atomic<bool> terminating_;
    std::thread thread_;

    // 改行 / カッコ釣り合いの単純検査で「式が完結したか」を判定する。
    // 文字列 / コメントの中も簡易に追跡。
    static bool isCompleteStatement(const std::string &script) {
        int paren = 0, brace = 0, bracket = 0;
        bool in_s = false, in_d = false, in_t = false;  // ', ", `
        bool in_line = false, in_block = false;
        size_t n = script.size();
        for (size_t i = 0; i < n; i++) {
            char c = script[i];
            char nx = (i + 1 < n) ? script[i + 1] : 0;
            if (in_line) { if (c == '\n') in_line = false; continue; }
            if (in_block) { if (c == '*' && nx == '/') { in_block = false; i++; } continue; }
            if (in_s) { if (c == '\\' && nx) { i++; continue; } if (c == '\'') in_s = false; continue; }
            if (in_d) { if (c == '\\' && nx) { i++; continue; } if (c == '"') in_d = false; continue; }
            if (in_t) { if (c == '\\' && nx) { i++; continue; } if (c == '`') in_t = false; continue; }
            if (c == '/' && nx == '/') { in_line = true; i++; continue; }
            if (c == '/' && nx == '*') { in_block = true; i++; continue; }
            if (c == '\'') { in_s = true; continue; }
            if (c == '"')  { in_d = true; continue; }
            if (c == '`')  { in_t = true; continue; }
            if (c == '(') paren++;
            else if (c == ')') paren--;
            else if (c == '{') brace++;
            else if (c == '}') brace--;
            else if (c == '[') bracket++;
            else if (c == ']') bracket--;
        }
        if (in_s || in_d || in_t || in_block) return false;
        if (paren > 0 || brace > 0 || bracket > 0) return false;
        return true;
    }

    void run() {
        fprintf(stderr, "[jsengine REPL] type JS expressions; '.help' for commands\n");
        std::string accum;
        char buf[8192];
        while (!terminating_.load(std::memory_order_acquire)) {
            const char *prompt = accum.empty() ? "js> " : "  > ";
            fputs(prompt, stderr);
            fflush(stderr);
            if (!fgets(buf, sizeof(buf), stdin)) {
                // EOF / error: REPL を抜けるだけ。 アプリ本体は止めない。
                fprintf(stderr, "\n[jsengine REPL] stdin closed, exiting REPL\n");
                break;
            }
            size_t len = strlen(buf);
            while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) buf[--len] = 0;
            std::string line(buf, len);

            // メタコマンド (式単独行で 1 つだけ判定)
            if (accum.empty()) {
                if (line.empty()) continue;
                if (line == ".help") {
                    fputs(
                        "  .help           - this help\n"
                        "  .clear          - discard pending multiline input\n"
                        "  .quit / .exit   - exit jsengine\n"
                        "  any other line  - evaluated as JS (global scope)\n",
                        stderr);
                    continue;
                }
                if (line == ".clear") { accum.clear(); continue; }
                if (line == ".quit" || line == ".exit") {
                    // メイン側に終了を伝える: SDL_QuitEvent を投げる。
                    SDL_Event ev{};
                    ev.type = SDL_EVENT_QUIT;
                    SDL_PushEvent(&ev);
                    break;
                }
            }

            if (!accum.empty()) accum += "\n";
            accum += line;
            if (!isCompleteStatement(accum)) continue;

            ReplResult r = repl_submit_and_wait(accum);
            accum.clear();
            if (r.ok) {
                fprintf(stderr, "=> %s\n", r.text.c_str());
            } else {
                fprintf(stderr, "!! %s\n", r.text.c_str());
            }
        }
    }
};

ConsoleReplThread *g_console = nullptr;

} // anonymous

// ============================================================
// file channel ワーカー (cmd → resp protocol)
//
// プロトコル (<dir> 配下):
//   1. エージェント: コマンド (UTF-8 JS) を `cmd.tmp` に書き、 `cmd` に rename。
//   2. チャネル: `cmd` を検出 → 読取 → 削除 → メイン実行 → 結果 JSON を
//      `resp.tmp` に書き `resp` に rename。
//   3. エージェント: `resp` の出現を待ち、 読取 → 削除。 次コマンドへ。
//
// 結果 JSON: { "ok": bool, "result": "<text>", "error": "<msg>" }
// ============================================================
namespace {

static std::string json_escape(const std::string &s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (unsigned char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (c < 0x20) {
                char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c);
                out += b;
            } else {
                out += (char)c;
            }
        }
    }
    return out;
}

class FileChannelThread {
public:
    explicit FileChannelThread(const std::string &dir) : dir_(dir), terminating_(false) {
        thread_ = std::thread([this]{ this->run(); });
    }
    ~FileChannelThread() {
        terminating_.store(true, std::memory_order_release);
        if (thread_.joinable()) thread_.join();
    }

private:
    std::string dir_;
    std::atomic<bool> terminating_;
    std::thread thread_;

    void run() {
        std::error_code ec;
        fs::path d = fs::path(dir_);
        fs::create_directories(d, ec);
        const fs::path cmd_p     = d / "cmd";
        const fs::path resp_p    = d / "resp";
        const fs::path resp_tmp  = d / "resp.tmp";

        fprintf(stderr, "[jsengine REPL] file channel watching: %s\n", dir_.c_str());

        while (!terminating_.load(std::memory_order_acquire)) {
            bool has_cmd  = fs::exists(cmd_p, ec);
            bool has_resp = fs::exists(resp_p, ec);
            if (has_cmd && !has_resp) {
                // cmd 読込 + 削除
                std::string script;
                {
                    std::ifstream f(cmd_p, std::ios::binary);
                    if (f) {
                        script.assign(std::istreambuf_iterator<char>(f),
                                      std::istreambuf_iterator<char>());
                    }
                }
                fs::remove(cmd_p, ec);

                ReplResult r = repl_submit_and_wait(script);
                if (terminating_.load(std::memory_order_acquire)) break;

                std::string json = std::string("{\"ok\":") + (r.ok ? "true" : "false")
                    + ",\"result\":\"" + (r.ok ? json_escape(r.text) : std::string())
                    + "\",\"error\":\"" + (r.ok ? std::string() : json_escape(r.text))
                    + "\"}";

                // resp.tmp に書いて resp に rename
                {
                    std::ofstream f(resp_tmp, std::ios::binary | std::ios::trunc);
                    if (f) f.write(json.data(), (std::streamsize)json.size());
                }
                fs::remove(resp_p, ec);
                fs::rename(resp_tmp, resp_p, ec);
                if (ec) {
                    // fallback: 直接 resp に書く
                    std::ofstream f(resp_p, std::ios::binary | std::ios::trunc);
                    if (f) f.write(json.data(), (std::streamsize)json.size());
                    ec.clear();
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        fprintf(stderr, "[jsengine REPL] file channel stopped\n");
    }
};

FileChannelThread *g_file_ch = nullptr;

} // anonymous

// ============================================================
// API
// ============================================================
namespace JsRepl {

void create(JsEngine *engine) {
    if (!engine) return;
    if (!wantsConsole() && !wantsFile()) return;
    g_engine = engine;
    repl_reset_queue();
    if (wantsConsole() && !g_console) {
        g_console = new ConsoleReplThread();
    }
    if (wantsFile() && !g_file_ch) {
        g_file_ch = new FileChannelThread(g_arg_file_dir);
    }
}

void destroy() {
    repl_shutdown_queue();
    if (g_file_ch) { delete g_file_ch; g_file_ch = nullptr; }
    if (g_console) { delete g_console; g_console = nullptr; }
    g_engine = nullptr;
}

void drain() {
    if (!g_engine) return;
    std::string script;
    {
        std::lock_guard<std::mutex> lk(g_req_mtx);
        if (!g_req_pending) return;
        script.swap(g_req_script);
        g_req_pending = false;
    }
    std::string result_text;
    bool ok = g_engine->evalForRepl(script, result_text);
    {
        std::lock_guard<std::mutex> lk(g_resp_mtx);
        g_resp_ok = ok;
        g_resp_text = std::move(result_text);
        g_resp_ready = true;
    }
    g_resp_cv.notify_all();
}

} // namespace JsRepl

#endif // JSENGINE_USE_REPL
