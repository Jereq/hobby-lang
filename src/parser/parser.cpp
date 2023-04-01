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

struct ParseInput
{
	std::string_view current;
	std::string_view full;
	std::string_view sourceFileName;

	[[nodiscard]] ParseInput consume(std::size_t offset) const
	{
		return { current.substr(offset), full, sourceFileName };
	}
};

[[noreturn]] void unrecoverableError(std::string_view description, ParseInput const& errorLocation)
{
	auto location = locate(errorLocation.current, errorLocation.full);
	throw std::runtime_error(fmt::format(
		"{}({}:{}): {}", errorLocation.sourceFileName, location.lineNumber, location.columnNumber, description));
}

template<typename Result>
struct ParseResult
{
	bool ok = false;
	ParseInput remaining;
	Result result;
};

ParseResult<std::string_view> parseLiteral(ParseInput const& input, std::string_view literal)
{
	if (input.current.starts_with(literal))
	{
		return { true, input.consume(literal.size()), input.current.substr(0, literal.size()) };
	}
	else
	{
		return {};
	}
}

ParseResult<std::string_view> parseWhitespace(ParseInput const& input)
{
	auto firstNotWhitespace = input.current.find_first_not_of(" \t\n");
	if (firstNotWhitespace == 0 || firstNotWhitespace == std::string_view::npos)
	{
		return {};
	}
	else
	{
		return { true, input.consume(firstNotWhitespace), input.current.substr(0, firstNotWhitespace) };
	}
}

ParseInput skipWhitespace(ParseInput const& input)
{
	auto firstNotWhitespace = input.current.find_first_not_of(" \t\n");
	if (firstNotWhitespace == std::string_view::npos)
	{
		return input.consume(input.current.size());
	}
	else
	{
		return input.consume(firstNotWhitespace);
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

ParseResult<std::string_view> parseIdentifier(ParseInput const& input)
{
	// TODO: Too strict. Replace with black list?
	constexpr static auto isBasicLetter
		= [](char letter) { return ('a' <= letter && letter <= 'z') || ('A' <= letter && letter <= 'Z'); };
	constexpr static auto isDigit = [](char letter) { return '0' <= letter && letter <= '9'; };
	auto firstNotIdentifierCharIt
		= std::ranges::find_if_not(input.current, [](char letter) { return isBasicLetter(letter) || isDigit(letter); });

	if (firstNotIdentifierCharIt == input.current.begin() || !isBasicLetter(input.current.front()))
	{
		return {};
	}
	else
	{
		auto identifierLength = std::distance(input.current.begin(), firstNotIdentifierCharIt);
		return { true, input.consume(identifierLength), input.current.substr(0, identifierLength) };
	}
}

ParseResult<ParameterDirection> parseParameterDirection(ParseInput const& input)
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

	unrecoverableError("Expected parameter direction", input);
}

ParseResult<std::shared_ptr<Type>> parseType(Program& program, ParseInput const& input);

ParseResult<FuncParameter> parseFuncTypeParameter(Program& program, ParseInput const& input) // NOLINT(misc-no-recursion)
{
	auto direction = parseParameterDirection(input);
	if (!direction.ok)
	{
		unrecoverableError("Expected parameter direction", input);
	}

	auto directionWhitespace = parseWhitespace(direction.remaining);
	if (!directionWhitespace.ok)
	{
		unrecoverableError("Expected parameter direction followed by whitespace", direction.remaining);
	}

	auto parameterName = parseIdentifier(directionWhitespace.remaining);
	if (!parameterName.ok)
	{
		unrecoverableError("Expected parameter name", directionWhitespace.remaining);
	}
	auto parameterWRemInput = skipWhitespace(parameterName.remaining);

	auto colonLiteral = parseLiteral(parameterWRemInput, ":");
	if (!colonLiteral.ok)
	{
		unrecoverableError("Expected colon between parameter name and type", parameterWRemInput);
	}
	auto colonWRemInput = skipWhitespace(colonLiteral.remaining);

	auto parameterType = parseType(program, colonWRemInput);
	if (!parameterType.ok)
	{
		unrecoverableError("Expected parameter type", colonWRemInput);
	}

	return { true,
		skipWhitespace(parameterType.remaining),
		FuncParameter{ std::string(parameterName.result), direction.result, parameterType.result } };
}

ParseResult<FuncType> parseFuncType(// NOLINT(misc-no-recursion)
	Program& program,
	ParseInput const& input)
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
		funcType.rep = trim(std::string_view(funWRemInput.current.data(), emptyParLiteral.remaining.current.data()));
		return { true, skipWhitespace(emptyParLiteral.remaining), funcType };
	}

	auto firstParameter = parseFuncTypeParameter(program, openParWRemInput);
	if (!firstParameter.ok)
	{
		unrecoverableError("Expected function parameter", openParWRemInput);
	}
	std::vector parameters = { firstParameter.result };

	auto currentInput = firstParameter.remaining;
	for (auto parameterSeparator = parseLiteral(currentInput, ","); parameterSeparator.ok;
		 parameterSeparator = parseLiteral(currentInput, ","))
	{
		auto parameterSeparatorWRemInput = skipWhitespace(parameterSeparator.remaining);
		auto parameter = parseFuncTypeParameter(program, parameterSeparatorWRemInput);
		if (!parameter.ok)
		{
			unrecoverableError("Expected function parameter", parameterSeparatorWRemInput);
		}
		parameters.push_back(std::move(parameter.result));
		currentInput = parameter.remaining;
	}

	auto closeParLiteral = parseLiteral(currentInput, ")");
	if (!closeParLiteral.ok)
	{
		unrecoverableError("Expected closing parenthesis", currentInput);
	}

	FuncType funcType;
	funcType.rep
		= trim(std::string_view(funLiteral.remaining.current.data(), closeParLiteral.remaining.current.data()));
	funcType.parameters = std::move(parameters);

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

