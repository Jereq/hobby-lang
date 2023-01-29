// SPDX-License-Identifier: MIT
// Copyright © 2022 Sebastian Larsson

#include <internal_use_only/config.hpp>

#include <hobbylang/ast/ast.hpp>
#include <hobbylang/interpreter/interpreter.hpp>
#include <hobbylang/parser/parser.hpp>
#include <hobbylang/wasm/wasm.hpp>

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

	std::filesystem::path outputPath{ "a.wasm" };
	app.add_option("-o,--output", outputPath, "Path where to put the compiled output. Defaults to a.wasm.")
		->option_text("FILE");
	app.set_version_flag("-v,--version", std::string(hobby_lang::cmake::project_version));
	bool execute = false;
	app.add_flag("-x,--execute", execute, "Execute the program instead of generating a compiled output");

	std::vector<std::filesystem::path> inputFiles;
	app.add_option("files", inputFiles, "Input files")->check(CLI::ExistingFile);

	CLI11_PARSE(app, argc, argv)

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

	if (execute)
	{
		std::int32_t executionResult = jereq::execute(parsedProgram);
		fmt::print("\nResult from execution: {}\n", executionResult);
	}
	else
	{
		std::ofstream output(outputPath, std::ofstream::binary);
		if (jereq::compile(parsedProgram, output))
		{
			spdlog::info("Successfully compiled program: {}", outputPath.string());
		}
		else
		{
			spdlog::error("Failed to compile program: {}", outputPath.string());
		}
	}

	return EXIT_SUCCESS;
}
catch (const std::exception& e)
{
	spdlog::error("Unhandled exception in main: {}", e.what());
}
