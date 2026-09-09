/*
 * This file is part of dtkwebkit.
 *
 * Copyright (c) 2026 UnionTech Software Technology Co., Ltd.
 *
 * LGPL-3.0-or-later
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Qt5/Qt6 compatibility shims.
 *
 * Qt 6 introduced QPointF-returning position() accessors on QMouseEvent
 * and QWheelEvent, replacing the int-returning x()/y() from Qt 5.
 * Qt 6 also introduced QVariant::typeId(), while Qt 5 only has
 * QVariant::userType() (both return int; userType is available in both).
 *
 * This header provides inline wrappers so source files can call
 * dtkwebkit::eventPos(event) and dtkwebkit::variantTypeId(variant)
 * uniformly across both versions.
 */

#ifndef DTKWEBKIT_QT_COMPAT_H
#define DTKWEBKIT_QT_COMPAT_H

#include <QtGlobal>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QMouseEvent>
#include <QWheelEvent>
#include <QVariant>
#include <QPointF>
#else
#include <QMouseEvent>
#include <QWheelEvent>
#include <QVariant>
#endif

namespace DTKWPE {

// QMouseEvent / QWheelEvent position: returns local coordinates as QPointF.
// Qt 6: event->position()
// Qt 5: event->localPos() (QPointF, available since 5.0)
inline QPointF eventPos(const QMouseEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position();
#else
    return event->localPos();
#endif
}

inline QPointF eventPos(const QWheelEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position();
#else
    return event->posF();
#endif
}

// QVariant type id: returns the QMetaType id of the stored value.
// Qt 6: variant.typeId()
// Qt 5: variant.userType()  (available in Qt 6 too, identical result)
inline int variantTypeId(const QVariant &variant)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return variant.typeId();
#else
    return variant.userType();
#endif
}

}  // namespace DTKWPE

#endif  // DTKWEBKIT_QT_COMPAT_H
