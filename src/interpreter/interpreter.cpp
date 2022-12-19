// SPDX-License-Identifier: MIT
// Copyright © 2022 Sebastian Larsson
#include <hobbylang/interpreter/interpreter.hpp>

#include <hobbylang/ast/ast.hpp>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace jereq
{
struct Local
{
	std::string name;
	std::int32_t value;
};

struct Frame
{
	std::vector<Local> locals;
};

struct ExpressionResult
{
	std::string type;// TODO: Replace
	std::int32_t value;
};

struct State
{
	ExpressionResult evaluateExpression(Frame& frame, Expression const& expr)// NOLINT(misc-no-recursion)
	{
		if (std::holds_alternative<Literal>(expr.expr))
		{
			auto const& literal = std::get<Literal>(expr.expr);
			return { "i32", literal.value };
		}
		else if (std::holds_alternative<InitAssignment>(expr.expr))
		{
			auto const& initAssignment = std::get<InitAssignment>(expr.expr);
			auto localIt
				= std::ranges::find(frame.locals, initAssignment.var, [](Local const& local) { return local.name; });
			if (localIt == frame.locals.end())
			{
				throw std::runtime_error("Undeclared variable: " + initAssignment.var);
			}

			ExpressionResult const& expressionResult = evaluateExpression(frame, *initAssignment.value);
			if (expressionResult.type != "i32")
			{
				throw std::runtime_error("Unexpected expression result type: " + expressionResult.type);
			}
			localIt->value = expressionResult.value;
			return { "", 0 };
		}
		else if (std::holds_alternative<Addition>(expr.expr))
		{
			auto const& addition = std::get<Addition>(expr.expr);
			auto [lhsType, lhsValue] = evaluateExpression(frame, *addition.lhs);
			auto [rhsType, rhsValue] = evaluateExpression(frame, *addition.rhs);
			if (lhsType != "i32" || rhsType != "i32")
			{
				throw std::runtime_error("Unexpected types for addition: " + lhsType + ", " + rhsType);
			}
			return { lhsType, lhsValue + rhsValue };
		}
		else
		{
			throw std::runtime_error("Unsupported expression: " + expr.rep);
		}
	}

	void executeFunction(Function const& func, std::vector<Local*> const& args)
	{
		Frame frame;

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

		Local& local = frame.locals.emplace_back();
		local.name = funcParam.name;

		auto [exprType, _] = evaluateExpression(frame, func.expression);
		if (!exprType.empty())
		{
			throw std::runtime_error("Function expression should not return a value");
		}

		args.at(0)->value = frame.locals.at(0).value;
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
