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
#include <utility>
#include <variant>

namespace jereq
{
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

[[noreturn]] void unrecoverableError(std::string_view description,
	std::string_view errorSubstring,
	std::string_view fullInput,
	std::string_view sourceFileName)
{
	auto location = locate(errorSubstring, fullInput);
	throw std::runtime_error(
		fmt::format("{}({}:{}): {}", sourceFileName, location.lineNumber, location.columnNumber, description));
}

template<typename Result>
struct ParseResult
{
	bool ok = false;
	std::string_view remaining;
	Result result;
};

ParseResult<std::string_view> parseLiteral(std::string_view input, std::string_view literal)
{
	if (input.starts_with(literal))
	{
		return { true, input.substr(literal.size()), input.substr(0, literal.size()) };
	}
	else
	{
		return {};
	}
}

ParseResult<std::string_view> parseWhitespace(std::string_view input)
{
	auto firstNotWhitespace = input.find_first_not_of(" \t\n");
	if (firstNotWhitespace == 0 || firstNotWhitespace == std::string_view::npos)
	{
		return {};
	}
	else
	{
		return { true, input.substr(firstNotWhitespace), input.substr(0, firstNotWhitespace) };
	}
}

std::string_view skipWhitespace(std::string_view input)
{
	auto firstNotWhitespace = input.find_first_not_of(" \t\n");
	if (firstNotWhitespace == std::string_view::npos)
	{
		return input.substr(input.size());
	}
	else
	{
		return input.substr(firstNotWhitespace);
	}
}

std::string_view trim(std::string_view input)
{
	auto firstNotWhitespace = input.find_first_not_of(" \t\n");
	if (firstNotWhitespace == std::string_view::npos)
	{
		return input.substr(input.size());
	}
	else
	{
		auto lastNotWhitespace = input.find_last_not_of(" \t\n");
		return input.substr(firstNotWhitespace, lastNotWhitespace - firstNotWhitespace + 1);
	}
}

ParseResult<std::string_view> parseIdentifier(std::string_view input)
{
	// TODO: Too strict. Replace with black list?
	constexpr static auto isBasicLetter
		= [](char letter) { return ('a' <= letter && letter <= 'z') || ('A' <= letter && letter <= 'Z'); };
	constexpr static auto isDigit = [](char letter) { return '0' <= letter && letter <= '9'; };
	auto firstNotIdentifierCharIt
		= std::ranges::find_if_not(input, [](char letter) { return isBasicLetter(letter) || isDigit(letter); });

	if (firstNotIdentifierCharIt == input.begin() || !isBasicLetter(input.front()))
	{
		return {};
	}
	else
	{
		auto identifierLength = std::distance(input.begin(), firstNotIdentifierCharIt);
		return { true, input.substr(identifierLength), input.substr(0, identifierLength) };
	}
}

ParseResult<ParameterDirection>
	parseParameterDirection(std::string_view input, std::string_view fullInput, std::string_view sourceFileName)
{
	auto inLiteral = parseLiteral(input, "in");
	if (inLiteral.ok)
	{
		return { true, inLiteral.remaining, ParameterDirection::in };
	}

	auto outLiteral = parseLiteral(input, "out");
	if (outLiteral.ok)
	{
		return { true, outLiteral.remaining, ParameterDirection::out };
	}

	auto inoutLiteral = parseLiteral(input, "inout");
	if (inoutLiteral.ok)
	{
		return { true, inoutLiteral.remaining, ParameterDirection::inout };
	}

	unrecoverableError("Expected parameter direction", input, fullInput, sourceFileName);
}

ParseResult<std::shared_ptr<Type>>
	parseType(Program& program, std::string_view input, std::string_view fullInput, std::string_view sourceFileName);

ParseResult<FuncType> parseFuncType(// NOLINT(misc-no-recursion)
	Program& program,
	std::string_view input,
	std::string_view fullInput,
	std::string_view sourceFileName)
{
	auto funLiteral = parseLiteral(input, "fun");
	if (!funLiteral.ok)
	{
		return {};
	}
	auto funWRemInput = skipWhitespace(funLiteral.remaining);

	auto openParLiteral = parseLiteral(funWRemInput, "(");
	if (!openParLiteral.ok)
	{
		return {};
	}
	auto openParWRemInput = skipWhitespace(openParLiteral.remaining);

	auto emptyParLiteral = parseLiteral(openParWRemInput, ")");
	if (emptyParLiteral.ok)
	{
		FuncType funcType;
		funcType.rep = trim(std::string_view(funWRemInput.data(), emptyParLiteral.remaining.data()));
		return { true, skipWhitespace(emptyParLiteral.remaining), funcType };
	}

	auto direction = parseParameterDirection(openParWRemInput, fullInput, sourceFileName);
	if (!direction.ok)
	{
		unrecoverableError("Expected parameter direction", openParWRemInput, fullInput, sourceFileName);
	}

	auto directionWhitespace = parseWhitespace(direction.remaining);
	if (!directionWhitespace.ok)
	{
		unrecoverableError(
			"Expected parameter direction followed by whitespace", direction.remaining, fullInput, sourceFileName);
	}

	auto parameterName = parseIdentifier(directionWhitespace.remaining);
	if (!parameterName.ok)
	{
		unrecoverableError("Expected parameter name", directionWhitespace.remaining, fullInput, sourceFileName);
	}
	auto parameterWRemInput = skipWhitespace(parameterName.remaining);

	auto colonLiteral = parseLiteral(parameterWRemInput, ":");
	if (!colonLiteral.ok)
	{
		unrecoverableError(
			"Expected colon between parameter name and type", parameterWRemInput, fullInput, sourceFileName);
	}
	auto colonWRemInput = skipWhitespace(colonLiteral.remaining);

	auto parameterType = parseType(program, colonWRemInput, fullInput, sourceFileName);
	if (!parameterType.ok)
	{
		unrecoverableError("Expected parameter type", colonWRemInput, fullInput, sourceFileName);
	}

	auto additionalParametersLiteral = parseLiteral(parameterType.remaining, ",");
	if (additionalParametersLiteral.ok)
	{
		unrecoverableError("Multiple parameters not implemented", parameterType.remaining, fullInput, sourceFileName);
	}

	auto closeParLiteral = parseLiteral(parameterType.remaining, ")");
	if (!closeParLiteral.ok)
	{
		unrecoverableError("Expected closing parenthesis", parameterType.remaining, fullInput, sourceFileName);
	}

	FuncType funcType;
	funcType.rep = trim(std::string_view(funLiteral.remaining.data(), closeParLiteral.remaining.data()));

	FuncParameter& param = funcType.parameters.emplace_back();
	param.name = parameterName.result;
	param.direction = direction.result;
	param.type = parameterType.result;

	return { true, skipWhitespace(closeParLiteral.remaining), std::move(funcType) };
}

std::shared_ptr<Type> findOrAddType(Program& program, Type&& maybeNewType)
{
	for (auto const& type : program.types)
	{
		if (*type == maybeNewType)
		{
			return type;
		}
	}

	return program.types.emplace_back(std::make_shared<Type>(std::move(maybeNewType)));
}

ParseResult<std::unique_ptr<Expression>>
	parseTerm(std::string_view input, std::string_view fullInput, std::string_view sourceFileName)
{
	std::int32_t value = -1;
	auto [ptr, ec] = std::from_chars(input.data(), input.data() + input.size(), value);
	if (ec != std::errc())
	{
		unrecoverableError("Expected number term", input, fullInput, sourceFileName);
	}

	std::string_view const afterNumber = input.substr(ptr - input.data());
	if (!afterNumber.starts_with("i32"))
	{
		unrecoverableError("Expected type after value", input, fullInput, sourceFileName);
	}

	std::unique_ptr<Expression> expression = std::make_unique<Expression>();
	expression->rep = input.substr(0, ptr - input.data() + 3);
	expression->expr = Literal{ value };
	return { true, afterNumber.substr(3), std::move(expression) };
}

BinaryOperator parseBinaryOperator(std::string_view input, std::string_view fullInput, std::string_view sourceFileName)
{
	char opChar = input.at(0);
	switch (opChar)
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
		unrecoverableError(fmt::format("Unknown binary operator: {}", opChar), input, fullInput, sourceFileName);
	}
}

