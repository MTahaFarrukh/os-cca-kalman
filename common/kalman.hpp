#ifndef OS_CCA_KALMAN_HPP
#define OS_CCA_KALMAN_HPP

#include <cstddef>
#include <cstdint>

#pragma pack(push, 1)
// Binary header for sensor and ground truth data files
struct SensorFileHeader {
    uint32_t num_tracks;
    uint32_t samples_per_track;
};
#pragma pack(pop)

/**
 * @brief 2D Constant-Velocity Kalman Filter (position and velocity state).
 * 
 * State vector: x = [position, velocity]^T
 * State transition:
 *   position_{k} = position_{k-1} + dt * velocity_{k-1}
 *   velocity_{k} = velocity_{k-1}
 * Measurement:
 *   z_{k} = position_{k} + noise
 * 
 * Implemented completely platform-independently using standard C++17.
 */
class KalmanFilter2D {
public:
    /**
     * @brief Construct a new KalmanFilter2D instance.
     * @param dt Time step duration (seconds). Default 1.0.
     * @param process_noise Process noise acceleration variance (sigma_a^2).
     * @param measurement_noise Measurement noise variance (R).
     * @param init_pos Initial position estimate.
     * @param init_vel Initial velocity estimate.
     */
    explicit KalmanFilter2D(double dt = 1.0,
                            double process_noise = 0.05,
                            double measurement_noise = 1.0,
                            double init_pos = 0.0,
                            double init_vel = 0.0);

    /**
     * @brief Execute one prediction-update cycle with a new position measurement.
     * @param measurement The noisy position measurement.
     * @return double The updated (filtered) position estimate.
     */
    double step(double measurement);

    /**
     * @brief Reset the filter state and covariance matrix.
     * @param init_pos New initial position.
     * @param init_vel New initial velocity.
     */
    void reset(double init_pos = 0.0, double init_vel = 0.0);

    // Getters for state estimates
    [[nodiscard]] double getPosition() const noexcept { return pos_; }
    [[nodiscard]] double getVelocity() const noexcept { return vel_; }

    // Getters for covariance matrix diagonal
    [[nodiscard]] double getPositionVariance() const noexcept { return p00_; }
    [[nodiscard]] double getVelocityVariance() const noexcept { return p11_; }

    // Tuning parameters
    void setTimeStep(double dt) noexcept;
    void setProcessNoise(double q) noexcept;
    void setMeasurementNoise(double r) noexcept;

private:
    double dt_;        // Time step
    double q_acc_;     // Continuous acceleration variance
    double r_meas_;    // Measurement noise covariance (R)

    // State estimates [p, v]
    double pos_;
    double vel_;

    // Error covariance matrix P = [[p00, p01], [p10, p11]] (symmetric: p10 == p01)
    double p00_;
    double p01_;
    double p11_;

    // Process noise covariance matrix elements Q
    double q00_;
    double q01_;
    double q11_;

    void updateProcessNoise();
};

/**
 * @brief Free function to filter an entire track of noisy measurements.
 * 
 * @param meas Pointer to input array of noisy position measurements.
 * @param out Pointer to output array where filtered positions will be written.
 * @param n Number of samples in the track.
 */
void filterTrack(const double* meas, double* out, size_t n);

/**
 * @brief Overload of filterTrack with custom filter tuning parameters.
 * 
 * @param meas Pointer to input array of noisy position measurements.
 * @param out Pointer to output array where filtered positions will be written.
 * @param n Number of samples in the track.
 * @param dt Time step between samples.
 * @param process_noise Process noise parameter.
 * @param measurement_noise Measurement noise parameter.
 */
void filterTrack(const double* meas, double* out, size_t n,
                 double dt, double process_noise, double measurement_noise);

#endif // OS_CCA_KALMAN_HPP
