#ifndef ESSENTIA_STREAMING_INSTRUMENT_SUM_H
#define ESSENTIA_STREAMING_INSTRUMENT_SUM_H

/*
 * InstrumentSum (streaming)
 * - Aggregates mel band energies into instrument-specific sums using
 *   multi-lobe Hann-weighted masks (Kick, Snare, Clap, CHat, OHatCrash)
 * - Masks are defined in Hz ranges and projected onto the mel filterbank
 * - Input:  TOKEN std::vector<Real> (mel band energies)
 * - Output: TOKEN std::vector<Real> (size 5: Kick, Snare, Clap, CHat, OHatCrash)
 */

#include "streamingalgorithm.h"
#include <essentia/algorithmfactory.h>
#include <vector>

namespace essentia {
namespace streaming {

class InstrumentSum : public Algorithm {
public:
  InstrumentSum();

  void declareParameters() {
    declareParameter("sampleRate", "Audio sample rate (Hz)", "[8000,192000]", 44100);
    declareParameter("nyquist", "Nyquist frequency (Hz), default sampleRate/2", "[4000,96000]", 0);
    declareParameter("expectedBands", "Expected mel band count (0=auto)", "[0,4096]", 0);
    declareParameter("lobeRolloff", "Edge rolloff fraction for Hann windows (0.05..0.5)", "[0.0,1.0]", 0.15);
  }

  void configure();
  AlgorithmStatus process();
  void reset();

  static const char* name;
  static const char* category;
  static const char* description;

private:
  // IO
  Sink<std::vector<Real>>   _in;
  Source<std::vector<Real>> _out;

  // Params
  Real _sampleRate{44100.0f};
  Real _nyquist{22050.0f};
  int  _expectedBands{0};
  Real _lobeRolloff{0.15f};

  // Derived
  size_t _numBands{0};
  bool _weightsReady{false};

  // Weight matrix: instruments x bands
  // Order: 0 Kick, 1 Snare, 2 Clap, 3 CHat, 4 OHatCrash
  std::vector<std::vector<Real>> _weights;

  // Helpers
  static Real hzToMel(Real hz);
  static Real melToHz(Real mel);
  void ensureWeights(size_t numBands);
  void buildDefaultMasks(size_t numBands,
                         std::vector<std::vector<Real>>& weights) const;
  static void addHannLobe(std::vector<Real>& dest,
                          const std::vector<Real>& bandCentersHz,
                          Real f1, Real f2, Real weight, Real rolloffFrac);
  static void normalize(std::vector<Real>& v);
};

} // namespace streaming
} // namespace essentia

#endif // ESSENTIA_STREAMING_INSTRUMENT_SUM_H


