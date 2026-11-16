#include "kalman.hpp"
#include "logger.hpp"
#include "timer.hpp"

#include <cmath>
#include <vector>
#include <random>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <iomanip>

namespace {

constexpr uint32_t NUM_TRACKS = 8;
constexpr uint32_t SAMPLES_PER_TRACK = 100000;
constexpr double TIME_STEP = 0.05;           // dt = 50ms
constexpr double NOISE_STD_DEV = 2.0;         // sigma = 2.0 meters
constexpr uint64_t RNG_SEED = 133742ULL;      // Fixed seed for reproducibility

// Determine appropriate output directory (supports running from root, bin/, or build/)
std::filesystem::path resolveDataDirectory(int argc, char* argv[]) {
    if (argc > 1) {
        return std::filesystem::path(argv[1]);
    }

    // Default search paths
    std::vector<std::filesystem::path> candidates = {
        "data",
        "../data",
        "../../data"
    };

    for (const auto& cand : candidates) {
        if (std::filesystem::exists(cand)) {
            return cand;
        }
    }

    // Default to "data" in current directory
    return std::filesystem::path("data");
}

} // namespace

int main(int argc, char* argv[]) {
    logInfo("Starting sensor_gen (CCA CT-353 Kalman Filter dataset generator)...");

    const std::filesystem::path dataDir = resolveDataDirectory(argc, argv);
    std::error_code ec;
    std::filesystem::create_directories(dataDir, ec);
    if (ec) {
        logError("Failed to create data directory: " + dataDir.string() + " (" + ec.message() + ")");
        return 1;
    }

    const std::filesystem::path sensorsPath = dataDir / "sensors.bin";
    const std::filesystem::path truthPath = dataDir / "truth.bin";

    logInfo("Writing sensor data to: " + sensorsPath.string());
    logInfo("Writing ground truth to: " + truthPath.string());

    std::ofstream sensorsFile(sensorsPath, std::ios::binary | std::ios::out);
    std::ofstream truthFile(truthPath, std::ios::binary | std::ios::out);

    if (!sensorsFile.is_open()) {
        logError("Could not open " + sensorsPath.string() + " for writing.");
        return 1;
    }
    if (!truthFile.is_open()) {
        logError("Could not open " + truthPath.string() + " for writing.");
        return 1;
    }

    // 1. Write headers
    SensorFileHeader header{};
    header.num_tracks = NUM_TRACKS;
    header.samples_per_track = SAMPLES_PER_TRACK;

    sensorsFile.write(reinterpret_cast<const char*>(&header), sizeof(header));
    truthFile.write(reinterpret_cast<const char*>(&header), sizeof(header));

    // 2. Generate tracks
    std::mt19937_64 rng(RNG_SEED);
    std::normal_distribution<double> noise_dist(0.0, NOISE_STD_DEV);

    std::vector<double> trackTruth(SAMPLES_PER_TRACK);
    std::vector<double> trackSensors(SAMPLES_PER_TRACK);
    std::vector<double> trackFiltered(SAMPLES_PER_TRACK);

    const uint64_t genStartNs = nowNs();

    double sampleTrack0_rawRmse = 0.0;
    double sampleTrack0_filteredRmse = 0.0;

    for (uint32_t t = 0; t < NUM_TRACKS; ++t) {
        // Diverse trajectory kinematics per track
        const double initialPos = 50.0 * static_cast<double>(t + 1);
        const double baseVelocity = (t % 2 == 0) ? (3.0 + 1.5 * t) : (-2.0 - 1.2 * t);
        const double maneuverFreq = 0.0005 * (t + 1);
        const double maneuverAmp = 0.4 * (t + 1);

        double currentPos = initialPos;
        double currentVel = baseVelocity;

        for (uint32_t s = 0; s < SAMPLES_PER_TRACK; ++s) {
            // Kinematic acceleration with smooth sinusoidal variations
            const double acc = maneuverAmp * std::sin(maneuverFreq * static_cast<double>(s));
            currentVel += acc * TIME_STEP;
            currentPos += currentVel * TIME_STEP;

            const double truth = currentPos;
            const double noise = noise_dist(rng);
            const double meas = truth + noise;

            trackTruth[s] = truth;
            trackSensors[s] = meas;
        }

        // Write track arrays to binary files
        sensorsFile.write(reinterpret_cast<const char*>(trackSensors.data()),
                          static_cast<std::streamsize>(SAMPLES_PER_TRACK * sizeof(double)));
        truthFile.write(reinterpret_cast<const char*>(trackTruth.data()),
                        static_cast<std::streamsize>(SAMPLES_PER_TRACK * sizeof(double)));

        // Run verification on Track 0 using common kalman filter
        if (t == 0) {
            filterTrack(trackSensors.data(), trackFiltered.data(), SAMPLES_PER_TRACK,
                        TIME_STEP, 25.0, NOISE_STD_DEV * NOISE_STD_DEV);

            double sumRawSq = 0.0;
            double sumFiltSq = 0.0;
            for (uint32_t i = 0; i < SAMPLES_PER_TRACK; ++i) {
                const double errRaw = trackSensors[i] - trackTruth[i];
                const double errFilt = trackFiltered[i] - trackTruth[i];
                sumRawSq += errRaw * errRaw;
                sumFiltSq += errFilt * errFilt;
            }
            sampleTrack0_rawRmse = std::sqrt(sumRawSq / SAMPLES_PER_TRACK);
            sampleTrack0_filteredRmse = std::sqrt(sumFiltSq / SAMPLES_PER_TRACK);
        }
    }

    sensorsFile.flush();
    truthFile.flush();
    sensorsFile.close();
    truthFile.close();

    const uint64_t genEndNs = nowNs();
    const double elapsedMs = static_cast<double>(genEndNs - genStartNs) / 1000000.0;

    // 3. Verify file sizes
    const auto sensorsSize = std::filesystem::file_size(sensorsPath, ec);
    const auto truthSize = std::filesystem::file_size(truthPath, ec);
    const uintmax_t expectedSize = sizeof(SensorFileHeader) +
        static_cast<uintmax_t>(NUM_TRACKS) * SAMPLES_PER_TRACK * sizeof(double);

    logInfo("Generation completed in " + std::to_string(elapsedMs) + " ms.");
    logInfo("sensors.bin size: " + std::to_string(sensorsSize) + " bytes (expected: " +
            std::to_string(expectedSize) + ")");
    logInfo("truth.bin size:   " + std::to_string(truthSize) + " bytes (expected: " +
            std::to_string(expectedSize) + ")");

    if (sensorsSize != expectedSize || truthSize != expectedSize) {
        logError("File size verification failed!");
        return 1;
    }

    std::cout << "\n======================================================\n";
    std::cout << "  OS CCA (CT-353) - Dataset Generation Summary\n";
    std::cout << "======================================================\n";
    std::cout << "  Tracks:              " << NUM_TRACKS << "\n";
    std::cout << "  Samples per Track:   " << SAMPLES_PER_TRACK << "\n";
    std::cout << "  Total Measurements:  " << (NUM_TRACKS * SAMPLES_PER_TRACK) << "\n";
    std::cout << "  Payload Size:        " << (expectedSize / (1024.0 * 1024.0)) << " MB\n";
    std::cout << "  Track 0 Raw RMSE:    " << std::fixed << std::setprecision(4)
              << sampleTrack0_rawRmse << " m\n";
    std::cout << "  Track 0 Kalman RMSE: " << sampleTrack0_filteredRmse << " m\n";
    std::cout << "  Noise Reduction:     "
              << ((1.0 - sampleTrack0_filteredRmse / sampleTrack0_rawRmse) * 100.0) << "%\n";
    std::cout << "======================================================\n\n";

    logInfo("Dataset generation and validation SUCCESSFUL.");
    return 0;
}
