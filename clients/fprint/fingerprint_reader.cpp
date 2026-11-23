// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "cutefish/fprint/fingerprint_reader.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

#include "device_session.h"

using namespace cutefish::base;

namespace cutefish::fprint {

static constexpr const char* kManagerPath = "/net/reactivated/Fprint/Manager";
static constexpr const char* kManagerInterface = "net.reactivated.Fprint.Manager";
static constexpr int kDefaultStages = 5;
// Deleting asks polkit, which may take a password's worth of time.
static constexpr std::chrono::seconds kTimeout{120};

Reader::Reader(dbus::Connection* bus)
    : m_bus(bus)
    , m_manager(bus, kService, kManagerPath, kManagerInterface)
{
    if (!bus) {
        m_ready = true;
        return;
    }
    findDevice();
}

Reader::~Reader() = default;

void Reader::findDevice()
{
    m_findCall = m_manager.callAsync("GetDefaultDevice", [this](const dbus::Reply& reply) {
        const auto path = reply.value<dbus::ObjectPath>();
        if (!path) {
            // No reader, or no fprintd at all.
            m_ready = true;
            readyChanged.emit();
            return;
        }
        m_device = dbus::Proxy(m_bus, kService, path->path, kDeviceInterface);
        m_device.setInteractiveAuthorizationAllowed(true);
        m_device.setTimeout(kTimeout);
        readDevice();
    });
}

void Reader::readDevice()
{
    m_propertiesCall = m_device.getAllAsync([this](const dbus::Reply& reply) {
        const auto properties = reply.value<dbus::VariantMap>();
        if (!properties) {
            m_ready = true;
            readyChanged.emit();
            return;
        }
        const auto find = [&](const char* name) -> const dbus::Variant* {
            const auto it = properties->find(name);
            return it == properties->end() ? nullptr : &it->second;
        };
        if (const dbus::Variant* name = find("name"))
            m_name = name->value<std::string>();
        if (const dbus::Variant* type = find("scan-type"))
            m_scanType = type->value<std::string>() == "swipe" ? ScanType::Swipe : ScanType::Press;
        if (const dbus::Variant* stages = find("num-enroll-stages")) {
            const int count = stages->value<int32_t>(-1);
            m_stages = count > 0 ? count : kDefaultStages;
        }
        m_available = true;
        m_ready = true;
        refresh();
        readyChanged.emit();
    });
}

void Reader::refresh()
{
    if (!m_available)
        return;
    m_listCall = m_device.callAsync("ListEnrolledFingers", [this](const dbus::Reply& reply) {
        std::vector<Finger> fingers;
        std::string error;
        if (reply.isError()) {
            // A user without prints is an error to fprintd, not a failure.
            if (!reply.error().name.ends_with(".NoEnrolledPrints"))
                error = reply.error().message;
        } else {
            for (const std::string& name : reply.value<std::vector<std::string>>().value_or(
                         std::vector<std::string>())) {
                if (const auto finger = fingerFromName(name))
                    fingers.push_back(*finger);
            }
            std::sort(fingers.begin(), fingers.end());
        }
        if (!error.empty())
            fprintf(stderr, "Listing fingerprints failed: %s\n", error.c_str());
        if (m_listed && fingers == m_enrolled && error == m_listError)
            return;
        m_listed = true;
        m_enrolled = std::move(fingers);
        m_listError = std::move(error);
        enrolledChanged.emit();
    }, std::string());
}

void Reader::remove(Finger finger, std::function<void(Failure failure, const std::string& error)> done)
{
    const auto finish = [done](const dbus::Error& error) {
        if (done)
            done(error.isSet() ? failureFromError(error) : Failure::None, error.message);
    };
    if (!m_available) {
        finish({"net.reactivated.Fprint.Error.NoSuchDevice", "No fingerprint reader"});
        return;
    }
    m_claimCall = m_device.callAsync("Claim", [this, finger, finish](const dbus::Reply& reply) {
        if (reply.isError()) {
            fprintf(stderr, "Claiming the fingerprint reader failed: %s\n", reply.error().message.c_str());
            finish(reply.error());
            return;
        }
        m_deleteCall = m_device.callAsync("DeleteEnrolledFinger", [this, finish](const dbus::Reply& reply) {
            m_device.send("Release");
            if (reply.isError())
                fprintf(stderr, "Deleting a fingerprint failed: %s\n", reply.error().message.c_str());
            refresh();
            finish(reply.isError() ? reply.error() : dbus::Error());
        }, std::string(fingerName(finger)));
    }, std::string());
}

std::unique_ptr<Enrollment> Reader::enroll(Finger finger)
{
    if (!m_available)
        return nullptr;
    auto enrollment = std::make_unique<Enrollment>(m_bus, m_device.path(), finger, m_stages);
    m_enrollmentChanged = enrollment->changed.connect([this, enrollment = enrollment.get()] {
        if (enrollment->isFinished())
            refresh();
    });
    return enrollment;
}

} // namespace cutefish::fprint
