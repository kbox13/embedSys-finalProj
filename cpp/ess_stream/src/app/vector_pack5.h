#ifndef ESSENTIA_STREAMING_VECTOR_PACK5_H
#define ESSENTIA_STREAMING_VECTOR_PACK5_H

#include "streamingalgorithm.h"

namespace essentia {
namespace streaming {

class VectorPack5 : public Algorithm {
public:
  VectorPack5();

  void declareParameters() {}
  void configure() {}
  void reset() { Algorithm::reset(); }
  AlgorithmStatus process();

  static const char* name;
  static const char* category;
  static const char* description;

private:
  Sink<Real> _in0;
  Sink<Real> _in1;
  Sink<Real> _in2;
  Sink<Real> _in3;
  Sink<Real> _in4;
  Source<std::vector<Real>> _out;
};

} // namespace streaming
} // namespace essentia

#endif // ESSENTIA_STREAMING_VECTOR_PACK5_H


