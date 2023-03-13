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
	if (firstNotWhitespace == 0 || firstNotWhitespace == std::string_view::npos)
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

std::tuple<bool, std::string_view, std::string_view> parseIdentifier(std::string_view input)
{
	// TODO: Too strict. Replace with black list?
	constexpr static auto isBasicLetter
		= [](char letter) { return ('a' <= letter && letter <= 'z') || ('A' <= letter && letter <= 'Z'); };
	constexpr static auto isDigit = [](char letter) { return '0' <= letter && letter <= '9'; };
	auto firstNotIdentifierCharIt
		= std::ranges::find_if_not(input, [](char letter) { return isBasicLetter(letter) || isDigit(letter); });

	if (firstNotIdentifierCharIt == input.begin() || !isBasicLetter(input.front()))
	{
		return { false, {}, {} };
	}
	else
	{
		auto identifierLength = std::distance(input.begin(), firstNotIdentifierCharIt);
		return { true, input.substr(0, identifierLength), input.substr(identifierLength) };
	}
}

std::tuple<bool, ParameterDirection, std::string_view>
	parseParameterDirection(std::string_view input, std::string_view fullInput, std::string_view sourceFileName)
{
	auto [inParam, inRemInput] = parseLiteral(input, "in");
	if (inParam)
	{
		return { true, ParameterDirection::in, inRemInput };
	}

	auto [outParam, outRemInput] = parseLiteral(input, "out");
	if (outParam)
	{
		return { true, ParameterDirection::out, outRemInput };
	}

	auto [inoutParam, inoutRemInput] = parseLiteral(input, "inout");
	if (inoutParam)
	{
		return { true, ParameterDirection::inout, inoutRemInput };
	}

	unrecoverableError("Expected parameter direction", input, fullInput, sourceFileName);
}

std::tuple<bool, std::shared_ptr<Type>, std::string_view>
	parseType(Program& program, std::string_view input, std::string_view fullInput, std::string_view sourceFileName);

