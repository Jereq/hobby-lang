// SPDX-License-Identifier: MIT
// Copyright © 2022 Sebastian Larsson

#include <internal_use_only/config.hpp>

#include <hobbylang/ast/ast.hpp>
#include <hobbylang/interpreter/interpreter.hpp>
#include <hobbylang/parser/parser.hpp>

#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>

#include <optional>
#include <sstream>
#include <string>


int main(int argc, const char** argv)
try
{
	CLI::App app{ fmt::format("{} version {}", hobby_lang::cmake::project_name, hobby_lang::cmake::project_version) };

	std::optional<std::string> message;
	app.add_option("-m,--message", message, "A message to print back out");
	bool show_version = false;
	app.add_flag("--version", show_version, "Show version information");

	CLI11_PARSE(app, argc, argv)

	if (show_version)
	{
		fmt::print("{}\n", hobby_lang::cmake::project_version);
		return EXIT_SUCCESS;
	}

	std::istringstream input("def main = fun(out exitCode: i32) { exitCode = 4i32 + 1i32 + -3i32; };");
	jereq::Program parsedProgram = jereq::parse(input, "<anonymous>");

	fmt::print("Types:\n");
	for (auto const& type : parsedProgram.types)
	{
		fmt::print("  {}\n", type->rep);
	}
	fmt::print("Functions:\n");
	for (auto const& func : parsedProgram.functions)
	{
		fmt::print("  {}: {} {}\n", func->name, func->type->rep, func->expression.rep);
	}
	fmt::print("Main function: {}\n", parsedProgram.mainFunction->name);

	std::int32_t executionResult = jereq::execute(parsedProgram);
	fmt::print("\nResult from execution: {}\n", executionResult);
}
catch (const std::exception& e)
{
	spdlog::error("Unhandled exception in main: {}", e.what());
}
