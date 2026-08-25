/*
 * This file is part of dtkwebkit.
 *
 * Copyright (c) 2026 UnionTech Software Technology Co., Ltd.
 *
 * LGPL-3.0-or-later
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#ifndef DWPE_SCHEME_HANDLER_H
#define DWPE_SCHEME_HANDLER_H

#include "wpe_export.h"

#include <QObject>
#include <QString>
#include <QHash>
#include <QByteArray>

#include <wpe/webkit.h>

namespace DTKWPE {

/**
 * @brief Handles app:// custom scheme for serving Vite build artifacts.
 *
 * Loads Vite dist/ into an in-memory file system (QHash<path, ResourceEntry>).
 * When WebKit requests app:///assets/index-xxx.js, the handler:
 *   1. Looks up the path in the memory fs
 *   2. If found -> return ResourceResponse with correct MIME
 *   3. If not found and path has no extension -> return index.html (history fallback)
 *   4. If not found and path has extension -> return 404
 *
 * Never falls back to file:// — all resources must be in the memory fs.
 */
class DWPESchemeHandler : public QObject
{
    Q_OBJECT

public:
    struct ResourceEntry
    {
        QByteArray data;
        QString mimeType;
    };

    explicit DWPESchemeHandler(QObject *parent = nullptr);

    // Register the app:// scheme with WebKitWebContext
    void registerScheme(WebKitWebContext *context);

    // Load Vite dist/ directory into memory fs
    void loadFromDirectory(const QString &dirPath);

    // Add a single resource
    void addResource(const QString &path, const QByteArray &data, const QString &mimeType);

    // Get the index.html content (SPA entry point)
    QByteArray indexHtml() const;

private:
    // The URI scheme request callback (called by WebKit)
    static void onUriSchemeRequest(WebKitURISchemeRequest *request, gpointer userData);

    // Memory file system: path -> resource
    QHash<QString, ResourceEntry> m_memoryFs;

    // MIME type detection
    static QString mimeTypeForPath(const QString &path);

    // Check if path has a file extension
    static bool hasFileExtension(const QString &path);
};

}  // namespace DTKWPE

#endif  // DWPE_SCHEME_HANDLER_H
