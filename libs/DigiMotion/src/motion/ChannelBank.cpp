/*
 * DigiMotion - ChannelBank impl (Layer 3)
 * Copyright (C) 2026 DigiCo LLC
 * Licensed under the GNU Affero General Public License version 3 or later.
 */

#include "ChannelBank.h"

ChannelBank::ChannelBank() : _count(0) {
    for (int i = 0; i < MAX_CHANNELS; ++i) _channels[i] = nullptr;
}

bool ChannelBank::addChannel(IActuatorChannel* channel) {
    if (channel == nullptr) return false;
    for (int i = 0; i < MAX_CHANNELS; ++i) {
        if (_channels[i] == channel) return false;  // duplicate
    }
    for (int i = 0; i < MAX_CHANNELS; ++i) {
        if (_channels[i] == nullptr) {
            _channels[i] = channel;
            ++_count;
            return true;
        }
    }
    return false;  // full
}

bool ChannelBank::removeChannel(IActuatorChannel* channel) {
    if (channel == nullptr) return false;
    for (int i = 0; i < MAX_CHANNELS; ++i) {
        if (_channels[i] == channel) {
            _channels[i] = nullptr;
            --_count;
            return true;
        }
    }
    return false;
}

int ChannelBank::count() const {
    return _count;
}

IActuatorChannel* ChannelBank::at(int index) const {
    if (index < 0 || index >= MAX_CHANNELS) return nullptr;
    return _channels[index];
}

void ChannelBank::setTargets(const long* targets, int n) {
    if (targets == nullptr || n <= 0) return;
    int idx = 0;
    for (int slot = 0; slot < MAX_CHANNELS && idx < n; ++slot) {
        if (_channels[slot] != nullptr) {
            _channels[slot]->setTarget(targets[idx]);
            ++idx;
        }
    }
}

void ChannelBank::setTarget(int index, long target) {
    if (index < 0 || index >= MAX_CHANNELS) return;
    if (_channels[index] == nullptr) return;
    _channels[index]->setTarget(target);
}

bool ChannelBank::allReached() const {
    if (_count == 0) return true;
    for (int i = 0; i < MAX_CHANNELS; ++i) {
        if (_channels[i] == nullptr) continue;
        if (!_channels[i]->hasReachedTarget()) return false;
    }
    return true;
}
