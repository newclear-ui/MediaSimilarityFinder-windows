#include "dataset_fingerprint.h"
#include "path_utils.h"
#include <fstream>
#include <iostream>
#include <string>

// D8a reporting tool. Prints the identity of a prepared dataset root so the
// value recorded in docs/build-history can be reproduced on any checkout, and
// writes <root>/dataset_fingerprint.json for side-by-side comparison with a
// scan's benchmark JSON.
//
// It does not create the dataset; scripts/prepare_dataset.ps1 does that.
// Keeping generation and reporting apart means the fingerprint can be
// re-verified against an existing fixture without regenerating it.
//
// Usage: msf_dataset_report <dataset-root>

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: msf_dataset_report <dataset-root>\n";
    return 2;
  }
  const std::string root = argv[1];
  const msf::DatasetFingerprint fp = msf::computeDatasetFingerprint(root);

  std::cout << "dataset_root=" << root << "\n"
            << "dataset_state=" << fp.state << "\n"
            << "dataset_fingerprint=" << (fp.fingerprint.empty() ? "(none)" : fp.fingerprint) << "\n"
            << "dataset_fingerprint_version=" << msf::kDatasetFingerprintVersion << "\n"
            << "dataset_file_count=" << fp.fileCount << "\n"
            << "dataset_total_bytes=" << fp.totalBytes << "\n";

  if (fp.state != "measured") {
    std::cerr << "dataset_fingerprint=failed state=" << fp.state << "\n";
    return 1;
  }

  // Written as a SIBLING of the dataset, never inside it. A file placed
  // under the root would join the next manifest walk and change the
  // fingerprint it is supposed to describe ??a self-reference that would
  // silently break reproducibility on the very next run.
  const msf::fs::path rootPath = msf::path_from_utf8(root);
  const msf::fs::path outPath =
      rootPath.parent_path() / (rootPath.filename().string() + ".fingerprint.json");
  std::ofstream f(outPath, std::ios::binary);
  if (!f) {
    std::cerr << "dataset_fingerprint=write_failed path=" << msf::path_to_utf8(outPath) << "\n";
    return 1;
  }
  f << "{\n  \"fingerprint\": \"" << fp.fingerprint << "\",\n"
    << "  \"fingerprintVersion\": " << msf::kDatasetFingerprintVersion << ",\n"
    << "  \"fileCount\": " << fp.fileCount << ",\n"
    << "  \"totalBytes\": " << fp.totalBytes << ",\n"
    << "  \"state\": \"" << fp.state << "\"\n}\n";
  f.close();
  std::cout << "dataset_fingerprint_file=" << msf::path_to_utf8(outPath) << "\n";
  return 0;
}
