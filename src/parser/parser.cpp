// SPDX-License-Identifier: MIT
// Copyright © 2022 Sebastian Larsson
#include <hobbylang/parser/parser.hpp>

#include <hobbylang/ast/ast.hpp>

#include <charconv>
#include <istream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace jereq
{
std::shared_ptr<Type> findOrAddType(Program& program, std::string_view rep);

FuncType parseFuncType(Program& program, std::string_view rep) // NOLINT(misc-no-recursion)
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

std::shared_ptr<Type> findOrAddType(Program& program, std::string_view rep) // NOLINT(misc-no-recursion)
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
		throw std::runtime_error("Type not implemented: " + std::string(rep));
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

Expression parseExpression(std::string_view input)
{
	Expression result;
	result.rep = input;

	if (input.size() < 5 || !input.starts_with("{ ") || !input.ends_with(" }"))
	{
		throw std::runtime_error("I don't understand this expression: " + std::string(input));
	}

	std::string_view const inner = input.substr(2, input.size() - 4);
	auto varEnd = inner.find(' ');
	if (varEnd == std::string_view::npos)
	{
		throw std::runtime_error("Missing space after var name in: " + std::string(inner));
	}

	if (inner.size() < varEnd + 3 || inner.substr(varEnd, 3) != " = ")
	{
		throw std::runtime_error("Expected assignment in: " + std::string(inner));
	}

	auto valStart = varEnd + 3;
	if (!inner.ends_with("i32;"))
	{
		throw std::runtime_error("Expected type after value in: " + std::string(inner));
	}

	std::string_view const valStr = inner.substr(valStart, inner.size() - valStart - 4);
	std::int32_t value;
	auto [ptr, ec] = std::from_chars(valStr.data(), valStr.data() + valStr.size(), value);

	if (ptr != valStr.data() + valStr.size() || ec != std::errc())
	{
		throw std::runtime_error("Failed to read value in: " + std::string(inner));
	}

	InitAssignment initAssignment;
	initAssignment.var = inner.substr(0, varEnd);
	initAssignment.value = value;
	result.expr = initAssignment;

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

Program parse(std::istream& input, std::string_view name)
{
	Program program;

	std::string line;
	while (std::getline(input, line))
	{
		if (line.starts_with("def "))
		{
			auto nameStart = line.find_first_not_of(' ', 4);
			if (nameStart == std::string::npos)
			{
				throw std::runtime_error("Missing name after def in: " + line);
			}

			auto nameEnd = line.find(' ', nameStart + 1);
			if (nameEnd == std::string::npos)
			{
				throw std::runtime_error("Missing end to name in: " + line);
			}

			if (line.substr(nameEnd, 3) != " = ")
			{
				throw std::runtime_error("Missing assignment in def: " + line);
			}

			auto typeStart = nameEnd + 3;
			auto funcTypeEnd = line.find(')', typeStart);
			if (funcTypeEnd == std::string::npos)
			{
				throw std::runtime_error("Missing func type end in: " + line);
			}
			funcTypeEnd += 1;

			auto funcBodyStart = line.find('{', funcTypeEnd + 1);
			if (funcBodyStart == std::string::npos)
			{
				throw std::runtime_error("Missing func body start in: " + line);
			}

			auto funcBodyEnd = line.find('}', funcBodyStart + 1);
			if (funcBodyEnd == std::string::npos)
			{
				throw std::runtime_error("Missing func body end in: " + line);
			}
			funcBodyEnd += 1;

			if (line.substr(funcBodyEnd) != ";")
			{
				throw std::runtime_error("Invalid def end in: " + line);
			}

			std::shared_ptr<Type> const& type = findOrAddType(program, line.substr(typeStart, funcTypeEnd - typeStart));

			std::shared_ptr<Function> const& mainFunc = program.functions.emplace_back(std::make_shared<Function>());
			mainFunc->name = line.substr(nameStart, nameEnd - nameStart);
			mainFunc->sourceFile = name;
			mainFunc->type = type;
			mainFunc->expression = parseExpression(line.substr(funcBodyStart, funcBodyEnd - funcBodyStart));

			if (mainFunc->name == "main")
			{
				if (!isMainFuncType(*mainFunc->type))
				{
					throw std::runtime_error("Wrong type for main");
				}

				if (program.mainFunction)
				{
					throw std::runtime_error("Multiple main functions found");
				}

				program.mainFunction = mainFunc;
			}
		}
		else
		{
			throw std::runtime_error("Invalid syntax in: " + line);
		}
	}

	if (!program.mainFunction)
	{
		throw std::runtime_error("No main function");
	}

	return program;
}
}
