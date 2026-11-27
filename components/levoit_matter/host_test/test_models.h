#pragma once
// ModelSpec fixtures for the host tests. The firmware fills ModelSpec from the
// core at runtime (purifier.cpp); the tests exercise the mapping with fixed
// specs instead, so they do not depend on which model a build selects.
#include "purifier_model.h"

namespace levoit_matter {
inline constexpr ModelSpec CORE_300S{"Core 300S", 3, true, true};
inline constexpr ModelSpec SPROUT{"Sprout", 4, true, true};
inline constexpr ModelSpec EVERESTAIR{"EverestAir", 3, true, true};
}  // namespace levoit_matter
