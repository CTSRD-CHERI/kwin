/*
    SPDX-FileCopyrightText: 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2020 David Edmundson <davidedmundson@kde.org>
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "thumbnailitem.h"
#include "abstract_client.h"
#include "composite.h"
#include "effects.h"
#include "scene.h"
#include "screens.h"
#include "scripting_logging.h"
#include "workspace.h"

#if QT_CONFIG(opengl)
#include <kwingltexture.h>
#include <kwinglutils.h>
#endif

#include <QSGImageNode>
#include <QRunnable>
#include <QQuickWindow>
#include <QSGTextureProvider>

namespace KWin
{
class ThumbnailTextureProvider : public QSGTextureProvider
{
public:
    explicit ThumbnailTextureProvider(QQuickWindow *window);

    QSGTexture *texture() const override;
    void setTexture(const QSharedPointer<GLTexture> &nativeTexture);
    void setTexture(QSGTexture *texture);

private:
    QQuickWindow *m_window;
    QSharedPointer<GLTexture> m_nativeTexture;
    QScopedPointer<QSGTexture> m_texture;
};

ThumbnailTextureProvider::ThumbnailTextureProvider(QQuickWindow *window)
    : m_window(window)
{
}

QSGTexture *ThumbnailTextureProvider::texture() const
{
    return m_texture.data();
}

#if QT_CONFIG(opengl)
void ThumbnailTextureProvider::setTexture(const QSharedPointer<GLTexture> &nativeTexture)
{
    if (m_nativeTexture != nativeTexture) {
        const GLuint textureId = nativeTexture->texture();
        m_nativeTexture = nativeTexture;
        m_texture.reset(m_window->createTextureFromNativeObject(QQuickWindow::NativeObjectTexture,
                                                                &textureId, 0,
                                                                nativeTexture->size(),
                                                                QQuickWindow::TextureHasAlphaChannel));
        m_texture->setFiltering(QSGTexture::Linear);
        m_texture->setHorizontalWrapMode(QSGTexture::ClampToEdge);
        m_texture->setVerticalWrapMode(QSGTexture::ClampToEdge);
    }

    // The textureChanged signal must be emitted also if only texture data changes.
    Q_EMIT textureChanged();
}
#endif

void ThumbnailTextureProvider::setTexture(QSGTexture *texture)
{
    m_nativeTexture = nullptr;
    m_texture.reset(texture);
    Q_EMIT textureChanged();
}

class ThumbnailTextureProviderCleanupJob : public QRunnable
{
public:
    explicit ThumbnailTextureProviderCleanupJob(ThumbnailTextureProvider *provider)
        : m_provider(provider)
    {
    }

    void run() override
    {
        m_provider.reset();
    }

private:
    QScopedPointer<ThumbnailTextureProvider> m_provider;
};

ThumbnailItemBase::ThumbnailItemBase(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents);
    handleCompositingToggled();

    connect(Compositor::self(), &Compositor::aboutToToggleCompositing,
            this, &ThumbnailItemBase::destroyOffscreenTexture);
    connect(Compositor::self(), &Compositor::compositingToggled,
            this, &ThumbnailItemBase::handleCompositingToggled);
}

ThumbnailItemBase::~ThumbnailItemBase()
{
    destroyOffscreenTexture();

    if (m_provider) {
        if (window()) {
            window()->scheduleRenderJob(new ThumbnailTextureProviderCleanupJob(m_provider),
                                        QQuickWindow::AfterSynchronizingStage);
        } else {
            qCCritical(KWIN_SCRIPTING) << "Can't destroy thumbnail texture provider because window is null";
        }
    }
}

void ThumbnailItemBase::releaseResources()
{
    if (m_provider) {
        window()->scheduleRenderJob(new ThumbnailTextureProviderCleanupJob(m_provider),
                                    QQuickWindow::AfterSynchronizingStage);
        m_provider = nullptr;
    }
}

bool ThumbnailItemBase::isTextureProvider() const
{
    return true;
}

QSGTextureProvider *ThumbnailItemBase::textureProvider() const
{
    if (QQuickItem::isTextureProvider()) {
        return QQuickItem::textureProvider();
    }
    if (!m_provider) {
        m_provider = new ThumbnailTextureProvider(window());
    }
    return m_provider;
}

void ThumbnailItemBase::handleCompositingToggled()
{
    if (!Compositor::self()) {
        return;
    }
    Scene *scene = Compositor::self()->scene();
    if (scene && scene->compositingType() == OpenGLCompositing) {
#if QT_CONFIG(opengl)
        connect(scene, &Scene::frameRendered, this, &ThumbnailItemBase::updateOffscreenTexture);
#endif
    }
}

QSize ThumbnailItemBase::sourceSize() const
{
    return m_sourceSize;
}

void ThumbnailItemBase::setSourceSize(const QSize &sourceSize)
{
    if (m_sourceSize != sourceSize) {
        m_sourceSize = sourceSize;
        invalidateOffscreenTexture();
        Q_EMIT sourceSizeChanged();
    }
}

void ThumbnailItemBase::destroyOffscreenTexture()
{
    if (!Compositor::self()) {
        return;
    }
#if QT_CONFIG(opengl)
    Scene *scene = Compositor::self()->scene();
    if (!scene || scene->compositingType() != OpenGLCompositing) {
        return;
    }

    if (m_offscreenTexture) {
        scene->makeOpenGLContextCurrent();
        m_offscreenTarget.reset();
        m_offscreenTexture.reset();

        if (m_acquireFence) {
            glDeleteSync(m_acquireFence);
            m_acquireFence = 0;
        }
        scene->doneOpenGLContextCurrent();
    }
#endif
}

QSGNode *ThumbnailItemBase::updatePaintNode(QSGNode *oldNode, QQuickItem::UpdatePaintNodeData *)
{
    if (Compositor::compositing() && !m_offscreenTexture) {
        return oldNode;
    }

#if QT_CONFIG(opengl)
    // Wait for rendering commands to the offscreen texture complete if there are any.
    if (m_acquireFence) {
        glClientWaitSync(m_acquireFence, GL_SYNC_FLUSH_COMMANDS_BIT, 5000);
        glDeleteSync(m_acquireFence);
        m_acquireFence = 0;
    }
#endif

    if (!m_provider) {
        m_provider = new ThumbnailTextureProvider(window());
    }

    if (m_offscreenTexture) {
#if QT_CONFIG(opengl)
        m_provider->setTexture(m_offscreenTexture);
#endif
    } else {
        const QImage placeholderImage = fallbackImage();
        m_provider->setTexture(window()->createTextureFromImage(placeholderImage));
        m_devicePixelRatio = placeholderImage.devicePixelRatio();
    }

    QSGImageNode *node = static_cast<QSGImageNode *>(oldNode);
    if (!node) {
        node = window()->createImageNode();
    }
    node->setTexture(m_provider->texture());

#if QT_CONFIG(opengl)
    if (m_offscreenTexture && m_offscreenTexture->isYInverted()) {
        node->setTextureCoordinatesTransform(QSGImageNode::MirrorVertically);
    } else {
#else
    if (true) {
#endif
        node->setTextureCoordinatesTransform(QSGImageNode::NoTransform);
    }

    const QSizeF size = QSizeF(node->texture()->textureSize() / m_devicePixelRatio)
            .scaled(boundingRect().size(), Qt::KeepAspectRatio);
    const qreal x = boundingRect().x() + (boundingRect().width() - size.width()) / 2;
    const qreal y = boundingRect().y() + (boundingRect().height() - size.height()) / 2;
    node->setRect(QRectF(QPointF(x, y), size));

    return node;
}

void ThumbnailItemBase::setSaturation(qreal saturation)
{
    Q_UNUSED(saturation)
    qCWarning(KWIN_SCRIPTING) << "ThumbnailItem.saturation is removed. Use a shader effect to change saturation";
}

void ThumbnailItemBase::setBrightness(qreal brightness)
{
    Q_UNUSED(brightness)
    qCWarning(KWIN_SCRIPTING) << "ThumbnailItem.brightness is removed. Use a shader effect to change brightness";
}

void ThumbnailItemBase::setClipTo(QQuickItem *clip)
{
    Q_UNUSED(clip)
    qCWarning(KWIN_SCRIPTING) << "ThumbnailItem.clipTo is removed and it has no replacements";
}

WindowThumbnailItem::WindowThumbnailItem(QQuickItem *parent)
    : ThumbnailItemBase(parent)
{
}

QUuid WindowThumbnailItem::wId() const
{
    return m_wId;
}

void WindowThumbnailItem::setWId(const QUuid &wId)
{
    if (m_wId == wId) {
        return;
    }
    m_wId = wId;
    if (!m_wId.isNull()) {
        setClient(workspace()->findAbstractClient(wId));
    } else if (m_client) {
        m_client = nullptr;
        Q_EMIT clientChanged();
    }
    Q_EMIT wIdChanged();
}

AbstractClient *WindowThumbnailItem::client() const
{
    return m_client;
}

void WindowThumbnailItem::setClient(AbstractClient *client)
{
    if (m_client == client) {
        return;
    }
    if (m_client) {
        disconnect(m_client, &AbstractClient::frameGeometryChanged,
                   this, &WindowThumbnailItem::invalidateOffscreenTexture);
        disconnect(m_client, &AbstractClient::damaged,
                   this, &WindowThumbnailItem::invalidateOffscreenTexture);
    }
    m_client = client;
    if (m_client) {
        connect(m_client, &AbstractClient::frameGeometryChanged,
                this, &WindowThumbnailItem::invalidateOffscreenTexture);
        connect(m_client, &AbstractClient::damaged,
                this, &WindowThumbnailItem::invalidateOffscreenTexture);
        setWId(m_client->internalId());
    } else {
        setWId(QUuid());
    }
    invalidateOffscreenTexture();
    Q_EMIT clientChanged();
}

QImage WindowThumbnailItem::fallbackImage() const
{
    if (m_client) {
        return m_client->icon().pixmap(window(), boundingRect().size().toSize()).toImage();
    }
    return QImage();
}

void WindowThumbnailItem::invalidateOffscreenTexture()
{
    m_dirty = true;
    update();
}

#if QT_CONFIG(opengl)
void WindowThumbnailItem::updateOffscreenTexture()
{
    if (m_acquireFence || !m_dirty || !m_client) {
        return;
    }

    const QRect geometry = m_client->frameGeometry();
    QSize textureSize = geometry.size();
    if (sourceSize().width() > 0) {
        textureSize.setWidth(sourceSize().width());
    }
    if (sourceSize().height() > 0) {
        textureSize.setHeight(sourceSize().height());
    }

    m_devicePixelRatio = window()->devicePixelRatio();
    textureSize *= m_devicePixelRatio;

    if (!m_offscreenTexture || m_offscreenTexture->size() != textureSize) {
        m_offscreenTexture.reset(new GLTexture(GL_RGBA8, textureSize));
        m_offscreenTexture->setFilter(GL_LINEAR);
        m_offscreenTexture->setWrapMode(GL_CLAMP_TO_EDGE);
        m_offscreenTarget.reset(new GLRenderTarget(*m_offscreenTexture));
    }

    GLRenderTarget::pushRenderTarget(m_offscreenTarget.data());
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);

    QMatrix4x4 projectionMatrix;
    projectionMatrix.ortho(geometry.x(), geometry.x() + geometry.width(),
                           geometry.y(), geometry.y() + geometry.height(), -1, 1);

    EffectWindowImpl *effectWindow = m_client->effectWindow();
    WindowPaintData data(effectWindow);
    data.setProjectionMatrix(projectionMatrix);

    // The thumbnail must be rendered using kwin's opengl context as VAOs are not
    // shared across contexts. Unfortunately, this also introduces a latency of 1
    // frame, which is not ideal, but it is acceptable for things such as thumbnails.
    const int mask = Scene::PAINT_WINDOW_TRANSFORMED;
    effectWindow->sceneWindow()->performPaint(mask, infiniteRegion(), data);
    GLRenderTarget::popRenderTarget();

    // The fence is needed to avoid the case where qtquick renderer starts using
    // the texture while all rendering commands to it haven't completed yet.
    m_dirty = false;
    m_acquireFence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

    // We know that the texture has changed, so schedule an item update.
    update();
}
#endif

DesktopThumbnailItem::DesktopThumbnailItem(QQuickItem *parent)
    : ThumbnailItemBase(parent)
{
}

int DesktopThumbnailItem::desktop() const
{
    return m_desktop;
}

void DesktopThumbnailItem::setDesktop(int desktop)
{
    desktop = qBound<int>(1, desktop, VirtualDesktopManager::self()->count());
    if (m_desktop != desktop) {
        m_desktop = desktop;
        invalidateOffscreenTexture();
        Q_EMIT desktopChanged();
    }
}

QImage DesktopThumbnailItem::fallbackImage() const
{
    return QImage();
}

void DesktopThumbnailItem::invalidateOffscreenTexture()
{
    update();
}

#if QT_CONFIG(opengl)
void DesktopThumbnailItem::updateOffscreenTexture()
{
    if (m_acquireFence) {
        return;
    }

    const QRect geometry = screens()->geometry();
    QSize textureSize = geometry.size();
    if (sourceSize().width() > 0) {
        textureSize.setWidth(sourceSize().width());
    }
    if (sourceSize().height() > 0) {
        textureSize.setHeight(sourceSize().height());
    }

    m_devicePixelRatio = window()->devicePixelRatio();
    textureSize *= m_devicePixelRatio;

    if (!m_offscreenTexture || m_offscreenTexture->size() != textureSize) {
        m_offscreenTexture.reset(new GLTexture(GL_RGBA8, textureSize));
        m_offscreenTexture->setFilter(GL_LINEAR);
        m_offscreenTexture->setWrapMode(GL_CLAMP_TO_EDGE);
        m_offscreenTexture->setYInverted(true);
        m_offscreenTarget.reset(new GLRenderTarget(*m_offscreenTexture));
    }

    GLRenderTarget::pushRenderTarget(m_offscreenTarget.data());
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);

    QMatrix4x4 projectionMatrix;
    projectionMatrix.ortho(geometry);
    ScreenPaintData data(projectionMatrix);

    // The thumbnail must be rendered using kwin's opengl context as VAOs are not
    // shared across contexts. Unfortunately, this also introduces a latency of 1
    // frame, which is not ideal, but it is acceptable for things such as thumbnails.
    const int mask = Scene::PAINT_WINDOW_TRANSFORMED | Scene::PAINT_SCREEN_TRANSFORMED;
    Scene *scene = Compositor::self()->scene();
    scene->paintDesktop(m_desktop, mask, infiniteRegion(), data);
    GLRenderTarget::popRenderTarget();

    // The fence is needed to avoid the case where qtquick renderer starts using
    // the texture while all rendering commands to it haven't completed yet.
    m_acquireFence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

    // We know that the texture has changed, so schedule an item update.
    update();
}
#endif

} // namespace KWin