ParseResult<std::unique_ptr<Expression>> parseVarExpression(ParseInput const& input)
{
	auto varIdentifier = parseIdentifier(input);
	if (!varIdentifier.ok)
	{
		return {};
	}

	std::unique_ptr<Expression> expression = std::make_unique<Expression>();
	expression->rep = varIdentifier.result;
	expression->expr = VarExpression{ std::string(varIdentifier.result) };
	return { true, skipWhitespace(varIdentifier.remaining), std::move(expression) };
}

ParseResult<std::unique_ptr<Expression>> parseNumberWithType(ParseInput const& input)
{
	std::int32_t value = -1;
	auto [ptr, ec] = std::from_chars(input.current.data(), input.current.data() + input.current.size(), value);
	if (ec != std::errc())
	{
		unrecoverableError("Expected number term", input);
	}

	auto afterNumber = input.consume(ptr - input.current.data());
	if (!afterNumber.current.starts_with("i32"))
	{
		unrecoverableError("Expected type after value", afterNumber);
	}

	std::unique_ptr<Expression> expression = std::make_unique<Expression>();
	expression->rep = input.current.substr(0, ptr - input.current.data() + 3);
	expression->expr = Literal{ value };
	return { true, afterNumber.consume(3), std::move(expression) };
}

ParseResult<std::unique_ptr<Expression>> parseExpressionTerms(ParseInput const& input);

ParseResult<std::unique_ptr<Expression>> parseFunctionCall(ParseInput const& input) // NOLINT(misc-no-recursion)
{
	auto funcName = parseIdentifier(input);
	if (!funcName.ok)
	{
		return {};
	}

	auto parStart = parseLiteral(skipWhitespace(funcName.remaining), "(");
	if (!parStart.ok)
	{
		return {};
	}
	auto parStartWRemInput = skipWhitespace(parStart.remaining);

	auto emptyParLiteral = parseLiteral(parStartWRemInput, ")");
	if (emptyParLiteral.ok)
	{
		std::unique_ptr<Expression> expression = std::make_unique<Expression>();
		expression->rep = std::string_view(input.current.data(), emptyParLiteral.remaining.current.data());
		expression->expr = FunctionCall{ std::string(funcName.result), {} };
		return { true, skipWhitespace(emptyParLiteral.remaining), std::move(expression) };
	}

	auto direction = parseParameterDirection(parStartWRemInput);
	if (!direction.ok)
	{
		unrecoverableError("Expected parameter direction", parStartWRemInput);
	}

	auto directionWhitespace = parseWhitespace(direction.remaining);
	if (!directionWhitespace.ok)
	{
		unrecoverableError("Expected parameter direction followed by whitespace", direction.remaining);
	}

	auto parameterName = parseIdentifier(directionWhitespace.remaining);
	if (!parameterName.ok)
	{
		unrecoverableError("Expected parameter name", directionWhitespace.remaining);
	}
	auto parameterWRemInput = skipWhitespace(parameterName.remaining);

	auto colonLiteral = parseLiteral(parameterWRemInput, ":");
	if (!colonLiteral.ok)
	{
		unrecoverableError("Expected colon between parameter name and value", parameterWRemInput);
	}
	auto colonLiteralWRemInput = skipWhitespace(colonLiteral.remaining);

	auto argumentExpr = parseExpressionTerms(colonLiteralWRemInput);
	if (!argumentExpr.ok)
	{
		unrecoverableError("Expected argument expression", colonLiteralWRemInput);
	}

	auto additionalParametersLiteral = parseLiteral(argumentExpr.remaining, ",");
	if (additionalParametersLiteral.ok)
	{
		unrecoverableError("Multiple arguments not implemented", argumentExpr.remaining);
	}

	auto closeParLiteral = parseLiteral(argumentExpr.remaining, ")");
	if (!closeParLiteral.ok)
	{
		unrecoverableError("Expected closing parenthesis", argumentExpr.remaining);
	}

	std::vector<FuncArgument> arguments;
	FuncArgument& arg = arguments.emplace_back();
	arg.name = parameterName.result;
	arg.direction = direction.result;
	arg.expr = std::move(*argumentExpr.result);

	std::unique_ptr<Expression> expression = std::make_unique<Expression>();
	expression->rep = std::string_view(input.current.data(), closeParLiteral.remaining.current.data());
	expression->expr = FunctionCall{ std::string(funcName.result), std::move(arguments) };
	return { true, skipWhitespace(closeParLiteral.remaining), std::move(expression) };
}

