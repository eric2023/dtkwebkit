/*
 * DWPESchemeHandler — app:// custom scheme for serving Vite build artifacts.
 *
 * Copyright (c) 2026 UnionTech Software Technology Co., Ltd.
 * LGPL-3.0-or-later
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include "wpe_export.h"
#include "wpe_scheme_handler.h"

#include <QDirIterator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QDebug>

#include <wpe/webkit.h>
#include <gio/gio.h>

DTKWPE_BEGIN_NAMESPACE

DWPESchemeHandler::DWPESchemeHandler(QObject *parent)
    : QObject(parent)
{
}

void DWPESchemeHandler::registerScheme(WebKitWebContext *context)
{
    webkit_web_context_register_uri_scheme(
        context,
        "app",
        [](WebKitURISchemeRequest *request, gpointer userData) {
            auto *handler = static_cast<DWPESchemeHandler *>(userData);
            handler->onUriSchemeRequest(request, handler);
        },
        this,
        nullptr);
}

void DWPESchemeHandler::loadFromDirectory(const QString &dirPath)
{
    QDir dir(dirPath);
    if (!dir.exists()) {
        qWarning() << "DWPESchemeHandler: directory does not exist:" << dirPath;
        return;
    }

    QDirIterator it(dirPath, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString filePath = it.next();
        QString relativePath = "/" + dir.relativeFilePath(filePath);

        QFile file(filePath);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray data = file.readAll();
            QString mime = mimeTypeForPath(relativePath);
            addResource(relativePath, data, mime);
        }
    }

    qDebug() << "DWPESchemeHandler: loaded" << m_memoryFs.size() << "resources from" << dirPath;
}

void DWPESchemeHandler::addResource(const QString &path, const QByteArray &data, const QString &mimeType)
{
    m_memoryFs[path] = {data, mimeType};
}

QByteArray DWPESchemeHandler::indexHtml() const
{
    auto it = m_memoryFs.find("/index.html");
    if (it != m_memoryFs.end())
        return it->data;
    return {};
}

void DWPESchemeHandler::onUriSchemeRequest(WebKitURISchemeRequest *request, gpointer userData)
{
    auto *handler = static_cast<DWPESchemeHandler *>(userData);

    const gchar *path = webkit_uri_scheme_request_get_path(request);
    QString qPath = QString::fromUtf8(path);

    auto it = handler->m_memoryFs.find(qPath);
    if (it != handler->m_memoryFs.end()) {
        GBytes *bytes = g_bytes_new(it->data.constData(), it->data.size());
        GInputStream *stream = g_memory_input_stream_new_from_bytes(bytes);
        webkit_uri_scheme_request_finish(request, stream, it->data.size(), it->mimeType.toUtf8().constData());
        g_input_stream_close(stream, nullptr, nullptr);
        g_bytes_unref(bytes);
        return;
    }

    if (!hasFileExtension(qPath)) {
        QByteArray indexData = handler->indexHtml();
        if (!indexData.isEmpty()) {
            GBytes *bytes = g_bytes_new(indexData.constData(), indexData.size());
            GInputStream *stream = g_memory_input_stream_new_from_bytes(bytes);
            webkit_uri_scheme_request_finish(request, stream, indexData.size(), "text/html");
            g_input_stream_close(stream, nullptr, nullptr);
            g_bytes_unref(bytes);
            return;
        }
    }

    GError *error = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Resource not found in app:// memory fs");
    webkit_uri_scheme_request_finish_error(request, error);
    g_error_free(error);
}

QString DWPESchemeHandler::mimeTypeForPath(const QString &path)
{
    QMimeDatabase db;
    QMimeType mime = db.mimeTypeForFile(path, QMimeDatabase::MatchExtension);
    return mime.name();
}

bool DWPESchemeHandler::hasFileExtension(const QString &path)
{
    int lastSlash = path.lastIndexOf('/');
    QString lastSegment = path.mid(lastSlash + 1);
    return lastSegment.contains('.');
}

DTKWPE_END_NAMESPACE