std::tuple<bool, FuncType, std::string_view> parseFuncType(// NOLINT(misc-no-recursion)
	Program& program,
	std::string_view input,
	std::string_view fullInput,
	std::string_view sourceFileName)
{
	auto [funOk, funRemInput] = parseLiteral(input, "fun");
	if (!funOk)
	{
		return { false, {}, {} };
	}
	auto funWRemInput = skipWhitespace(funRemInput);

	auto [openParOk, openParRemInput] = parseLiteral(funWRemInput, "(");
	if (!openParOk)
	{
		return { false, {}, {} };
	}
	auto openParWRemInput = skipWhitespace(openParRemInput);

	auto [emptyPar, emptyParRemInput] = parseLiteral(openParWRemInput, ")");
	if (emptyPar)
	{
		FuncType funcType;
		funcType.rep = trim(std::string_view(funWRemInput.data(), emptyParRemInput.data()));
		return { true, funcType, skipWhitespace(emptyParRemInput) };
	}

	auto [directionOk, direction, directionRemInput]
		= parseParameterDirection(openParWRemInput, fullInput, sourceFileName);
	if (!directionOk)
	{
		unrecoverableError("Expected parameter direction", openParWRemInput, fullInput, sourceFileName);
	}

	auto [directionWOk, directionWRemInput] = parseWhitespace(directionRemInput);
	if (!directionWOk)
	{
		unrecoverableError(
			"Expected parameter direction followed by whitespace", directionRemInput, fullInput, sourceFileName);
	}

	auto [parameterNameOk, parameterName, parameterRemInput] = parseIdentifier(directionWRemInput);
	if (!parameterNameOk)
	{
		unrecoverableError("Expected parameter name", directionWRemInput, fullInput, sourceFileName);
	}
	auto parameterWRemInput = skipWhitespace(parameterRemInput);

	auto [colonOk, colonRemInput] = parseLiteral(parameterWRemInput, ":");
	if (!colonOk)
	{
		unrecoverableError(
			"Expected colon between parameter name and type", parameterWRemInput, fullInput, sourceFileName);
	}
	auto colonWRemInput = skipWhitespace(colonRemInput);

	auto [typeOk, parameterType, typeRemInput] = parseType(program, colonWRemInput, fullInput, sourceFileName);
	if (!typeOk)
	{
		unrecoverableError("Expected parameter type", colonWRemInput, fullInput, sourceFileName);
	}

	auto [additionalParameters, additionalParametersRemInput] = parseLiteral(typeRemInput, ",");
	if (additionalParameters)
	{
		unrecoverableError("Multiple parameters not implemented", typeRemInput, fullInput, sourceFileName);
	}

	auto [closeParOk, closeParRemInput] = parseLiteral(typeRemInput, ")");
	if (!closeParOk)
	{
		unrecoverableError("Expected closing parenthesis", typeRemInput, fullInput, sourceFileName);
	}

	FuncType funcType;
	funcType.rep = trim(std::string_view(funRemInput.data(), closeParRemInput.data()));

	FuncParameter& param = funcType.parameters.emplace_back();
	param.name = parameterName;
	param.direction = direction;
	param.type = parameterType;

	return { true, std::move(funcType), skipWhitespace(closeParRemInput) };
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

struct ParseTermResult
{
	std::unique_ptr<Expression> expr;
	std::string_view remainingInput;
};

ParseTermResult parseTerm(std::string_view input, std::string_view fullInput, std::string_view sourceFileName)
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
	return { std::move(expression), afterNumber.substr(3) };
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

std::tuple<bool, std::unique_ptr<Expression>, std::string_view>
	parseExpressionTerms(std::string_view input, std::string_view fullInput, std::string_view sourceFileName)
{
	auto [headExpr, remainingInput] = parseTerm(input, fullInput, sourceFileName);
	auto wRemainingInput = skipWhitespace(remainingInput);
	while (!wRemainingInput.empty()
		   && (wRemainingInput[0] == '+' || wRemainingInput[0] == '-' || wRemainingInput[0] == '*'
			   || wRemainingInput[0] == '/' || wRemainingInput[0] == '%'))
	{
		BinaryOperator const binaryOperator = parseBinaryOperator(wRemainingInput, fullInput, sourceFileName);

		std::string_view const tail = skipWhitespace(wRemainingInput.substr(1));
		auto [nextExpr, remainingTailInput] = parseTerm(tail, fullInput, sourceFileName);

		std::unique_ptr<Expression> binaryOpExpression = std::make_unique<Expression>();
		binaryOpExpression->rep = trim(std::string_view(input.data(), remainingTailInput.data()));
		binaryOpExpression->expr = BinaryOpExpression{ binaryOperator, std::move(headExpr), std::move(nextExpr) };

		std::swap(headExpr, binaryOpExpression);
		wRemainingInput = skipWhitespace(remainingTailInput);
	}

	return { true, std::move(headExpr), wRemainingInput };
}

std::tuple<bool, Expression, std::string_view>
	parseExpression(std::string_view input, std::string_view fullInput, std::string_view sourceFileName)
{
	auto [identifierOk, identifierName, identifierRemInput] = parseIdentifier(input);
	if (!identifierOk)
	{
		unrecoverableError("Expected identifier at start of expression", input, fullInput, sourceFileName);
	}
	auto identifierWRemInput = skipWhitespace(identifierRemInput);

	if (!identifierWRemInput.starts_with('='))
	{
		unrecoverableError("Expected assignment after var", identifierWRemInput, fullInput, sourceFileName);
	}
	auto assignmentWRemInput = skipWhitespace(identifierWRemInput.substr(1));

	auto [valueOk, valueExpr, valueRemInput] = parseExpressionTerms(assignmentWRemInput, fullInput, sourceFileName);
	if (!valueOk)
	{
		unrecoverableError("Failed to parse expression terms", assignmentWRemInput, fullInput, sourceFileName);
	}
	if (!valueRemInput.starts_with(';'))
	{
		unrecoverableError("Expected assignment to be followed by ';'", valueRemInput, fullInput, sourceFileName);
	}
	auto leftOver = valueRemInput.substr(1);

	InitAssignment initAssignment;
	initAssignment.var = identifierName;
	initAssignment.value = std::move(valueExpr);

	Expression expr;
	expr.rep = trim(std::string_view(input.data(), leftOver.data()));
	expr.expr = std::move(initAssignment);

	return { true, std::move(expr), skipWhitespace(leftOver) };
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

std::tuple<bool, std::shared_ptr<Type>, std::string_view> parseType(// NOLINT(misc-no-recursion)
	Program& program,
	std::string_view input,
	std::string_view fullInput,
	std::string_view sourceFileName)
{
	auto [funcTypeOk, funcType, funcTypeRemInput] = parseFuncType(program, input, fullInput, sourceFileName);
	if (funcTypeOk)
	{
		Type maybeNewType;
		maybeNewType.rep = trim(std::string_view(input.data(), funcTypeRemInput.data()));
		maybeNewType.t = funcType;

		std::shared_ptr<Type> const& type = findOrAddType(program, std::move(maybeNewType));
		return { true, type, skipWhitespace(funcTypeRemInput) };
	}

	auto [plainTypeOk, plainType, plainTypeRemInput] = parseIdentifier(input);
	if (plainTypeOk)
	{
		if (plainType == "i32")
		{
			Type maybeNewType;
			maybeNewType.rep = plainType;
			maybeNewType.t = BuiltInType{ std::string(plainType) };

			std::shared_ptr<Type> const& type = findOrAddType(program, std::move(maybeNewType));
			return { true, type, skipWhitespace(plainTypeRemInput) };
		}
		else
		{
			unrecoverableError(fmt::format("Type not implemented: {}", plainType), input, fullInput, sourceFileName);
		}
	}

	unrecoverableError("Expected type", input, fullInput, sourceFileName);
}

std::tuple<bool, Expression, std::string_view>
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
		auto [ok, expr, remaining] = parseExpression(remainingInput, fullInput, sourceFileName);
		if (!ok)
		{
			unrecoverableError("Expected an expression in function body", remainingInput, fullInput, sourceFileName);
		}
		expressions.push_back(std::move(expr));
		remainingInput = remaining;
	}

	if (!remainingInput.starts_with('}'))
	{
		unrecoverableError("Missing '}' at end of function", remainingInput, fullInput, sourceFileName);
	}

	auto leftOverInput = skipWhitespace(remainingInput.substr(1));

	if (expressions.size() == 1)
	{
		return { true, std::move(expressions[0]), leftOverInput };
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

	auto [typeOk, type, typeRemInput] = parseType(program, assignmentWRemInput, fullInput, sourceFileName);
	if (!typeOk)
	{
		unrecoverableError("Unable to parse type", assignmentWRemInput, input, sourceFileName);
	}

	auto [funcBodyOk, funcExpr, funcBodyRemInput] = parseFunctionBody(typeRemInput, fullInput, sourceFileName);
	if (!funcBodyOk)
	{
		unrecoverableError("Failed to parse function body", typeRemInput, fullInput, sourceFileName);
	}
	auto funcBodyWRemInput = skipWhitespace(funcBodyRemInput);

	if (!funcBodyWRemInput.starts_with(';'))
	{
		unrecoverableError("Invalid def end", funcBodyWRemInput, fullInput, sourceFileName);
	}
	auto remainingInput = skipWhitespace(funcBodyWRemInput.substr(1));

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
