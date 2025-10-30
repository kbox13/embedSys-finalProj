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
#include "sumrange.h"
#include "hit_gate_quantile.h"
#include "hit_gate_multiframe.h"
#include "hit_gate_onset.h"
#include "zeromq_publisher.h"


using namespace std;
using namespace essentia;
using namespace essentia::streaming;
using namespace essentia::scheduler;

static std::atomic<bool> g_running{true};

// Simple lock-free circular buffer for float audio (single-producer PortAudio, single-consumer Essentia)
struct Ring {
  explicit Ring(size_t cap) : buf(cap), cap(cap) {}
  vector<float> buf;
  size_t cap;
  atomic<size_t> head{0}, tail{0}; // head = write, tail = read

  // push up to n samples, returns how many written
  size_t push(const float* in, size_t n) {
    size_t written = 0;
    while (written < n) {
      size_t h = head.load(memory_order_relaxed);
      size_t t = tail.load(memory_order_acquire);
      size_t free = (t + cap - h - 1) % cap; // leave 1 slot to distinguish full/empty
      if (free == 0) break;
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
  bool pop(float* out, size_t n) {
    size_t t = tail.load(memory_order_relaxed);
    size_t h = head.load(memory_order_acquire);
    size_t available = (h + cap - t) % cap;
    if (available < n) return false;
    size_t idx = t % cap;
    size_t chunk = min(n, cap - idx);
    memcpy(out, &buf[idx], chunk * sizeof(float));
    if (n > chunk) memcpy(out + chunk, &buf[0], (n - chunk) * sizeof(float));
    tail.store((t + n) % cap, memory_order_release);
    return true;
  }
};

// PortAudio stream callback: write input frames into Ring
static int paCallback(const void* input, void*, unsigned long frameCount,
                      const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void* userData) {
  auto* ring = reinterpret_cast<Ring*>(userData);
  if (!input) return paContinue;
  const float* in = static_cast<const float*>(input);
  size_t pushed = ring->push(in, frameCount);
  (void)pushed; // best-effort; drop if ring is full
  return g_running ? paContinue : paComplete;
}

static void ensurePa(PaError err, const char* where) {
  if (err != paNoError) {
    cerr << where << " failed: " << Pa_GetErrorText(err) << endl;
    exit(2);
  }
}

// Find the first input device whose name contains "BlackHole"
static int findBlackHoleDevice() {
  int num = Pa_GetDeviceCount();
  for (int i = 0; i < num; ++i) {
    const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
    if (!info) continue;
    const PaHostApiInfo* api = Pa_GetHostApiInfo(info->hostApi);
    std::string name = info->name ? info->name : "";
    std::string apiName = api && api->name ? api->name : "";
    // Look for CoreAudio device named like "BlackHole 2ch"
    if (name.find("BlackHole") != std::string::npos && info->maxInputChannels > 0) {
      cerr << "Using input device: [" << i << "] " << name << " via " << apiName << endl;
      return i;
    }
  }
  return paNoDevice;
}

int main(int argc, char* argv[]) {
  if (argc != 2) {
    cerr << "Usage: " << argv[0] << " output.yaml\n";
    return 1;
  }
  string outputFilename = argv[1];

  // ---------- Essentia init ----------
  essentia::init();

  // Parameters (match your original example)
  const Real sampleRate = 44100.0;
  const int frameSize = 1024;
  const int hopSize = 256;

  // Pool to collect features
  Pool pool;

  // ---------- Build Essentia streaming graph ----------
  AlgorithmFactory& F = streaming::AlgorithmFactory::instance();

  // Manually register RingBufferInput since it's not in the factory
  AlgorithmFactory::Registrar<streaming::RingBufferInput> regRingBufferInput;
  
  // Register our custom SumRange algorithm
  AlgorithmFactory::Registrar<streaming::SumRange> regSumRange;

  // Register our custom HitGateQuantile algorithm
  AlgorithmFactory::Registrar<streaming::HitGateQuantile> regHitGateQuantile;

  // Register our custom HitGateMultiFrame algorithm
  AlgorithmFactory::Registrar<streaming::HitGateMultiFrame> regHitGateMultiFrame;

  // Register our custom HitGateOnset algorithm
  AlgorithmFactory::Registrar<streaming::HitGateOnset> regHitGateOnset;

  // Register our custom ZeroMQPublisher algorithm
  AlgorithmFactory::Registrar<streaming::ZeroMQPublisher> regZeroMQPublisher;

  // Create algorithms
  Algorithm* fc   = F.create("FrameCutter",
                             "frameSize", frameSize,
                             "hopSize",   hopSize,
                             "silentFrames", "noise");

  Algorithm* win  = F.create("Windowing", "type", "blackmanharris62");
  Algorithm* spec = F.create("Spectrum");
  Algorithm* melbands = F.create("MelBands");
  const int BASS_LO = 0,  BASS_HI = 5;    // ~≤250 Hz
  const int MID_LO  = 6,  MID_HI  = 24;   // ~250 Hz–2 kHz
  const int HIGH_LO = 25, HIGH_HI = 63;   // ~≥2 kHz

  Algorithm* bass = F.create("SumRange", "lo", BASS_LO, "hi", BASS_HI);
  Algorithm* mid = F.create("SumRange", "lo", MID_LO, "hi", MID_HI);
  Algorithm* high = F.create("SumRange", "lo", HIGH_LO, "hi", HIGH_HI);

  Algorithm* bass_gate = F.create("HitGateOnset",
                                 "method", "hfc",
                                 "threshold", 0.2,
                                 "refractory", 4,
                                 "warmup", 8,
                                 "sensitivity", 1.5,
                                 "smooth_window", 2);
  Algorithm* mid_gate = F.create("HitGateOnset",
                                 "method", "flux",
                                 "threshold", 0.15,
                                 "refractory", 4,
                                 "warmup", 8,
                                 "sensitivity", 1.8,
                                 "smooth_window", 2);
  Algorithm* high_gate = F.create("HitGateOnset",
                                  "method", "hfc",
                                  "threshold", 0.18,
                                  "refractory", 4,
                                  "warmup", 8,
                                  "sensitivity", 1.6,
                                  "smooth_window", 2);

  // Algorithm *bass_gate = F.create("HitGateQuantile",
  //                                 "q_hi", 0.98,
  //                                 "q_lo", 0.80,
  //                                 "refractory", 4,
  //                                 "warmup", 8);
  // Algorithm *mid_gate = F.create("HitGateQuantile",
  //                                "q_hi", 0.98,
  //                                "q_lo", 0.80,
  //                                "refractory", 4,
  //                                "warmup", 8);
  // Algorithm *high_gate = F.create("HitGateQuantile",
  //                                 "q_hi", 0.98,
  //                                 "q_lo", 0.80,
  //                                 "refractory", 4,
  //                                 "warmup", 8);

  // Create ZeroMQ publishers for each feature (all connecting to same port)
//   Algorithm* bass_publisher = F.create("ZeroMQPublisher",
//                                       "endpoint", "tcp://localhost:5555",
//                                       "feature_name", "bass",
//                                       "buffer_size", 1);
//   Algorithm* mid_publisher = F.create("ZeroMQPublisher",
//                                      "endpoint", "tcp://localhost:5555",
//                                      "feature_name", "mid",
//                                      "buffer_size", 1);
//   Algorithm* high_publisher = F.create("ZeroMQPublisher",
//                                       "endpoint", "tcp://localhost:5555",
//                                       "feature_name", "high",
//                                       "buffer_size", 1);
  Algorithm* bass_gate_publisher = F.create("ZeroMQPublisher",
                                           "endpoint", "tcp://localhost:5555",
                                           "feature_name", "bass_gate",
                                           "buffer_size", 1,
                                           "threshold", 0.5,
                                           "threshold_mode", "above");
  Algorithm* mid_gate_publisher = F.create("ZeroMQPublisher",
                                          "endpoint", "tcp://localhost:5555",
                                          "feature_name", "mid_gate",
                                          "buffer_size", 1,
                                          "threshold", 0.5,
                                          "threshold_mode", "above");
  Algorithm* high_gate_publisher = F.create("ZeroMQPublisher",
                                           "endpoint", "tcp://localhost:5555",
                                           "feature_name", "high_gate",
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
  melbands->output("bands") >> bass->input("in");
  melbands->output("bands") >> mid->input("in");
  melbands->output("bands") >> high->input("in");
  
  bass->output("out") >> PC(pool,"lowlevel.bass");
  mid->output("out") >> PC(pool,"lowlevel.mid");
  high->output("out") >> PC(pool,"lowlevel.high");

  bass->output("out") >> bass_gate->input("in");
  mid->output("out") >> mid_gate->input("in");
  high->output("out") >> high_gate->input("in");
  
  bass_gate->output("out") >> PC(pool,"bass_gate");
  mid_gate->output("out") >> PC(pool,"mid_gate");
  high_gate->output("out") >> PC(pool,"high_gate");

  // Connect each feature to its own ZeroMQ publisher
  //bass->output("out") >> bass_publisher->input("in");
  //mid->output("out") >> mid_publisher->input("in");
  //high->output("out") >> high_publisher->input("in");
  bass_gate->output("out") >> bass_gate_publisher->input("in");
  mid_gate->output("out") >> mid_gate_publisher->input("in");
  high_gate->output("out") >> high_gate_publisher->input("in");
 


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
  std::this_thread::sleep_for(std::chrono::seconds(20)); // Process for 20 seconds
  
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
  const char* stats[] = {"mean","var","min","max","cov","icov"};
  standard::Algorithm* aggr = standard::AlgorithmFactory::create("PoolAggregator",
                                  "defaultStats", arrayToVector<string>(stats));
  aggr->input("input").set(pool);
  aggr->output("output").set(aggrPool);
  aggr->compute();

  // Merge the frequency band data instead of MFCC
  aggrPool.merge("lowlevel.bass.frames", pool.value<vector<Real>>("lowlevel.bass"));
  aggrPool.merge("lowlevel.mid.frames", pool.value<vector<Real>>("lowlevel.mid"));
  aggrPool.merge("lowlevel.high.frames", pool.value<vector<Real>>("lowlevel.high"));

  aggrPool.merge("bass_gate.frames", pool.value<vector<Real>>("bass_gate"));
  aggrPool.merge("mid_gate.frames", pool.value<vector<Real>>("mid_gate"));
  aggrPool.merge("high_gate.frames", pool.value<vector<Real>>("high_gate"));

  standard::Algorithm* output = standard::AlgorithmFactory::create("YamlOutput",
                                  "filename", outputFilename);
  output->input("pool").set(aggrPool);
  output->compute();

  delete aggr;
  delete output;
  delete src;
//   delete bass_publisher;
//   delete mid_publisher;
//   delete high_publisher;
  delete bass_gate_publisher;
  delete mid_gate_publisher;
  delete high_gate_publisher;
  essentia::shutdown();

  cerr << "Wrote " << outputFilename << endl;
  cerr << "Exiting..." << endl;
  return 0;
}
