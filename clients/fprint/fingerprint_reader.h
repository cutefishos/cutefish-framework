// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "base/dbus.h"
#include "base/signal.h"
#include "cutefish/fprint/fingerprint_enrollment.h"
#include "cutefish/fprint/fingerprint_finger.h"

namespace cutefish::fprint {

enum class ScanType {
    // The finger rests on the sensor.
    Press,
    // The finger is drawn across it.
    Swipe,
};

// The computer's fingerprint reader as fprintd offers it: whether there is
// one, the user's stored fingers, and the calls that delete a print or
// record a new one. Read when made.
class Reader
{
public:
    explicit Reader(base::dbus::Connection* bus = base::dbus::Connection::system());
    ~Reader();

    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;

    // Whether fprintd has answered which reader there is.
    bool isReady() const { return m_ready; }
    bool isAvailable() const { return m_available; }
    // The reader's name, such as "Goodix TLS Fingerprint Sensor 511".
    const std::string& name() const { return m_name; }
    ScanType scanType() const { return m_scanType; }
    // How many good scans a print takes; 5 where the driver does not say.
    int enrollStages() const { return m_stages; }

    // The user's stored fingers, in fprintd's order; filled once the first
    // listing answers.
    const std::vector<Finger>& enrolled() const { return m_enrolled; }
    bool isListed() const { return m_listed; }
    // Why the last listing failed; empty when it worked.
    const std::string& listError() const { return m_listError; }
    // Reads the stored fingers again.
    void refresh();

    // Deletes a stored print. The service may ask polkit first. Calls back
    // with how it went; enrolled() has changed by then.
    void remove(Finger finger, std::function<void(Failure failure, const std::string& error)> done = {});

    // Starts recording a print on a finger; null without a reader. The
    // stored fingers are read again when the enrollment is over.
    std::unique_ptr<Enrollment> enroll(Finger finger);

    // After isReady() turned true.
    base::Signal<> readyChanged;
    // After enrolled() or listError() changed.
    base::Signal<> enrolledChanged;

private:
    void findDevice();
    void readDevice();

    base::dbus::Connection* m_bus;
    base::dbus::Proxy m_manager;
    base::dbus::Proxy m_device;
    base::dbus::PendingCall m_findCall;
    base::dbus::PendingCall m_propertiesCall;
    base::dbus::PendingCall m_listCall;
    base::dbus::PendingCall m_claimCall;
    base::dbus::PendingCall m_deleteCall;
    base::Subscription m_enrollmentChanged;
    std::string m_name;
    ScanType m_scanType = ScanType::Press;
    int m_stages = 5;
    std::vector<Finger> m_enrolled;
    std::string m_listError;
    bool m_ready = false;
    bool m_available = false;
    bool m_listed = false;
};

} // namespace cutefish::fprint
