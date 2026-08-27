/*
 * DWPEBridge -- Native <-> JS bidirectional bridge via window.host.
 *
 * Copyright (c) 2026 UnionTech Software Technology Co., Ltd.
 * LGPL-3.0-or-later
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include "wpe_export.h"
#include "wpe_bridge.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>

#include <wpe/webkit.h>
#include <jsc/jsc.h>

DTKWPE_BEGIN_NAMESPACE

// The user script injected into every page at document start.
// Creates window.host with postMessage/onMessage and a qt Proxy for
// QWebChannel 12-channel compatibility (frontend source zero modification).
//
// postMessage uses window.webkit.messageHandlers.host.postMessage() which
// returns a Promise (WPE WebKit 2.40+ with-reply API).
// Bridge code injected into the page's MAIN JavaScript world via
// webkit_web_view_evaluate_javascript. User scripts (webkit_user_script_new)
// run in an ISOLATED world invisible to page JS, so we inject the bridge
// code directly via evaluate_javascript on WEBKIT_LOAD_COMMITTED instead.
//
// Defines:
//   window.host — postMessage/onMessage bridge to native
//   window.qt — QWebChannel Proxy compatibility
//   window.__TAURI_IPC__ — Tauri IPC stub for Tauri-built apps
static const char *s_mainWorldScript = R"JS(
(function() {
    if (window.__dtkwebkitBridgeInjected) return;
    window.__dtkwebkitBridgeInjected = true;

    var messageId = 0;
    var pendingPromises = {};
    var channelCallbacks = {};

    window.host = {
        postMessage: function(msg) {
            var id = ++messageId;
            var payload = JSON.stringify({ id: id, data: msg });
            return window.webkit.messageHandlers.host.postMessage(payload).then(
                function(reply) {
                    var result = reply;
                    try { result = JSON.parse(reply); } catch(e) {}
                    if (pendingPromises[id]) {
                        pendingPromises[id].resolve(result);
                        delete pendingPromises[id];
                    }
                    return result;
                },
                function(error) {
                    if (pendingPromises[id]) {
                        pendingPromises[id].reject(error);
                        delete pendingPromises[id];
                    }
                    return Promise.reject(error);
                }
            );
        },
        onMessage: null,
        _resolve: function(id, result) {
            if (pendingPromises[id]) {
                pendingPromises[id].resolve(result);
                delete pendingPromises[id];
            }
        },
        _reject: function(id, error) {
            if (pendingPromises[id]) {
                pendingPromises[id].reject(error);
                delete pendingPromises[id];
            }
        },
        _registerChannelCallback: function(channel, signal, callback) {
            if (!channelCallbacks[channel]) channelCallbacks[channel] = {};
            channelCallbacks[channel][signal] = callback;
        },
        _emitSignal: function(channel, signal, data) {
            if (channelCallbacks[channel] && channelCallbacks[channel][signal]) {
                channelCallbacks[channel][signal](data);
            }
        }
    };

    // QWebChannel compatibility: window.qt proxy routes to host bridge
    window.qt = new Proxy({}, {
        get: function(_, channel) {
            return new Proxy({}, {
                get: function(_, method) {
                    return function() {
                        var args = Array.prototype.slice.call(arguments);
                        return window.host.postMessage({
                            channel: channel,
                            method: method,
                            data: args
                        });
                    };
                }
            });
        }
    });

    // Tauri IPC compatibility: stub window.__TAURI_IPC__
    // Tauri apps call window.__TAURI_IPC__({cmd, callback, error, ...args})
    // and expect window['_' + callback](result) to be called.
    window.__TAURI_IPC__ = function(opts) {
        if (!opts || !opts.cmd) return;
        var cmd = opts.cmd;
        var callbackId = opts.callback;
        var errorId = opts.error;
        var args = {};
        for (var k in opts) {
            if (k !== 'cmd' && k !== 'callback' && k !== 'error')
                args[k] = opts[k];
        }
        window.host.postMessage({channel: 'tauri', method: cmd, data: args}).then(
            function(reply) {
                if (callbackId && window['_' + callbackId]) {
                    var result = reply;
                    if (typeof reply === 'string') {
                        try { result = JSON.parse(reply); } catch(e) {}
                    }
                    window['_' + callbackId](result);
                }
            },
            function(error) {
                if (errorId && window['_' + errorId]) {
                    window['_' + errorId](String(error));
                }
            }
        );
    };
})();
)JS";

DWPEBridge::DWPEBridge(WebKitWebView *webView, QObject *parent)
    : QObject(parent)
    , m_webView(webView)
{
}

DWPEBridge::~DWPEBridge() = default;

void DWPEBridge::initialize()
{
    if (!m_webView)
        return;

    m_ucm = webkit_web_view_get_user_content_manager(m_webView);
    if (!m_ucm) {
        qWarning() << "DWPEBridge: no UserContentManager available";
        return;
    }

    // 1. Connect to script-message-with-reply-received::host BEFORE registering
    //    the handler (recommended by WebKit docs to avoid race conditions).
    g_signal_connect(m_ucm, "script-message-with-reply-received::host", G_CALLBACK(onScriptMessageReceived), this);

    // 2. Register the "host" script message handler with reply support
    //    (enables Promise round-trip: JS postMessage returns a Promise)
    gboolean ok = webkit_user_content_manager_register_script_message_handler_with_reply(m_ucm, "host", nullptr);
    if (!ok)
        qWarning() << "DWPEBridge: failed to register host message handler with reply";

    // 3. Inject dark background at document end (DOM changes are visible
    // across all worlds, so a user script in the isolated world works).
    // Tauri apps rely on the OS window's dark theme; without a body
    // background, semi-transparent white elements (#ffffff14) are invisible.
    static const char *s_bgScript = R"JS(
        (function() {
            var style = document.createElement('style');
            style.id = 'dtkwebkit-dark-bg';
            style.textContent = 'body{background:#1a1a2e !important;color:#fff;margin:0;}';
            document.head.appendChild(style);
        })();
    )JS";
    WebKitUserScript *bgScript = webkit_user_script_new(
        s_bgScript, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES, WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END, nullptr, nullptr);
    webkit_user_content_manager_add_script(m_ucm, bgScript);
    webkit_user_script_unref(bgScript);
}

void DWPEBridge::injectIntoMainWorld()
{
    if (!m_webView)
        return;

    // Inject bridge code into the main JavaScript world via evaluate_javascript.
    // world_name=NULL means the default world, which for evaluate_javascript
    // is the PAGE'S MAIN WORLD (unlike user scripts which run in an isolated world).
    // Called on WEBKIT_LOAD_COMMITTED, before page scripts execute.
    webkit_web_view_evaluate_javascript(m_webView, s_mainWorldScript, -1, nullptr, nullptr, nullptr, nullptr, nullptr);
}

void DWPEBridge::postMessage(const QVariant &message)
{
    if (!m_webView)
        return;

    QJsonDocument doc = QJsonDocument::fromVariant(message);
    QString json = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));

    // Escape for JS string literal
    QString escaped = json;
    escaped.replace('\\', "\\\\").replace('\'', "\\'").replace('\n', "\\n");

    QString js = QString("if (window.host && window.host.onMessage) { window.host.onMessage('%1'); }").arg(escaped);

    webkit_web_view_evaluate_javascript(m_webView, js.toUtf8().constData(), -1, nullptr, nullptr, nullptr, nullptr, nullptr);
}

QString DWPEBridge::userScriptSource()
{
    return QString::fromUtf8(s_mainWorldScript);
}

gboolean DWPEBridge::onScriptMessageReceived(WebKitUserContentManager *ucm,
                                             JSCValue *value,
                                             WebKitScriptMessageReply *reply,
                                             gpointer userData)
{
    auto *bridge = static_cast<DWPEBridge *>(userData);
    if (!bridge || !bridge->m_messageHandler)
        return FALSE;

    // Convert JSCValue (JSON string) to QVariantMap
    QVariantMap msgData = jscValueToVariantMap(value);

    // Call the message handler
    QVariant result = bridge->m_messageHandler(msgData);

    // Return reply to JS (resolves the Promise)
    if (reply) {
        JSCContext *context = jsc_value_get_context(value);
        if (result.isValid()) {
            QJsonDocument replyDoc = QJsonDocument::fromVariant(result);
            QByteArray replyJson = replyDoc.toJson(QJsonDocument::Compact);
            JSCValue *replyValue = jsc_value_new_string(context, replyJson.constData());
            webkit_script_message_reply_return_value(reply, replyValue);
            g_object_unref(replyValue);
        } else {
            JSCValue *undefined = jsc_value_new_undefined(context);
            webkit_script_message_reply_return_value(reply, undefined);
            g_object_unref(undefined);
        }
    }

    return TRUE;
}

QVariantMap DWPEBridge::jscValueToVariantMap(JSCValue *value)
{
    QVariantMap result;

    if (!value || jsc_value_is_undefined(value) || jsc_value_is_null(value))
        return result;

    // The JS side sends a JSON string via postMessage
    if (jsc_value_is_string(value)) {
        const char *str = jsc_value_to_string(value);
        if (str) {
            QJsonDocument doc = QJsonDocument::fromJson(QByteArray(str));
            if (doc.isObject())
                result = doc.object().toVariantMap();
            g_free(const_cast<char *>(str));
        }
    }

    return result;
}

DTKWPE_END_NAMESPACE
