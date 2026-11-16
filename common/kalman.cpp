#include "kalman.hpp"
#include <algorithm>

KalmanFilter2D::KalmanFilter2D(double dt,
                               double process_noise,
                               double measurement_noise,
                               double init_pos,
                               double init_vel)
    : dt_(dt),
      q_acc_(process_noise),
      r_meas_(measurement_noise),
      pos_(init_pos),
      vel_(init_vel),
      p00_(measurement_noise > 0.0 ? measurement_noise : 1.0),
      p01_(0.0),
      p11_((measurement_noise > 0.0 && dt > 0.0) ? (2.0 * measurement_noise / (dt * dt)) : 100.0),
      q00_(0.0),
      q01_(0.0),
      q11_(0.0)
{
    updateProcessNoise();
}

void KalmanFilter2D::updateProcessNoise() {
    const double dt2 = dt_ * dt_;
    const double dt3 = dt2 * dt_;
    const double dt4 = dt3 * dt_;

    // Discrete continuous white noise acceleration model:
    // Q = q_acc * [[dt^4 / 4, dt^3 / 2], [dt^3 / 2, dt^2]]
    q00_ = q_acc_ * (dt4 / 4.0);
    q01_ = q_acc_ * (dt3 / 2.0);
    q11_ = q_acc_ * dt2;
}

void KalmanFilter2D::setTimeStep(double dt) noexcept {
    if (dt > 0.0) {
        dt_ = dt;
        updateProcessNoise();
    }
}

void KalmanFilter2D::setProcessNoise(double q) noexcept {
    if (q >= 0.0) {
        q_acc_ = q;
        updateProcessNoise();
    }
}

void KalmanFilter2D::setMeasurementNoise(double r) noexcept {
    if (r > 0.0) {
        r_meas_ = r;
    }
}

void KalmanFilter2D::reset(double init_pos, double init_vel) {
    pos_ = init_pos;
    vel_ = init_vel;
    p00_ = (r_meas_ > 0.0) ? r_meas_ : 1.0;
    p01_ = 0.0;
    p11_ = (r_meas_ > 0.0 && dt_ > 0.0) ? (2.0 * r_meas_ / (dt_ * dt_)) : 100.0;
}

double KalmanFilter2D::step(double measurement) {
    // 1. Time Update (Predict)
    // x_{k|k-1} = F * x_{k-1|k-1}
    const double pred_pos = pos_ + dt_ * vel_;
    const double pred_vel = vel_;

    // P_{k|k-1} = F * P_{k-1|k-1} * F^T + Q
    const double pred_p00 = p00_ + 2.0 * dt_ * p01_ + dt_ * dt_ * p11_ + q00_;
    const double pred_p01 = p01_ + dt_ * p11_ + q01_;
    const double pred_p11 = p11_ + q11_;

    // 2. Measurement Update (Correct)
    // Innovation (residual) y_k = z_k - H * x_{k|k-1}
    const double y = measurement - pred_pos;

    // Innovation covariance S_k = H * P_{k|k-1} * H^T + R = P00' + R
    const double s = pred_p00 + r_meas_;
    const double inv_s = (s > 1e-15) ? (1.0 / s) : 0.0;

    // Kalman gain K = P_{k|k-1} * H^T * inv(S)
    const double k0 = pred_p00 * inv_s;
    const double k1 = pred_p01 * inv_s;

    // Updated state estimate: x_{k|k} = x_{k|k-1} + K * y_k
    pos_ = pred_pos + k0 * y;
    vel_ = pred_vel + k1 * y;

    // Updated error covariance: P_{k|k} = (I - K * H) * P_{k|k-1}
    // Using symmetric formulation for numerical stability
    const double one_minus_k0 = 1.0 - k0;
    p00_ = std::max(1e-12, one_minus_k0 * pred_p00);
    p01_ = one_minus_k0 * pred_p01;
    p11_ = std::max(1e-12, pred_p11 - k1 * pred_p01);

    return pos_;
}

void filterTrack(const double* meas, double* out, size_t n) {
    // Default tuning: dt=1.0, process_noise=0.05, measurement_noise=1.0
    filterTrack(meas, out, n, 1.0, 0.05, 1.0);
}

void filterTrack(const double* meas, double* out, size_t n,
                 double dt, double process_noise, double measurement_noise) {
    if (!meas || !out || n == 0) {
        return;
    }

    // Estimate initial velocity if at least 2 points are available
    double init_vel = 0.0;
    if (n > 1 && dt > 0.0) {
        init_vel = (meas[1] - meas[0]) / dt;
    }

    KalmanFilter2D kf(dt, process_noise, measurement_noise, meas[0], init_vel);
    out[0] = meas[0];

    for (size_t i = 1; i < n; ++i) {
        out[i] = kf.step(meas[i]);
    }
}
