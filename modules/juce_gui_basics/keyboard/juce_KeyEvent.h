namespace juce
{

class JUCE_API  KeyEvent
{
public:
    const int rawKeyCode;
    const bool keyDown;
    
    KeyEvent(int rawKeyCode, bool keyDown);

    ~KeyEvent() = default;
};
}
