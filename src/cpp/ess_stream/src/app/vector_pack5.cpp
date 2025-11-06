#include "vector_pack5.h"

namespace essentia {
namespace streaming {

const char* VectorPack5::name = "VectorPack5";
const char* VectorPack5::category = "Streaming";
const char* VectorPack5::description = "Pack 5 scalar inputs into a vector<Real> of length 5";

VectorPack5::VectorPack5() : Algorithm() {
  declareInput(_in0, "in0", "scalar input 0");
  declareInput(_in1, "in1", "scalar input 1");
  declareInput(_in2, "in2", "scalar input 2");
  declareInput(_in3, "in3", "scalar input 3");
  declareInput(_in4, "in4", "scalar input 4");
  declareOutput(_out, "out", "vector output");
  _in0.setAcquireSize(1); _in0.setReleaseSize(1);
  _in1.setAcquireSize(1); _in1.setReleaseSize(1);
  _in2.setAcquireSize(1); _in2.setReleaseSize(1);
  _in3.setAcquireSize(1); _in3.setReleaseSize(1);
  _in4.setAcquireSize(1); _in4.setReleaseSize(1);
  _out.setAcquireSize(1); _out.setReleaseSize(1);
}

AlgorithmStatus VectorPack5::process() {
  AlgorithmStatus status = acquireData();
  if (status != OK) return status;

  // For Sink<Real>, tokens() returns std::vector<Real>&
  const std::vector<Real>& v0 = _in0.tokens();
  const std::vector<Real>& v1 = _in1.tokens();
  const std::vector<Real>& v2 = _in2.tokens();
  const std::vector<Real>& v3 = _in3.tokens();
  const std::vector<Real>& v4 = _in4.tokens();
  
  // For Source<std::vector<Real>>, tokens() returns std::vector<std::vector<Real>>&
  std::vector<std::vector<Real>>& outTokens = _out.tokens();
  if (outTokens.size() < 1) outTokens.resize(1);
  
  outTokens[0].assign(5, 0.0f);
  outTokens[0][0] = v0.empty() ? 0.0f : v0[0];
  outTokens[0][1] = v1.empty() ? 0.0f : v1[0];
  outTokens[0][2] = v2.empty() ? 0.0f : v2[0];
  outTokens[0][3] = v3.empty() ? 0.0f : v3[0];
  outTokens[0][4] = v4.empty() ? 0.0f : v4[0];

  releaseData();
  return OK;
}

} // namespace streaming
} // namespace essentia


