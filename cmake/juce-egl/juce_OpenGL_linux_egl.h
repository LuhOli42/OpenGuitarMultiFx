/*
  ==============================================================================

   This file is part of the JUCE framework.
   Copyright (c) Raw Material Software Limited

   JUCE is an open source framework subject to commercial or open source
   licensing.

   By downloading, installing, or using the JUCE framework, or combining the
   JUCE framework with any other source code, object code, content or any other
   copyrightable work, you agree to the terms of the JUCE End User Licence
   Agreement, and all incorporated terms including the JUCE Privacy Policy and
   the JUCE Website Terms of Service, as applicable, which will bind you. If you
   do not agree to the terms of these agreements, we will not license the JUCE
   framework to you, and you must discontinue the installation or download
   process and cease use of the JUCE framework.

   JUCE End User Licence Agreement: https://juce.com/legal/juce-8-licence/
   JUCE Privacy Policy: https://juce.com/juce-privacy-policy
   JUCE Website Terms of Service: https://juce.com/juce-website-terms-of-service/

   Or:

   You may also use this code under the terms of the AGPLv3:
   https://www.gnu.org/licenses/agpl-3.0.en.html

   THE JUCE FRAMEWORK IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL
   WARRANTIES, WHETHER EXPRESSED OR IMPLIED, INCLUDING WARRANTY OF
   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, ARE DISCLAIMED.

  ==============================================================================
*/

namespace juce
{

struct XFreeDeleter
{
    void operator() (void* ptr) const
    {
        if (ptr != nullptr)
            X11Symbols::getInstance()->xFree (ptr);
    }
};

template <typename Data>
std::unique_ptr<Data, XFreeDeleter> makeXFreePtr (Data* raw) { return std::unique_ptr<Data, XFreeDeleter> (raw); }

//==============================================================================
// Defined in juce_Windowing_linux.cpp
void juce_LinuxAddRepaintListener (ComponentPeer*, Component* dummy);
void juce_LinuxRemoveRepaintListener (ComponentPeer*, Component* dummy);

class PeerListener : private ComponentMovementWatcher
{
public:
    PeerListener (Component& comp, Window embeddedWindow)
        : ComponentMovementWatcher (&comp),
          window (embeddedWindow),
          association (comp.getPeer(), window) {}

private:
    using ComponentMovementWatcher::componentMovedOrResized,
          ComponentMovementWatcher::componentVisibilityChanged;

    void componentMovedOrResized (bool, bool) override {}
    void componentVisibilityChanged() override {}

    void componentPeerChanged() override
    {
        // This should not be rewritten as a ternary expression or similar.
        // The old association must be destroyed before the new one is created.
        association = {};

        if (auto* comp = getComponent())
            association = ScopedWindowAssociation (comp->getPeer(), window);
    }

    Window window{};
    ScopedWindowAssociation association;
};

//==============================================================================
// OpenGuitarMultiFx: EGL + OpenGL ES backend for X11 (replaces the GLX one). The Radxa Cubie A7S's PowerVR BXM only
// exposes OpenGL ES through EGL -- its Xorg has no hardware GLX (Mesa falls back to llvmpipe) -- so the stock GLX
// context would render in software. Built when JUCE_OPENGL_LINUX_EGL=1; see cmake/JuceEglPatch.cmake.
namespace egl_linux
{
    inline EGLDisplay getDisplay()
    {
        static EGLDisplay display = [] () -> EGLDisplay
        {
            auto* x = XWindowSystem::getInstance()->getDisplay();
            auto d = eglGetDisplay ((EGLNativeDisplayType) x);
            if (d == EGL_NO_DISPLAY)
                return EGL_NO_DISPLAY;
            EGLint major = 0, minor = 0;
            if (eglInitialize (d, &major, &minor) != EGL_TRUE)
                return EGL_NO_DISPLAY;
            eglBindAPI (EGL_OPENGL_ES_API);
            return d;
        }();
        return display;
    }
}

class OpenGLContext::NativeContext
{
private:
    struct DummyComponent  : public Component
    {
        DummyComponent (OpenGLContext::NativeContext& nativeParentContext)
            : native (nativeParentContext) {}

        void handleCommandMessage (int commandId) override
        {
            if (commandId == 0)
                native.triggerRepaint();
        }

