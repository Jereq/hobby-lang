// SPDX-License-Identifier: MIT
// Copyright © 2022 Sebastian Larsson
#include <hobbylang/interpreter/interpreter.hpp>

#include <hobbylang/ast/ast.hpp>

#include <fmt/core.h>

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
	std::int32_t value = 0;
};

struct ParameterValue
{
	std::string name;
	std::int32_t value = 0;
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
	Program const* program;

	struct ExpressionVisitor
	{
		State* self;
		Frame* frame;

		ExpressionResult operator()(Literal const& literal) { return { "i32", literal.value }; }

		ExpressionResult operator()(InitAssignment const& initAssignment)
		{
			auto localIt
				= std::ranges::find(frame->locals, initAssignment.var, [](Local const& local) { return local.name; });
			if (localIt == frame->locals.end())
			{
				throw std::runtime_error("Undeclared variable: " + initAssignment.var);
			}

			ExpressionResult const& expressionResult = self->evaluateExpression(*frame, *initAssignment.value);
			if (expressionResult.type != "i32")
			{
				throw std::runtime_error("Unexpected expression result type: " + expressionResult.type);
			}
			localIt->value = expressionResult.value;
			return { "", 0 };
		}

		ExpressionResult operator()(BinaryOpExpression const& binaryOp)
		{
			auto [lhsType, lhsValue] = self->evaluateExpression(*frame, *binaryOp.lhs);
			auto [rhsType, rhsValue] = self->evaluateExpression(*frame, *binaryOp.rhs);

			if (lhsType != "i32" || rhsType != "i32")
			{
				throw std::runtime_error("Unexpected types for addition: " + lhsType + ", " + rhsType);
			}

			std::int32_t result = -1;
			switch (binaryOp.op)
			{
			case BinaryOperator::add:
				result = lhsValue + rhsValue;
				break;
			case BinaryOperator::subtract:
				result = lhsValue - rhsValue;
				break;
			case BinaryOperator::multiply:
				result = lhsValue * rhsValue;
				break;
			case BinaryOperator::divide:
				result = lhsValue / rhsValue;
				break;
			case BinaryOperator::modulo:
				result = lhsValue % rhsValue;
				break;
			default:
				throw std::runtime_error(
					"Unexpected binary operator: "
					+ std::to_string(static_cast<std::underlying_type_t<BinaryOperator>>(binaryOp.op)));
			}
			return { lhsType, result };
		}

		ExpressionResult operator()(FunctionCall const& functionCall)
		{
			auto funcIt = std::ranges::find_if(self->program->functions,
				[&](std::shared_ptr<Function> const& func) { return func->name == functionCall.functionName; });
			if (funcIt == self->program->functions.cend())
			{
				throw std::runtime_error(fmt::format("Couldn't find function {}", functionCall.functionName));
			}
			std::shared_ptr<Function> const& function = *funcIt;

			std::vector<ParameterValue> inArgs;
			for (auto const& arg : functionCall.arguments)
			{
				if (arg.direction == ParameterDirection::in)
				{
					auto [argType, argValue] = self->evaluateExpression(*frame, arg.expr);
					if (argType != "i32")
					{
						throw std::runtime_error("Only i32 is implemented");
					}
					inArgs.push_back(ParameterValue{ arg.name, argValue });
				}
				else if (arg.direction == ParameterDirection::out)
				{
					throw std::runtime_error("Named output arguments not implemented");
				}
				else
				{
					throw std::runtime_error("Unknown direction (inout?) when calling function not implemented");
				}
			}

			std::vector<ParameterValue> outArgs;
			for (auto const& param : std::get<FuncType>(function->type->t).parameters)
			{
				if (param.direction == ParameterDirection::out)
				{
					outArgs.push_back(ParameterValue{ param.name, 0 });
				}
			}

			self->executeFunction(*function, inArgs, outArgs);

			if (outArgs.empty())
			{
				return { "", 0 };
			}
			else if (outArgs.size() == 1)
			{
				return { "i32", outArgs[0].value };
			}
			else
			{
				throw std::runtime_error("Multiple out args not implemented");
			}
		}

		ExpressionResult operator()(VarExpression const& varExpression)
		{
			auto localIt = std::ranges::find_if(
				frame->locals, [&](Local const& local) { return local.name == varExpression.varName; });
			if (localIt == frame->locals.end())
			{
				throw std::runtime_error(fmt::format("Local \"{}\" not found", varExpression.varName));
			}

			return { "i32", localIt->value };
		}
	};

	ExpressionResult evaluateExpression(Frame& frame, Expression const& expr)// NOLINT(misc-no-recursion)
	{
		return std::visit(ExpressionVisitor{ this, &frame }, expr.expr);
	}

	void executeFunction(Function const& func,// NOLINT(misc-no-recursion)
		std::vector<ParameterValue> const& inArgs,
		std::vector<ParameterValue>& outArgs)
	{
		Frame frame;

		auto const& funcType = std::get<FuncType>(func.type->t);
		for (auto const& funcParam : funcType.parameters)
		{
			if (!std::holds_alternative<BuiltInType>(funcParam.type->t))
			{
				throw std::runtime_error("Only built in types supported as parameter types: " + funcType.rep);
			}
			auto const& paramType = std::get<BuiltInType>(funcParam.type->t);
			if (paramType.name != "i32")
			{
				throw std::runtime_error("Only i32 support is implemented: " + funcType.rep);
			}

			switch (funcParam.direction)
			{
			case ParameterDirection::in:
			{
				auto argIt
					= std::ranges::find_if(inArgs, [&](ParameterValue const& arg) { return arg.name == funcParam.name; });
				if (argIt == inArgs.cend())
				{
					throw std::runtime_error(fmt::format("No arg provided for param  \"{}\"", funcParam.name));
				}
				frame.locals.push_back(Local{ funcParam.name, argIt->value });
				break;
			}
			case ParameterDirection::out:
			{
				auto argIt
					= std::ranges::find_if(outArgs, [&](ParameterValue const& arg) { return arg.name == funcParam.name; });
				if (argIt == outArgs.cend())
				{
					throw std::runtime_error(fmt::format("No arg provided for param  \"{}\"", funcParam.name));
				}
				frame.locals.push_back(Local{ funcParam.name, 0 });
				break;
			}
			default:
				throw std::runtime_error("Unknown (inout?) parameter direction not implemented");
			}
		}
		if (funcType.parameters.size() != frame.locals.size())
		{
			throw std::runtime_error("Arg count doesn't match parameter count");
		}

		auto [exprType, _] = evaluateExpression(frame, func.expression);
		if (!exprType.empty())
		{
			throw std::runtime_error("Function expression should not return a value");
		}

		for (auto& outArg : outArgs)
		{
			auto localIt
				= std::ranges::find_if(frame.locals, [&](Local const& local) { return local.name == outArg.name; });
			if (localIt == frame.locals.end())
			{
				throw std::runtime_error(fmt::format("Local \"{}\" missing", outArg.name));
			}
			outArg.value = localIt->value;
		}
	}
};

std::int32_t execute(Program const& program)
{
	State programState{ &program };
	if (!program.mainFunction)
	{
		throw std::runtime_error("Missing main function");
	}

	std::vector<ParameterValue> outArgs;
	outArgs.push_back(ParameterValue{ "exitCode" });
	programState.executeFunction(*program.mainFunction, {}, outArgs);

	return outArgs.front().value;
}
}
