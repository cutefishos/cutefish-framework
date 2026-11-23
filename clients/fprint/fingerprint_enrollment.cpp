// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "cutefish/fprint/fingerprint_enrollment.h"

#include <chrono>
#include <cstdio>

#include "device_session.h"

using namespace cutefish::base;

namespace cutefish::fprint {

// Enrolling asks polkit, which may take a password's worth of time.
static constexpr std::chrono::seconds kTimeout{120};

Enrollment::Enrollment(dbus::Connection* bus, std::string devicePath, Finger finger, int stages)
    : m_device(bus, kService, std::move(devicePath), kDeviceInterface)
    , m_finger(finger)
    , m_stages(stages)
{
    if (!bus) {
        m_state = State::Failed;
        m_failure = Failure::Failed;
        return;
    }
    m_device.setInteractiveAuthorizationAllowed(true);
    m_device.setTimeout(kTimeout);
    m_statusMatch = m_device.onSignal("EnrollStatus", [this](const dbus::Message& message) {
        std::string result;
        bool done = false;
        if (m_enrolling && message.read(result, done))
            onStatus(result, done);
    });
    m_claimSent = true;
    m_claimCall = m_device.callAsync("Claim", [this](const dbus::Reply& reply) {
        if (reply.isError()) {
            m_claimSent = false;
            fail(failureFromError(reply.error()), reply.error().message);
            return;
        }
        m_startCall = m_device.callAsync("EnrollStart", [this](const dbus::Reply& reply) {
            if (reply.isError()) {
                fail(failureFromError(reply.error()), reply.error().message);
                return;
            }
            m_enrolling = true;
            m_state = State::Scanning;
            changed.emit();
        }, std::string(fingerName(m_finger)));
    }, std::string());
}

Enrollment::~Enrollment()
{
    // A claim still being answered is given back too: the reply is dropped
    // with this, so nobody would learn that it went through.
    if (m_claimSent)
        releaseDevice(m_device, m_enrolling);
}

void Enrollment::cancel()
{
    if (isFinished())
        return;
    m_claimCall.reset();
    m_startCall.reset();
    finish(State::Cancelled);
}

void Enrollment::onStatus(const std::string& result, bool done)
{
    if (isFinished())
        return;
    if (result == "enroll-stage-passed") {
        m_passed = std::min(m_passed + 1, m_stages);
        m_hint = Hint::None;
    } else if (result == "enroll-completed") {
        m_passed = m_stages;
        m_hint = Hint::None;
        finish(State::Completed);
        return;
    } else if (result == "enroll-retry-scan") {
        m_hint = Hint::RetryScan;
    } else if (result == "enroll-swipe-too-short") {
        m_hint = Hint::SwipeTooShort;
    } else if (result == "enroll-too-fast") {
        m_hint = Hint::TooFast;
    } else if (result == "enroll-finger-not-centered") {
        m_hint = Hint::NotCentered;
    } else if (result == "enroll-remove-and-retry") {
        m_hint = Hint::RemoveAndRetry;
    } else if (result == "enroll-duplicate") {
        fail(Failure::Duplicate, result);
        return;
    } else if (result == "enroll-data-full") {
        fail(Failure::Full, result);
        return;
    } else if (result == "enroll-disconnected") {
        fail(Failure::Disconnected, result);
        return;
    } else if (done) {
        fail(Failure::Failed, result);
        return;
    }
    changed.emit();
}

void Enrollment::fail(Failure failure, std::string error)
{
    if (failure != Failure::NotAuthorized)
        fprintf(stderr, "Fingerprint enrollment failed: %s\n", error.c_str());
    m_failure = failure;
    m_error = std::move(error);
    finish(State::Failed);
}

void Enrollment::finish(State state)
{
    m_state = state;
    if (m_claimSent)
        releaseDevice(m_device, m_enrolling);
    m_claimSent = false;
    m_enrolling = false;
    changed.emit();
}

} // namespace cutefish::fprint
