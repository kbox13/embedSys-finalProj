#include <iostream>
#include <vector>
#include <string>

#include <essentia/algorithmfactory.h>
#include <essentia/essentia.h>
#include <essentia/pool.h>

int main(int argc, char** argv) {
  try {
    essentia::init();

    std::cout << "Essentia initialized. Version: "
              << ESSENTIA_VERSION << std::endl;

    // Minimal check: create and delete a Spectrum algorithm
    using namespace essentia;
    using namespace standard;
    AlgorithmFactory& factory = AlgorithmFactory::instance();
    Algorithm* spectrum = factory.create("Spectrum");
    delete spectrum;

    essentia::shutdown();
    std::cout << "Essentia shutdown cleanly." << std::endl;
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
}


