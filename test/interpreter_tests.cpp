// SPDX-License-Identifier: MIT
// Copyright © 2022 Sebastian Larsson

#include <hobbylang/ast/ast.hpp>
#include <hobbylang/interpreter/interpreter.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>

TEST_CASE("Interpreter should execute minimal AST", "[interpreter]")
{
	jereq::Program program;
	auto const i32Type = std::make_shared<jereq::Type>();
	i32Type->t = jereq::BuiltInType{ "i32" };
	program.types.emplace_back(i32Type);

	jereq::FuncType funcType;
	funcType.parameters.emplace_back(jereq::FuncParameter{ "exitCode", jereq::ParameterDirection::out, i32Type });
	auto const mainFuncType = std::make_shared<jereq::Type>();
	mainFuncType->t = funcType;
	program.types.emplace_back(mainFuncType);

	std::shared_ptr<jereq::Function> const& mainFunc = std::make_shared<jereq::Function>();
	mainFunc->name = "main";
	mainFunc->sourceFile = "test case";
	mainFunc->type = mainFuncType;
	mainFunc->expression.expr = jereq::InitAssignment{ "exitCode", 0 };
	program.functions.emplace_back(mainFunc);
	program.mainFunction = mainFunc;

	REQUIRE(jereq::execute(program) == 0);
}
