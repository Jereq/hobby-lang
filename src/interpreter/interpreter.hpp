// SPDX-License-Identifier: MIT
// Copyright © 2022 Sebastian Larsson
#pragma once

#include "../ast/ast.hpp"

#include <cstdint>

namespace jereq
{
std::int32_t execute(Program const& program);
}