        OpenGLContext::NativeContext& native;
    };

public:
    NativeContext (Component& comp,
                   const OpenGLPixelFormat& cPixelFormat,
                   void* shareContext,
                   bool useMultisamplingIn,
                   OpenGLVersion)
        : component (comp), contextToShareWith (shareContext), dummy (*this)
    {
        display = XWindowSystem::getInstance()->getDisplay();
        eglDisplay = egl_linux::getDisplay();
        if (eglDisplay == EGL_NO_DISPLAY)
            return;

        XWindowSystemUtilities::ScopedXLock xLock;
        X11Symbols::getInstance()->xSync (display, False);

        if (! chooseConfig (cPixelFormat, useMultisamplingIn) && ! chooseConfig (cPixelFormat, false))
            return;

        auto* peer = component.getPeer();
        jassert (peer != nullptr);
        auto windowH = (Window) peer->getNativeHandle();

        // The X visual the chosen EGL config renders to; the default visual when the driver does not name one.
        EGLint visualId = 0;
        eglGetConfigAttrib (eglDisplay, config, EGL_NATIVE_VISUAL_ID, &visualId);
        const auto screen = X11Symbols::getInstance()->xDefaultScreen (display);
        Visual* visual = X11Symbols::getInstance()->xDefaultVisual (display, screen);
        int depth = 24;
        {
            XVisualInfo templ{};
            templ.visualid = (VisualID) visualId;
            templ.screen = screen;
            int count = 0;
            auto infos = makeXFreePtr (X11Symbols::getInstance()->xGetVisualInfo (display, visualId != 0 ? (VisualIDMask | VisualScreenMask) : VisualScreenMask, &templ, &count));
            if (infos != nullptr && count > 0)
            {
                if (visualId != 0)
                {
                    visual = infos.get()[0].visual;
                    depth = infos.get()[0].depth;
                }
                else
                    for (int i = 0; i < count; ++i)
                        if (infos.get()[i].visual == visual)
                            depth = infos.get()[i].depth;
            }
        }

        auto colourMap = X11Symbols::getInstance()->xCreateColormap (display, windowH, visual, AllocNone);

        XSetWindowAttributes swa;
        swa.colormap = colourMap;
        swa.border_pixel = 0;
        swa.event_mask = embeddedWindowEventMask;

        auto glBounds = component.getTopLevelComponent()->getLocalArea (&component, component.getLocalBounds());
        glBounds = Desktop::getInstance().getDisplays().logicalToPhysical (glBounds);

        embeddedWindow = X11Symbols::getInstance()->xCreateWindow (display, windowH,
                                                                   glBounds.getX(), glBounds.getY(),
                                                                   (unsigned int) jmax (1, glBounds.getWidth()),
                                                                   (unsigned int) jmax (1, glBounds.getHeight()),
                                                                   0, depth, InputOutput, visual,
                                                                   CWBorderPixel | CWColormap | CWEventMask, &swa);

        peerListener.emplace (component, embeddedWindow);

        X11Symbols::getInstance()->xMapWindow (display, embeddedWindow);
        X11Symbols::getInstance()->xFreeColormap (display, colourMap);
        X11Symbols::getInstance()->xSync (display, False);

        juce_LinuxAddRepaintListener (peer, &dummy);
    }

    ~NativeContext()
    {
        if (auto* peer = component.getPeer())
        {
            juce_LinuxRemoveRepaintListener (peer, &dummy);

            if (embeddedWindow != 0)
            {
                XWindowSystemUtilities::ScopedXLock xLock;
                X11Symbols::getInstance()->xUnmapWindow (display, embeddedWindow);
                X11Symbols::getInstance()->xDestroyWindow (display, embeddedWindow);
                X11Symbols::getInstance()->xSync (display, False);

                XEvent event;
                while (X11Symbols::getInstance()->xCheckWindowEvent (display, embeddedWindow, embeddedWindowEventMask, &event) == True) {}
            }
        }
    }

    InitResult initialiseOnRenderThread (OpenGLContext& c)
    {
        if (eglDisplay == EGL_NO_DISPLAY || config == nullptr || embeddedWindow == 0)
            return InitResult::fatal;

        XWindowSystemUtilities::ScopedXLock xLock;
        eglBindAPI (EGL_OPENGL_ES_API);

        for (const EGLint version : { 3, 2 })
        {
            const EGLint attribs[] = { EGL_CONTEXT_CLIENT_VERSION, version, EGL_NONE };
            renderContext = eglCreateContext (eglDisplay, config, (EGLContext) contextToShareWith, attribs);
            if (renderContext != EGL_NO_CONTEXT)
                break;
        }
        if (renderContext == EGL_NO_CONTEXT)
            return InitResult::fatal;

        surface = eglCreateWindowSurface (eglDisplay, config, (EGLNativeWindowType) embeddedWindow, nullptr);
        if (surface == EGL_NO_SURFACE)
        {
            eglDestroyContext (eglDisplay, renderContext);
            renderContext = EGL_NO_CONTEXT;
            return InitResult::fatal;
        }

        c.makeActive();
        context = &c;
        return InitResult::success;
    }

