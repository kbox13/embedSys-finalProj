/*
 * SumRange implementation (standard & streaming)
 * Modeled after Essentia's Trimmer example code patterns.
 */

#include "sumrange.h"
#include "essentiamath.h"
#include <essentia/algorithmfactory.h>

using namespace std;

namespace essentia {
namespace standard {

const char* SumRange::name = "SumRange";
const char* SumRange::category = "Standard";
const char* SumRange::description =
  DOC("Sums elements in [lo, hi] of a vector<Real> and outputs a scalar Real.\n"
      "Bounds are clamped to [0, size-1]. If hi < lo after clamping, the sum is 0.");

void SumRange::configure() {
  _lo = parameter("lo").toInt();
  _hi = parameter("hi").toInt();
}

void SumRange::compute() {
  const vector<Real>& v = _in.get();
  Real& out = _out.get();

  if (v.empty()) { out = 0.0; return; }

  int lo = std::max(0, _lo);
  int hi = std::min<int>((int)v.size() - 1, _hi);

  Real sum = 0.0;
  if (hi >= lo) {
    for (int i = lo; i <= hi; ++i) sum += v[i];
  }
  out = sum;
}

} // namespace standard
} // namespace essentia


namespace essentia {
namespace streaming {

const char* SumRange::name = essentia::standard::SumRange::name;
const char* SumRange::category = "Streaming";
const char* SumRange::description =
  DOC("Streaming version of SumRange: for each input vector token, outputs one scalar sum.\n"
      "Bounds are clamped per token size.");

void SumRange::configure() {
  _lo = parameter("lo").toInt();
  _hi = parameter("hi").toInt();

  // Ensure we always acquire/release 1 token for both ports.
  _in.setAcquireSize(1);
  _in.setReleaseSize(1);
  _out.setAcquireSize(1);
  _out.setReleaseSize(1);
}

AlgorithmStatus SumRange::process() {
  // Try to acquire one token from input and one slot in output.
  AlgorithmStatus status = acquireData();
  if (status != OK) {
    // If no input is present or output is full, return and let scheduler retry.
    return status;
  }

  // tokens() on Sink<std::vector<Real>> returns std::vector< std::vector<Real> >&
  const std::vector<std::vector<Real>>& inTokens = _in.tokens();
  std::vector<Real>& outTokens = _out.tokens();

  // We declared size 1, so operate on index 0.
  const std::vector<Real>& v = inTokens[0];
  Real sum = 0.0;

  if (!v.empty()) {
    const int n = static_cast<int>(v.size());
    const int lo = std::max(0, _lo);
    const int hi = std::min(std::max(0, n - 1), _hi);
    if (hi >= lo) {
      for (int i = lo; i <= hi; ++i) sum += v[i];
    }
  }

  outTokens[0] = sum;

  // We produced exactly one output token corresponding to one input token.
  _in.setReleaseSize(1);
  _out.setReleaseSize(1);

  releaseData();
  return OK;
}

void SumRange::reset() {
  Algorithm::reset();
  // Maintain one-token I/O policy after reset.
  _in.setAcquireSize(1);
  _in.setReleaseSize(1);
  _out.setAcquireSize(1);
  _out.setReleaseSize(1);
}

} // namespace streaming
} // namespace essentia
