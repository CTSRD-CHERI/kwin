/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2019 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "abstract_egl_backend.h"
#include "outputlayer.h"
#include "utils/damagejournal.h"
// wayland
#include <dmabuftexture.h>
#include <optional>
#include <wayland-egl.h>

#include <memory>

class QTemporaryFile;
struct wl_buffer;
struct wl_shm;
struct gbm_bo;

namespace KWin
{
class GLFramebuffer;

namespace Wayland
{
class WaylandBackend;
class WaylandOutput;
class WaylandEglBackend;

class WaylandEglOutput : public OutputLayer
{
public:
    WaylandEglOutput(WaylandOutput *output, WaylandEglBackend *backend);
    ~WaylandEglOutput() override;

    bool init();
    void updateSize();

    GLFramebuffer *fbo() const;
    bool makeContextCurrent() const;
    void present();

    std::optional<OutputLayerBeginFrameInfo> beginFrame() override;
    bool endFrame(const QRegion &renderedRegion, const QRegion &damagedRegion) override;
    void aboutToStartPainting(const QRegion &damage) override;

private:
    void resetBufferAge();

    WaylandOutput *m_waylandOutput;
    wl_egl_window *m_overlay = nullptr;
    EGLSurface m_eglSurface = EGL_NO_SURFACE;
    int m_bufferAge = 0;
    DamageJournal m_damageJournal;
    std::unique_ptr<GLFramebuffer> m_fbo;
    WaylandEglBackend *const m_backend;

    friend class WaylandEglBackend;
};

/**
 * @brief OpenGL Backend using Egl on a Wayland surface.
 *
 * This Backend is the basis for a session compositor running on top of a Wayland system compositor.
 * It creates a Surface as large as the screen and maps it as a fullscreen shell surface on the
 * system compositor. The OpenGL context is created on the Wayland surface, so for rendering X11 is
 * not involved.
 *
 * Also in repainting the backend is currently still rather limited. Only supported mode is fullscreen
 * repaints, which is obviously not optimal. Best solution is probably to go for buffer_age extension
 * and make it the only available solution next to fullscreen repaints.
 */
class WaylandEglBackend : public AbstractEglBackend
{
    Q_OBJECT
public:
    WaylandEglBackend(WaylandBackend *b);
    ~WaylandEglBackend() override;

    std::unique_ptr<SurfaceTexture> createSurfaceTextureInternal(SurfacePixmapInternal *pixmap) override;
    std::unique_ptr<SurfaceTexture> createSurfaceTextureWayland(SurfacePixmapWayland *pixmap) override;

    void init() override;
    void present(Output *output) override;
    OutputLayer *primaryLayer(Output *output) override;

    bool havePlatformBase() const
    {
        return m_havePlatformBase;
    }

    std::shared_ptr<KWin::GLTexture> textureForOutput(KWin::Output *output) const override;

private:
    gbm_bo *createBo(const QSize &size, quint32 format, const QVector<uint64_t> &modifiers);
    bool initializeEgl();
    bool initBufferConfigs();
    bool initRenderingContext();

    bool createEglWaylandOutput(Output *output);

    void cleanupSurfaces() override;

    void presentOnSurface(WaylandEglOutput *output, const QRegion &damagedRegion);

    WaylandBackend *m_backend;
    QMap<Output *, std::shared_ptr<WaylandEglOutput>> m_outputs;
    bool m_havePlatformBase;
    friend class EglWaylandTexture;
};

} // namespace Wayland
} // namespace KWin
