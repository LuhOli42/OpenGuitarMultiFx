#pragma once

#include "Theme.h"
#include "TouchSizing.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace openguitarmultifx
{

/**
    Touch keyboard for the device (kiosk) build. The panel has no physical keyboard and the app is the only X
    client (no window manager, so no system on-screen keyboard either): every juce::TextEditor that takes focus
    gets this keyboard slid over the top or bottom of the window, whichever leaves the field visible.

    Keys never take keyboard focus (setMouseClickGrabsKeyboardFocus (false)), so the editor keeps its caret while
    the keys are tapped; text goes in through insertTextAtCaret()/keyPressed(), which is what a hardware keyboard
    ends up calling anyway (onTextChange, onReturnKey and friends fire normally).
*/
class OnScreenKeyboard : public juce::Component
{
public:
    OnScreenKeyboard()
    {
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
        setOpaque (true);
        rebuild();
    }

    std::function<void (const juce::String&)> onText;
    std::function<void()> onBackspace;
    std::function<void()> onEnter;
    std::function<void()> onClose;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::current().surface);
        g.setColour (theme::current().lineStrong);
        g.fillRect (getLocalBounds().removeFromTop (1));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10, 8);
        const int rows = (int) layout.size();
        const int gap = 6;
        const int rowHeight = (area.getHeight() - gap * (rows - 1)) / rows;

        int idx = 0;
        for (auto& row : layout)
        {
            auto rowArea = area.removeFromTop (rowHeight);
            area.removeFromTop (gap);

            float totalWeight = 0.0f;
            for (auto& spec : row)
                totalWeight += spec.weight;

            const float unit = (float) (rowArea.getWidth() - gap * ((int) row.size() - 1)) / totalWeight;
            float x = (float) rowArea.getX();
            for (auto& spec : row)
            {
                const int w = juce::roundToInt (unit * spec.weight);
                keys[(size_t) idx++]->setBounds ((int) x, rowArea.getY(), w, rowHeight);
                x += (float) w + (float) gap;
            }
        }
    }

private:
    enum class Kind { character, shift, backspace, enter, layer, space, close };

    struct Spec
    {
        juce::String label;
        Kind kind;
        float weight;
    };

    class Key : public juce::Component
    {
    public:
        Key (OnScreenKeyboard& ownerIn, Spec specIn) : owner (ownerIn), spec (std::move (specIn))
        {
            setWantsKeyboardFocus (false);
            setMouseClickGrabsKeyboardFocus (false);
        }

        void paint (juce::Graphics& g) override
        {
            const auto& tk = theme::current();
            const bool special = spec.kind != Kind::character && spec.kind != Kind::space;
            auto fill = down ? tk.raisedHigh : (special ? tk.raisedHigh.darker (0.15f) : tk.raised);
            if (spec.kind == Kind::shift && owner.shifted)
                fill = tk.text.withAlpha (0.85f);

            auto b = getLocalBounds().toFloat();
            g.setColour (fill);
            g.fillRoundedRectangle (b, tk.smallRadius);
            g.setColour (tk.lineStrong);
            g.drawRoundedRectangle (b.reduced (0.5f), tk.smallRadius, 1.0f);

            g.setColour (spec.kind == Kind::shift && owner.shifted ? tk.background : tk.text);
            g.setFont (juce::Font (juce::FontOptions ((float) getHeight() * 0.42f)));
            g.drawText (displayLabel(), getLocalBounds(), juce::Justification::centred, false);
        }

        void mouseDown (const juce::MouseEvent&) override { down = true; repaint(); }
        void mouseUp (const juce::MouseEvent& e) override
        {
            down = false;
            repaint();
            if (getLocalBounds().contains (e.getPosition()))
                owner.pressed (spec);
        }

    private:
        juce::String displayLabel() const
        {
            if (spec.kind == Kind::character && owner.shifted)
                return spec.label.toUpperCase();
            return spec.label;
        }

        OnScreenKeyboard& owner;
        Spec spec;
        bool down = false;
    };

    void pressed (const Spec& spec)
    {
        switch (spec.kind)
        {
            case Kind::character:
            {
                const auto text = shifted ? spec.label.toUpperCase() : spec.label;
                if (onText != nullptr) onText (text);
                if (shifted) { shifted = false; repaintKeys(); }
                break;
            }
            case Kind::space:     if (onText != nullptr) onText (" "); break;
            case Kind::backspace: if (onBackspace != nullptr) onBackspace(); break;
            case Kind::enter:     if (onEnter != nullptr) onEnter(); break;
            case Kind::close:     if (onClose != nullptr) onClose(); break;
            case Kind::shift:     shifted = ! shifted; repaintKeys(); break;
            case Kind::layer:     symbols = ! symbols; shifted = false; rebuild(); break;
        }
    }

    void repaintKeys() { for (auto& k : keys) k->repaint(); }

    static std::vector<Spec> chars (const char* letters, float weight = 1.0f)
    {
        std::vector<Spec> row;
        for (const char* p = letters; *p != 0; ++p)
            row.push_back ({ juce::String::charToString ((juce::juce_wchar) *p), Kind::character, weight });
        return row;
    }

    void rebuild()
    {
        keys.clear();
        layout.clear();

        auto numbers = chars ("1234567890");
        numbers.push_back ({ "<", Kind::backspace, 1.6f });
        layout.push_back (numbers);

        if (! symbols)
        {
            layout.push_back (chars ("qwertyuiop"));
            auto row3 = chars ("asdfghjkl");
            row3.push_back ({ "OK", Kind::enter, 1.6f });
            layout.push_back (row3);
            auto row4 = std::vector<Spec> { { "^", Kind::shift, 1.5f } };
            for (auto& s : chars ("zxcvbnm")) row4.push_back (s);
            row4.push_back ({ "-", Kind::character, 1.0f });
            row4.push_back ({ "_", Kind::character, 1.0f });
            layout.push_back (row4);
        }
        else
        {
            layout.push_back (chars ("@#&+()/:;!"));
            layout.push_back (chars ("?'\"*=%$[]{}"));
            auto row4 = chars (".,<>~|\\^`");
            row4.push_back ({ "OK", Kind::enter, 1.6f });
            layout.push_back (row4);
        }

        layout.push_back ({ { symbols ? "abc" : "123", Kind::layer, 1.6f },
                            { ".", Kind::character, 1.0f },
                            { "space", Kind::space, 6.0f },
                            { ",", Kind::character, 1.0f },
                            { "fechar", Kind::close, 1.8f } });

        removeAllChildren();
        for (auto& row : layout)
            for (auto& spec : row)
            {
                keys.push_back (std::make_unique<Key> (*this, spec));
                addAndMakeVisible (*keys.back());
            }
        resized();
        repaint();
    }

    std::vector<std::vector<Spec>> layout;
    std::vector<std::unique_ptr<Key>> keys;
    bool shifted = false;
    bool symbols = false;
};

