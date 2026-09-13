#pragma once

namespace juce
{
    struct JUCE_API RawKeyEvent
    {
        int keyCode;
        bool keyDown;
    };

    class JUCE_API RawKeyEventSink
    {
    public:
        virtual void handleRawKeyEvent(const RawKeyEvent &) = 0;
    };
} // namespace juce