/*
  ==============================================================================

   This file is part of the JUCE 9 preview.
   Copyright (c) Raw Material Software Limited

   You may use this code under the terms of the AGPLv3
   (see www.gnu.org/licenses).

   For the JUCE 9 preview this file cannot be licensed commercially.

   JUCE IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
   EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
   DISCLAIMED.

  ==============================================================================
*/

// Backported from JUCE 032a1f433b and 55b2f9cf66. Included by juce_gui_basics.cpp.

namespace juce
{

//=============================== X11 - XInput =================================
#if JUCE_USE_XINPUT
 namespace XInputHelpers
 {
     static MultiTouchMapper<int> currentTouches;

     void deleteAllTouchesForPeer (ComponentPeer* peer)
     {
         currentTouches.deleteAllTouchesForPeer (peer);
     }

     std::optional<int> setupXI2 (::Display* display)
     {
         if (display == nullptr)
             return {};

         static const auto result = std::invoke ([display]() -> std::optional<int>
         {
             int xinputOpcode, first_event, first_error;
             if (! X11Symbols::getInstance()->xQueryExtension (display, "XInputExtension", &xinputOpcode, &first_event, &first_error))
                 return {};

             int major = 2, minor = 2;
             if (X11Symbols::getInstance()->xiQueryVersion (display, &major, &minor) != Success)
                return {};

             if (std::tuple (major, minor) < std::tuple (2, 2))
                 return {};

             unsigned char maskData[XIMaskLen (XI_LASTEVENT)] = {};
             XISetMask (maskData, XI_HierarchyChanged);

             XIEventMask eventMask;
             eventMask.deviceid = XIAllDevices;
             eventMask.mask_len = sizeof (maskData);
             eventMask.mask = maskData;

             X11Symbols::getInstance()->xiSelectEvents (display,
                                                        X11Symbols::getInstance()->xDefaultRootWindow (display),
                                                        &eventMask,
                                                        1);
             X11Symbols::getInstance()->xFlush (display);

             return xinputOpcode;
         });

         return result;
     }

