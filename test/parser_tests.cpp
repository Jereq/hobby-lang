// SPDX-License-Identifier: MIT
// Copyright © 2022 Sebastian Larsson

#include <hobbylang/ast/ast.hpp>
#include <hobbylang/parser/parser.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string_view>

TEST_CASE("Parser should handle minimal program", "[parser]")
{
	std::string_view const input = "def main = fun(out exitCode: i32) { exitCode = 0i32; };";
	jereq::Program program = jereq::parse(input, "test name");

	REQUIRE(program.types.size() == 2);
	REQUIRE(program.functions.size() == 1);
	REQUIRE(program.mainFunction == program.functions.at(0));
	REQUIRE(program.mainFunction->name == "main");
}