ParseResult<std::unique_ptr<Expression>>
	parseExpressionTerms(std::string_view input, std::string_view fullInput, std::string_view sourceFileName)
{
	auto firstTerm = parseTerm(input, fullInput, sourceFileName);
	if (!firstTerm.ok)
	{
		unrecoverableError("Expected an expression term", input, fullInput, sourceFileName);
	}
	auto currentHead = std::move(firstTerm.result);
	auto currentRemainingInput = skipWhitespace(firstTerm.remaining);

	while (!currentRemainingInput.empty()
		   && (currentRemainingInput[0] == '+' || currentRemainingInput[0] == '-' || currentRemainingInput[0] == '*'
			   || currentRemainingInput[0] == '/' || currentRemainingInput[0] == '%'))
	{
		BinaryOperator const binaryOperator = parseBinaryOperator(currentRemainingInput, fullInput, sourceFileName);

		std::string_view const tail = skipWhitespace(currentRemainingInput.substr(1));
		auto nextTerm = parseTerm(tail, fullInput, sourceFileName);
		if (!nextTerm.ok)
		{
			unrecoverableError("Expected a right-hand side term for binary operator", tail, fullInput, sourceFileName);
		}

		std::unique_ptr<Expression> binaryOpExpression = std::make_unique<Expression>();
		binaryOpExpression->rep = trim(std::string_view(input.data(), nextTerm.remaining.data()));
		binaryOpExpression->expr = BinaryOpExpression{ binaryOperator, std::move(currentHead), std::move(nextTerm.result) };

		std::swap(currentHead, binaryOpExpression);
		currentRemainingInput = skipWhitespace(nextTerm.remaining);
	}

	return { true, currentRemainingInput, std::move(currentHead) };
}

