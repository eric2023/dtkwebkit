/*
 * DWPEInputMethodContext — Bridge Qt input method events to WebKit.
 *
 * Copyright (c) 2026 UnionTech Software Technology Co., Ltd.
 * LGPL-3.0-or-later
 */

#include "wpe_input_method_context.h"

#include <glib.h>
#include <glib-object.h>
#include <wpe/webkit.h>

#include <QDebug>

DTKWPE_BEGIN_NAMESPACE

// ---------------------------------------------------------------------------
// GObject type registration for a WebKitInputMethodContext subclass.
//
// WebKitInputMethodContext is a derivable GObject type. We define a subclass
// "DWpeImContext" whose virtual functions forward to the C++ bridge object.
// ---------------------------------------------------------------------------

#define DWPE_TYPE_IM_CONTEXT (dwpe_im_context_get_type())
#define DWPE_IM_CONTEXT(obj)        (G_TYPE_CHECK_INSTANCE_CAST((obj), DWPE_TYPE_IM_CONTEXT, DWpeImContext))
#define DWPE_IM_CONTEXT_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), DWPE_TYPE_IM_CONTEXT, DWpeImContextClass))
#define DWPE_IS_IM_CONTEXT(obj)     (G_TYPE_CHECK_INSTANCE_TYPE((obj), DWPE_TYPE_IM_CONTEXT))
#define DWPE_IS_IM_CONTEXT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), DWPE_TYPE_IM_CONTEXT))

typedef struct _DWpeImContext        DWpeImContext;
typedef struct _DWpeImContextClass   DWpeImContextClass;

struct _DWpeImContext
{
    WebKitInputMethodContext parent;
    DWPEInputMethodContext   *bridge;   // back-pointer to C++ object
    gboolean                  preeditEnabled;
};

struct _DWpeImContextClass
{
    WebKitInputMethodContextClass parent_class;
};

// --- Forward declarations of virtual function overrides -------------------

static void      dwpe_im_context_set_enable_preedit(WebKitInputMethodContext *ctx, gboolean enabled);
static void      dwpe_im_context_get_preedit(WebKitInputMethodContext *ctx, gchar **text, GList **underlines, guint *cursor_offset);
static gboolean  dwpe_im_context_filter_key_event(WebKitInputMethodContext *ctx, gpointer key_event);
static void      dwpe_im_context_notify_focus_in(WebKitInputMethodContext *ctx);
static void      dwpe_im_context_notify_focus_out(WebKitInputMethodContext *ctx);
static void      dwpe_im_context_notify_cursor_area(WebKitInputMethodContext *ctx, int x, int y, int width, int height);
static void      dwpe_im_context_notify_surrounding(WebKitInputMethodContext *ctx, const gchar *text, guint length, guint cursor_index, guint selection_index);
static void      dwpe_im_context_reset(WebKitInputMethodContext *ctx);

// --- GObject type implementation ------------------------------------------

static void dwpe_im_context_class_init(DWpeImContextClass *klass)
{
    auto *imClass = WEBKIT_INPUT_METHOD_CONTEXT_CLASS(klass);

    imClass->set_enable_preedit = dwpe_im_context_set_enable_preedit;
    imClass->get_preedit        = dwpe_im_context_get_preedit;
    imClass->filter_key_event   = dwpe_im_context_filter_key_event;
    imClass->notify_focus_in    = dwpe_im_context_notify_focus_in;
    imClass->notify_focus_out   = dwpe_im_context_notify_focus_out;
    imClass->notify_cursor_area = dwpe_im_context_notify_cursor_area;
    imClass->notify_surrounding = dwpe_im_context_notify_surrounding;
    imClass->reset              = dwpe_im_context_reset;
}

static void dwpe_im_context_init(DWpeImContext *self)
{
    self->bridge = nullptr;
    self->preeditEnabled = TRUE;
}

// Register the GType (idempotent).
static GType dwpe_im_context_get_type()
{
    static GType type = 0;
    if (G_UNLIKELY(type == 0)) {
        const GTypeInfo info = {
            sizeof(DWpeImContextClass),
            nullptr,  // base_init
            nullptr,  // base_finalize
            reinterpret_cast<GClassInitFunc>(dwpe_im_context_class_init),
            nullptr,  // class_finalize
            nullptr,  // class_data
            sizeof(DWpeImContext),
            0,        // n_preallocs
            reinterpret_cast<GInstanceInitFunc>(dwpe_im_context_init),
        };
        type = g_type_register_static(WEBKIT_TYPE_INPUT_METHOD_CONTEXT,
                                      "DWpeImContext", &info,
                                      GTypeFlags(0));
    }
    return type;
}

// --- Virtual function implementations -------------------------------------