/**
    Shows/hides one OnScreenKeyboard as text editors gain and lose focus. Only meant to be created in kiosk mode.
*/
class OnScreenKeyboardManager : private juce::FocusChangeListener,
                                private juce::Timer
{
public:
    OnScreenKeyboardManager()
    {
        keyboard.onText = [this] (const juce::String& t)
        {
            if (target != nullptr) target->insertTextAtCaret (t);
        };
        keyboard.onBackspace = [this]
        {
            if (target != nullptr) target->keyPressed (juce::KeyPress (juce::KeyPress::backspaceKey));
        };
        keyboard.onEnter = [this]
        {
            if (target != nullptr) target->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
            hide();
        };
        keyboard.onClose = [this] { hide(); };

        juce::Desktop::getInstance().addFocusChangeListener (this);
        startTimer (250);
    }

    ~OnScreenKeyboardManager() override
    {
        juce::Desktop::getInstance().removeFocusChangeListener (this);
        hide();
    }

private:
    void globalFocusChanged (juce::Component* focused) override
    {
        if (auto* editor = dynamic_cast<juce::TextEditor*> (focused); editor != nullptr && ! editor->isReadOnly())
            show (*editor);
    }

    void timerCallback() override
    {
        if (target != nullptr && ! target->isShowing())
            hide();
    }

    void show (juce::TextEditor& editor)
    {
        auto* window = editor.getTopLevelComponent();
        if (window == nullptr)
            return;

        // The GPU renderer is attached to the window's CONTENT, not to the window: anything added to the window itself
        // is never drawn (the GL surface covers it) yet still takes the touches -- an invisible keyboard that also
        // swallowed the bottom half of the screen. Host the keyboard inside the content so it is drawn with the rest.
        auto* top = window;
        if (auto* resizable = dynamic_cast<juce::ResizableWindow*> (window))
            if (auto* content = resizable->getContentComponent())
                top = content;

        target = &editor;
        if (keyboard.getParentComponent() != top)
            top->addChildComponent (keyboard);

        const int height = juce::jlimit (240, 420, juce::roundToInt ((float) top->getHeight() * 0.44f));
        const auto fieldBounds = top->getLocalArea (&editor, editor.getLocalBounds());
        const bool fieldLowInWindow = fieldBounds.getCentreY() > top->getHeight() / 2;

        keyboard.setBounds (0, fieldLowInWindow ? 0 : top->getHeight() - height, top->getWidth(), height);
        keyboard.setVisible (true);
        keyboard.toFront (false);
    }

    void hide()
    {
        target = nullptr;
        keyboard.setVisible (false);
        if (auto* parent = keyboard.getParentComponent())
            parent->removeChildComponent (&keyboard);
    }

    OnScreenKeyboard keyboard;
    juce::Component::SafePointer<juce::TextEditor> target;
};

} // namespace openguitarmultifx
