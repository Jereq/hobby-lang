// SPDX-License-Identifier: MIT
// Copyright © 2022 Sebastian Larsson
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace jereq
{
enum struct ParameterDirection
{
	in,
	out,
	inout,
};

struct Type;

struct FuncParameter
{
	std::string name;
	ParameterDirection direction;
	std::shared_ptr<Type> type;

	friend bool operator==(FuncParameter const& lhs, FuncParameter const& rhs) noexcept = default;
};

struct FuncType
{
	std::string rep;// TODO: Replace
	std::vector<FuncParameter> parameters;

	friend bool operator==(FuncType const& lhs, FuncType const& rhs) noexcept = default;
};

struct BuiltInType
{
	std::string name;

	friend bool operator==(BuiltInType const& lhs, BuiltInType const& rhs) noexcept = default;
};

struct Type
{
	std::string rep;// TODO: Replace
	std::variant<BuiltInType, FuncType> t;

	friend bool operator==(Type const& lhs, Type const& rhs) noexcept = default;
};

struct Expression;

struct Literal
{
	std::int32_t value;
};

struct InitAssignment
{
	std::string var;
	std::unique_ptr<Expression> value;
};

enum struct BinaryOperator
{
	add,
	subtract,
	multiply,
	divide,
	modulo,
};

struct BinaryOpExpression
{
	BinaryOperator op;
	std::unique_ptr<Expression> lhs;
	std::unique_ptr<Expression> rhs;
};

struct FuncArgument;

struct FunctionCall
{
	std::string functionName;
	// TODO: scope
	std::vector<FuncArgument> arguments;
};

struct VarExpression
{
	std::string varName;
	// TODO: scope
};

struct Expression
{
	std::string rep;// TODO: Replace
	std::variant<Literal, InitAssignment, BinaryOpExpression, FunctionCall, VarExpression> expr;
};

struct FuncArgument
{
	std::string name;
	ParameterDirection direction;
	Expression expr;
};

struct Function
{
	std::string name;// TODO: Remove/Move to debug information?
	std::string sourceFile;
	std::shared_ptr<Type> type;
	Expression expression;
};

struct Program
{
	std::vector<std::shared_ptr<Type>> types;
	std::vector<std::shared_ptr<Function>> functions;
	std::shared_ptr<Function> mainFunction;
};
}
