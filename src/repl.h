// ============================================================
// REPL (Read-Eval-Print Loop) — jsengine 開発・AI 操作用
//
// 二系統のチャネル:
//   - console REPL (`-repl` 指定で有効): stdin から行入力、 結果を stdout に出力。
//     対話デバッグ用。
//   - file channel REPL (`-replfile <dir>` 指定で有効): <dir>/cmd を監視し、
//     UTF-8 の JS を読み取って評価、 結果を <dir>/resp に JSON で書く。
//     AI / 外部エージェントが console (CONIN$) を介さずに駆動する用途。
//
// どちらも内部的にはワーカースレッドが式を受け取り、 メインスレッドの drain で
// JS_Eval を呼ぶ。 QuickJS のランタイムはマルチスレッド非対応なので、 評価は
// 必ずメインスレッドで行う。
//
// ビルド時 `JSENGINE_USE_REPL` (CMake オプション `JSENGINE_USE_REPL`、 デフォルト ON)
// が定義されていないと、 ここで宣言される関数は実体を持たない。 アプリ側は
// `#ifdef JSENGINE_USE_REPL` で guard する。
// ============================================================
#pragma once

#ifdef JSENGINE_USE_REPL

class JsEngine;

namespace JsRepl {

// 起動時引数を見て -repl / -replfile <dir> を内部にラッチする。 init() より前に呼ぶ。
void parseArgs(int argc, char *argv[]);

// 解析した結果 console / file チャネルを起動したいかどうか。
bool wantsConsole();
bool wantsFile();

// チャネルワーカーを起動する。 wantsConsole/File が両方 false ならば何もしない。
// engine は JsEngine の生存期間 ≧ REPL の生存期間という前提でポインタ保持される。
void create(JsEngine *engine);

// 全ワーカーに shutdown を要求し、 join する。 終了時に呼ぶ。
void destroy();

// メインスレッドからフレーム毎に呼ぶ。 ワーカーから提出された 1 件のリクエストを
// JS_Eval して、 結果をレスポンスに詰めて wake する。 提出が無ければ即 return。
void drain();

} // namespace JsRepl

#endif // JSENGINE_USE_REPL
