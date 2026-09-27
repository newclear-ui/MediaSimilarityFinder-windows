// Benchmark JSON schema compatibility probe.
//
// The question this answers is narrow and was previously assumed: when the
// analyze block gained new fields (D9c), can a consumer written before those
// fields existed still read the record correctly, or does the new field set
// break it?
//
// It matters because kBenchmarkSchemaVersion stayed at 9. Leaving it there is
// only correct if the schema really is additive, and "JSON ignores unknown
// fields" is not good enough to record as a decision.
//
// What the codebase actually does, which this test pins down rather than
// assumes:
//
//  * The only structured reader of benchmark JSON is
//    MainWindow::showBenchmarkDialog (gui/mainwindow.cpp), which uses
//    QJsonDocument::fromJson and then does key lookups: root["analyze"] style
//    access via toObject(), and scalar access via toDouble()/toBool().
//  * Nothing iterates the object's keys, compares a field count, or requires
//    any particular field order.
//  * No code validates schemaVersion on the read path at all. Writers and tests
//    compare it, but a reader never branches on it.
//
// That combination is additive by construction. This test proves it end to end
// instead of reasoning about it: it builds a real record, parses it exactly the
// way the GUI does, and checks that every field a pre-D9c consumer read still
// reads to the same value, that the new fields coexist, and that a record
// carrying the new fields parses identically to one that does not.
#include "analyze_telemetry.h"
#include "benchmark.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <cstdint>
#include <iostream>
#include <string>

namespace {
int checks = 0, failures = 0;
void expect(bool ok, const char* what) {
  ++checks;
  if (!ok) { ++failures; std::cerr << "FAIL: " << what << "\n"; }
}

// Exactly the access pattern MainWindow::showBenchmarkDialog uses.
QJsonObject parseLikeTheGui(const std::string& json, bool* parsedOk) {
  QJsonParseError perr;
  const QJsonDocument doc = QJsonDocument::fromJson(
      QString::fromStdString(json).toUtf8(), &perr);
  *parsedOk = (perr.error == QJsonParseError::NoError) && doc.isObject();
  return doc.isObject() ? doc.object() : QJsonObject();
}

// The keys the GUI reader touches, with the values a pre-D9c build produced.
// The GUI does not read the analyze block today, so a second, deliberately
// "old-style" consumer is modelled over the analyze object: the D9a fields,
// which is what an analyze-aware consumer from before D9c would have used.
void readPreD9cAnalyzeFields(const QJsonObject& analyze, double* v) {
  v[0] = analyze["indexMs"].toDouble();
  v[1] = analyze["scanMs"].toDouble();
  v[2] = analyze["verifyMs"].toDouble();
  v[3] = analyze["videoMs"].toDouble();
  v[4] = analyze["verifyCalls"].toDouble();
  v[5] = analyze["verifyDecodeMisses"].toDouble();
  v[6] = analyze["verifyCacheHits"].toDouble();
  v[7] = analyze["ssimEvals"].toDouble();
  v[8] = analyze["frameSsimEvals"].toDouble();
  v[9] = analyze["videoTemporalPairs"].toDouble();
}
}  // namespace

