// SPDX-License-Identifier: MIT
// Copyright © 2022 Sebastian Larsson
#include "interpreter.hpp"

#include "../ast/ast.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace jereq
{
struct Local
{
	std::int32_t value;
};

struct Frame
{
	std::vector<Local> locals;
};

struct State
{
	void executeFunction(Function const& func, std::vector<Local*> args)
	{
		auto const& funcType = std::get<FuncType>(func.type->t);
		if (funcType.parameters.size() != 1)
		{
			throw std::runtime_error("Only functions with a single parameter is supported: " + funcType.rep);
		}
		FuncParameter const& funcParam = funcType.parameters.at(0);
		if (funcParam.direction != ParameterDirection::out)
		{
			throw std::runtime_error("Only output parameters are supported: " + funcType.rep);
		}
		if (!std::holds_alternative<BuiltInType>(funcParam.type->t))
		{
			throw std::runtime_error("Only built in types supported as parameter types: " + funcType.rep);
		}
		auto const& paramType = std::get<BuiltInType>(funcParam.type->t);
		if (paramType.name != "i32")
		{
			throw std::runtime_error("Only i32 support is implemented: " + funcType.rep);
		}

		if (!std::holds_alternative<InitAssignment>(func.expression.expr))
		{
			throw std::runtime_error("Unsupported expression: " + func.expression.rep);
		}

		auto const& initAssignment = std::get<InitAssignment>(func.expression.expr);
		if (initAssignment.var != funcParam.name)
		{
			throw std::runtime_error("Undeclared variable: " + initAssignment.var);
		}
		args.at(0)->value = initAssignment.value;
	}
};

std::int32_t execute(Program const& program)
{
	State programState{};
	Frame rootFrame{};
	Local& rLocal = rootFrame.locals.emplace_back();
	if (!program.mainFunction)
	{
		throw std::runtime_error("Missing main function");
	}
	programState.executeFunction(*program.mainFunction, { &rLocal });

	return rLocal.value;
}
}
