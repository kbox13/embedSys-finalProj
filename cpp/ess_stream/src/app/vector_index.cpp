#include "vector_index.h"
#include <algorithm>

namespace essentia {
namespace streaming {

const char* VectorIndex::name = "VectorIndex";
const char* VectorIndex::category = "Streaming";
const char* VectorIndex::description =
  "Extract a single Real from a vector<Real> by index.";

VectorIndex::VectorIndex() : Algorithm() {
  declareInput (_in,  "in",  "vector input");
  declareOutput(_out, "out", "scalar output");
  _in.setAcquireSize(1);
  _out.setAcquireSize(1);
  _in.setReleaseSize(1);
  _out.setReleaseSize(1);
}

void VectorIndex::configure() {
  _index = parameter("index").toInt();
}

void VectorIndex::reset() {
  Algorithm::reset();
}

AlgorithmStatus VectorIndex::process() {
  AlgorithmStatus status = acquireData();
  if (status != OK) return status;

  const std::vector<std::vector<Real>>& inTokens = _in.tokens();
  std::vector<Real>& out = _out.tokens();
  
  Real val = 0.0f;
  if (!inTokens.empty()) {
    const std::vector<Real>& v = inTokens[0];
    if (!v.empty()) {
      size_t idx = static_cast<size_t>(std::max(0, _index));
      if (idx < v.size()) val = v[idx];
    }
  }
  
  if (out.size() < 1) out.resize(1);
  out[0] = val;

  releaseData();
  return OK;
}

} // namespace streaming
} // namespace essentia