ParseResult<Expression>
	parseExpression(std::string_view input, std::string_view fullInput, std::string_view sourceFileName)
{
	auto varIdentifier = parseIdentifier(input);
	if (!varIdentifier.ok)
	{
		unrecoverableError("Expected identifier at start of expression", input, fullInput, sourceFileName);
	}
	auto identifierWRemInput = skipWhitespace(varIdentifier.remaining);

	if (!identifierWRemInput.starts_with('='))
	{
		unrecoverableError("Expected assignment after var", identifierWRemInput, fullInput, sourceFileName);
	}
	auto assignmentWRemInput = skipWhitespace(identifierWRemInput.substr(1));

	auto valueExpr = parseExpressionTerms(assignmentWRemInput, fullInput, sourceFileName);
	if (!valueExpr.ok)
	{
		unrecoverableError("Failed to parse expression terms", assignmentWRemInput, fullInput, sourceFileName);
	}
	if (!valueExpr.remaining.starts_with(';'))
	{
		unrecoverableError("Expected assignment to be followed by ';'", valueExpr.remaining, fullInput, sourceFileName);
	}
	auto leftOver = valueExpr.remaining.substr(1);

	InitAssignment initAssignment;
	initAssignment.var = varIdentifier.result;
	initAssignment.value = std::move(valueExpr.result);

	Expression expr;
	expr.rep = trim(std::string_view(input.data(), leftOver.data()));
	expr.expr = std::move(initAssignment);

	return { true, skipWhitespace(leftOver), std::move(expr) };
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

ParseResult<std::shared_ptr<Type>> parseType(// NOLINT(misc-no-recursion)
	Program& program,
	std::string_view input,
	std::string_view fullInput,
	std::string_view sourceFileName)
{
	auto funcType = parseFuncType(program, input, fullInput, sourceFileName);
	if (funcType.ok)
	{
		Type maybeNewType;
		maybeNewType.rep = trim(std::string_view(input.data(), funcType.remaining.data()));
		maybeNewType.t = funcType.result;

		std::shared_ptr<Type> const& type = findOrAddType(program, std::move(maybeNewType));
		return { true, skipWhitespace(funcType.remaining), type };
	}

	auto plainType = parseIdentifier(input);
	if (plainType.ok)
	{
		if (plainType.result == "i32")
		{
			Type maybeNewType;
			maybeNewType.rep = plainType.result;
			maybeNewType.t = BuiltInType{ std::string(plainType.result) };

			std::shared_ptr<Type> const& type = findOrAddType(program, std::move(maybeNewType));
			return { true, skipWhitespace(plainType.remaining), type };
		}
		else
		{
			unrecoverableError(fmt::format("Type not implemented: {}", plainType.result), input, fullInput, sourceFileName);
		}
	}

	unrecoverableError("Expected type", input, fullInput, sourceFileName);
}

ParseResult<Expression>
	parseFunctionBody(std::string_view input, std::string_view fullInput, std::string_view sourceFileName)
{
	if (!input.starts_with('{'))
	{
		unrecoverableError("Missing '{' at start of function", input, fullInput, sourceFileName);
	}

	std::vector<Expression> expressions;
	auto remainingInput = skipWhitespace(input.substr(1));
	while (!remainingInput.empty() && !remainingInput.starts_with('}'))
	{
		auto expression = parseExpression(remainingInput, fullInput, sourceFileName);
		if (!expression.ok)
		{
			unrecoverableError("Expected an expression in function body", remainingInput, fullInput, sourceFileName);
		}
		expressions.push_back(std::move(expression.result));
		remainingInput = expression.remaining;
	}

	if (!remainingInput.starts_with('}'))
	{
		unrecoverableError("Missing '}' at end of function", remainingInput, fullInput, sourceFileName);
	}

	auto leftOverInput = skipWhitespace(remainingInput.substr(1));

	if (expressions.size() == 1)
	{
		return { true, leftOverInput, std::move(expressions[0]) };
	}
	else if (expressions.empty())
	{
		// TODO: Implement empty function body
		unrecoverableError("Empty function body not implemented", remainingInput, fullInput, sourceFileName);
	}
	else
	{
		// TODO: Implement function body with multiple expressions
		unrecoverableError("Function body with multiple expressions not implemented", input, fullInput, sourceFileName);
	}
}

ParseResult<std::shared_ptr<Function>> parseDefinition(std::string_view input,
	Program& program,
	std::string_view fullInput,
	std::string_view sourceFileName)
{
	auto lineWRemInput = skipWhitespace(input);
	auto defLiteral = parseLiteral(lineWRemInput, "def");
	auto defWhitespace = parseWhitespace(defLiteral.remaining);
	if (!defLiteral.ok || !defWhitespace.ok)
	{
		unrecoverableError("Invalid syntax", input, fullInput, sourceFileName);
	}

	auto defIdentifier = parseIdentifier(defWhitespace.remaining);
	if (!defIdentifier.ok)
	{
		unrecoverableError("Missing name after def", defWhitespace.remaining, fullInput, sourceFileName);
	}
	auto identifierWRemInput = skipWhitespace(defIdentifier.remaining);

	auto assignmentLiteral = parseLiteral(identifierWRemInput, "=");
	if (!assignmentLiteral.ok)
	{
		unrecoverableError("Missing assignment in def", identifierWRemInput, fullInput, sourceFileName);
	}
	auto assignmentWRemInput = skipWhitespace(assignmentLiteral.remaining);

	auto type = parseType(program, assignmentWRemInput, fullInput, sourceFileName);
	if (!type.ok)
	{
		unrecoverableError("Unable to parse type", assignmentWRemInput, input, sourceFileName);
	}

	auto functionBody = parseFunctionBody(type.remaining, fullInput, sourceFileName);
	if (!functionBody.ok)
	{
		unrecoverableError("Failed to parse function body", type.remaining, fullInput, sourceFileName);
	}
	auto funcBodyWRemInput = skipWhitespace(functionBody.remaining);

	if (!funcBodyWRemInput.starts_with(';'))
	{
		unrecoverableError("Invalid def end", funcBodyWRemInput, fullInput, sourceFileName);
	}
	auto remainingInput = skipWhitespace(funcBodyWRemInput.substr(1));

	std::shared_ptr<Function> const& mainFunc = program.functions.emplace_back(std::make_shared<Function>());
	mainFunc->name = defIdentifier.result;
	mainFunc->sourceFile = sourceFileName;
	mainFunc->type = type.result;
	mainFunc->expression = std::move(functionBody.result);

	if (mainFunc->name == "main")
	{
		if (!isMainFuncType(*mainFunc->type))
		{
			unrecoverableError("Wrong type for main", assignmentWRemInput, fullInput, sourceFileName);
		}

		if (program.mainFunction)
		{
			unrecoverableError("Multiple main functions found", defWhitespace.remaining, fullInput, sourceFileName);
		}

		program.mainFunction = mainFunc;
	}

	return { true, remainingInput, mainFunc };
}

Program parse(std::string_view input, std::string_view name)
{
	Program program;

	std::string_view remainingInput = input;
	while (!remainingInput.empty())
	{
		auto funcDef = parseDefinition(remainingInput, program, input, name);
		if (!funcDef.ok)
		{
			unrecoverableError("Failed to parse definition", remainingInput, input, name);
		}
		remainingInput = funcDef.remaining;
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
