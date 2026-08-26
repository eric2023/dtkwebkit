/*
 * DWPEInputMethodContext — Bridge Qt input method events to WebKit.
 *
 * Copyright (c) 2026 UnionTech Software Technology Co., Ltd.
 * LGPL-3.0-or-later
 */

#ifndef DWPE_INPUT_METHOD_CONTEXT_H
#define DWPE_INPUT_METHOD_CONTEXT_H

#include "wpe_export.h"

#include <wpe/webkit.h>
#include <QRect>

DTKWPE_BEGIN_NAMESPACE

/**
 * @brief Bridges Qt's input method (IME) events to WebKit's input method context.
 *
 * Qt delivers QInputMethodEvent with preedit text and commit text.
 * This class wraps WebKitInputMethodContext and forwards:
 *   - Preedit text (composition in-progress) via preedit_changed signal
 *   - Commit text (final composition) via committed signal
 *   - Focus in/out via notify_focus_in/out
 *   - Cursor area via notify_cursor_area
 *
 * The DWPEView calls setPreeditText() / commitText() when it receives
 * QInputMethodEvent from Qt's input method system.
 */
class DWPEInputMethodContext
{
public:
    DWPEInputMethodContext();
    ~DWPEInputMethodContext();

    // Returns the WebKitInputMethodContext to pass to webkit_web_view_set_input_method_context.
    WebKitInputMethodContext *context() const { return m_context; }

    // Called by DWPEView when a QInputMethodEvent arrives.
    // Sets the preedit (composition) text and notifies WebKit.
    void setPreeditText(const QString &preedit, int cursorPos);

    // Called by DWPEView when the IME commits final text.
    void commitText(const QString &text);

    // Called when the input field gains/loses focus.
    void notifyFocusIn();
    void notifyFocusOut();

    // Called when the cursor position changes.
    void notifyCursorArea(int x, int y, int width, int height);

    // Returns the last cursor rectangle reported by WebKit.
    QRect cursorRect() const { return m_cursorRect; }

    // Called to reset the IM state (e.g. on focus change).
    void reset();

    // Accessors for C-style GObject callbacks (defined in .cpp).
    QString preeditText() const { return m_currentPreedit; }
    int preeditLength() const { return m_currentPreedit.length(); }

private:
    WebKitInputMethodContext *m_context{nullptr};
    QRect m_cursorRect;
    QString m_currentPreedit;
};

DTKWPE_END_NAMESPACE

#endif // DWPE_INPUT_METHOD_CONTEXT_H
