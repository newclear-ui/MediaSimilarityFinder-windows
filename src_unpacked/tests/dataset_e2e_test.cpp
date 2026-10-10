#include "media_search_engine.h"
#include "benchmark.h"
#include "dataset_fingerprint.h"
#include "path_utils.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

// D8a end-to-end check: a real engine scan over a prepared dataset must
// record the dataset identity in its benchmark JSON, and the recorded value
// must equal what the standalone reporter computes for the same root.
//
// This is the property every later comparison depends on: if the scan cannot
// say which data produced it, no A/B run is meaningful.
//
// The fixture is built in-process from the same deterministic generator as
// scripts/prepare_dataset.ps1, so CTest needs no committed assets and no
// external encoder. Passing an explicit root on the command line uses that
// root instead, which is how the real test_sample_img_vid asset is checked.

namespace {
constexpr int kW = 8, kH = 8;
void writeBmp(const std::filesystem::path& p, int seed) {
  const int row = kW * 3, img = row * kH, fileSize = 54 + img;
  std::ofstream f(p, std::ios::binary);
  unsigned char hd[54] = {0};
  hd[0] = 'B'; hd[1] = 'M';
  hd[2] = (unsigned char)(fileSize & 0xFF); hd[3] = (unsigned char)((fileSize >> 8) & 0xFF);
  hd[10] = 54; hd[14] = 40;
  hd[18] = (unsigned char)kW; hd[22] = (unsigned char)kH;
  hd[26] = 1; hd[28] = 24;
  hd[34] = (unsigned char)(img & 0xFF); hd[35] = (unsigned char)((img >> 8) & 0xFF);
  f.write((const char*)hd, 54);
  for (int y = 0; y < kH; ++y)
    for (int x = 0; x < kW; ++x) {
      const unsigned char v = (unsigned char)(((x * 2 + y * 3 + seed) * 7) % 256);
      f.put((char)v); f.put((char)v); f.put((char)v);
    }
}
// Same composition as scripts/prepare_dataset.ps1, so both roots describe the
// same dataset and their fingerprints must match.
void buildFixture(const std::filesystem::path& root) {
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "images" / "exact", ec);
  std::filesystem::create_directories(root / "images" / "varied", ec);
  for (int g = 0; g < 12; ++g) {
    const int seed = 17 + g * 13;
    for (int m = 0; m < 4; ++m) {
      char name[64];
      std::snprintf(name, sizeof(name), "dup%02d_%02d.bmp", g, m);
      writeBmp(root / "images" / "exact" / name, seed);
    }
  }
  for (int i = 0; i < 12; ++i) {
    char name[64];
    std::snprintf(name, sizeof(name), "v%02d.bmp", i);
    writeBmp(root / "images" / "varied" / name, 101 + i * 29);
  }
}
}  // namespace

int main(int argc, char** argv) {
  namespace fs = std::filesystem;
  const bool external = argc >= 2;
  const auto base = fs::temp_directory_path() / "msf_dataset_e2e";
  const fs::path mediaDir = external ? fs::path(msf::path_from_utf8(argv[1])) : (base / "media");
  const fs::path appDir = external ? fs::path(msf::path_from_utf8(argv[2])) : (base / "app");
  if (!external) {
    buildFixture(mediaDir);
    std::error_code ec;
    fs::create_directories(appDir, ec);
  }
  const std::string root = msf::path_to_utf8(mediaDir);
  const std::string app = msf::path_to_utf8(appDir);

  const msf::DatasetFingerprint expected = msf::computeDatasetFingerprint(root);
  if (expected.state != "measured") {
    std::cerr << "expected a measured dataset, got " << expected.state << "\n";
    return 3;
  }

  msf::MediaSearchEngine engine;
  if (!engine.openIndexForRoot(root, app)) {
    std::cerr << "openIndexForRoot failed\n";
    return 4;
  }
  msf::ScanControl control;
  control.buildVersion = "0.9.4.21";
  const msf::SearchReport report = engine.scan(root, 8, &control);
  const msf::DatasetFingerprint& recorded = engine.lastTelemetryDataset();

  int rc = 0;
  if (recorded.state != "measured") {
    std::cerr << "scan did not record a measured dataset: " << recorded.state << "\n";
    rc = 5;
  }
  if (recorded.fingerprint != expected.fingerprint) {
    std::cerr << "fingerprint mismatch\n  expected " << expected.fingerprint
              << "\n  recorded " << recorded.fingerprint << "\n";
    rc = 6;
  }
  if (recorded.fileCount != expected.fileCount || recorded.totalBytes != expected.totalBytes) {
    std::cerr << "count/byte mismatch: expected " << expected.fileCount << "/"
              << expected.totalBytes << " recorded " << recorded.fileCount << "/"
              << recorded.totalBytes << "\n";
    rc = 7;
  }
  // The scan must still have done its real work; a fingerprint recorded while
  // the scan silently did nothing would prove nothing.
  if (report.scanned == 0) {
    std::cerr << "scan walked no files, so this proves nothing\n";
    rc = 8;
  }
  // 0.9.4.86: a second scan of the unchanged dataset must reuse the cached
  // fingerprint instead of re-reading every file. bytesRead==0 on the second
  // run proves the full-content re-read was skipped, and the identity must be
  // unchanged.
  {
    const msf::SearchReport report2 = engine.scan(root, 8, &control);
    const msf::DatasetFingerprint& recorded2 = engine.lastTelemetryDataset();
    if (recorded2.state != "measured" || recorded2.fingerprint != expected.fingerprint) {
      std::cerr << "second scan fingerprint mismatch (state=" << recorded2.state << ")\n";
      rc = 9;
    }
    if (recorded2.bytesRead != 0) {
      std::cerr << "second scan re-read the dataset (bytesRead=" << recorded2.bytesRead
                << "), expected a cache hit\n";
      rc = 9;
    }
    if (report2.scanned == 0) {
      std::cerr << "second scan walked no files\n";
      rc = 9;
    }
  }
  std::cout << "dataset_e2e_fingerprint=" << recorded.fingerprint
            << " files=" << recorded.fileCount
            << " bytes=" << recorded.totalBytes
            << " state=" << recorded.state
            << " scanned=" << report.scanned
            << " analyzed=" << report.analyzed
            << " groups=" << report.groups << "\n";

  if (!external) {
    std::error_code ec;
    fs::remove_all(base, ec);
  }
  return rc;
}