     void registerForXI2Events (::Display* display, ::Window windowH)
     {
         if (! setupXI2 (display))
             return;

         const auto shouldHandleMouseClicks = std::invoke ([windowH]
         {
             if (auto* peer = getPeerFor (windowH))
                 return (peer->getStyleFlags() & ComponentPeer::windowIgnoresMouseClicks) == 0;

             return true;
         });

         int numDevices = 0;
         auto* info = X11Symbols::getInstance()->xiQueryDevice (display, XIAllDevices, &numDevices);

         if (info == nullptr)
             return;

         const ScopeGuard scope { [info] { X11Symbols::getInstance()->xiFreeDeviceInfo (info); } };

         for (int i = 0; i < numDevices; ++i)
         {
             const auto& deviceInfo = info[i];
             if (deviceInfo.use != XISlavePointer)
                continue;

             unsigned char maskData[XIMaskLen (XI_LASTEVENT)] = {};

             if (shouldHandleMouseClicks)
             {
                 const auto isTouchCapable = std::any_of (deviceInfo.classes,
                                                          deviceInfo.classes + deviceInfo.num_classes,
                                                          [] (const auto& x) { return x->type == XITouchClass; });

                 if (isTouchCapable)
                 {
                    XISetMask (maskData, XI_TouchBegin);
                    XISetMask (maskData, XI_TouchUpdate);
                    XISetMask (maskData, XI_TouchEnd);
                 }
             }

             const auto isButtonCapable = std::any_of (deviceInfo.classes,
                                                       deviceInfo.classes + deviceInfo.num_classes,
                                                       [] (const auto& x) { return x->type == XIButtonClass; });

             if (isButtonCapable)
             {
                 XISetMask (maskData, XI_Motion);

                 if (shouldHandleMouseClicks)
                 {
                    XISetMask (maskData, XI_ButtonPress);
                    XISetMask (maskData, XI_ButtonRelease);
                 }
             }

             XIEventMask eventMask;
             eventMask.deviceid = deviceInfo.deviceid;
             eventMask.mask_len = sizeof (maskData);
             eventMask.mask = maskData;

             X11Symbols::getInstance()->xiSelectEvents (display,
                                                        windowH,
                                                        &eventMask,
                                                        1);
         }

         X11Symbols::getInstance()->xFlush (display);
     }
 }
#endif

#if JUCE_USE_XINPUT
void XWindowSystem::handleXIDeviceEvent (LinuxComponentPeer* peer, int eventType, XIDeviceEvent& deviceEvent) const
{
    const Point eventPos { deviceEvent.event_x, deviceEvent.event_y };

    switch (eventType)
    {
        case XI_ButtonPress:
        {
            handleButtonPressEvent (peer,
                                    deviceEvent.mods.effective,
                                    deviceEvent.detail,
                                    deviceEvent.time,
                                    eventPos);
            return;
        }
        case XI_ButtonRelease:
        {
            handleButtonReleaseEvent (peer,
                                      deviceEvent.mods.effective,
                                      deviceEvent.detail,
                                      deviceEvent.time,
                                      eventPos);
            return;
        }
        case XI_Motion:
        {
            handleMotionNotifyEvent (peer,
                                     deviceEvent.mods.effective,
                                     deviceEvent.time,
                                     eventPos);
            return;
        }
    }

    updateKeyModifiers (deviceEvent.mods.effective);
    const auto keyboardMods = ModifierKeys::getCurrentModifiers().withoutMouseButtons();

    const auto touchIndex = XInputHelpers::currentTouches.getIndexOfTouch (peer, deviceEvent.detail);
    const auto touchPos = getLogicalMousePos (eventPos, *peer);
    const auto time = getEventTime (deviceEvent.time);

    const auto sendTouchEvent = [peer, time, touchIndex] (Point<float> pos, ModifierKeys mods)
    {
        peer->handleMouseEvent (MouseInputSource::InputSourceType::touch,
                                pos,
                                mods,
                                MouseInputSource::defaultPressure,
                                MouseInputSource::defaultOrientation,
                                time,
                                {},
                                touchIndex);

        // In case this component was deleted by the event
        return ComponentPeer::isValidPeer (peer);
    };

    switch (eventType)
    {
        case XI_TouchBegin:
        {
            // This forces a mouse-enter/up event, in case we didn't get one before.
            if (! sendTouchEvent (touchPos, keyboardMods))
                return;

            break;
        }
        case XI_TouchEnd:
        {
            XInputHelpers::currentTouches.clearTouch (touchIndex);
            break;
        }
    }

    const auto mouseKeys = eventType == XI_TouchEnd ? keyboardMods
                                                  : keyboardMods.withFlags (ModifierKeys::leftButtonModifier);
    if (! sendTouchEvent (touchPos, mouseKeys))
        return;

    if (eventType == XI_TouchEnd)
        sendTouchEvent (MouseInputSource::offscreenMousePos, keyboardMods);
}

void XWindowSystem::updateXInputDevices() const
{
    for (auto wh : windowHandles)
        XInputHelpers::registerForXI2Events (display, wh);
}
#endif

#if JUCE_USE_XINPUT
bool XWindowSystem::handleXInputEvent (XEvent& event) const
{
    if (auto* xDisplay = getDisplay();
        const auto xInputOpcode = XInputHelpers::setupXI2 (xDisplay))
    {
        // We need to collect and process these mouse events via XInput so that
        // it is possible to detect, and then ignore (via XISlavePointer),
        // emulated mouse events when a touch device is connected.
        switch (event.xany.type)
        {
            case ButtonPress:
            case ButtonRelease:
            case MotionNotify:
                return true;
        }

        if (event.xcookie.type == GenericEvent && event.xcookie.extension == xInputOpcode)
        {

            // You can only call xGetEventData once per event, even if you free the data afterwards
            if (! X11Symbols::getInstance()->xGetEventData (xDisplay, &event.xcookie))
                return true;

            const ScopeGuard scope { [xDisplay, &event] { X11Symbols::getInstance()->xFreeEventData (xDisplay, &event.xcookie); }};

            switch (event.xcookie.evtype)
            {
                case XI_HierarchyChanged:
                    updateXInputDevices();
                    break;

                case XI_ButtonPress:
                case XI_ButtonRelease:
                case XI_Motion:
                case XI_TouchBegin:
                case XI_TouchEnd:
                case XI_TouchUpdate:
                    if (auto* deviceEvent = (XIDeviceEvent*) event.xcookie.data;
                        auto* peer = dynamic_cast<LinuxComponentPeer*> (getPeerFor (deviceEvent->event)))
                        handleXIDeviceEvent (peer, event.xcookie.evtype, *deviceEvent);

                    break;

                default:
                    // Unhandled event type
                    jassertfalse;
                    break;
            }

            return true;
        }
    }
    return false;
}
#endif

} // namespace juce
