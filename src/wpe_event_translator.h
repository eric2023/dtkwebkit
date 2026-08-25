/*
 * This file is part of dtkwebkit.
 *
 * Copyright (c) 2026 UnionTech Software Technology Co., Ltd.
 *
 * LGPL-3.0-or-later
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#ifndef DWPE_EVENT_TRANSLATOR_H
#define DWPE_EVENT_TRANSLATOR_H

#include "wpe_export.h"

#include <QObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <cstdint>
#include <wpe/wpe.h>
#include <QTouchEvent>

namespace DTKWPE {

/**
 * @brief Translates Qt input events to WPE input events.
 *
 * Maps:
 *   - Qt::Key -> Linux keycode (evdev table)
 *   - QMouseEvent coordinates -> WPE pointer (divided by devicePixelRatio)
 *   - QWheelEvent -> WPE axis event
 *   - QTouchEvent -> WPE touch event
 *   - FocusIn/Out -> WPE activity state
 *
 * Reference: WPEPlatform GLFW example (Igalia) -- the ONLY reference
 * for event translation. Do not invent mappings.
 */
class DWPEEventTranslator
{
public:
    explicit DWPEEventTranslator(::wpe_view_backend *backend = nullptr);
    ~DWPEEventTranslator();

    void setViewBackend(::wpe_view_backend *backend) { m_backend = backend; }

    // Initialize the xkb keymap so WPE can convert evdev keycodes to text.
    // Must be called once after the backend is connected.
    void initializeXkbKeymap();

    // Event dispatch
    void translateKeyEvent(QKeyEvent *event);
    void translateMouseEvent(QMouseEvent *event);
    void translateWheelEvent(QWheelEvent *event);
    void translateTouchEvent(QTouchEvent *event);

    // Focus/activity state
    void setFocused(bool focused);
    void setVisible(bool visible);

    // View size (WPE backend dispatch)
    void setViewSize(int width, int height, float devicePixelRatio);

private:
    ::wpe_view_backend *m_backend{nullptr};


    // Qt::Key -> Linux evdev keycode mapping
    static uint32_t qtKeyToLinuxKeyCode(int qtKey);

    // Track modifier state for xkb key code resolution
    uint32_t m_modifiers{0};

    // xkb keymap handle (for proper key→text conversion)
    struct wpe_input_xkb_context *m_xkbContext{nullptr};

    // Throttle mouse-move dispatches to avoid excessive repainting.
    uint32_t m_lastMouseMoveTime{0};
    static constexpr uint32_t kMouseMoveThrottleMs = 16; // ~60 FPS max

    // WPE input event structs (stack-allocated, dispatched immediately)
    struct wpe_input_keyboard_event m_keyboardEvent
    {
    };
    struct wpe_input_pointer_event m_pointerEvent
    {
    };
    struct wpe_input_axis_event m_axisEvent
    {
    };

    float m_devicePixelRatio{1.0f};
};

}  // namespace DTKWPE

#endif  // DWPE_EVENT_TRANSLATOR_H
