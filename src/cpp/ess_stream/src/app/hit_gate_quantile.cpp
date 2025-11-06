
#include "hit_gate_quantile.h"
#include <essentia/algorithmfactory.h>

namespace essentia {
namespace streaming {

const char* HitGateQuantile::name = "HitGateQuantile";
const char* HitGateQuantile::category = "Streaming";
const char* HitGateQuantile::description =
  "Gate that emits 1 when a novelty signal crosses quantile-based thresholds.\n"
  "Arms when input > q_hi; fires on next drop below q_lo, with refractory period.";

HitGateQuantile::HitGateQuantile() : Algorithm() {
  declareInput (_in,  "in",  "novelty (scalar per frame)");
  declareOutput(_out, "out", "hit (scalar; 0 or 1)");
  // TOKEN mode: one token per call
  _in.setAcquireSize(1);
  _out.setAcquireSize(1);
  _in.setReleaseSize(1);
  _out.setReleaseSize(1);
}

void HitGateQuantile::configure() {
  _qhi = parameter("q_hi").toReal();
  _qlo = parameter("q_lo").toReal();
  _R   = parameter("refractory").toInt();
  _warm= parameter("warmup").toInt();
  reset();
}

void HitGateQuantile::reset() {
  Algorithm::reset();
  _armed = false;
  _ref   = 0;
  _seen  = 0;
  _seed.clear();
  _p2_hi.init = _p2_lo.init = false;
  _p2_hi.q = _qhi; _p2_lo.q = _qlo;
}

AlgorithmStatus HitGateQuantile::process() {
  AlgorithmStatus status = acquireData();
  if (status != OK) return status;

  const std::vector<Real>& inBuf = _in.tokens();
  std::vector<Real>& outBuf = _out.tokens();

  const Real xi = inBuf[0];
  ++_seen;

  // Update online quantiles
  p2Update(_p2_hi, xi);
  p2Update(_p2_lo, xi);

  if (_ref > 0) --_ref;
  Real hit = 0.f;

  if (_seen > _warm && _p2_hi.init && _p2_lo.init) {
    const Real Thi = (Real)p2Value(_p2_hi);
    const Real Tlo = (Real)p2Value(_p2_lo);

    if (!_armed && _ref==0 && xi > Thi) _armed = true;
    if (_armed && xi < Tlo) {
      hit   = 1.f;
      _armed= false;
      _ref  = _R;
    }
  }

  outBuf[0] = hit;

  releaseData();
  return OK;
}

// ---- P² quantile estimator helpers ----
void HitGateQuantile::p2InitFromFive(P2& p, const std::vector<double>& s) {
  std::vector<double> a = s; std::sort(a.begin(), a.end());
  for (int i=0;i<5;++i){ p.m[i]=a[i]; p.n[i]=i+1; }
  p.np[0]=1; p.np[1]=1+2*p.q; p.np[2]=1+4*p.q; p.np[3]=1+6*p.q; p.np[4]=5;
  p.dn[0]=0; p.dn[1]=p.q/2; p.dn[2]=p.q; p.dn[3]=(1+p.q)/2; p.dn[4]=1;
  p.init=true;
}

double HitGateQuantile::parabolic(const P2& p, int i) {
  double a = (p.n[i]-p.n[i-1]+(p.n[i+1]-p.n[i])) *
             ((p.m[i+1]-p.m[i])/(p.n[i+1]-p.n[i]) -
              (p.m[i]-p.m[i-1])/(p.n[i]-p.n[i-1]));
  return p.m[i] + a / (p.n[i+1]-p.n[i-1]);
}

double HitGateQuantile::linear(const P2& p, int i, int di) {
  return p.m[i] + di*(p.m[i+di]-p.m[i])/(p.n[i+di]-p.n[i]);
}

void HitGateQuantile::p2Update(P2& p, double x) {
  if (!p.init) {
    _seed.push_back(x);
    if ((int)_seed.size()==5) { p2InitFromFive(p, _seed); _seed.clear(); }
    return;
  }
  int k;
  if (x < p.m[0]) { p.m[0]=x; k=0; }
  else if (x >= p.m[4]) { p.m[4]=x; k=3; }
  else { for (k=0;k<4;++k) if (x < p.m[k+1]) break; }
  for (int i=0;i<5;++i) p.n[i] += (i<=k ? 1 : 0);
  for (int i=0;i<5;++i) p.np[i] += p.dn[i];
  for (int i=1;i<=3;++i) {
    double d = p.np[i] - p.n[i];
    if ((d>=1 && p.n[i+1]-p.n[i]>1) || (d<=-1 && p.n[i]-p.n[i-1]>1)) {
      int di = d>=1 ? 1 : -1;
      double mPar = parabolic(p,i);
      double bounded = (mPar > p.m[i-1] && mPar < p.m[i+1]) ? mPar : linear(p,i,di);
      p.m[i] = bounded;
      p.n[i] += di;
    }
  }
}

} // namespace streaming
} // namespace essentia
