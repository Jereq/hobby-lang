// SPDX-License-Identifier: MIT
// Copyright © 2022 Sebastian Larsson
#pragma once

#include <hobbylang/ast/ast.hpp>

#include <istream>
#include <string_view>

namespace jereq
{
Program parse(std::string_view input, std::string_view name);
Program parse(std::istream& input, std::string_view name);
}
