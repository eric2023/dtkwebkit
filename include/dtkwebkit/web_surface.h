/*
 * This file is part of dtkwebkit, a WPE WebKit Qt6 embedding layer.
 *
 * Copyright (c) 2026 UnionTech Software Technology Co., Ltd.
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <https://www.gnu.org/licenses/>.
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#ifndef WEB_SURFACE_H
#define WEB_SURFACE_H

#include <QObject>
#include <QString>
#include <QVariant>
#include <functional>

#include "wpe_export.h"

namespace DTKWPE {

/**
 * @brief Abstract web surface interface for embedding WPE WebKit in Qt6.
 *
 * Provides a platform-agnostic facade for loading web content, executing
 * JavaScript, and bridging native <-> JS communication.  The actual
 * implementation uses WPE WebKit (WPEBackend-FDO EGL) on Linux/UOS;
 * alternative backends (WKWebView on macOS, WebView2 on Windows) can be
 * plugged in without changing application-layer code.
 */
class DWPE_EXPORT WebSurface : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool sandboxEnabled READ isSandboxEnabled WRITE setSandboxEnabled NOTIFY sandboxEnabledChanged)
    Q_PROPERTY(bool devToolsEnabled READ isDevToolsEnabled WRITE setDevToolsEnabled NOTIFY devToolsEnabledChanged)
    Q_PROPERTY(QString rootUrl READ rootUrl WRITE setRootUrl NOTIFY rootUrlChanged)

public:
    explicit WebSurface(QObject *parent = nullptr)
        : QObject(parent)
    {
    }
    ~WebSurface() override = default;

    // --- Core web content API ---

    virtual void loadUrl(const QString &url) = 0;
    virtual void setHtml(const QString &html, const QString &baseUrl = QString()) = 0;
    virtual void runJavaScript(const QString &code, std::function<void(QVariant)> callback = nullptr) = 0;
    virtual void postMessage(const QVariant &message) = 0;

    // --- QWebChannel 12-channel migration support ---

    using ChannelHandler = std::function<QVariant(const QString &method, const QVariantList &args)>;

    virtual void registerChannel(const QString &name, ChannelHandler handler) = 0;
    virtual void emitChannelSignal(const QString &channel, const QString &signal, const QVariant &data) = 0;

    // --- Properties ---

    virtual bool isSandboxEnabled() const = 0;
    virtual void setSandboxEnabled(bool enabled) = 0;

    virtual bool isDevToolsEnabled() const = 0;
    virtual void setDevToolsEnabled(bool enabled) = 0;

    virtual QString rootUrl() const = 0;
    virtual void setRootUrl(const QString &url) = 0;

Q_SIGNALS:
    void onLoadFinished(bool ok);
    void onLoadProgress(double progress);
    void onConsoleMessage(int level, const QString &message, int line, const QString &source);
    void onMessage(const QVariant &message);
    void onRenderCrashed();
    void sandboxEnabledChanged(bool enabled);
    void devToolsEnabledChanged(bool enabled);
    void rootUrlChanged(const QString &url);
    void onChannelSignalReceived(const QString &channel, const QString &signal, const QVariant &data);
};

}  // namespace DTKWPE

#endif  // WEB_SURFACE_H
