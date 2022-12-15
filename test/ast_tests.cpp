// SPDX-License-Identifier: MIT
// Copyright © 2022 Sebastian Larsson

#include <hobbylang/ast/ast.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("AST equals operator compare equal", "[AST]")
{
	REQUIRE(jereq::FuncParameter{} == jereq::FuncParameter{});

	jereq::FuncParameter const funcParam1{ "name" };
	jereq::FuncParameter const funcParam2{ "name" };
	jereq::FuncParameter const funcParam3{ "other name" };
	REQUIRE(funcParam1 == funcParam2);
	REQUIRE(funcParam1 != funcParam3);

	jereq::FuncType funcType1;
	funcType1.parameters.emplace_back(funcParam1);
	jereq::FuncType funcType2;
	funcType2.parameters.emplace_back(funcParam2);
	jereq::FuncType funcType3;
	funcType3.parameters.emplace_back(funcParam3);
	REQUIRE(funcType1 == funcType2);
	REQUIRE(funcType1 != funcType3);

	jereq::BuiltInType const builtInType1{"type"};
	jereq::BuiltInType const builtInType2{"type"};
	jereq::BuiltInType const builtInType3{"other type"};
	REQUIRE(builtInType1 == builtInType2);
	REQUIRE(builtInType1 != builtInType3);

	jereq::Type type1;
	type1.t = builtInType1;
	jereq::Type type2;
	type2.t = builtInType2;
	jereq::Type type3;
	type3.t = funcType1;
	REQUIRE(type1 == type2);
	REQUIRE(type1 != type3);
}
