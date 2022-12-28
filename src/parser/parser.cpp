// SPDX-License-Identifier: MIT
// Copyright © 2022 Sebastian Larsson
#include <hobbylang/parser/parser.hpp>

#include <hobbylang/ast/ast.hpp>

#include <fmt/core.h>

#include <algorithm>
#include <charconv>
#include <istream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>

namespace jereq
{
std::shared_ptr<Type> findOrAddType(Program& program, std::string_view rep);

FuncType parseFuncType(Program& program, std::string_view rep)// NOLINT(misc-no-recursion)
{
	FuncType funcType;
	funcType.rep = rep.substr(3);

	if (funcType.rep.find(',') != std::string::npos)
	{
		throw std::runtime_error("Multiple parameters not implemented");
	}

	std::string const args = funcType.rep.substr(1, funcType.rep.size() - 2);
	if (!funcType.rep.empty())
	{
		FuncParameter& param = funcType.parameters.emplace_back();
		std::size_t nameStart = 0;
		if (args.starts_with("in "))
		{
			param.direction = ParameterDirection::in;
			nameStart = std::string_view("in ").size();
		}
		else if (args.starts_with("out "))
		{
			param.direction = ParameterDirection::out;
			nameStart = std::string_view("out ").size();
		}
		else if (args.starts_with("inout "))
		{
			param.direction = ParameterDirection::inout;
			nameStart = std::string_view("inout ").size();
		}
		else
		{
			throw std::runtime_error("Parameter direction not found: " + args);
		}

		auto nameEnd = args.find(':', nameStart);
		if (nameEnd == nameStart || nameEnd == std::string::npos)
		{
			throw std::runtime_error("Parameter name not found: " + args);
		}

		param.name = args.substr(nameStart, nameEnd - nameStart);

		auto typeStart = nameEnd + 2;
		param.type = findOrAddType(program, args.substr(typeStart));
	}

	return funcType;
}

std::shared_ptr<Type> findOrAddType(Program& program, std::string_view rep)// NOLINT(misc-no-recursion)
{
	Type maybeNewType;
	maybeNewType.rep = rep;

	if (rep.starts_with("fun(") && rep.ends_with(')'))
	{
		maybeNewType.t = parseFuncType(program, rep);
	}
	else if (rep == "i32")
	{
		maybeNewType.t = BuiltInType{ std::string(rep) };
	}
	else
	{
		throw std::runtime_error(fmt::format("Type not implemented: {}", rep));
	}


	for (auto const& type : program.types)
	{
		if (*type == maybeNewType)
		{
			return type;
		}
	}

	return program.types.emplace_back(std::make_shared<Type>(maybeNewType));
}

struct ParseTermResult
{
	std::unique_ptr<Expression> expr;
	std::string_view remainingInput;
};

ParseTermResult parseTerm(std::string_view input)
{
	std::int32_t value = -1;
	auto [ptr, ec] = std::from_chars(input.data(), input.data() + input.size(), value);
	if (ec != std::errc())
	{
		throw std::runtime_error(fmt::format("Expected number term: {}", input));
	}

	std::string_view const afterNumber = input.substr(ptr - input.data());
	if (!afterNumber.starts_with("i32"))
	{
		throw std::runtime_error(fmt::format("Expected type after value in: {}", input));
	}

	std::unique_ptr<Expression> expression = std::make_unique<Expression>();
	expression->rep = input.substr(0, ptr - input.data() + 3);
	expression->expr = Literal{ value };
	return { std::move(expression), afterNumber.substr(3) };
}

BinaryOperator parseBinaryOperator(char c)
{
	switch (c)
	{
	case '+':
		return BinaryOperator::add;
	case '-':
		return BinaryOperator::subtract;
	case '*':
		return BinaryOperator::multiply;
	case '/':
		return BinaryOperator::divide;
	case '%':
		return BinaryOperator::modulo;
	default:
		throw std::runtime_error(fmt::format("Unknown binary operator: {}", c));
	}
}

std::unique_ptr<Expression> parseExpressionTerms(std::string_view input)
{
	auto [headExpr, remainingInput] = parseTerm(input);
	while (remainingInput.starts_with(" + ") || remainingInput.starts_with(" - ") || remainingInput.starts_with(" * ")
		   || remainingInput.starts_with(" / ") || remainingInput.starts_with(" % "))
	{
		BinaryOperator const binaryOperator = parseBinaryOperator(remainingInput.at(1));

		std::string_view const tail = remainingInput.substr(3);
		auto [nextExpr, remainingTailInput] = parseTerm(tail);

		std::unique_ptr<Expression> binaryOpExpression = std::make_unique<Expression>();
		binaryOpExpression->rep = std::string_view(input.data(), remainingTailInput.data());
		binaryOpExpression->expr = BinaryOpExpression{ binaryOperator, std::move(headExpr), std::move(nextExpr) };

		std::swap(headExpr, binaryOpExpression);
		remainingInput = remainingTailInput;
	}

	if (!remainingInput.empty())
	{
		throw std::runtime_error(fmt::format("Invalid expression terms: {}", input));
	}

	return std::move(headExpr);
}

Expression parseExpression(std::string_view input)
{
	if (input.size() < 5 || !input.starts_with("{ ") || !input.ends_with(" }"))
	{
		throw std::runtime_error(fmt::format("I don't understand this expression: {}", input));
	}

	std::string_view const inner = input.substr(2, input.size() - 4);
	auto varEnd = inner.find(' ');
	if (varEnd == std::string_view::npos)
	{
		throw std::runtime_error(fmt::format("Missing space after var name in: {}", inner));
	}

	if (inner.size() < varEnd + 3 || inner.substr(varEnd, 3) != " = ")
	{
		throw std::runtime_error(fmt::format("Expected assignment in: {}", inner));
	}

	auto valStart = varEnd + 3;
	if (!inner.ends_with(';'))
	{
		throw std::runtime_error(fmt::format("Expression should end with ';': {}", inner));
	}
	std::string_view const valInput = inner.substr(valStart, inner.size() - valStart - 1);

	InitAssignment initAssignment;
	initAssignment.var = inner.substr(0, varEnd);
	initAssignment.value = parseExpressionTerms(valInput);

	Expression result;
	result.rep = input;
	result.expr = std::move(initAssignment);

	return result;
}

bool isMainFuncType(Type const& type)
{
	if (!std::holds_alternative<FuncType>(type.t))
	{
		return false;
	}

	auto const& funcType = std::get<FuncType>(type.t);
	if (funcType.parameters.size() != 1)
	{
		return false;
	}

	FuncParameter const& param = funcType.parameters.at(0);
	if (param.name != "exitCode" || param.direction != ParameterDirection::out)
	{
		return false;
	}

	Type const& paramType = *param.type;
	if (!std::holds_alternative<BuiltInType>(paramType.t))
	{
		return false;
	}

	return std::get<BuiltInType>(paramType.t).name == "i32";
}

std::pair<bool, std::string_view> parseLiteral(std::string_view input, std::string_view literal)
{
	if (input.starts_with(literal))
	{
		return { true, input.substr(literal.size()) };
	}
	else
	{
		return { false, {} };
	}
}

std::pair<bool, std::string_view> parseWhitespace(std::string_view input)
{
	auto firstNotWhitespace = input.find_first_not_of(" \t\n");
	if (firstNotWhitespace == 0)
	{
		return { false, {} };
	}
	else
	{
		return { true, input.substr(firstNotWhitespace) };
	}
}

std::string_view skipWhitespace(std::string_view input)
{
	auto firstNotWhitespace = input.find_first_not_of(" \t\n");
	return input.substr(firstNotWhitespace);
}

std::tuple<bool, std::string_view, std::string_view> parseIdentifier(std::string_view input)
{
	// TODO: Too strict. Replace with black list?
	auto firstNotIdentifierCharIt = std::ranges::find_if_not(
		input, [](char letter) { return ('a' <= letter && letter <= 'z') || ('A' <= letter && letter <= 'Z'); });
	if (firstNotIdentifierCharIt == input.begin())
	{
		return { false, {}, {} };
	}
	else
	{
		auto identifierLength = std::distance(input.begin(), firstNotIdentifierCharIt);
		return { true, input.substr(0, identifierLength), input.substr(identifierLength) };
	}
}

std::tuple<bool, std::shared_ptr<Type>, std::string_view> parseType(std::string_view input, Program& program)
{
	if (!input.starts_with("fun("))
	{
		throw std::runtime_error("Non-function types not implemented");
	}

	auto funcTypeEnd = input.find(')');
	if (funcTypeEnd == std::string_view::npos)
	{
		return { false, {}, {} };
	}
	funcTypeEnd += 1;

	std::shared_ptr<Type> const& type = findOrAddType(program, input.substr(0, funcTypeEnd));
	return { true, type, input.substr(funcTypeEnd) };
}

std::tuple<bool, Expression, std::string_view> parseFunctionBody(std::string_view input)
{
	if (!input.starts_with('{'))
	{
		throw std::runtime_error(fmt::format("Missing func body start in: {}", input));
	}

	auto funcBodyEnd = input.find('}', 1);
	if (funcBodyEnd == std::string::npos)
	{
		throw std::runtime_error(fmt::format("Missing func body end in: {}", input));
	}
	funcBodyEnd += 1;

	Expression expr = parseExpression(input.substr(0, funcBodyEnd));
	return { true, std::move(expr), input.substr(funcBodyEnd) };
}

struct Location
{
	std::size_t lineNumber;
	std::size_t columnNumber;
	std::size_t byteOffset;
};

Location locate(std::string_view substring, std::string_view original)
{
	char const* substringStart = substring.data();
	char const* originalStart = original.data();
	if (substringStart < originalStart || originalStart + original.size() < substringStart)
	{
		throw std::runtime_error("locate can only be used on substrings of the original string");
	}

	std::basic_string_view<char> const& originalBeforeSubstring
		= std::string_view(originalStart, std::distance(originalStart, substringStart));

	Location result = {};
	result.lineNumber = std::ranges::count(originalBeforeSubstring, '\n') + 1;
	if (result.lineNumber == 1)
	{
		result.columnNumber = originalBeforeSubstring.size() + 1;
	}
	else
	{
		auto lastLineBreak = originalBeforeSubstring.rfind('\n');
		result.columnNumber = originalBeforeSubstring.size() - lastLineBreak;
	}
	result.byteOffset = originalBeforeSubstring.size();
	return result;
}

std::string_view getLine(std::string_view substring, std::string_view original)
{
	auto location = locate(substring, original);
	auto nextLineBreak = original.find('\n', location.byteOffset);
	if (nextLineBreak == std::string_view::npos)
	{
		return original.substr(location.byteOffset);
	}
	else
	{
		return original.substr(location.byteOffset, nextLineBreak);
	}
}

[[noreturn]] void unrecoverableError(std::string_view description,
	std::string_view errorSubstring,
	std::string_view fullInput,
	std::string_view sourceFileName)
{
	auto location = locate(errorSubstring, fullInput);
	throw std::runtime_error(
		fmt::format("{}({}:{}): {}", sourceFileName, location.lineNumber, location.columnNumber, description));
}

std::pair<bool, std::string_view> parseDefinition(std::string_view input,
	Program& program,
	std::string_view fullInput,
	std::string_view sourceFileName)
{
	auto lineWRemInput = skipWhitespace(input);
	auto [defOk, defRemInput] = parseLiteral(lineWRemInput, "def");
	auto [defWOk, defWRemInput] = parseWhitespace(defRemInput);
	if (!defOk || !defWOk)
	{
		unrecoverableError("Invalid syntax", input, fullInput, sourceFileName);
	}

	auto [identifierOk, identifier, identifierRemInput] = parseIdentifier(defWRemInput);
	if (!identifierOk)
	{
		unrecoverableError("Missing name after def", defWRemInput, fullInput, sourceFileName);
	}
	auto identifierWRemInput = skipWhitespace(identifierRemInput);

	auto [assignmentOk, assignmentRemInput] = parseLiteral(identifierWRemInput, "=");
	if (!assignmentOk)
	{
		unrecoverableError("Missing assignment in def", identifierWRemInput, fullInput, sourceFileName);
	}
	auto assignmentWRemInput = skipWhitespace(assignmentRemInput);

	auto [typeOk, type, typeRemInput] = parseType(assignmentWRemInput, program);
	if (!typeOk)
	{
		unrecoverableError("Unable to parse type", assignmentWRemInput, input, sourceFileName);
	}
	auto typeWRemInput = skipWhitespace(typeRemInput);

	auto [funcBodyOk, funcExpr, funcBodyRemInput] = parseFunctionBody(typeWRemInput);
	if (!funcBodyOk)
	{
		unrecoverableError("Failed to parse function body", typeWRemInput, fullInput, sourceFileName);
	}
	auto funcBodyWRemInput = skipWhitespace(funcBodyRemInput);

	if (!funcBodyWRemInput.starts_with(';'))
	{
		unrecoverableError("Invalid def end", funcBodyWRemInput, fullInput, sourceFileName);
	}
	auto remainingInput = funcBodyWRemInput.substr(1);

	std::shared_ptr<Function> const& mainFunc = program.functions.emplace_back(std::make_shared<Function>());
	mainFunc->name = identifier;
	mainFunc->sourceFile = sourceFileName;
	mainFunc->type = type;
	mainFunc->expression = std::move(funcExpr);

	if (mainFunc->name == "main")
	{
		if (!isMainFuncType(*mainFunc->type))
		{
			unrecoverableError("Wrong type for main", assignmentWRemInput, fullInput, sourceFileName);
		}

		if (program.mainFunction)
		{
			unrecoverableError("Multiple main functions found", identifier, fullInput, sourceFileName);
		}

		program.mainFunction = mainFunc;
	}

	return { true, remainingInput };
}

Program parse(std::string_view input, std::string_view name)
{
	Program program;

	std::string_view remainingInput = input;
	while (!remainingInput.empty())
	{
		auto [ok, remaining] = parseDefinition(remainingInput, program, input, name);
		if (!ok)
		{
			unrecoverableError("Failed to parse definition", remainingInput, input, name);
		}
		remainingInput = remaining;
	}

	if (!program.mainFunction)
	{
		throw std::runtime_error("No main function");
	}

	return program;
}

Program parse(std::istream& input, std::string_view name)
{
	auto fpos = input.tellg();
	input.seekg(0, std::istream::end);
	auto inputSize = input.tellg() - fpos;
	input.seekg(fpos);

	std::string inputContent;
	inputContent.resize(inputSize);
	input.read(inputContent.data(), inputSize);

	return parse(inputContent, name);
}
}
