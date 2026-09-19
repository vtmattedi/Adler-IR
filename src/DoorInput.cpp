#include <DoorInput.h>

static constexpr uint32_t DOOR_MAX_AGE_MS = 10UL * 60UL * 1000UL;

bool DoorInput::begin(NightMare::ResourceManager& resources, const String& owner,
                      const String& resourceId, bool invert) {
    if (!owner.length() || !resourceId.length() || _value) return false;
    _owner = owner;
    _resourceId = resourceId;
    _invert = invert;
    auto* value = new NightMare::NetValue<bool>(_resourceId.c_str());
    if (!value) return false;
    if (!resources.mirror(*value, _owner.c_str())) {
        delete value;
        return false;
    }
    _value = value;
    _resources = &resources;
    return true;
}

NightMare::ResourceFreshness DoorInput::freshness() const {
    return _value ? _resources->freshness(*_value, DOOR_MAX_AGE_MS)
                  : NightMare::ResourceFreshness::UNKNOWN;
}

uint32_t DoorInput::ageMs() const {
    return _value ? _resources->ageMs(*_value) : UINT32_MAX;
}

bool DoorInput::read(bool& open) const {
    if (freshness() != NightMare::ResourceFreshness::FRESH) return false;
    open = _invert ? !_value->get() : _value->get();
    return true;
}
