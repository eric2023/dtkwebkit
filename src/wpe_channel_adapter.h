/*
 * This file is part of dtkwebkit.
 *
 * Copyright (c) 2026 UnionTech Software Technology Co., Ltd.
 *
 * LGPL-3.0-or-later
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#ifndef DWPE_CHANNEL_ADAPTER_H
#define DWPE_CHANNEL_ADAPTER_H

#include <QObject>
#include <QString>
#include <QVariant>
#include <QHash>
#include "wpe_export.h"

#include "wpe_bridge.h"

namespace DTKWPE {

/**
 * @brief Adapts the existing 12 QWebChannel channels to window.host.
 *
 * Existing Vue3 SPA uses QWebChannel with 12 registered channels:
 *   session, window, assistant, conversation, file, audio,
 *   task, skillsMgr, report, ...
 *
 * These channels are the sole data source for the frontend.  This adapter
 * routes {channel, method, data} messages received via DWPEBridge to the
 * appropriate native handler, and pushes signal callbacks back to JS.
 *
 * The JS-side Proxy (injected by DWPEBridge::userScriptSource) redefines
 * window.qt so that qt.session.getUser() → window.host.postMessage({channel:
 * "session", method: "getUser", data: [...]}), allowing the Vue3 SPA to
 * run without source modification.
 */
class DWPEChannelAdapter : public QObject
{
    Q_OBJECT

public:
    using ChannelHandler = std::function<QVariant(const QString &method, const QVariantList &args)>;

    explicit DWPEChannelAdapter(DWPEBridge *bridge, QObject *parent = nullptr);
    ~DWPEChannelAdapter() override;

    // Register a channel handler (e.g. "session", "window", "assistant", ...)
    void registerChannel(const QString &name, ChannelHandler handler);

    // Push a signal to the JS side (native → JS channel signal)
    void emitSignal(const QString &channel, const QString &signal, const QVariant &data);

    // Get list of registered channel names
    QStringList registeredChannels() const;

private:
    DWPEBridge *m_bridge{nullptr};

    // channel name → handler
    QHash<QString, ChannelHandler> m_channelHandlers;

    // Handle incoming message from bridge: {channel, method, data}
    QVariant handleMessage(const QVariantMap &message);
};

}  // namespace DTKWPE

#endif  // DWPE_CHANNEL_ADAPTER_H
