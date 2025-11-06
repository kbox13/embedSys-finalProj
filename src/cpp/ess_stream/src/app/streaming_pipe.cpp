// Build on macOS (Homebrew): 
//   brew install essentia portaudio yaml-cpp
//   clang++ -std=c++17 live_blackhole_mfcc.cpp -o live_mfcc \
//     -I/usr/local/include -L/usr/local/lib \
//     -lessentia -lportaudio -lyaml-cpp
//
// Run:
//   ./live_mfcc output.yaml
//
// Make sure macOS Sound > Output is set to a device that routes to BlackHole
// (or use a Multi-Output so you can hear it) and that BlackHole is selected
// as the **Input** device for the app (PortAudio selects it by name below).

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
#include <future>

#include <portaudio.h>

#include <essentia/essentia.h>
#include <essentia/algorithmfactory.h>
#include <essentia/streaming/algorithms/poolstorage.h>
#include <essentia/streaming/algorithms/ringbufferinput.h>
#include <essentia/scheduler/network.h>
#include "hit_gate_onset.h"
#include "instrument_sum.h"
#include "vector_index.h"
#include "vector_pack5.h"
#include "hit_prediction_logger.h" // Include before instrument_predictor.h
#include "instrument_predictor.h"
#include "zeromq_publisher.h"
#include "gate_logger_sink.h"

using namespace std;
using namespace essentia;
using namespace essentia::streaming;
using namespace essentia::scheduler;

static std::atomic<bool> g_running{true};

// Simple lock-free circular buffer for float audio (single-producer PortAudio, single-consumer Essentia)
struct Ring
{
  explicit Ring(size_t cap) : buf(cap), cap(cap) {}
  vector<float> buf;
  size_t cap;
  atomic<size_t> head{0}, tail{0}; // head = write, tail = read

  // push up to n samples, returns how many written
  size_t push(const float *in, size_t n)
  {
    size_t written = 0;
    while (written < n)
    {
      size_t h = head.load(memory_order_relaxed);
      size_t t = tail.load(memory_order_acquire);
      size_t free = (t + cap - h - 1) % cap; // leave 1 slot to distinguish full/empty
      if (free == 0)
        break;
      size_t to = min(free, n - written);
      size_t idx = h % cap;
      size_t chunk = min(to, cap - idx);
      memcpy(&buf[idx], &in[written], chunk * sizeof(float));
      head.store((h + chunk) % cap, memory_order_release);
      written += chunk;
    }
    return written;
  }

  // pop exactly n samples if available; returns false if not enough data
  bool pop(float *out, size_t n)
  {
    size_t t = tail.load(memory_order_relaxed);
    size_t h = head.load(memory_order_acquire);
    size_t available = (h + cap - t) % cap;
    if (available < n)
      return false;
    size_t idx = t % cap;
    size_t chunk = min(n, cap - idx);
    memcpy(out, &buf[idx], chunk * sizeof(float));
    if (n > chunk)
      memcpy(out + chunk, &buf[0], (n - chunk) * sizeof(float));
    tail.store((t + n) % cap, memory_order_release);
    return true;
  }
};

// PortAudio stream callback: write input frames into Ring
static int paCallback(const void *input, void *, unsigned long frameCount,
                      const PaStreamCallbackTimeInfo *, PaStreamCallbackFlags, void *userData)
{
  auto *ring = reinterpret_cast<Ring *>(userData);
  if (!input)
    return paContinue;
  const float *in = static_cast<const float *>(input);
  size_t pushed = ring->push(in, frameCount);
  (void)pushed; // best-effort; drop if ring is full
  return g_running ? paContinue : paComplete;
}

static void ensurePa(PaError err, const char *where)
{
  if (err != paNoError)
  {
    cerr << where << " failed: " << Pa_GetErrorText(err) << endl;
    exit(2);
  }
}

// Find the first input device whose name contains "BlackHole"
static int findBlackHoleDevice()
{
  int num = Pa_GetDeviceCount();
  for (int i = 0; i < num; ++i)
  {
    const PaDeviceInfo *info = Pa_GetDeviceInfo(i);
    if (!info)
      continue;
    const PaHostApiInfo *api = Pa_GetHostApiInfo(info->hostApi);
    std::string name = info->name ? info->name : "";
    std::string apiName = api && api->name ? api->name : "";
    // Look for CoreAudio device named like "BlackHole 2ch"
    if (name.find("BlackHole") != std::string::npos && info->maxInputChannels > 0)
    {
      cerr << "Using input device: [" << i << "] " << name << " via " << apiName << endl;
      return i;
    }
  }
  return paNoDevice;
}

