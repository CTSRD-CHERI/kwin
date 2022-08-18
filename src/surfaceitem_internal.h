/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "surfaceitem.h"

class QOpenGLFramebufferObject;

namespace KWin
{

class InternalWindow;

/**
 * The SurfaceItemInternal class represents an internal surface in the scene.
 */
class KWIN_EXPORT SurfaceItemInternal : public SurfaceItem
{
    Q_OBJECT

public:
    explicit SurfaceItemInternal(InternalWindow *window, Item *parent = nullptr);

    QRegion shape() const override;

private Q_SLOTS:
    void handleBufferGeometryChanged(Window *window, const QRectF &old);

protected:
    std::unique_ptr<SurfacePixmap> createPixmap() override;
};

class KWIN_EXPORT SurfacePixmapInternal final : public SurfacePixmap
{
    Q_OBJECT

public:
    explicit SurfacePixmapInternal(SurfaceItemInternal *item, QObject *parent = nullptr);

    QOpenGLFramebufferObject *fbo() const;
    QImage image() const;

    void create() override;
    void update() override;
    bool isValid() const override;

private:
    SurfaceItemInternal *m_item;
    std::shared_ptr<QOpenGLFramebufferObject> m_fbo;
    QImage m_rasterBuffer;
};

} // namespace KWin
