
#ifndef ESSENTIA_STREAMING_HITGATEQUANTILE_H
#define ESSENTIA_STREAMING_HITGATEQUANTILE_H

/*
 * HitGateQuantile (streaming)
 * - Arms when input exceeds a high quantile (q_hi)
 * - Fires a 1.0 "hit" when the signal, while armed, falls below a low quantile (q_lo)
 * - Enforces a refractory period (in hops) after each hit
 * - Uses the P² online quantile estimator (5-marker method) per quantile
 *
 * Input:  TOKEN stream of Real (scalar) per frame (novelty)
 * Output: TOKEN stream of Real (scalar) per frame (0.0 or 1.0)
 *
 * This header/impl follows the Essentia streaming style as in Trimmer (streaming)
 * and keeps the original hit-gate functionality.
 */

#include "streamingalgorithm.h"
#include <vector>
#include <algorithm>
#include <cmath>

namespace essentia {
namespace streaming {

class HitGateQuantile : public Algorithm {
public:
  HitGateQuantile();

  void declareParameters() {
    declareParameter("q_hi", "High quantile (0..1) to arm", "(0,1)", 0.98);
    declareParameter("q_lo", "Low  quantile (0..1) to disarm", "(0,1)", 0.80);
    declareParameter("refractory", "Refractory in hops", "[0,inf)", 4);
    declareParameter("warmup",     "Frames before gating enabled", "[0,inf)", 100);
  }

  void configure();
  AlgorithmStatus process();
  void reset();

  static const char* name;
  static const char* category;
  static const char* description;

private:
  // TOKEN-style connectors for scalar values
  Sink<Real>   _in;
  Source<Real> _out;

  // params
  Real _qhi{0.98f}, _qlo{0.80f};
  int  _R{4}, _warm{200};

  // state
  bool _armed{false};
  int  _ref{0}, _seen{0};

  // ---- P² quantile estimator (5-marker) ----
  struct P2 {
    bool init=false; double q=0.5;
    double m[5]{}, n[5]{}, np[5]{}, dn[5]{};
  } _p2_hi, _p2_lo;
  std::vector<double> _seed; // shared bootstrap for first 5 samples per stream

  inline double clip(double v,double lo,double hi){return v<lo?lo:(v>hi?hi:v);}

  void p2InitFromFive(P2& p, const std::vector<double>& s);
  double parabolic(const P2& p, int i);
  double linear(const P2& p, int i, int di);
  void p2Update(P2& p, double x);
  double p2Value(const P2& p) const { return p.m[2]; } // marker 2 tracks target q
};

} // namespace streaming
} // namespace essentia

#endif // ESSENTIA_STREAMING_HITGATEQUANTILE_H
