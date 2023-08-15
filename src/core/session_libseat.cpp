/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
    SPDX-FileCopyrightText: 2022 Jessica Clarke <jrtc27@jrtc27.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "session_libseat.h"
#include "main.h"
#include "utils/common.h"
#include "wayland_server.h"

#include <KWaylandServer/display.h>

#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <libseat.h>

#include <wayland-server-core.h>

namespace KWin
{

class LibseatSessionPrivate {
public:
    static void handleEnableSeat(struct libseat *seat, void *data)
    {
        LibseatSession *session = (LibseatSession *)data;
        Q_EMIT session->handleEnableSeat();
        Q_UNUSED(seat);
    }

    static void handleDisableSeat(struct libseat *seat, void *data)
    {
        LibseatSession *session = (LibseatSession *)data;
        Q_EMIT session->handleDisableSeat();
        Q_UNUSED(seat);
    }
};

static const struct libseat_seat_listener s_listener = {
    .enable_seat = &LibseatSessionPrivate::handleEnableSeat,
    .disable_seat = &LibseatSessionPrivate::handleDisableSeat,
};

static int libseatEvent(int fd, uint32_t mask, void *data)
{
    struct libseat *seat = (struct libseat *)data;
    if (libseat_dispatch(seat, 0) == -1) {
        qCDebug(KWIN_CORE, "Failed to dispatch libseat event (%s)",
                strerror(errno));
    }
    Q_UNUSED(fd);
    Q_UNUSED(mask);
    return 1;
}

LibseatSession *LibseatSession::create(QObject *parent)
{
    // We're lazy and abuse Wayland event loop since we don't care about
    // X11, rather than dealing with creating our own thread as is used
    // for D-Bus behind the scenes.
    if (!waylandServer()) {
        return nullptr;
    }

    LibseatSession *session = new LibseatSession(parent);
    struct libseat *seat = libseat_open_seat(&s_listener, (void *)session);
    if (!seat) {
        delete session;
        return nullptr;
    }

    struct wl_event_loop *eventLoop = wl_display_get_event_loop(*waylandServer()->display());
    struct wl_event_source *eventSource =
        wl_event_loop_add_fd(eventLoop, libseat_get_fd(seat), WL_EVENT_READABLE,
                             libseatEvent, seat);
    if (!eventSource) {
        qCDebug(KWIN_CORE, "Failed to add libseat event source (%s)",
                strerror(errno));
        libseat_close_seat(seat);
        delete session;
        return nullptr;
    }

    if (libseat_dispatch(seat, 0) == -1) {
        qCDebug(KWIN_CORE, "Failed to dispatch libseat event (%s)",
                strerror(errno));
        libseat_close_seat(seat);
        wl_event_source_remove(eventSource);
        delete session;
        return nullptr;
    }

    session->initialize(seat, eventSource);
    return session;
}

LibseatSession::LibseatSession(QObject *parent)
    : Session(parent)
    , m_seat(nullptr)
    , m_eventSource(nullptr)
{
}

void LibseatSession::initialize(struct libseat *seat, struct wl_event_source *eventSource)
{
    m_seat = seat;
    m_eventSource = eventSource;
}

LibseatSession::~LibseatSession()
{
    if (m_seat) {
        libseat_close_seat(m_seat);
    }
    if (m_eventSource) {
        wl_event_source_remove(m_eventSource);
    }
}

bool LibseatSession::isActive() const
{
    return m_isActive;
}

LibseatSession::Capabilities LibseatSession::capabilities() const
{
    return Capability::SwitchTerminal;
}

QString LibseatSession::seat() const
{
    return QString::fromUtf8(libseat_seat_name(m_seat));
}

uint LibseatSession::terminal() const
{
    return 0;
}

int LibseatSession::openRestricted(const QString &fileName)
{
    struct stat st;
    if (stat(fileName.toUtf8(), &st) < 0) {
        return -1;
    }

    int fileDescriptor;
    int libseatId = libseat_open_device(m_seat, fileName.toUtf8(), &fileDescriptor);
    if (libseatId == -1) {
        qCDebug(KWIN_CORE, "Failed to open %s device (%s)",
                qPrintable(fileName), strerror(errno));
        return -1;
    }

    m_devices.push_back({libseatId, fileDescriptor, st.st_rdev});
    return fileDescriptor;
}

void LibseatSession::closeRestricted(int fileDescriptor)
{
    auto it = std::find_if(m_devices.begin(), m_devices.end(),
        [fileDescriptor](const LibseatDevice &device) {
            return device.fileDescriptor == fileDescriptor;
        }
    );
    if (it == m_devices.end()) {
        qCDebug(KWIN_CORE, "Tried to close unknown device FD %d",
                fileDescriptor);
        close(fileDescriptor);
        return;
    }

    libseat_close_device(m_seat, it->libseatId);
    m_devices.erase(it);
    close(fileDescriptor);
}

void LibseatSession::switchTo(uint terminal)
{
    if (libseat_switch_session(m_seat, terminal) == -1) {
        qCDebug(KWIN_CORE, "Failed to switch to terminal %d (%s)",
                terminal, strerror(errno));
    }
}

void LibseatSession::updateActive(bool active)
{
    if (m_isActive != active) {
        m_isActive = active;
        Q_EMIT activeChanged(active);
    }
}

void LibseatSession::handleEnableSeat()
{
    for (const LibseatDevice &device : m_devices) {
        Q_EMIT deviceResumed(device.deviceId);
    }
    updateActive(true);
}

void LibseatSession::handleDisableSeat()
{
    updateActive(false);
    for (const LibseatDevice &device : m_devices) {
        Q_EMIT devicePaused(device.deviceId);
    }
    libseat_disable_seat(m_seat);
}

} // namespace KWin
