// SPDX-License-Identifier: MIT
// Copyright © 2022-2023 Sebastian Larsson
#include <hobbylang/wasm/wasm.hpp>

#include <hobbylang/ast/ast.hpp>

#include <ostream>

namespace jereq
{
bool compile(Program const& program, std::ostream& out)
{
	out.write("test\n", 5);
	out << program.mainFunction->sourceFile;
	return static_cast<bool>(out);
}
}
