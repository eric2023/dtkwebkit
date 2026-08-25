/*
 * DWPEChannelAdapter — Routes 12 QWebChannel channels to native handlers.
 *
 * Copyright (c) 2026 UnionTech Software Technology Co., Ltd.
 * LGPL-3.0-or-later
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include "wpe_export.h"
#include "wpe_channel_adapter.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

DTKWPE_BEGIN_NAMESPACE

DWPEChannelAdapter::DWPEChannelAdapter(DWPEBridge *bridge, QObject *parent)
    : QObject(parent)
    , m_bridge(bridge)
{
    if (m_bridge) {
        m_bridge->setMessageHandler([this](const QVariantMap &message) { return this->handleMessage(message); });
    }
}

DWPEChannelAdapter::~DWPEChannelAdapter() = default;

void DWPEChannelAdapter::registerChannel(const QString &name, ChannelHandler handler)
{
    m_channelHandlers[name] = std::move(handler);
    qDebug() << "DWPEChannelAdapter: registered channel" << name;
}

void DWPEChannelAdapter::emitSignal(const QString &channel, const QString &signal, const QVariant &data)
{
    if (!m_bridge)
        return;

    // Push signal to JS side via window.host._emitSignal
    QVariantMap msg;
    msg["type"] = "signal";
    msg["channel"] = channel;
    msg["signal"] = signal;
    msg["data"] = data;

    m_bridge->postMessage(msg);
}

QStringList DWPEChannelAdapter::registeredChannels() const
{
    return m_channelHandlers.keys();
}

QVariant DWPEChannelAdapter::handleMessage(const QVariantMap &message)
{
    // Extract channel, method, data from the message
    // The JS-side qt Proxy sends: {channel: "session", method: "getUser", data: [...]}
    QString channel = message.value("channel").toString();
    QString method = message.value("method").toString();
    QVariantList args = message.value("data").toList();

    if (channel.isEmpty() || method.isEmpty()) {
        qWarning() << "DWPEChannelAdapter: missing channel or method in message" << message;
        return QVariant();
    }

    auto it = m_channelHandlers.find(channel);
    if (it == m_channelHandlers.end()) {
        qWarning() << "DWPEChannelAdapter: no handler for channel" << channel;
        return QVariant();
    }

    return it.value()(method, args);
}

DTKWPE_END_NAMESPACE
