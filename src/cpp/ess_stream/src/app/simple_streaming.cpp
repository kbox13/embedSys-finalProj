// Simple streaming pipeline that works with available algorithms
// This demonstrates a working Essentia streaming pipeline

#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>

#include <essentia/essentia.h>
#include <essentia/algorithmfactory.h>
#include <essentia/streaming/algorithms/poolstorage.h>
#include <essentia/streaming/algorithms/vectorinput.h>
#include <essentia/scheduler/network.h>

using namespace std;
using namespace essentia;
using namespace essentia::streaming;
using namespace essentia::scheduler;

static std::atomic<bool> g_running{true};

int main(int argc, char* argv[]) {
  if (argc != 2) {
    cerr << "Usage: " << argv[0] << " output.yaml\n";
    return 1;
  }
  string outputFilename = argv[1];

  // Initialize Essentia
  essentia::init();

  // Parameters
  const Real sampleRate = 44100.0;
  const int frameSize = 2048;
  const int hopSize = 1024;

  // Pool to collect features
  Pool pool;

  // Create a simple streaming pipeline
  AlgorithmFactory& F = streaming::AlgorithmFactory::instance();

  // Create algorithms
  Algorithm* fc   = F.create("FrameCutter",
                             "frameSize", frameSize,
                             "hopSize",   hopSize,
                             "silentFrames", "noise");

  Algorithm* win  = F.create("Windowing", "type", "blackmanharris62");
  Algorithm* spec = F.create("Spectrum");
  Algorithm* mfcc = F.create("MFCC");

  // Create a simple data source (sine wave for testing)
  vector<Real> testData(frameSize * 10); // 10 frames of test data
  for (int i = 0; i < testData.size(); ++i) {
    testData[i] = sin(2.0 * M_PI * 440.0 * i / sampleRate); // 440 Hz sine wave
  }

  // Create VectorInput with our test data
  VectorInput<Real>* src = new VectorInput<Real>(&testData);

  // Wire the pipeline: src -> fc -> win -> spec -> mfcc -> pool
  src->output("data")      >> fc->input("signal");
  fc->output("frame")      >> win->input("frame");
  win->output("frame")     >> spec->input("frame");
  spec->output("spectrum")  >> mfcc->input("spectrum");
  mfcc->output("bands")     >> NOWHERE;
  mfcc->output("mfcc")     >> PC(pool, "lowlevel.mfcc");

  // Create network
  Network net(src);

  cerr << "Running streaming pipeline with test data..." << endl;

  // Run the network
  net.run();

  cerr << "Pipeline completed. Processing results..." << endl;

  // Aggregate & write YAML
  Pool aggrPool;
  const char* stats[] = {"mean","var","min","max","cov","icov"};
  standard::Algorithm* aggr = standard::AlgorithmFactory::create("PoolAggregator",
                                  "defaultStats", arrayToVector<string>(stats));
  aggr->input("input").set(pool);
  aggr->output("output").set(aggrPool);
  aggr->compute();

  aggrPool.merge("lowlevel.mfcc.frames",
                 pool.value<vector<vector<Real>>>("lowlevel.mfcc"));

  standard::Algorithm* output = standard::AlgorithmFactory::create("YamlOutput",
                                  "filename", outputFilename);
  output->input("pool").set(aggrPool);
  output->compute();

  // Cleanup
  net.clear();
  delete aggr;
  delete output;
  delete src;
  essentia::shutdown();

  cerr << "Wrote " << outputFilename << endl;
  return 0;
}
