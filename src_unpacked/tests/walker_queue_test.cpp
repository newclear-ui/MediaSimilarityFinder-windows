// Node D3-Minimal regression: bounded walker queue.
//
// Unit section drives WalkerQueue directly (deterministic, no timing
// dependence except bounded deadline polls): capacity, FIFO order,
// block/resume across threads, cancel escape, shutdown escape.
// Integration section runs real scans: a small-capacity ScanWorker run
// (completion, maxDepth bound, parity) and a direct engine scan with
// background pause toggles (pause transparency + parity).
#include "walker_queue.h"
#include "mainwindow.h"
#include "scan_worker.h"
#include "media_search_engine.h"
#include "index_manager.h"
#include <sqlite3.h>
#include <QCoreApplication>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
namespace {
int failures = 0;
void check(bool ok, const char* name) {
  if (ok) return;
  std::cerr << "fail: " << name << "\n";
  ++failures;
}
bool waitUntil(const std::function<bool()>& cond, int timeoutMs = 10000) {
  const auto t0 = std::chrono::steady_clock::now();
  while (!cond()) {
    if (std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0)
            .count() > timeoutMs)
      return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return true;
}
msf::FileState fileState(const std::string& path) {
  msf::FileState f;
  f.path = path;
  return f;
}
// Minimal 8x8 24-bit BMP. Identical files hash identically -> distance 0.
void bmp(const std::filesystem::path& p) {
  std::ofstream f(p, std::ios::binary);
  const int w = 8, h = 8, row = w * 3, img = row * h, fs = 54 + img;
  unsigned char hd[54] = {0};
  hd[0] = 'B'; hd[1] = 'M';
  hd[2] = (unsigned char)(fs & 0xFF); hd[3] = (unsigned char)((fs >> 8) & 0xFF);
  hd[10] = 54; hd[14] = 40; hd[18] = (unsigned char)w; hd[22] = (unsigned char)h;
  hd[26] = 1; hd[28] = 24;
  hd[34] = (unsigned char)(img & 0xFF); hd[35] = (unsigned char)((img >> 8) & 0xFF);
  f.write((const char*)hd, 54);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const unsigned char v = (x >= 4) ? 255 : 0;
      f.put((char)v); f.put((char)v); f.put((char)v);
    }
}
std::uint64_t jsonUint(const std::string& js, const char* key) {
  const std::string k = std::string("\"") + key + "\":";
  const auto pos = js.find(k);
  if (pos == std::string::npos) return 0;
  return (std::uint64_t)std::strtoull(js.c_str() + pos + k.size(), nullptr, 10);
}
bool jsonHas(const std::string& js, const char* s) { return js.find(s) != std::string::npos; }
} // namespace
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  // A. capacity + FIFO order, single thread.
  {
    msf::WalkerQueue q(4);
    check(q.capacity() == 4, "unit-capacity");
    check(q.empty() && q.size() == 0, "unit-empty");
    msf::FileState out;
    check(!q.tryPop(out), "unit-pop-empty");
    check(!q.waitForData(0), "unit-wait-empty");
    for (int i = 0; i < 4; ++i) {
      const auto pr = q.push(fileState("f" + std::to_string(i)), nullptr);
      check(pr == msf::WalkerQueue::PushResult::Pushed, "unit-push");
    }
    check(q.size() == 4, "unit-full");
    check(q.waitForData(0), "unit-wait-data");
    for (int i = 0; i < 4; ++i) {
      msf::FileState got;
      check(q.tryPop(got), "unit-pop");
      check(got.path == "f" + std::to_string(i), "unit-fifo");
    }
    check(q.empty(), "unit-drained");
  }
  // B. block/resume across threads (consumer starts only after the
  // producer is observed blocked: deterministic by construction).
  {
    msf::WalkerQueue q(2);
    std::atomic_bool cancel{false};
    std::vector<std::string> pushed;
    std::thread prod([&]() {
      for (int i = 0; i < 5; ++i) {
        const auto pr = q.push(fileState("g" + std::to_string(i)), &cancel);
        if (pr != msf::WalkerQueue::PushResult::Pushed) return;
        pushed.push_back("g" + std::to_string(i));
      }
    });
    check(waitUntil([&]() { return q.blockedTicks() > 0; }), "unit-blocked");
    std::vector<std::string> popped;
    for (int i = 0; i < 5; ++i) {
      check(waitUntil([&]() { return !q.empty(); }), "unit-data-arrives");
      msf::FileState got;
      check(q.tryPop(got), "unit-pop2");
      popped.push_back(got.path);
    }
    prod.join();
    check(pushed.size() == 5 && popped.size() == 5, "unit-all-moved");
    check(popped == pushed, "unit-order-kept");
  }
  // C. cancel escape: a blocked push returns immediately without taking.
  {
    msf::WalkerQueue q(1);
    std::atomic_bool cancel{false};
    check(q.push(fileState("a"), &cancel) == msf::WalkerQueue::PushResult::Pushed, "unit-fill");
    cancel.store(true);
    check(q.push(fileState("b"), &cancel) == msf::WalkerQueue::PushResult::Cancelled, "unit-cancel");
    check(q.size() == 1, "unit-cancel-untaken");
    cancel.store(false);
    q.shutdown();
    check(q.push(fileState("c"), &cancel) == msf::WalkerQueue::PushResult::Shutdown, "unit-shutdown");
    msf::FileState got;
    check(q.tryPop(got) && got.path == "a", "unit-post-shutdown-pop");
  }
  namespace fs = std::filesystem;
  auto d = fs::temp_directory_path() / "msf_walker_test";
  std::error_code ec;
  fs::remove_all(d, ec);
  fs::create_directories(d / "media", ec);
  fs::create_directories(d / "appdir", ec);
  for (int i = 0; i < 60; ++i) bmp(d / "media" / ("dup" + std::to_string(i) + ".bmp"));
  const std::string root = (d / "media").string(), ad = (d / "appdir").string();
  // D. integration: small-capacity worker scan completes, stays bounded,
  // and streams every pair (60 identical -> 60*59/2).
  {
    ScanWorker w(QString::fromStdString(root), QString::fromStdString(ad), 8, 50, 50, false, true,
                 false);
    w.setWalkerQueueCapacity(16);
    w.run();
    const auto pending = w.takePending();
    check(pending.size() == 60 * 59 / 2, "int-pairs");
    const std::string bj = w.scanEngine().telemetryJson();
    check(jsonHas(bj, "\"capacity\":16"), "int-capacity");
    const std::uint64_t maxDepth = jsonUint(bj, "maxDepth");
    check(maxDepth <= 16, "int-bounded");
    check(jsonHas(bj, "\"blockedTicks\":"), "int-blocked-key");
    check(jsonHas(bj, "\"scheduler\":{\"state\":\"measured\""), "int-scheduler");
  }
  // E. pause transparency: background pause toggles during a direct engine
  // scan (fresh data) must still complete with the full pair set.
  {
    auto d2 = fs::temp_directory_path() / "msf_walker_test2";
    fs::remove_all(d2, ec);
    fs::create_directories(d2 / "media", ec);
    fs::create_directories(d2 / "appdir", ec);
    for (int i = 0; i < 12; ++i) bmp(d2 / "media" / ("p" + std::to_string(i) + ".bmp"));
    msf::MediaSearchEngine e2;
    if (!e2.openIndexForRoot((d2 / "media").string(), (d2 / "appdir").string())) {
      std::cerr << "fail: engine-open\n";
      return 2;
    }
    msf::ScanControl c;
    std::size_t pairs = 0;
    c.onMatch = [&](const msf::SearchMatch&) { ++pairs; };
    std::atomic_bool stop{false};
    std::thread toggler([&]() {
      for (int i = 0; i < 3 && !stop.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        c.pause.store(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        c.pause.store(false);
      }
    });
    const msf::SearchReport r = e2.scan((d2 / "media").string(), 8, &c);
    stop.store(true);
    toggler.join();
    e2.close();
    check(r.completed, "pause-completed");
    check(pairs == 12 * 11 / 2, "pause-parity");
    fs::remove_all(d2, ec);
  }
  // F. Hybrid (AB) live parity (0.9.4.129 regression pin). With a streaming
  // consumer the AB final pass skips both-changed image pairs because the live
  // pass owns them, so a fresh AB scan must still surface the full pair set
  // through onMatch. Before the fix the live emitter rejected every match
  // (right index arrived one past the pipeline's file list), so this scan
  // returned zero pairs while A/B modes returned the full set -- the field
  // bug behind "similar groups 0 / duplicate files 0" in Auto mode.
  {
    auto d3 = fs::temp_directory_path() / "msf_walker_test3";
    fs::remove_all(d3, ec);
    fs::create_directories(d3 / "media", ec);
    fs::create_directories(d3 / "appdir", ec);
    for (int i = 0; i < 12; ++i) bmp(d3 / "media" / ("h" + std::to_string(i) + ".bmp"));
    msf::MediaSearchEngine e3;
    if (!e3.openIndexForRoot((d3 / "media").string(), (d3 / "appdir").string())) {
      std::cerr << "fail: engine-open\n";
      return 2;
    }
    msf::ScanControl c3;
    c3.analyzeMode = msf::AnalyzeMode::Hybrid;
    std::size_t pairs = 0;
    c3.onMatch = [&](const msf::SearchMatch&) { ++pairs; };
    const msf::SearchReport r = e3.scan((d3 / "media").string(), 8, &c3);
    e3.close();
    check(r.completed, "ab-completed");
    check(pairs == 12 * 11 / 2, "ab-live-parity");
    // The report mirrors the streamed set: st.groups counts the (empty) final
    // pass, so the live-owned pairs must be added back or the log/panel drop
    // to 0 even though the GUI shows the matches.
    check(r.groups == 12 * 11 / 2, "ab-report-pairs");
    check(r.imgPairs == 12 * 11 / 2, "ab-img-pairs");
    check(r.imgGroups == 1, "ab-img-groups");
    check(r.imgDupFiles == 12, "ab-img-dup-files");
    fs::remove_all(d3, ec);
  }
  // G. Completeness-baseline self-heal (0.9.4.130). An index written by
  // 0.9.4.81-0.9.4.129 holds no persisted live pairs while every file is
  // already unchanged; A/AB used to compute nothing and display an empty set
  // forever (the field "similar groups 0" after upgrading). The engine must
  // run one full-coverage pass when the baseline marker is absent, stamp it
  // afterwards, and not repeat the pass on the next unchanged scan.
  {
    auto d4 = fs::temp_directory_path() / "msf_walker_test4";
    fs::remove_all(d4, ec);
    fs::create_directories(d4 / "media", ec);
    fs::create_directories(d4 / "appdir", ec);
    for (int i = 0; i < 12; ++i) bmp(d4 / "media" / ("g" + std::to_string(i) + ".bmp"));
    const std::string root4 = (d4 / "media").string(), ad4 = (d4 / "appdir").string();
    msf::IndexPaths paths;
    check(msf::IndexManager::resolve(ad4, root4, paths), "heal-resolve");
    const std::string dbPath4 = (paths.directory / "index.sqlite").string();
    {
      msf::MediaSearchEngine e4;
      check(e4.openIndexForRoot(root4, ad4), "heal-open");
      const msf::SearchReport r1 = e4.scan(root4, 8, nullptr);
      check(r1.groups == 12 * 11 / 2, "heal-seed-pairs");
      e4.close();
    }
    // Simulate the bug-era index: stored pairs and the completeness baseline
    // are gone while engine_version stays current and all files stay indexed,
    // exactly like an index whose scans ran the broken live path.
    {
      sqlite3* db = nullptr;
      check(sqlite3_open(dbPath4.c_str(), &db) == SQLITE_OK, "heal-db-open");
      char* err = nullptr;
      check(sqlite3_exec(db, "DELETE FROM matches; DELETE FROM meta WHERE key='match_baseline';",
                         nullptr, nullptr, &err) == SQLITE_OK, "heal-db-wipe");
      sqlite3_free(err);
      sqlite3_close(db);
    }
    {
      msf::MediaSearchEngine e5;
      check(e5.openIndexForRoot(root4, ad4), "heal-open2");
      msf::ScanControl c5;
      c5.analyzeMode = msf::AnalyzeMode::Hybrid;
      std::size_t pairs = 0;
      c5.onMatch = [&](const msf::SearchMatch&) { ++pairs; };
      const msf::SearchReport r2 = e5.scan(root4, 8, &c5);
      e5.close();
      check(r2.analyzed == 0 && r2.unchanged == 12, "heal-unchanged");
      check(pairs == 12 * 11 / 2, "heal-fullpass-pairs");
      check(r2.groups == 12 * 11 / 2, "heal-report-pairs");
    }
    {
      sqlite3* db = nullptr;
      check(sqlite3_open(dbPath4.c_str(), &db) == SQLITE_OK, "heal-db-open2");
      bool stamped = false;
      sqlite3_stmt* st = nullptr;
      if (sqlite3_prepare_v2(db, "SELECT value FROM meta WHERE key='match_baseline'", -1, &st,
                             nullptr) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) {
          const char* t = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
          stamped = t && std::string(t) == msf::MediaSearchEngine::kEngineVersion;
        }
        sqlite3_finalize(st);
      }
      sqlite3_close(db);
      check(stamped, "heal-baseline-stamped");
    }
    {
      msf::MediaSearchEngine e6;
      check(e6.openIndexForRoot(root4, ad4), "heal-open3");
      msf::ScanControl c6;
      c6.analyzeMode = msf::AnalyzeMode::Hybrid;
      std::size_t pairs = 0;
      c6.onMatch = [&](const msf::SearchMatch&) { ++pairs; };
      const msf::SearchReport r3 = e6.scan(root4, 8, &c6);
      e6.close();
      check(r3.analyzed == 0 && r3.unchanged == 12, "heal-second-unchanged");
      check(pairs == 0, "heal-one-time");
    }
    fs::remove_all(d4, ec);
  }
  // H. Displayed result-set telemetry (0.9.4.131). Case D's Sequential scan
  // left a complete stored set and the 0.9.4.130 baseline. A Hybrid rescan over
  // unchanged files recomputes NOTHING (targeted pass, no changed files) yet
  // the list shows the stored 1770 pairs; the record must describe that
  // displayed set instead of "pairs 0, groups 0" (the field complaint this
  // build answers). Quick-load delivery is pinned by takePending.
  {
    const std::string rootH = (d / "media").string(), adH = (d / "appdir").string();
    ScanWorker w2(QString::fromStdString(rootH), QString::fromStdString(adH), 8, 50, 50, false, true,
                  false);
    w2.setAnalyzeMode(msf::AnalyzeMode::Hybrid);
    w2.run();
    const auto pending2 = w2.takePending();
    check(pending2.size() == 60 * 59 / 2, "display-pending-full");
    const std::string tj = w2.scanEngine().telemetryJson();
    check(jsonUint(tj, "pairs") == 60 * 59 / 2, "display-telemetry-pairs");
    check(jsonUint(tj, "groups") == 1, "display-telemetry-groups");
    check(jsonUint(tj, "imagePairs") == 60 * 59 / 2, "display-telemetry-img-pairs");
    check(jsonUint(tj, "imageGroups") == 1, "display-telemetry-img-groups");
    check(jsonUint(tj, "imageDuplicateFiles") == 60, "display-telemetry-img-dup");
    check(jsonUint(tj, "videoPairs") == 0, "display-telemetry-vid-pairs");
  }
  fs::remove_all(d, ec);
  if (failures) {
    std::cerr << "walker_queue failures=" << failures << "\n";
    return 1;
  }
  std::cout << "walker_queue=ok\n";
  return 0;
}