static void dwpe_im_context_set_enable_preedit(WebKitInputMethodContext *ctx, gboolean enabled)
{
    auto *self = DWPE_IM_CONTEXT(ctx);
    self->preeditEnabled = enabled;
}

static void dwpe_im_context_get_preedit(WebKitInputMethodContext *ctx, gchar **text, GList **underlines, guint *cursor_offset)
{
    auto *self = DWPE_IM_CONTEXT(ctx);
    auto *bridge = self->bridge;
    if (text)
        *text = g_strdup(bridge ? bridge->preeditText().toUtf8().constData() : "");
    if (underlines)
        *underlines = nullptr;
    if (cursor_offset)
        *cursor_offset = bridge ? static_cast<guint>(bridge->preeditLength()) : 0;
}

static gboolean dwpe_im_context_filter_key_event(WebKitInputMethodContext *ctx, gpointer key_event)
{
    // We do not intercept hardware key events — Qt's input method handles IME
    // composition and delivers results via QInputMethodEvent. All hardware keys
    // should pass through to WPE for normal text entry.
    return FALSE;
}

static void dwpe_im_context_notify_focus_in(WebKitInputMethodContext *ctx)
{
    auto *self = DWPE_IM_CONTEXT(ctx);
    if (self->bridge)
        self->bridge->notifyFocusIn();
}
static void dwpe_im_context_notify_focus_out(WebKitInputMethodContext *ctx)
{
    auto *self = DWPE_IM_CONTEXT(ctx);
    if (self->bridge)
        self->bridge->notifyFocusOut();
}

static void dwpe_im_context_notify_surrounding(WebKitInputMethodContext *ctx, const gchar *text, guint length, guint cursor_index, guint selection_index)
{
    // We don't need to track surrounding text — Qt's input method manages this.
}

static void dwpe_im_context_notify_cursor_area(WebKitInputMethodContext *ctx, int x, int y, int width, int height)
{
    auto *self = DWPE_IM_CONTEXT(ctx);
    if (self->bridge)
        self->bridge->notifyCursorArea(x, y, width, height);
}


static void dwpe_im_context_reset(WebKitInputMethodContext *ctx)
{
    auto *self = DWPE_IM_CONTEXT(ctx);
    if (self->bridge)
        self->bridge->reset();
}

// --- C++ bridge class implementation --------------------------------------

DWPEInputMethodContext::DWPEInputMethodContext()
{
    m_context = WEBKIT_INPUT_METHOD_CONTEXT(g_object_new(DWPE_TYPE_IM_CONTEXT, nullptr));
    auto *self = DWPE_IM_CONTEXT(m_context);
    self->bridge = this;
}

DWPEInputMethodContext::~DWPEInputMethodContext()
{
    if (m_context) {
        auto *self = DWPE_IM_CONTEXT(m_context);
        self->bridge = nullptr;
        g_object_unref(m_context);
        m_context = nullptr;
    }
}

void DWPEInputMethodContext::setPreeditText(const QString &preedit, int cursorPos)
{
    bool wasEmpty = m_currentPreedit.isEmpty();
    m_currentPreedit = preedit;

    if (preedit.isEmpty()) {
        if (!wasEmpty)
            g_signal_emit_by_name(m_context, "preedit-finished");
    } else {
        if (wasEmpty)
            g_signal_emit_by_name(m_context, "preedit-started");
        g_signal_emit_by_name(m_context, "preedit-changed");
    }
}

void DWPEInputMethodContext::commitText(const QString &text)
{
    if (!m_currentPreedit.isEmpty()) {
        m_currentPreedit.clear();
        g_signal_emit_by_name(m_context, "preedit-finished");
    }
    g_signal_emit_by_name(m_context, "committed",
                          text.toUtf8().constData());
}

void DWPEInputMethodContext::notifyFocusIn()
{
    // Qt's input method is managed by the platform plugin — no action needed.
}

void DWPEInputMethodContext::notifyFocusOut()
{
    // Clear preedit on focus loss.
    if (!m_currentPreedit.isEmpty()) {
        m_currentPreedit.clear();
        g_signal_emit_by_name(m_context, "preedit-finished");
    }
}

void DWPEInputMethodContext::notifyCursorArea(int x, int y, int width, int height)
{
    m_cursorRect = QRect(x, y, width, height);
    // Forward the cursor rectangle to Qt's input method system so the
    // platform IME (e.g. fcitx5) positions its candidate window at the
    // correct on-screen location relative to the text cursor.
    // The DWPEView reads this via inputMethodQuery(ImCursorRectangle).
}

void DWPEInputMethodContext::reset()
{
    if (!m_currentPreedit.isEmpty()) {
        m_currentPreedit.clear();
        g_signal_emit_by_name(m_context, "preedit-finished");
    }
}

DTKWPE_END_NAMESPACE
