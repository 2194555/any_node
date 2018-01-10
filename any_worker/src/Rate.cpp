/**********************************************************************
 * Software License Agreement (BSD License)
 *
 * Copyright (c) 2018, Remo Diethelm
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Robotic Systems Lab nor ETH Zurich
 *     nor the names of its contributors may be used to endorse or
 *     promote products derived from this software without specific
 *     prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

/*!
 * @file    Rate.cpp
 * @author  Remo Diethelm
 * @date    January, 2018
 */

// std
#include <cmath>

// message logger
#include <message_logger/message_logger.hpp>

// any worker
#include "any_worker/Rate.hpp"


namespace any_worker {


Rate::Rate(const std::string& name,
           const double timeStep)
:   Rate(name, timeStep, timeStep, 10.0*timeStep, true, CLOCK_MONOTONIC) {
}

Rate::Rate(const std::string& name,
           const double timeStep,
           const double maxTimeStepWarning,
           const double maxTimeStepError,
           const bool enforceRate,
           const clockid_t clockId)
:   name_(name),
    clockId_(clockId) {
    setTimeStep(timeStep);
    setMaxTimeStepWarning(maxTimeStepWarning);
    setMaxTimeStepError(maxTimeStepError);
    setEnforceRate(enforceRate);
    reset();
}

Rate::Rate(Rate&& other)
:   name_(std::move(other.name_)),
    timeStep_(other.timeStep_.load()),
    maxTimeStepWarning_(other.maxTimeStepWarning_.load()),
    maxTimeStepError_(other.maxTimeStepError_.load()),
    enforceRate_(other.enforceRate_.load()),
    clockId_(std::move(other.clockId_)),
    stepTime_(std::move(other.stepTime_)),
    numTimeSteps_(std::move(other.numTimeSteps_)),
    numWarnings_(std::move(other.numWarnings_)),
    numErrors_(std::move(other.numErrors_)),
    awakeTime_(std::move(other.awakeTime_)),
    awakeTimeMean_(std::move(other.awakeTimeMean_)),
    awakeTimeM2_(std::move(other.awakeTimeM2_)) {
}

const std::string& Rate::getName() const {
    return name_;
}

double Rate::getTimeStep() const {
    return timeStep_;
}

void Rate::setTimeStep(const double timeStep) {
    if (!TimeStepIsValid(timeStep)) {
        MELO_ERROR_STREAM("Rate '" << name_ << "': " <<
            "Cannot set the time step to an invalid value " << timeStep << " s.");
        return;
    }
    timeStep_ = timeStep;
}

double Rate::getMaxTimeStepWarning() const {
    return maxTimeStepWarning_;
}

void Rate::setMaxTimeStepWarning(const double maxTimeStepWarning) {
    if (!MaxTimeStepIsValid(maxTimeStepWarning)) {
        MELO_ERROR_STREAM("Rate '" << name_ << "': " <<
            "Cannot set the max time step for warnings to invalid value " << maxTimeStepWarning << " s.");
        return;
    }
    maxTimeStepWarning_ = maxTimeStepWarning;
}

double Rate::getMaxTimeStepError() const {
    return maxTimeStepError_;
}

void Rate::setMaxTimeStepError(const double maxTimeStepError) {
    if (!MaxTimeStepIsValid(maxTimeStepError)) {
        MELO_ERROR_STREAM("Rate '" << name_ << "': " <<
            "Cannot set the max time step for errors to invalid value " << maxTimeStepError << " s.");
        return;
    }
    maxTimeStepError_ = maxTimeStepError;
}

bool Rate::getEnforceRate() const {
    return enforceRate_;
}

void Rate::setEnforceRate(const bool enforceRate) {
    enforceRate_ = enforceRate;
}

clockid_t Rate::getClockId() const {
    return clockId_;
}

void Rate::reset() {
    // Reset the counters and statistics.
    numTimeSteps_ = 0;
    numWarnings_ = 0;
    numErrors_ = 0;
    awakeTime_ = 0.0;
    awakeTimeMean_ = 0.0;
    awakeTimeM2_ = 0.0;

    // Update the sleep time to the current time.
    timespec now;
    clock_gettime(clockId_, &now);
    sleepStartTime_ = now;
    sleepEndTime_ = now;
    stepTime_ = now;
}

void Rate::sleep() {
    // NOTE: Even if the time step is 0.0, one might want to set maxima for warnings
    // and errors as well as get statistics. Therefore we do not skip anything for this case.

    // Get the current time and compute the time which the thread has been awake.
    clock_gettime(clockId_, &sleepStartTime_);
    awakeTime_ = GetDuration(sleepEndTime_, sleepStartTime_);

    // Update the statistics. The algorithm is described here:
    // https://en.wikipedia.org/wiki/Algorithms_for_calculating_variance
    numTimeSteps_++;
    const double delta = awakeTime_ - awakeTimeMean_;
    awakeTimeMean_ += delta / numTimeSteps_;
    const double delta2 = awakeTime_ - awakeTimeMean_;
    awakeTimeM2_ += delta * delta2;

    // Check if the awake exceeds the threshold for warnings or errors.
    if (awakeTime_ > maxTimeStepError_) {
        // Print and count the error.
        MELO_ERROR_STREAM("Rate '" << name_ << "': " <<
            "Processing took too long (" << awakeTime_ << " s > " << timeStep_.load() << " s).");
        numErrors_++;
    } else if (awakeTime_ > maxTimeStepWarning_) {
        // Print and count the warning (only if no error).
        MELO_WARN_STREAM("Rate '" << name_ << "': " <<
            "Processing took too long (" << awakeTime_ << " s > " << timeStep_.load() << " s).");
        numWarnings_++;
    }

    // Compute the next desired step time.
    stepTime_.tv_nsec += timeStep_ * NSecPerSec_;
    stepTime_.tv_sec += stepTime_.tv_nsec / NSecPerSec_;
    stepTime_.tv_nsec = stepTime_.tv_nsec % NSecPerSec_;

    // Get the current time again and check if the step time has already past.
    clock_gettime(clockId_, &sleepEndTime_);
    const bool isBehind = (GetDuration(sleepEndTime_, stepTime_) < 0.0);
    if (isBehind) {
        if (!enforceRate_) {
            // We are behind schedule but do not enforce the rate, so we increase the length of
            // the current time step by setting the desired step time to when sleep() ends.
            stepTime_ = sleepEndTime_;
        }
    } else {
        // sleep() will finish in time. The end of the function will be at
        // the target time of clock_nanosleep(..).
        sleepEndTime_ = stepTime_;

        // Sleep until the step time is reached.
        clock_nanosleep(clockId_, TIMER_ABSTIME, &stepTime_, NULL);

        // Do nothing here to ensure sleep() does not consume time after clock_nanosleep(..).
    }
}

const timespec& Rate::getSleepStartTime() const {
    return sleepStartTime_;
}

const timespec& Rate::getSleepEndTime() const {
    return sleepEndTime_;
}

const timespec& Rate::getStepTime() const {
    return stepTime_;
}

unsigned int Rate::getNumTimeSteps() const {
    return numTimeSteps_;
}

unsigned int Rate::getNumWarnings() const {
    return numWarnings_;
}

unsigned int Rate::getNumErrors() const {
    return numErrors_;
}

double Rate::getAwakeTime() const {
    if (numTimeSteps_ == 0) {
        return std::numeric_limits<double>::quiet_NaN();
    } else {
        return awakeTime_;
    }
}

double Rate::getAwakeTimeMean() const {
    if (numTimeSteps_ == 0) {
        return std::numeric_limits<double>::quiet_NaN();
    } else {
        return awakeTimeMean_;
    }
}

double Rate::getAwakeTimeVar() const {
    if (numTimeSteps_ <= 1) {
        return std::numeric_limits<double>::quiet_NaN();
    } else {
        return awakeTimeM2_ / (numTimeSteps_ - 1);
    }
}

double Rate::getAwakeTimeStdDev() const {
    return std::sqrt(getAwakeTimeVar());
}

double Rate::GetDuration(const timespec& start, const timespec& end) {
    return (end.tv_sec - start.tv_sec) +
           (end.tv_nsec - start.tv_nsec) * SecPerNSec_;
}

bool Rate::TimeStepIsValid(const double timeStep) {
    return (
        timeStep >= 0.0 &&
        !std::isinf(timeStep) &&
        !std::isnan(timeStep));
}

bool Rate::MaxTimeStepIsValid(const double maxTimeStep) {
    return (
        maxTimeStep >= 0.0 &&
        !std::isnan(maxTimeStep));
}


} // namespace any_worker
