/*
 * DWPEEventTranslator — Qt input → WPE input event conversion.
 *
 * Copyright (c) 2026 UnionTech Software Technology Co., Ltd.
 * LGPL-3.0-or-later
 *
 * Reference: WPEPlatform GLFW example (Igalia) — the ONLY few-shot reference.
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include "wpe_export.h"
#include "wpe_event_translator.h"

#include <QGuiApplication>

DTKWPE_BEGIN_NAMESPACE

// Qt::Key → Linux evdev keycode mapping table
// Based on the standard Linux input event codes (linux/input-event-codes.h)
static const struct
{
    int qtKey;
    uint32_t linuxCode;
} s_keyMap[] = {
    {Qt::Key_Escape, 1},  // KEY_ESC
    {Qt::Key_1, 2},
    {Qt::Key_2, 3},
    {Qt::Key_3, 4},
    {Qt::Key_4, 5},
    {Qt::Key_5, 6},
    {Qt::Key_6, 7},
    {Qt::Key_7, 8},
    {Qt::Key_8, 9},
    {Qt::Key_9, 10},
    {Qt::Key_0, 11},
    {Qt::Key_Minus, 12},
    {Qt::Key_Equal, 13},
    {Qt::Key_Backspace, 14},  // KEY_BACKSPACE
    {Qt::Key_Tab, 15},
    {Qt::Key_Q, 16},
    {Qt::Key_W, 17},
    {Qt::Key_E, 18},
    {Qt::Key_R, 19},
    {Qt::Key_T, 20},
    {Qt::Key_Y, 21},
    {Qt::Key_U, 22},
    {Qt::Key_I, 23},
    {Qt::Key_O, 24},
    {Qt::Key_P, 25},
    {Qt::Key_BracketLeft, 26},
    {Qt::Key_BracketRight, 27},
    {Qt::Key_Return, 28},   // KEY_ENTER
    {Qt::Key_Control, 29},  // KEY_LEFTCTRL
    {Qt::Key_A, 30},
    {Qt::Key_S, 31},
    {Qt::Key_D, 32},
    {Qt::Key_F, 33},
    {Qt::Key_G, 34},
    {Qt::Key_H, 35},
    {Qt::Key_J, 36},
    {Qt::Key_K, 37},
    {Qt::Key_L, 38},
    {Qt::Key_Semicolon, 39},
    {Qt::Key_Apostrophe, 40},
    {Qt::Key_QuoteLeft, 41},  // KEY_GRAVE
    {Qt::Key_Shift, 42},      // KEY_LEFTSHIFT
    {Qt::Key_Backslash, 43},
    {Qt::Key_Z, 44},
    {Qt::Key_X, 45},
    {Qt::Key_C, 46},
    {Qt::Key_V, 47},
    {Qt::Key_B, 48},
    {Qt::Key_N, 49},
    {Qt::Key_M, 50},
    {Qt::Key_Comma, 51},
    {Qt::Key_Period, 52},
    {Qt::Key_Slash, 53},
    {Qt::Key_Alt, 56},  // KEY_LEFTALT
    {Qt::Key_Space, 57},
    {Qt::Key_CapsLock, 58},
    {Qt::Key_F1, 59},
    {Qt::Key_F2, 60},
    {Qt::Key_F3, 61},
    {Qt::Key_F4, 62},
    {Qt::Key_F5, 63},
    {Qt::Key_F6, 64},
    {Qt::Key_F7, 65},
    {Qt::Key_F8, 66},
    {Qt::Key_F9, 67},
    {Qt::Key_F10, 68},
    {Qt::Key_F11, 87},
    {Qt::Key_F12, 88},
    {Qt::Key_NumLock, 69},
    {Qt::Key_ScrollLock, 70},
    {Qt::Key_Home, 102},
    {Qt::Key_Up, 103},
    {Qt::Key_PageUp, 104},
    {Qt::Key_Left, 105},
    {Qt::Key_Right, 106},
    {Qt::Key_End, 107},
    {Qt::Key_Down, 108},
    {Qt::Key_PageDown, 109},
    {Qt::Key_Insert, 110},
    {Qt::Key_Delete, 111},
    {Qt::Key_Pause, 119},
};

static const int s_keyMapSize = sizeof(s_keyMap) / sizeof(s_keyMap[0]);

uint32_t DWPEEventTranslator::qtKeyToLinuxKeyCode(int qtKey)
{
    for (int i = 0; i < s_keyMapSize; ++i) {
        if (s_keyMap[i].qtKey == qtKey)
            return s_keyMap[i].linuxCode;
    }
    return 0;
}

DWPEEventTranslator::DWPEEventTranslator(struct wpe_view_backend *backend)
    : m_backend(backend)
{
}

DWPEEventTranslator::~DWPEEventTranslator() = default;

void DWPEEventTranslator::translateKeyEvent(QKeyEvent *event)
{
    if (!m_backend)
        return;

    m_keyboardEvent.time = static_cast<uint32_t>(event->timestamp());
    m_keyboardEvent.key_code = qtKeyToLinuxKeyCode(event->key());
    m_keyboardEvent.hardware_key_code = m_keyboardEvent.key_code;
    m_keyboardEvent.pressed = (event->type() == QEvent::KeyPress);

    // Modifiers
    uint32_t mods = 0;
    if (event->modifiers() & Qt::ControlModifier)
        mods |= wpe_input_keyboard_modifier_control;
    if (event->modifiers() & Qt::ShiftModifier)
        mods |= wpe_input_keyboard_modifier_shift;
    if (event->modifiers() & Qt::AltModifier)
        mods |= wpe_input_keyboard_modifier_alt;
    if (event->modifiers() & Qt::MetaModifier)
        mods |= wpe_input_keyboard_modifier_meta;
    m_keyboardEvent.modifiers = mods;

    wpe_view_backend_dispatch_keyboard_event(m_backend, &m_keyboardEvent);
}

void DWPEEventTranslator::translateMouseEvent(QMouseEvent *event)
{
    if (!m_backend)
        return;

    m_pointerEvent.type = wpe_input_pointer_event_type_button;
    m_pointerEvent.time = static_cast<uint32_t>(event->timestamp());
    m_pointerEvent.x = static_cast<int>(event->position().x() / m_devicePixelRatio);
    m_pointerEvent.y = static_cast<int>(event->position().y() / m_devicePixelRatio);

    // Button mapping
    if (event->type() == QEvent::MouseMove) {
        m_pointerEvent.type = wpe_input_pointer_event_type_motion;
        m_pointerEvent.button = 0;
        m_pointerEvent.state = 0;
    } else {
        uint32_t button = 0;
        uint32_t state = 0;
        if (event->button() == Qt::LeftButton) {
            button = 1;
            state = (event->type() == QEvent::MouseButtonPress) ? 1 : 0;
        } else if (event->button() == Qt::RightButton) {
            button = 2;
            state = (event->type() == QEvent::MouseButtonPress) ? 1 : 0;
        } else if (event->button() == Qt::MiddleButton) {
            button = 3;
            state = (event->type() == QEvent::MouseButtonPress) ? 1 : 0;
        }
        m_pointerEvent.button = button;
        m_pointerEvent.state = state;
    }

    // Modifiers
    uint32_t mods = 0;
    if (event->buttons() & Qt::LeftButton)
        mods |= wpe_input_pointer_modifier_button1;
    if (event->buttons() & Qt::RightButton)
        mods |= wpe_input_pointer_modifier_button2;
    if (event->buttons() & Qt::MiddleButton)
        mods |= wpe_input_pointer_modifier_button3;
    m_pointerEvent.modifiers = mods;

    wpe_view_backend_dispatch_pointer_event(m_backend, &m_pointerEvent);
}

void DWPEEventTranslator::translateWheelEvent(QWheelEvent *event)
{
    if (!m_backend)
        return;

    m_axisEvent.type = wpe_input_axis_event_type_motion_smooth;
    m_axisEvent.time = static_cast<uint32_t>(event->timestamp());
    m_axisEvent.x = static_cast<int>(event->position().x() / m_devicePixelRatio);
    m_axisEvent.y = static_cast<int>(event->position().y() / m_devicePixelRatio);

    // Vertical scroll
    m_axisEvent.axis = 0;
    m_axisEvent.value = -event->angleDelta().y() / 8;  // WPE expects steps

    wpe_view_backend_dispatch_axis_event(m_backend, &m_axisEvent);

    // Horizontal scroll (if any)
    if (event->angleDelta().x() != 0) {
        m_axisEvent.axis = 1;
        m_axisEvent.value = event->angleDelta().x() / 8;
        wpe_view_backend_dispatch_axis_event(m_backend, &m_axisEvent);
    }
}

void DWPEEventTranslator::translateTouchEvent(QTouchEvent *event)
{
    if (!m_backend)
        return;

    // TODO: implement touch event translation (M3)
    // Reference: WPEPlatform GLFW example touch handling
}

void DWPEEventTranslator::setFocused(bool focused)
{
    if (!m_backend)
        return;

    if (focused)
        wpe_view_backend_add_activity_state(
            m_backend, wpe_view_activity_state_visible | wpe_view_activity_state_focused | wpe_view_activity_state_in_window);
    else
        wpe_view_backend_remove_activity_state(m_backend, wpe_view_activity_state_focused);
}

void DWPEEventTranslator::setVisible(bool visible)
{
    if (!m_backend)
        return;

    if (visible)
        wpe_view_backend_add_activity_state(m_backend, wpe_view_activity_state_visible);
    else
        wpe_view_backend_remove_activity_state(m_backend, wpe_view_activity_state_visible);
}

void DWPEEventTranslator::setViewSize(int width, int height, float devicePixelRatio)
{
    m_devicePixelRatio = devicePixelRatio;
    if (!m_backend)
        return;

    uint32_t w = static_cast<uint32_t>(width * devicePixelRatio);
    uint32_t h = static_cast<uint32_t>(height * devicePixelRatio);
    wpe_view_backend_dispatch_set_size(m_backend, w, h);
    wpe_view_backend_dispatch_set_device_scale_factor(m_backend, devicePixelRatio);
}

DTKWPE_END_NAMESPACE
