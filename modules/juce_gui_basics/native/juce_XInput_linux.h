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

// Internal declarations for the Linux XInput backport.

#pragma once

#if JUCE_USE_XINPUT
namespace juce::XInputHelpers
{
    std::optional<int> setupXI2 (::Display*);
    void registerForXI2Events (::Display*, ::Window);
    void deleteAllTouchesForPeer (ComponentPeer*);
}
#endif
