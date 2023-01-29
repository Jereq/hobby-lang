// SPDX-License-Identifier: MIT
// Copyright © 2023 Sebastian Larsson
#pragma once

#include <hobbylang/ast/ast.hpp>

#include <cstdint>
#include <ostream>

namespace jereq
{
bool compile(Program const& program, std::ostream& out);
}
