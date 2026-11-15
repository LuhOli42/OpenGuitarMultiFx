#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_opengl/juce_opengl.h>

#include <atomic>

namespace openguitarmultifx
{

/**
    Draws the whole window on the GPU (OpenGL ES through EGL, see cmake/JuceEglPatch.cmake) instead of JUCE's software
    renderer, which on the device cost most of a little core for the UI thread plus Xorg pushing every repainted pixel.

    No silent fallback, by choice: if the GL context has not come up a few seconds after attaching, or comes up on a
    software rasteriser (llvmpipe/softpipe), the app logs it and quits with an error, so a CPU-drawn UI is never
    mistaken for the GPU one. OGMFX_GL=0 runs on the CPU renderer on purpose (desktop debugging, a broken driver).
*/
class GpuRendering : private juce::OpenGLRenderer,
                     private juce::Timer
{
public:
    void attachTo (juce::Component& target)
    {
        if (juce::SystemStats::getEnvironmentVariable ("OGMFX_GL", "1") == "0")
        {
            juce::Logger::writeToLog ("GPU rendering: off (OGMFX_GL=0)");
            return;
        }
        context.setRenderer (this);
        context.setComponentPaintingEnabled (true);
        context.setContinuousRepainting (false);
        context.attachTo (target);
        startTimer (6000);
    }

    ~GpuRendering() override
    {
        stopTimer();
        context.detach();
    }

private:
    void newOpenGLContextCreated() override
    {
        const auto* renderer = reinterpret_cast<const char*> (juce::gl::glGetString (juce::gl::GL_RENDERER));
        const auto* version = reinterpret_cast<const char*> (juce::gl::glGetString (juce::gl::GL_VERSION));
        const juce::String name (renderer != nullptr ? renderer : "?");
        const bool software = name.containsIgnoreCase ("llvmpipe") || name.containsIgnoreCase ("softpipe")
                               || name.containsIgnoreCase ("swrast");
        juce::Logger::writeToLog ("GPU rendering: " + name + " / " + juce::String (version != nullptr ? version : "?"));
        status.store (software ? softwareRenderer : onGpu);
    }

    void renderOpenGL() override { juce::OpenGLHelpers::clear (juce::Colours::black); }
    void openGLContextClosing() override {}

    void timerCallback() override
    {
        stopTimer();
        const auto s = status.load();
        if (s == onGpu)
            return;
        juce::Logger::writeToLog (s == softwareRenderer
                                      ? "GPU rendering: FAILED -- the GL context is a software rasteriser; quitting (OGMFX_GL=0 runs on the CPU)"
                                      : "GPU rendering: FAILED -- no OpenGL context after 6 s; quitting (OGMFX_GL=0 runs on the CPU)");
        context.detach();
        if (auto* app = juce::JUCEApplicationBase::getInstance())
            app->setApplicationReturnValue (3);
        juce::JUCEApplicationBase::quit();
    }

    enum Status { pending, onGpu, softwareRenderer };
    juce::OpenGLContext context;
    std::atomic<int> status { pending };
};

} // namespace openguitarmultifx
