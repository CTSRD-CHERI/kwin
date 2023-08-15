/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
    SPDX-FileCopyrightText: 2022 Jessica Clarke <jrtc27@jrtc27.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "session.h"

struct libseat;
struct wl_event_source;

namespace KWin
{

namespace Wayland
{
class WaylandBackend;
}

class LibseatSessionPrivate;

class LibseatSession : public Session
{
    Q_OBJECT

    struct LibseatDevice {
        int libseatId;
        int fileDescriptor;
        dev_t deviceId;
    };

public:
    static LibseatSession *create(QObject *parent = nullptr);
    ~LibseatSession() override;

    bool isActive() const override;
    Capabilities capabilities() const override;
    QString seat() const override;
    uint terminal() const override;
    int openRestricted(const QString &fileName) override;
    void closeRestricted(int fileDescriptor) override;
    void switchTo(uint terminal) override;

private Q_SLOTS:
    void handleEnableSeat();
    void handleDisableSeat();

private:
    explicit LibseatSession(QObject *parent = nullptr);

    void initialize(struct libseat *seat, struct wl_event_source *eventSource);
    void updateActive(bool active);

    struct libseat *m_seat;
    struct wl_event_source *m_eventSource;
    QList<LibseatDevice> m_devices;
    bool m_isActive = false;

    friend class LibseatSessionPrivate;
};

} // namespace KWin
