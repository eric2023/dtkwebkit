/*
 * This file is part of dtkwebkit.
 *
 * Copyright (c) 2026 UnionTech Software Technology Co., Ltd.
 *
 * LGPL-3.0-or-later
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#ifndef DWPE_BRIDGE_H
#define DWPE_BRIDGE_H

#include "wpe_export.h"

#include <QObject>
#include <QString>
#include <QVariant>
#include <QHash>
#include <functional>

#include <wpe/webkit.h>

namespace DTKWPE {

/**
 * @brief Native <-> JS bidirectional bridge via window.host.
 *
 * Uses WebKitUserContentManager script-message-received signal for
 * JS -> native, and webkit_web_view_evaluate_javascript for native -> JS.
 *
 * JS side creates window.host:
 *   - postMessage(msg) -> Promise (round-trip via with-reply handler)
 *   - onMessage callback (native -> JS push)
 *   - qt Proxy (QWebChannel 12-channel compatibility)
 *
 * The JS adapter redefines window.qt as a Proxy that routes
 * qt.channelName.method() -> window.host.postMessage({channel, method, data}),
 * enabling the existing Vue3 SPA to call native channels without modification.
 */
class DWPEBridge : public QObject
{
    Q_OBJECT

public:
    using MessageHandler = std::function<QVariant(const QVariantMap &message)>;

    explicit DWPEBridge(WebKitWebView *webView, QObject *parent = nullptr);
    ~DWPEBridge() override;

    // Initialize: inject user script, register message handler, connect signals
    void initialize();

    // Send message JS-ward (native -> JS): calls window.host.onMessage(data)
    void postMessage(const QVariant &message);

    // Set handler for incoming JS messages (JS -> native)
    void setMessageHandler(MessageHandler handler) { m_messageHandler = std::move(handler); }

    // Get the injected user script source
    static QString userScriptSource();

private:
    WebKitWebView *m_webView{nullptr};
    WebKitUserContentManager *m_ucm{nullptr};
    MessageHandler m_messageHandler;

    // Signal handler for script-message-with-reply-received::host
    static gboolean
    onScriptMessageReceived(WebKitUserContentManager *ucm, JSCValue *value, WebKitScriptMessageReply *reply, gpointer userData);

    // Convert JSCValue to QVariantMap
    static QVariantMap jscValueToVariantMap(JSCValue *value);

    // Convert QVariant to JSCValue for reply
    static JSCValue *variantToJscValue(JSCContext *context, const QVariant &variant);
};

}  // namespace DTKWPE

#endif  // DWPE_BRIDGE_H