int main() {
  msf::BenchmarkRecorder rec;
  msf::AnalyzeTelemetry tel;
  // D9a fields.
  tel.analyzeRan = true;
  tel.indexMs = 3.9;
  tel.scanMs = 358.7;
  tel.verifyMs = 117614.6;
  tel.videoMs = 0.0;
  tel.verifyCalls = 158020;
  tel.verifyDecodeMisses = 12949;
  tel.verifyCacheHits = 14519;
  tel.ssimEvals = 137340;
  tel.frameSsimEvals = 274680;
  tel.videoTemporalPairs = 0;
  // D9c fields: the additions under test.
  tel.verifyKeyMs = 4421.9;
  tel.verifyDecodeMs = 111621.7;
  tel.verifyCacheStoreMs = 42.9;
  tel.verifyCacheCopyMs = 11.0;
  tel.verifyCropMs = 201.9;
  tel.verifyFlipMs = 188.7;
  tel.verifyFrameSsimMs = 645.2;
  tel.verifyOtherMs = 481.4;
  tel.verifyBufferLookups = 27468;
  tel.verifyQuickHashReads = 27468;
  tel.verifyQuickHashBytes = 380999408ULL;
  tel.verifyDecodes = 25898;
  tel.verifyCacheCopies = 14519;
  tel.verifyCropCalls = 109872;
  tel.verifyFlipCalls = 137340;

  rec.setAnalyzeTelemetry(tel);
  rec.addImageStageMs(120.0);
  rec.addWalkMs(1400.0);
  rec.addStreamedMatch();
  rec.finalize(true, 100, 55, 45, 200, 12, 6, 75.5, 18, 2);
  const std::string js = rec.toJson();

  bool parsed = false;
  const QJsonObject root = parseLikeTheGui(js, &parsed);
  expect(parsed, "record with the D9c fields parses with QJsonDocument");

  // The schema constant the writers advertise. It lives under "meta", alongside
  // build/engine/db and the dataset identity block -- not at the document root
  // and not under "config".
  const QJsonObject meta = root["meta"].toObject();
  expect(meta["schemaVersion"].toInt() == msf::BenchmarkRecorder::kBenchmarkSchemaVersion,
         "meta.schemaVersion equals the recorder constant");
  expect(meta["schemaVersion"].toInt() == 9,
         "meta.schemaVersion is still 9 (additive fields did not force a bump)");

  // --- the GUI's own fields, unchanged by the new ones -------------------
  const QJsonObject sum = root["summary"].toObject();
  const QJsonObject mat = root["matches"].toObject();
  expect(meta["completed"].toBool(), "meta.completed still reads true");
  expect(sum["scanned"].toDouble() == 100, "summary.scanned still reads 100");
  expect(sum["analyzed"].toDouble() == 55, "summary.analyzed still reads 55");
  expect(sum["walkMs"].toDouble() == 1400.0, "summary.walkMs still reads 1400");
  expect(mat["candidates"].toDouble() == 200, "matches.candidates still reads 200");

  // --- a pre-D9c analyze-aware consumer ----------------------------------
  const QJsonObject analyze = root["analyze"].toObject();
  expect(analyze.isEmpty() == false, "analyze object is present");
  double v[10] = {0};
  readPreD9cAnalyzeFields(analyze, v);
  expect(v[0] == 3.9, "D9a indexMs readable and unchanged");
  expect(v[1] == 358.7, "D9a scanMs readable and unchanged");
  expect(v[2] == 117614.6, "D9a verifyMs readable and unchanged");
  expect(v[3] == 0.0, "D9a videoMs readable and unchanged");
  expect(v[4] == 158020, "D9a verifyCalls readable and unchanged");
  expect(v[5] == 12949, "D9a verifyDecodeMisses readable and unchanged");
  expect(v[6] == 14519, "D9a verifyCacheHits readable and unchanged");
  expect(v[7] == 137340, "D9a ssimEvals readable and unchanged");
  expect(v[8] == 274680, "D9a frameSsimEvals readable and unchanged");
  expect(v[9] == 0.0, "D9a videoTemporalPairs readable and unchanged");

  // --- the new fields coexist -------------------------------------------
  expect(analyze["verifyKeyMs"].toDouble() == 4421.9, "D9c verifyKeyMs present");
  expect(analyze["verifyDecodeMs"].toDouble() == 111621.7, "D9c verifyDecodeMs present");
  expect(analyze["verifyFrameSsimMs"].toDouble() == 645.2, "D9c verifyFrameSsimMs present");
  expect(analyze["verifyOtherMs"].toDouble() == 481.4, "D9c verifyOtherMs present");
  expect(analyze["verifyQuickHashBytes"].toDouble() == 380999408.0,
         "D9c verifyQuickHashBytes present as a number");
  // Per-field state strings, part of the "measured vs not measured" contract.
  expect(analyze["verifyKeyMsState"].toString() == "measured",
         "D9c per-field state is published alongside the value");

  // --- the decisive comparison -------------------------------------------
  // A record whose analyze block carries ONLY the pre-D9c fields must be read
  // identically by the same access code. If adding fields changed any read, or
  // if any consumer required an exact field set, this would diverge.
  msf::BenchmarkRecorder oldRec;
  msf::AnalyzeTelemetry oldTel;
  oldTel.analyzeRan = true;
  oldTel.indexMs = tel.indexMs;
  oldTel.scanMs = tel.scanMs;
  oldTel.verifyMs = tel.verifyMs;
  oldTel.videoMs = tel.videoMs;
  oldTel.verifyCalls = tel.verifyCalls;
  oldTel.verifyDecodeMisses = tel.verifyDecodeMisses;
  oldTel.verifyCacheHits = tel.verifyCacheHits;
  oldTel.ssimEvals = tel.ssimEvals;
  oldTel.frameSsimEvals = tel.frameSsimEvals;
  oldTel.videoTemporalPairs = tel.videoTemporalPairs;
  // Every D9c field left at its default: this is the shape a v9-era writer
  // produced.
  oldRec.setAnalyzeTelemetry(oldTel);
  oldRec.addImageStageMs(120.0);
  oldRec.addWalkMs(1400.0);
  oldRec.addStreamedMatch();
  oldRec.finalize(true, 100, 55, 45, 200, 12, 6, 75.5, 18, 2);

  bool oldParsed = false;
  const QJsonObject oldRoot = parseLikeTheGui(oldRec.toJson(), &oldParsed);
  expect(oldParsed, "pre-D9c-shaped record also parses");
  const QJsonObject oldAnalyze = oldRoot["analyze"].toObject();
  double ov[10] = {0};
  readPreD9cAnalyzeFields(oldAnalyze, ov);
  bool same = true;
  for (int i = 0; i < 10; ++i) if (v[i] != ov[i]) same = false;
  expect(same, "every pre-D9c field reads identically with and without the new fields");

  // A consumer that knows nothing about the new keys must not fail. This is
  // the property the old reader relies on.
  expect(oldAnalyze["verifyKeyMs"].toDouble() == 0.0,
         "an unknown-to-the-consumer key degrades to 0, not an error");
  expect(oldRoot["summary"].toObject()["analyzeMs"].toDouble()
             == root["summary"].toObject()["analyzeMs"].toDouble(),
         "summary.analyzeMs is unaffected by the analyze block's field set");

  if (failures) { std::cout << "benchmark_schema=failed " << failures << "\n"; return 1; }
  std::cout << "benchmark_schema=ok checks=" << checks
            << " verdict=additive-read-confirmed\n";
  return 0;
}
