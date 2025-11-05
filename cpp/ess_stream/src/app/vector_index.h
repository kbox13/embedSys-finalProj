#ifndef ESSENTIA_STREAMING_VECTOR_INDEX_H
#define ESSENTIA_STREAMING_VECTOR_INDEX_H

#include "streamingalgorithm.h"
#include <vector>

namespace essentia {
namespace streaming {

class VectorIndex : public Algorithm {
public:
  VectorIndex();

  void declareParameters() {
    declareParameter("index", "Index to extract from vector", "[0,inf)", 0);
  }

  void configure();
  AlgorithmStatus process();
  void reset();

  static const char* name;
  static const char* category;
  static const char* description;

private:
  Sink<std::vector<Real>> _in;
  Source<Real>            _out;

  int _index{0};
};

} // namespace streaming
} // namespace essentia

#endif // ESSENTIA_STREAMING_VECTOR_INDEX_H