int main(int argc, char *argv[])
{
  if (argc < 2 || argc > 3)
  {
    cerr << "Usage: " << argv[0] << " output.yaml [timeout_seconds]\n";
    cerr << "  output.yaml: Output file path\n";
    cerr << "  timeout_seconds: Optional timeout in seconds (default: 20)\n";
    return 1;
  }
  string outputFilename = argv[1];

  // Parse optional timeout argument
  int timeoutSeconds = 20; // default
  if (argc == 3)
  {
    try
    {
      timeoutSeconds = std::stoi(argv[2]);
      if (timeoutSeconds <= 0)
      {
        cerr << "Error: timeout must be a positive integer\n";
        return 1;
      }
    }
    catch (const std::exception &e)
    {
      cerr << "Error: invalid timeout value '" << argv[2] << "': " << e.what() << "\n";
      return 1;
    }
  }

  // ---------- Essentia init ----------
  essentia::init();

  // Parameters (match your original example)
  const Real sampleRate = 44100.0;
  const int sampleRateInt = 44100;
  const int frameSize = 1024;
  const int hopSize = 256;

  // Create logger instance for hits and predictions
  HitPredictionLogger logger(sampleRate, hopSize, "logs");
  streaming::GateLoggerSink::register_logger(&logger); // Register for gate loggers to access

  // Pool to collect features
  Pool pool;

  // ---------- Build Essentia streaming graph ----------
  AlgorithmFactory& F = streaming::AlgorithmFactory::instance();

  // Manually register RingBufferInput since it's not in the factory
  AlgorithmFactory::Registrar<streaming::RingBufferInput> regRingBufferInput;

  // Register our custom HitGateOnset algorithm
  AlgorithmFactory::Registrar<streaming::HitGateOnset> regHitGateOnset;

  // Register instrument aggregator and helpers
  AlgorithmFactory::Registrar<streaming::InstrumentSum> regInstrumentSum;
  AlgorithmFactory::Registrar<streaming::VectorIndex> regVectorIndex;
  AlgorithmFactory::Registrar<streaming::VectorPack5> regVectorPack5;
  AlgorithmFactory::Registrar<streaming::InstrumentPredictor> regInstrumentPredictor;

  // Register our custom ZeroMQPublisher algorithm
  AlgorithmFactory::Registrar<streaming::ZeroMQPublisher> regZeroMQPublisher;

  // Register gate logger sink for hit/prediction logging
  AlgorithmFactory::Registrar<streaming::GateLoggerSink> regGateLoggerSink;

  // Create algorithms
  Algorithm* fc   = F.create("FrameCutter",
                             "frameSize", frameSize,
                             "hopSize",   hopSize,
                             "silentFrames", "noise");

  Algorithm* win  = F.create("Windowing", "type", "blackmanharris62");
  Algorithm* spec = F.create("Spectrum");
  Algorithm *melbands = F.create("MelBands",
                                 "numberBands", 64,
                                 "sampleRate", sampleRate);
  Algorithm *instr = F.create("InstrumentSum",
                              "sampleRate", sampleRateInt,
                              "expectedBands", 64,
                              "lobeRolloff", 0.15);
  // Create ZeroMQ publishers for each instrument gate
  Algorithm *kick_gate_publisher = F.create("ZeroMQPublisher",
                                            "endpoint", "tcp://localhost:5555",
                                            "feature_name", "gate.kick",
                                            "buffer_size", 1,
                                            "threshold", 0.5,
                                            "threshold_mode", "above");
  Algorithm *snare_gate_publisher = F.create("ZeroMQPublisher",
                                             "endpoint", "tcp://localhost:5555",
                                             "feature_name", "gate.snare",
                                             "buffer_size", 1,
                                             "threshold", 0.5,
                                             "threshold_mode", "above");
  Algorithm *clap_gate_publisher = F.create("ZeroMQPublisher",
                                            "endpoint", "tcp://localhost:5555",
                                            "feature_name", "gate.clap",
                                            "buffer_size", 1,
                                            "threshold", 0.5,
                                            "threshold_mode", "above");
  Algorithm *chat_gate_publisher = F.create("ZeroMQPublisher",
                                            "endpoint", "tcp://localhost:5555",
                                            "feature_name", "gate.chat",
                                            "buffer_size", 1,
                                            "threshold", 0.5,
                                            "threshold_mode", "above");
  Algorithm *ohc_gate_publisher = F.create("ZeroMQPublisher",
                                           "endpoint", "tcp://localhost:5555",
                                           "feature_name", "gate.ohc",
                                           "buffer_size", 1,
                                           "threshold", 0.5,
                                           "threshold_mode", "above");

  // Create RingBufferInput for real-time streaming
  Algorithm* src = F.create("RingBufferInput", "bufferSize", frameSize * 10);

  // Wire the pipeline: src -> fc -> win -> spec -> mfcc -> pool
  src->output("signal")    >> fc->input("signal");
  fc->output("frame")      >> win->input("frame");
  win->output("frame")     >> spec->input("frame");
  spec->output("spectrum") >> melbands->input("spectrum");
  // Instrument aggregation from mel bands
  melbands->output("bands") >> instr->input("in");

  // Extract per-instrument scalar sums (order: Kick, Snare, Clap, CHat, OHatCrash)
  Algorithm *idx_kick = F.create("VectorIndex", "index", 0);
  Algorithm *idx_snare = F.create("VectorIndex", "index", 1);
  Algorithm *idx_clap = F.create("VectorIndex", "index", 2);
  Algorithm *idx_chat = F.create("VectorIndex", "index", 3);
  Algorithm *idx_ohc = F.create("VectorIndex", "index", 4);

  instr->output("out") >> idx_kick->input("in");
  instr->output("out") >> idx_snare->input("in");
  instr->output("out") >> idx_clap->input("in");
  instr->output("out") >> idx_chat->input("in");
  instr->output("out") >> idx_ohc->input("in");

  idx_kick->output("out") >> PC(pool, "instrument.kick.sum");
  idx_snare->output("out") >> PC(pool, "instrument.snare.sum");
  idx_clap->output("out") >> PC(pool, "instrument.clap.sum");
  idx_chat->output("out") >> PC(pool, "instrument.chat.sum");
  idx_ohc->output("out") >> PC(pool, "instrument.ohc.sum");

  // Instrument gates (adaptive onset gating on instrument sums)
  // Kick: Increased threshold to 1.6 for more selectivity (fewer false positives)
  Algorithm *kick_gate = F.create("HitGateOnset",
                                  "method", "hfc",
                                  "threshold", 10,
                                  "refractory", 30,
                                  "warmup", 8,
                                  "sensitivity", 5,
                                  "smooth_window", 2,
                                  "odf_window", 64);
  Algorithm *snare_gate = F.create("HitGateOnset",
                                   "method", "flux",
                                   "threshold", 1.4,
                                   "refractory", 4,
                                   "warmup", 8,
                                   "sensitivity", 1.8,
                                   "smooth_window", 2,
                                   "odf_window", 64);
  Algorithm *clap_gate = F.create("HitGateOnset",
                                  "method", "flux",
                                  "threshold", 1.4,
                                  "refractory", 3,
                                  "warmup", 8,
                                  "sensitivity", 1.8,
                                  "smooth_window", 2,
                                  "odf_window", 48);
  Algorithm *chat_gate = F.create("HitGateOnset",
                                  "method", "hfc",
                                  "threshold", 1.6,
                                  "refractory", 3,
                                  "warmup", 8,
                                  "sensitivity", 1.6,
                                  "smooth_window", 2,
                                  "odf_window", 48);
  Algorithm *ohc_gate = F.create("HitGateOnset",
                                 "method", "hfc",
                                 "threshold", 1.5,
                                 "refractory", 4,
                                 "warmup", 8,
                                 "sensitivity", 1.6,
                                 "smooth_window", 2,
                                 "odf_window", 64);

  idx_kick->output("out") >> kick_gate->input("in");
  idx_snare->output("out") >> snare_gate->input("in");
  idx_clap->output("out") >> clap_gate->input("in");
  idx_chat->output("out") >> chat_gate->input("in");
  idx_ohc->output("out") >> ohc_gate->input("in");

  kick_gate->output("out") >> PC(pool, "gate.kick");
  snare_gate->output("out") >> PC(pool, "gate.snare");
  clap_gate->output("out") >> PC(pool, "gate.clap");
  chat_gate->output("out") >> PC(pool, "gate.chat");
  ohc_gate->output("out") >> PC(pool, "gate.ohc");

  kick_gate->output("out") >> kick_gate_publisher->input("in");
  // snare_gate->output("out") >> snare_gate_publisher->input("in");
  // clap_gate->output("out") >> clap_gate_publisher->input("in");
  // chat_gate->output("out") >> chat_gate_publisher->input("in");
  // ohc_gate->output("out") >> ohc_gate_publisher->input("in");

  // Create gate logger sinks for logging hits to file
  Algorithm *kick_gate_logger = F.create("GateLoggerSink", "instrument_index", 0);
  Algorithm *snare_gate_logger = F.create("GateLoggerSink", "instrument_index", 1);
  Algorithm *clap_gate_logger = F.create("GateLoggerSink", "instrument_index", 2);
  Algorithm *chat_gate_logger = F.create("GateLoggerSink", "instrument_index", 3);
  Algorithm *ohc_gate_logger = F.create("GateLoggerSink", "instrument_index", 4);

  // Wire gates to loggers (parallel to existing connections)
  kick_gate->output("out") >> kick_gate_logger->input("in");
  snare_gate->output("out") >> snare_gate_logger->input("in");
  clap_gate->output("out") >> clap_gate_logger->input("in");
  chat_gate->output("out") >> chat_gate_logger->input("in");
  ohc_gate->output("out") >> ohc_gate_logger->input("in");

  // Pack 5 gates into a single vector and publish once
  Algorithm *gate_pack = F.create("VectorPack5");
  kick_gate->output("out") >> gate_pack->input("in0");
  snare_gate->output("out") >> gate_pack->input("in1");
  clap_gate->output("out") >> gate_pack->input("in2");
  chat_gate->output("out") >> gate_pack->input("in3");
  ohc_gate->output("out") >> gate_pack->input("in4");

  // Instrument hit predictor (consumes gate vector, publishes predictions)
  Algorithm *predictor = F.create("InstrumentPredictor",
                                  "sampleRate", sampleRateInt,
                                  "hopSize", hopSize,
                                  "endpoint", "tcp://localhost:5556",
                                  "min_hits_for_seed", 8,
                                  "min_bpm", 60,
                                  "max_bpm", 200,
                                  "horizon_seconds", 2.0,
                                  "max_predictions_per_instrument", 2,
                                  "confidence_threshold_min", 0.3,
                                  "periodic_interval_sec", 0.15);
  gate_pack->output("out") >> predictor->input("in");
  predictor->output("out") >> PC(pool, "predictions");

  // Set logger in predictor for logging predictions
  static_cast<streaming::InstrumentPredictor *>(predictor)->set_logger(&logger);

  // Note: predictor publishes to ZMQ directly, no zmq output connection needed but needed to run graph

  // Create network
  Network net(src);

  // ---------- PortAudio setup for BlackHole ----------
  ensurePa(Pa_Initialize(), "Pa_Initialize");

  int dev = findBlackHoleDevice();
  if (dev == paNoDevice) {
    cerr << "Could not find a 'BlackHole' input device. Is BlackHole installed and enabled?\n";
    cerr << "Tip: Install BlackHole and/or select it as a capture source. " 
         << "You can also print devices here by iterating Pa_GetDeviceInfo().\n";
    return 2;
  }

  PaStreamParameters inParams{};
  inParams.device = dev;
  inParams.channelCount = 1;             // mono into MFCC pipeline
  inParams.sampleFormat = paFloat32;     // Essentia uses float (Real)
  inParams.suggestedLatency = Pa_GetDeviceInfo(dev)->defaultLowInputLatency;
  inParams.hostApiSpecificStreamInfo = nullptr;

  // Shared ring between PortAudio callback and Essentia source
  Ring ring(44100 * 5); // ~5 seconds buffer safety

  PaStream* stream = nullptr;
  ensurePa(Pa_OpenStream(&stream,
                         &inParams,            // input
                         nullptr,              // no output
                         (double)sampleRate,
                         (unsigned long)hopSize, // callback chunk ~ hop
                         paNoFlag,
                         &paCallback,
                         &ring), "Pa_OpenStream");

  ensurePa(Pa_StartStream(stream), "Pa_StartStream");

  // ---------- Real-time streaming: Feed data from Ring to RingBufferInput ----------
  // This enables concurrent processing as data comes in from BlackHole
  std::thread feeder([&](){
    std::vector<float> chunk(hopSize);
    int noDataCount = 0;
    int framesProcessed = 0;
    while (g_running) {
      if (!ring.pop(chunk.data(), hopSize)) {
        // No data available, check g_running and sleep briefly
        noDataCount++;
        if (noDataCount % 1000 == 0) {
          cerr << "Feeder: no data for " << noDataCount << " iterations" << endl;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        continue;
      }
      
      noDataCount = 0; // Reset counter when we get data
      framesProcessed++;
      
      // Add some basic audio level checking to avoid processing silent frames
      float rms = 0.0f;
      for (int i = 0; i < hopSize; ++i) {
        rms += chunk[i] * chunk[i];
      }
      rms = sqrt(rms / hopSize);
      
      // Only process frames with sufficient audio content (threshold of 0.001)
      if (rms > 0.001f || framesProcessed < 10) { // Always process first 10 frames
        static_cast<streaming::RingBufferInput*>(src)->add(chunk.data(), hopSize);
      }
    }
    cerr << "Feeder thread stopping... processed " << framesProcessed << " frames" << endl;
  });

  // Graceful stop on Ctrl+C
  std::signal(SIGINT, [](int){ g_running = false; });

  cerr << "Streaming from BlackHole… processing audio in real-time..." << endl;
  cerr << "Timeout set to " << timeoutSeconds << " seconds (Ctrl+C to stop early)" << endl;

  // Start the network in a separate thread for concurrent processing
  std::thread networkThread([&](){
    try {
      net.run();
    } catch (const essentia::EssentiaException& e) {
      cerr << "Essentia error during processing: " << e.what() << endl;
      cerr << "This is likely due to silent audio or insufficient data." << endl;
    } catch (const std::exception& e) {
      cerr << "Unexpected error during processing: " << e.what() << endl;
    }
  });

  // Wait for user to stop (Ctrl+C) or run for a specified duration
  std::this_thread::sleep_for(std::chrono::seconds(timeoutSeconds));

  cerr << "Stopping streaming..." << endl;
  g_running = false;
  cerr << "g_running false..." << endl;
  
  // Signal the network to stop first
  src->shouldStop(true);
  cerr << "Network stop signal sent..." << endl;
  
  // Wait for feeder thread to finish
  feeder.join();
  cerr << "Feeder Done..." << endl;
  
  // Stop audio capture
  Pa_StopStream(stream);
  Pa_CloseStream(stream);
  Pa_Terminate();
  
  // Wait for network to finish processing with timeout
  cerr << "Waiting for network to finish..." << endl;
  
  // Use a timeout approach with std::thread
  auto start = std::chrono::steady_clock::now();
  bool networkFinished = false;
  
  // Check if network thread is still running
  if (networkThread.joinable()) {
    // Try to join with a timeout by checking periodically
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) {
      if (!networkThread.joinable()) {
        networkFinished = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
  
  if (networkFinished) {
    cerr << "Network finished gracefully..." << endl;
  } else {
    cerr << "Network did not finish within timeout, detaching thread..." << endl;
    networkThread.detach(); // Detach the thread so we can exit
    // delete networkThread;
  }
  
  

  // ---------- Aggregate & write YAML (same as your example) ----------
  Pool aggrPool;
  // Removed "cov" and "icov" to avoid singular matrix errors when data has insufficient variance
  const char *stats[] = {"mean", "var", "min", "max"};
  standard::Algorithm* aggr = standard::AlgorithmFactory::create("PoolAggregator",
                                  "defaultStats", arrayToVector<string>(stats));
  aggr->input("input").set(pool);
  aggr->output("output").set(aggrPool);
  aggr->compute();

  // Store instrument sums and gates
  aggrPool.merge("instrument.kick.sum.frames", pool.value<vector<Real>>("instrument.kick.sum"));
  aggrPool.merge("instrument.snare.sum.frames", pool.value<vector<Real>>("instrument.snare.sum"));
  aggrPool.merge("instrument.clap.sum.frames", pool.value<vector<Real>>("instrument.clap.sum"));
  aggrPool.merge("instrument.chat.sum.frames", pool.value<vector<Real>>("instrument.chat.sum"));
  aggrPool.merge("instrument.ohc.sum.frames", pool.value<vector<Real>>("instrument.ohc.sum"));

  aggrPool.merge("gate.kick.frames", pool.value<vector<Real>>("gate.kick"));
  aggrPool.merge("gate.snare.frames", pool.value<vector<Real>>("gate.snare"));
  aggrPool.merge("gate.clap.frames", pool.value<vector<Real>>("gate.clap"));
  aggrPool.merge("gate.chat.frames", pool.value<vector<Real>>("gate.chat"));
  aggrPool.merge("gate.ohc.frames", pool.value<vector<Real>>("gate.ohc"));

  standard::Algorithm* output = standard::AlgorithmFactory::create("YamlOutput",
                                  "filename", outputFilename);
  output->input("pool").set(aggrPool);
  output->compute();

  delete aggr;
  delete output;
  delete src;
  delete kick_gate_publisher;
  delete snare_gate_publisher;
  delete clap_gate_publisher;
  delete chat_gate_publisher;
  delete ohc_gate_publisher;
  essentia::shutdown();

  cerr << "Wrote " << outputFilename << endl;
  cerr << "Exiting..." << endl;
  return 0;
}
