// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <string>

#include "base/dbus.h"
#include "base/signal.h"
#include "cutefish/fprint/fingerprint_finger.h"

namespace cutefish::fprint {

// Why an action on the reader did not go through.
enum class Failure {
    None,
    // polkit refused, or the user dismissed its password prompt.
    NotAuthorized,
    // Another program, such as a login check, is using the reader.
    InUse,
    // The finger's print is already stored, under this or another finger.
    Duplicate,
    // The reader has no room for another print.
    Full,
    Disconnected,
    Failed,
};

// One fingerprint being recorded on the reader: it claims the reader, starts
// the enrollment (which may ask polkit for the password first) and follows
// fprintd's EnrollStatus until the print is stored or the attempt fails.
// Dropping it, or cancel(), stops the enrollment and gives the reader back.
class Enrollment
{
public:
    enum class State {
        // The reader is being claimed and polkit may be asking.
        Starting,
        // Waiting for the finger, scan after scan.
        Scanning,
        Completed,
        Failed,
        Cancelled,
    };

    // The last scan that did not count, until the next one that does.
    enum class Hint {
        None,
        RetryScan,
        SwipeTooShort,
        TooFast,
        NotCentered,
        RemoveAndRetry,
    };

    // The reader is a Reader's; `stages` is how many good scans the print takes.
    Enrollment(base::dbus::Connection* bus, std::string devicePath, Finger finger, int stages);
    ~Enrollment();

    Enrollment(const Enrollment&) = delete;
    Enrollment& operator=(const Enrollment&) = delete;

    State state() const { return m_state; }
    bool isFinished() const
    {
        return m_state == State::Completed || m_state == State::Failed || m_state == State::Cancelled;
    }
    Finger finger() const { return m_finger; }
    int stages() const { return m_stages; }
    int stagesPassed() const { return m_passed; }
    Hint hint() const { return m_hint; }
    // Set once the state is Failed.
    Failure failure() const { return m_failure; }
    // The service's own words for a failure, for the log; may be empty.
    const std::string& error() const { return m_error; }

    // Stops a running enrollment; nothing for a finished one.
    void cancel();

    // After the state, the progress or the hint changed.
    base::Signal<> changed;

private:
    void onStatus(const std::string& result, bool done);
    void fail(Failure failure, std::string error);
    void finish(State state);

    base::dbus::Proxy m_device;
    base::dbus::Match m_statusMatch;
    base::dbus::PendingCall m_claimCall;
    base::dbus::PendingCall m_startCall;
    Finger m_finger;
    int m_stages;
    int m_passed = 0;
    State m_state = State::Starting;
    Hint m_hint = Hint::None;
    Failure m_failure = Failure::None;
    std::string m_error;
    bool m_claimSent = false;
    bool m_enrolling = false;
};

} // namespace cutefish::fprint
