#pragma once
#include <NightMare/Resources/ResourceManager.h>

// One remote Value binding. The manager owns its freshness timestamp; this
// class only retains the configured address and inversion policy.
class DoorInput {
public:
    bool begin(NightMare::ResourceManager& resources, const String& owner,
               const String& resourceId, bool invert);
    bool read(bool& open) const;
    bool bound() const { return _value != nullptr; }
    const String& owner() const { return _owner; }
    const String& resourceId() const { return _resourceId; }
    bool inverted() const { return _invert; }
    NightMare::ResourceFreshness freshness() const;
    uint32_t ageMs() const;
private:
    NightMare::ResourceManager* _resources = nullptr;
    NightMare::NetValue<bool>* _value = nullptr;
    String _owner;
    String _resourceId;
    bool _invert = false;
};