ParseResult<std::unique_ptr<Expression>> parseTerm(ParseInput const& input) // NOLINT(misc-no-recursion)
{
	auto parStart = parseLiteral(input, "(");
	if (parStart.ok)
	{
		auto innerExpr = parseExpressionTerms(skipWhitespace(parStart.remaining));
		if (!innerExpr.ok)
		{
			unrecoverableError("Expected inner expression", parStart.remaining);
		}

		auto parEnd = parseLiteral(innerExpr.remaining, ")");
		if (!parEnd.ok)
		{
			unrecoverableError("Expected closing parenthesis", innerExpr.remaining);
		}

		return { true, parEnd.remaining, std::move(innerExpr.result) };
	}

	auto functionCallExpr = parseFunctionCall(input);
	if (functionCallExpr.ok)
	{
		return functionCallExpr;
	}

	auto varExpression = parseVarExpression(input);
	if (varExpression.ok)
	{
		return varExpression;
	}

	return parseNumberWithType(input);
}

BinaryOperator parseBinaryOperator(ParseInput const& input)
{
	char opChar = input.current.at(0);
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
		unrecoverableError(fmt::format("Unknown binary operator: {}", opChar), input);
	}
}

ParseResult<std::unique_ptr<Expression>> parseExpressionTerms(ParseInput const& input) // NOLINT(misc-no-recursion)
{
	auto firstTerm = parseTerm(input);
	if (!firstTerm.ok)
	{
		unrecoverableError("Expected an expression term", input);
	}
	auto currentHead = std::move(firstTerm.result);
	auto currentRemainingInput = skipWhitespace(firstTerm.remaining);

	while (!currentRemainingInput.current.empty()
		   && (currentRemainingInput.current[0] == '+' || currentRemainingInput.current[0] == '-'
			   || currentRemainingInput.current[0] == '*' || currentRemainingInput.current[0] == '/'
			   || currentRemainingInput.current[0] == '%'))
	{
		BinaryOperator const binaryOperator = parseBinaryOperator(currentRemainingInput);

		auto tail = skipWhitespace(currentRemainingInput.consume(1));
		auto nextTerm = parseTerm(tail);
		if (!nextTerm.ok)
		{
			unrecoverableError("Expected a right-hand side term for binary operator", tail);
		}

		std::unique_ptr<Expression> binaryOpExpression = std::make_unique<Expression>();
		binaryOpExpression->rep = trim(std::string_view(input.current.data(), nextTerm.remaining.current.data()));
		binaryOpExpression->expr = BinaryOpExpression{ binaryOperator, std::move(currentHead), std::move(nextTerm.result) };

		std::swap(currentHead, binaryOpExpression);
		currentRemainingInput = skipWhitespace(nextTerm.remaining);
	}

	return { true, currentRemainingInput, std::move(currentHead) };
}

ParseResult<Expression> parseExpression(ParseInput const& input)
{
	auto varIdentifier = parseIdentifier(input);
	if (!varIdentifier.ok)
	{
		unrecoverableError("Expected identifier at start of expression", input);
	}
	auto identifierWRemInput = skipWhitespace(varIdentifier.remaining);

	if (!identifierWRemInput.current.starts_with('='))
	{
		unrecoverableError("Expected assignment after var", identifierWRemInput);
	}
	auto assignmentWRemInput = skipWhitespace(identifierWRemInput.consume(1));

	auto valueExpr = parseExpressionTerms(assignmentWRemInput);
	if (!valueExpr.ok)
	{
		unrecoverableError("Failed to parse expression terms", assignmentWRemInput);
	}
	if (!valueExpr.remaining.current.starts_with(';'))
	{
		unrecoverableError("Expected assignment to be followed by ';'", valueExpr.remaining);
	}
	auto leftOver = valueExpr.remaining.consume(1);

	InitAssignment initAssignment;
	initAssignment.var = varIdentifier.result;
	initAssignment.value = std::move(valueExpr.result);

	Expression expr;
	expr.rep = trim(std::string_view(input.current.data(), leftOver.current.data()));
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
	ParseInput const& input)
{
	auto funcType = parseFuncType(program, input);
	if (funcType.ok)
	{
		Type maybeNewType;
		maybeNewType.rep = trim(std::string_view(input.current.data(), funcType.remaining.current.data()));
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
			unrecoverableError(fmt::format("Type not implemented: {}", plainType.result), input);
		}
	}

	unrecoverableError("Expected type", input);
}