    void shutdownOnRenderThread()
    {
        XWindowSystemUtilities::ScopedXLock xLock;
        context = nullptr;
        deactivateCurrentContext();
        if (surface != EGL_NO_SURFACE)    { eglDestroySurface (eglDisplay, surface); surface = EGL_NO_SURFACE; }
        if (renderContext != EGL_NO_CONTEXT) { eglDestroyContext (eglDisplay, renderContext); renderContext = EGL_NO_CONTEXT; }
    }

    bool makeActive() const noexcept
    {
        XWindowSystemUtilities::ScopedXLock xLock;
        return renderContext != EGL_NO_CONTEXT
                 && eglMakeCurrent (eglDisplay, surface, surface, renderContext) == EGL_TRUE;
    }

    bool isActive() const noexcept
    {
        return renderContext != EGL_NO_CONTEXT && eglGetCurrentContext() == renderContext;
    }

    static void deactivateCurrentContext()
    {
        const auto d = egl_linux::getDisplay();
        if (d != EGL_NO_DISPLAY)
        {
            XWindowSystemUtilities::ScopedXLock xLock;
            eglMakeCurrent (d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        }
    }

    void swapBuffers()
    {
        eglSwapBuffers (eglDisplay, surface);
    }

    void updateWindowPosition (Rectangle<int> newBounds)
    {
        bounds = newBounds;
        auto physicalBounds = Desktop::getInstance().getDisplays().logicalToPhysical (bounds);

        XWindowSystemUtilities::ScopedXLock xLock;
        X11Symbols::getInstance()->xMoveResizeWindow (display, embeddedWindow,
                                                      physicalBounds.getX(), physicalBounds.getY(),
                                                      (unsigned int) jmax (1, physicalBounds.getWidth()),
                                                      (unsigned int) jmax (1, physicalBounds.getHeight()));
    }

    bool setSwapInterval (int numFramesPerSwap)
    {
        if (numFramesPerSwap == swapFrames)
            return true;
        if (eglSwapInterval (eglDisplay, numFramesPerSwap) == EGL_TRUE)
        {
            swapFrames = numFramesPerSwap;
            return true;
        }
        return false;
    }

    int getSwapInterval() const                 { return swapFrames; }
    bool createdOk() const noexcept             { return true; }
    void* getRawContext() const noexcept        { return renderContext; }
    GLuint getFrameBufferID() const noexcept    { return 0; }

    void triggerRepaint()
    {
        if (context != nullptr)
            context->triggerRepaint();
    }

    struct Locker
    {
        explicit Locker (NativeContext& ctx) : lock (ctx.mutex) {}
        const ScopedLock lock;
    };

    void addListener (NativeContextListener&) {}
    void removeListener (NativeContextListener&) {}

private:
    bool chooseConfig (const OpenGLPixelFormat& format, bool multisample)
    {
        const EGLint attribs[] =
        {
            EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_RED_SIZE,        format.redBits,
            EGL_GREEN_SIZE,      format.greenBits,
            EGL_BLUE_SIZE,       format.blueBits,
            EGL_ALPHA_SIZE,      0, // the X window is opaque; an alpha channel only narrows the matching configs
            EGL_DEPTH_SIZE,      format.depthBufferBits,
            EGL_STENCIL_SIZE,    format.stencilBufferBits,
            EGL_SAMPLE_BUFFERS,  multisample ? 1 : 0,
            EGL_SAMPLES,         multisample ? format.multisamplingLevel : 0,
            EGL_NONE
        };

        EGLint count = 0;
        return eglChooseConfig (eglDisplay, attribs, &config, 1, &count) == EGL_TRUE && count > 0;
    }

    static constexpr int embeddedWindowEventMask = ExposureMask | StructureNotifyMask;

    CriticalSection mutex;
    Component& component;
    EGLDisplay eglDisplay = EGL_NO_DISPLAY;
    EGLConfig config = nullptr;
    EGLContext renderContext = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    Window embeddedWindow = {};

    std::optional<PeerListener> peerListener;

    int swapFrames = 0;
    Rectangle<int> bounds;
    void* contextToShareWith;

    OpenGLContext* context = nullptr;
    DummyComponent dummy;

    ::Display* display = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NativeContext)
};

//==============================================================================
bool OpenGLHelpers::isContextActive()
{
    return eglGetCurrentContext() != EGL_NO_CONTEXT;
}

} // namespace juce
