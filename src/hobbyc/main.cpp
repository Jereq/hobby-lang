// SPDX-License-Identifier: MIT
// Copyright © 2022 Sebastian Larsson

#include <internal_use_only/config.hpp>

#include <hobbylang/ast/ast.hpp>
#include <hobbylang/interpreter/interpreter.hpp>
#include <hobbylang/parser/parser.hpp>

#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>


int main(int argc, const char** argv)
try
{
	CLI::App app{ fmt::format("{} version {}", hobby_lang::cmake::project_name, hobby_lang::cmake::project_version) };

	app.set_version_flag("-v,--version", std::string(hobby_lang::cmake::project_version));
	bool execute = false;
	app.add_flag("-x,--execute", execute, "Execute the program instead of generating a compiled output");

	std::vector<std::filesystem::path> inputFiles;
	app.add_option("files", inputFiles, "Input files")->check(CLI::ExistingFile);

	CLI11_PARSE(app, argc, argv)

	if (!execute)
	{
		fmt::print("Compiling program not implemented, use --execute to execute the program.\n");
		return EXIT_FAILURE;
	}

	if (inputFiles.empty())
	{
		fmt::print("Missing input files.\n");
		return EXIT_FAILURE;
	}

	if (inputFiles.size() > 1)
	{
		fmt::print("Multiple input files not implemented.\n");
		return EXIT_FAILURE;
	}

	auto absPath = std::filesystem::absolute(inputFiles.at(0));
	std::ifstream input(absPath, std::ifstream::binary);
	jereq::Program parsedProgram = jereq::parse(input, absPath.string());

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

	return EXIT_SUCCESS;
}
catch (const std::exception& e)
{
	spdlog::error("Unhandled exception in main: {}", e.what());
}