ParseResult<Expression> parseFunctionBody(ParseInput const& input)
{
	if (!input.current.starts_with('{'))
	{
		unrecoverableError("Missing '{' at start of function", input);
	}

	std::vector<Expression> expressions;
	auto remainingInput = skipWhitespace(input.consume(1));
	while (!remainingInput.current.empty() && !remainingInput.current.starts_with('}'))
	{
		auto expression = parseExpression(remainingInput);
		if (!expression.ok)
		{
			unrecoverableError("Expected an expression in function body", remainingInput);
		}
		expressions.push_back(std::move(expression.result));
		remainingInput = expression.remaining;
	}

	if (!remainingInput.current.starts_with('}'))
	{
		unrecoverableError("Missing '}' at end of function", remainingInput);
	}

	auto leftOverInput = skipWhitespace(remainingInput.consume(1));

	if (expressions.size() == 1)
	{
		return { true, leftOverInput, std::move(expressions[0]) };
	}
	else if (expressions.empty())
	{
		// TODO: Implement empty function body
		unrecoverableError("Empty function body not implemented", remainingInput);
	}
	else
	{
		// TODO: Implement function body with multiple expressions
		unrecoverableError("Function body with multiple expressions not implemented", input);
	}
}

ParseResult<std::shared_ptr<Function>> parseDefinition(Program& program, ParseInput const& input)
{
	auto lineWRemInput = skipWhitespace(input);
	auto defLiteral = parseLiteral(lineWRemInput, "def");
	auto defWhitespace = parseWhitespace(defLiteral.remaining);
	if (!defLiteral.ok || !defWhitespace.ok)
	{
		unrecoverableError("Invalid syntax", input);
	}

	auto defIdentifier = parseIdentifier(defWhitespace.remaining);
	if (!defIdentifier.ok)
	{
		unrecoverableError("Missing name after def", defWhitespace.remaining);
	}
	auto identifierWRemInput = skipWhitespace(defIdentifier.remaining);

	auto assignmentLiteral = parseLiteral(identifierWRemInput, "=");
	if (!assignmentLiteral.ok)
	{
		unrecoverableError("Missing assignment in def", identifierWRemInput);
	}
	auto assignmentWRemInput = skipWhitespace(assignmentLiteral.remaining);

	auto type = parseType(program, assignmentWRemInput);
	if (!type.ok)
	{
		unrecoverableError("Unable to parse type", assignmentWRemInput);
	}

	auto functionBody = parseFunctionBody(type.remaining);
	if (!functionBody.ok)
	{
		unrecoverableError("Failed to parse function body", type.remaining);
	}
	auto funcBodyWRemInput = skipWhitespace(functionBody.remaining);

	if (!funcBodyWRemInput.current.starts_with(';'))
	{
		unrecoverableError("Invalid def end", funcBodyWRemInput);
	}
	auto remainingInput = skipWhitespace(funcBodyWRemInput.consume(1));

	std::shared_ptr<Function> const& mainFunc = program.functions.emplace_back(std::make_shared<Function>());
	mainFunc->name = defIdentifier.result;
	mainFunc->sourceFile = input.sourceFileName;
	mainFunc->type = type.result;
	mainFunc->expression = std::move(functionBody.result);

	if (mainFunc->name == "main")
	{
		if (!isMainFuncType(*mainFunc->type))
		{
			unrecoverableError("Wrong type for main", assignmentWRemInput);
		}

		if (program.mainFunction)
		{
			unrecoverableError("Multiple main functions found", defWhitespace.remaining);
		}

		program.mainFunction = mainFunc;
	}

	return { true, remainingInput, mainFunc };
}

Program parse(std::string_view input, std::string_view name)
{
	Program program;

	ParseInput remainingInput{ input, input, name };
	while (!remainingInput.current.empty())
	{
		auto funcDef = parseDefinition(program, remainingInput);
		if (!funcDef.ok)
		{
			unrecoverableError("Failed to parse definition", remainingInput);
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
