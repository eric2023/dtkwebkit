/*
 * This file is part of dtkwebkit.
 *
 * Copyright (c) 2026 UnionTech Software Technology Co., Ltd.
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#ifndef DWPE_EXPORT_H
#define DWPE_EXPORT_H

#include <qglobal.h>

#ifdef DWPE_STATIC
#define DWPE_EXPORT
#define DWPE_NO_EXPORT
#else
#ifdef DWPE_LIBRARY
#define DWPE_EXPORT Q_DECL_EXPORT
#define DWPE_NO_EXPORT Q_DECL_HIDDEN
#else
#define DWPE_EXPORT Q_DECL_IMPORT
#define DWPE_NO_EXPORT Q_DECL_HIDDEN
#endif
#endif

#define DTKWPE_BEGIN_NAMESPACE namespace DTKWPE {
#define DTKWPE_END_NAMESPACE }

#define DTKWPE_USE_NAMESPACE using namespace DTKWPE;

#endif  // DWPE_EXPORT_H
