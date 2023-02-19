// SPDX-License-Identifier: MIT
// Copyright © 2022-2023 Sebastian Larsson
#include <hobbylang/wasm/wasm.hpp>

#include <hobbylang/ast/ast.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <map>
#include <ostream>
#include <span>
#include <sstream>

namespace
{
std::ostream& writeByte(std::ostream& out, std::byte value)
{
	return out.put(static_cast<char>(value));
}

template<std::size_t extent = std::dynamic_extent>
std::ostream& writeBytes(std::ostream& out, std::span<std::byte const, extent> bytes)
{
	if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
	{
		throw std::runtime_error("Writing ridiculous amounts of data at once is not supported");
	}
	return out.write(reinterpret_cast<char const*>(bytes.data()), bytes.size());
}

template<std::size_t extent>
std::ostream& writeBytes(std::ostream& out, std::array<std::byte, extent> const& bytes)
{
	return writeBytes(out, std::span(bytes));
}

void writeMagic(std::ostream& out)
{
	static constexpr std::array<std::byte, 4> magic{
		std::byte{ 0x00 }, std::byte{ 0x61 }, std::byte{ 0x73 }, std::byte{ 0x6D }
	};
	writeBytes(out, magic);
}

void writeVersion(std::ostream& out)
{
	static constexpr std::array<std::byte, 4> version{
		std::byte{ 0x01 }, std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x00 }
	};
	writeBytes(out, version);
}

void writeULEB128(std::ostream& out, std::uint32_t value)
{
	static constexpr std::uint32_t _7_BITS = 7;
	static constexpr std::uint32_t firstBit = 1U << _7_BITS;
	while (value >= firstBit)
	{
		writeByte(out, std::byte(value | firstBit));
		value >>= _7_BITS;
	}
	writeByte(out, std::byte(value));
}

void writeSLEB128(std::ostream& out, std::int32_t value)
{
	static constexpr std::uint32_t _6_BITS = 6;
	static constexpr std::uint32_t _7_BITS = 7;
	static constexpr std::uint32_t firstBit = 1U << _7_BITS;
	std::uint32_t const flipper = value < 0 ? ~0U : 0;

	auto unsignedValue = std::bit_cast<std::uint32_t>(value) ^ flipper;
	while (unsignedValue >= 1U << _6_BITS)
	{
		writeByte(out, std::byte((unsignedValue ^ flipper) | firstBit));
		unsignedValue >>= _7_BITS;
	}
	writeByte(out, std::byte((unsignedValue ^ flipper) & ~firstBit));
}

void writeVector(std::ostream& out, std::span<std::byte const> vector)
{
	writeULEB128(out, static_cast<std::uint32_t>(vector.size()));
	writeBytes(out, vector);
}

std::span<std::byte const> asBytes(std::string_view str)
{
	return { reinterpret_cast<std::byte const*>(str.data()), str.size() };
}

void writeName(std::ostream& out, std::string_view name)
{
	writeVector(out, asBytes(name));
}

void writeSection(std::ostream& out, std::uint8_t sectionNumber, std::span<std::byte const> contents)
{
	writeByte(out, std::byte{ sectionNumber });
	writeVector(out, contents);
}

struct WasmFuncType
{
	std::vector<std::byte> inParameters;
	std::vector<std::byte> outParameters;
};

WasmFuncType translateFuncType(jereq::FuncType const& funcType)
{
	std::vector<std::byte> inParameters;
	std::vector<std::byte> outParameters;

	for (auto const& parameter : funcType.parameters)
	{
		if (parameter.direction == jereq::ParameterDirection::inout)
		{
			throw std::runtime_error("inout parameter direction no supported yet");
		}

		std::vector<std::byte>& parameterList
			= parameter.direction == jereq::ParameterDirection::out ? outParameters : inParameters;
		if (std::holds_alternative<jereq::BuiltInType>(parameter.type->t))
		{
			auto const& builtInType = std::get<jereq::BuiltInType>(parameter.type->t);
			if (builtInType.name == "i32")
			{
				parameterList.push_back(std::byte{ 0x7F });
			}
			else
			{
				throw std::runtime_error("Built-in type " + builtInType.name + " not implemented");
			}
		}
		else
		{
			throw std::runtime_error("Only build-in types implemented");
		}
	}

	if (outParameters.size() > 1)
	{
		throw std::runtime_error("Multiple out parameters not supported yet");
	}

	return { inParameters, outParameters };
}

struct WasmFuncTypeTranslation
{
	std::vector<WasmFuncType> wasmFuncTypes;
	std::map<jereq::Type const*, std::uint32_t> translation;
};

WasmFuncTypeTranslation translateFuncTypes(std::vector<std::shared_ptr<jereq::Type>>& types)
{
	std::vector<WasmFuncType> wasmFuncTypes;
	std::map<jereq::Type const*, std::uint32_t> translation;

	for (const auto& type : types)
	{
		if (std::holds_alternative<jereq::FuncType>(type->t))
		{
			translation.try_emplace(type.get(), static_cast<std::uint32_t>(wasmFuncTypes.size()));
			wasmFuncTypes.push_back(translateFuncType(std::get<jereq::FuncType>(type->t)));
		}
	}

	return { wasmFuncTypes, translation };
}

void writeResultType(std::ostream& out, std::vector<std::byte> const& parameters)
{
	writeVector(out, parameters);
}

void writeType(std::ostream& out, WasmFuncType const& funcType)
{
	writeByte(out, std::byte{ 0x60 });
	writeResultType(out, funcType.inParameters);
	writeResultType(out, funcType.outParameters);
}

void writeTypeSection(std::ostream& out, WasmFuncTypeTranslation const& typeTranslation)
{
	std::ostringstream typeVecOut;
	writeULEB128(typeVecOut, typeTranslation.wasmFuncTypes.size());
	for (auto const& funcType : typeTranslation.wasmFuncTypes)
	{
		writeType(typeVecOut, funcType);
	}

	std::string const& typeVecOutStr = typeVecOut.str();
	writeSection(out, 1, asBytes(typeVecOutStr));
}

struct ImportFunctionInformation
{
	std::string module;
	std::string name;
	jereq::Type* type;
};

void writeImport(std::ostream& out, std::string_view moduleName, std::string_view functionName, std::uint32_t typeIdx)
{
	writeName(out, moduleName);
	writeName(out, functionName);
	writeByte(out, std::byte{ 0x00 });
	writeULEB128(out, typeIdx);
}

void writeImportSection(std::ostream& out,
	std::vector<ImportFunctionInformation> const& importFunctionInfo,
	WasmFuncTypeTranslation const& typeTranslation)
{
	std::ostringstream importVecOut;
	writeULEB128(importVecOut, importFunctionInfo.size());
	for (auto const& functionInfo : importFunctionInfo)
	{
		writeImport(
			importVecOut, functionInfo.module, functionInfo.name, typeTranslation.translation.at(functionInfo.type));
	}

	std::string const& importVecOutStr = importVecOut.str();
	writeSection(out, 2, asBytes(importVecOutStr));
}

void writeFunction(std::ostream& out, jereq::Type const* functionType, WasmFuncTypeTranslation const& typeTranslation)
{
	auto const& iter = typeTranslation.translation.find(functionType);
	if (iter == typeTranslation.translation.cend())
	{
		throw std::runtime_error("Function type not found");
	}

	writeULEB128(out, iter->second);
}

void writeFunctionSection(std::ostream& out,
	std::vector<std::shared_ptr<jereq::Function>> const& functions,
	WasmFuncTypeTranslation const& typeTranslation)
{
	std::ostringstream funcVecOut;
	writeULEB128(funcVecOut, functions.size());
	for (auto const& function : functions)
	{
		writeFunction(funcVecOut, function->type.get(), typeTranslation);
	}

	std::string const& funcVecOutStr = funcVecOut.str();
	writeSection(out, 3, asBytes(funcVecOutStr));
}

void writeLimits(std::ostream& out)
{
	writeByte(out, std::byte{ 0x01 });
	writeULEB128(out, 0);
	writeULEB128(out, 1024);
}

void writeMemory(std::ostream& out)
{
	writeLimits(out);
}

void writeMemorySection(std::ostream& out)
{
	std::ostringstream memoryVecOut;
	writeULEB128(memoryVecOut, 1);
	writeMemory(memoryVecOut);

	std::string const& memoryVecOutStr = memoryVecOut.str();
	writeSection(out, 5, asBytes(memoryVecOutStr));
}

void writeExportFunction(std::ostream& out, std::string_view name, std::uint32_t idx)
{
	writeName(out, name);
	writeByte(out, std::byte{ 0x00 });
	writeULEB128(out, idx);
}

void writeExportMemory(std::ostream& out)
{
	writeName(out, "memory");
	writeByte(out, std::byte{ 0x02 });
	writeByte(out, std::byte{ 0x00 });
}

struct Index
{
	std::map<jereq::Function const*, std::uint32_t> functions;
};

struct ExportFunctionInformation
{
	std::string exportName;
	jereq::Function const* function;
};

void writeExportSection(std::ostream& out,
	std::vector<ExportFunctionInformation> const& exportFunctionInfo,
	Index const& index)
{
	std::ostringstream exportVecOut;
	writeULEB128(exportVecOut, exportFunctionInfo.size() + 1);
	for (auto const& info : exportFunctionInfo)
	{
		writeExportFunction(exportVecOut, info.exportName, index.functions.at(info.function));
	}
	writeExportMemory(exportVecOut);

	std::string const& exportVecOutStr = exportVecOut.str();
	writeSection(out, 7, asBytes(exportVecOutStr));
}

void writeLocals(std::ostream& out)
{
	writeVector(out, {});
}

void writeExpression(std::ostream& out, jereq::Expression const& expression, Index const& index)
{
	if (expression.rep.empty())
	{
		for (auto const& [func, idx] : index.functions)
		{
			if (func->name == "main")
			{
				writeByte(out, std::byte{ 0x10 });
				writeULEB128(out, idx);
				writeByte(out, std::byte{ 0x10 });
				writeByte(out, std::byte{ 0x00 });
				return;
			}
		}

		throw std::runtime_error("Expected to find main in index");
	}
	else
	{
		if (std::holds_alternative<jereq::Literal>(expression.expr))
		{
			auto const& literal = std::get<jereq::Literal>(expression.expr);
			writeByte(out, std::byte{ 0x41 });
			writeSLEB128(out, literal.value);
		}
		else if (std::holds_alternative<jereq::InitAssignment>(expression.expr))
		{
			auto const& initAssignment = std::get<jereq::InitAssignment>(expression.expr);
			writeExpression(out, *initAssignment.value, index);
			// TODO: Verify variable has not been assigned before
			// TODO: Figure out locals and return values
			// TODO: Type checks
		}
		else if (std::holds_alternative<jereq::BinaryOpExpression>(expression.expr))
		{
			// TODO: type checks
			auto const& binExpr = std::get<jereq::BinaryOpExpression>(expression.expr);
			writeExpression(out, *binExpr.lhs, index);
			writeExpression(out, *binExpr.rhs, index);
			switch (binExpr.op)
			{
			case jereq::BinaryOperator::add:
				writeByte(out, std::byte{ 0x6A });
				break;
			case jereq::BinaryOperator::subtract:
				writeByte(out, std::byte{ 0x6B });
				break;
			case jereq::BinaryOperator::multiply:
				writeByte(out, std::byte{ 0x6C });
				break;
			case jereq::BinaryOperator::divide:
				// TODO: signed/unsigned
				writeByte(out, std::byte{ 0x6D });
				break;
			case jereq::BinaryOperator::modulo:
				// TODO: signed/unsigned
				writeByte(out, std::byte{ 0x6F });
				break;
			default:
				throw std::runtime_error("Operator not supported");
			}
		}
		else
		{
			throw std::runtime_error("Unexpected expression alternative");
		}
	}
}

void writeCode(std::ostream& out, jereq::Function const& function, Index const& index)
{
	std::ostringstream codeOut;
	writeLocals(codeOut);
	writeExpression(codeOut, function.expression, index);
	writeByte(codeOut, std::byte{ 0x0B });

	std::string const& codeOutStr = codeOut.str();
	writeVector(out, asBytes(codeOutStr));
}

void writeCodeSection(std::ostream& out,
	std::vector<std::shared_ptr<jereq::Function>> const& functions,
	Index const& index)
{
	std::ostringstream codeVecOut;
	writeULEB128(codeVecOut, functions.size());
	for (auto const& function : functions)
	{
		writeCode(codeVecOut, *function, index);
	}

	std::string const& codeVecOutStr = codeVecOut.str();
	writeSection(out, 10, asBytes(codeVecOutStr));
}

void injectFunctions(std::vector<std::shared_ptr<jereq::Type>>& types,
	std::vector<std::shared_ptr<jereq::Function>>& functions,
	std::vector<ImportFunctionInformation>& importFunctionInfo,
	std::vector<ExportFunctionInformation>& exportFunctionInfo)
{
	auto& startType = types.emplace_back(std::make_shared<jereq::Type>());
	startType->t = jereq::FuncType{ "", {} };

	std::shared_ptr<jereq::Function> const& startFunc = functions.emplace_back(std::make_shared<jereq::Function>());
	startFunc->name = "_start";
	startFunc->sourceFile = "generated";
	startFunc->type = startType;
	startFunc->expression = {};

	exportFunctionInfo.push_back(ExportFunctionInformation{ "_start", startFunc.get() });

	std::shared_ptr<jereq::Type> const& i32 = std::make_shared<jereq::Type>();
	i32->t = jereq::BuiltInType{ "i32" };
	jereq::FuncParameter const exitCode{ "exitCode", jereq::ParameterDirection::in, i32 };

	auto& procExitType = types.emplace_back(std::make_shared<jereq::Type>());
	procExitType->t = jereq::FuncType{ "", { exitCode } };
	importFunctionInfo.push_back(
		ImportFunctionInformation{ "wasi_snapshot_preview1", "proc_exit", procExitType.get() });
}

Index createIndex(std::uint32_t numImportFunctions, std::vector<std::shared_ptr<jereq::Function>> const& functions)
{
	Index result;

	std::uint32_t nextIndex = numImportFunctions;
	for (auto const& function : functions)
	{
		result.functions.try_emplace(function.get(), nextIndex);
		++nextIndex;
	}

	return result;
}
}

namespace jereq
{
bool compile(Program const& program, std::ostream& out)
{
	std::vector<std::shared_ptr<Type>> types = program.types;
	std::vector<std::shared_ptr<Function>> functions = program.functions;

	std::vector<ImportFunctionInformation> importFunctionInformation;
	std::vector<ExportFunctionInformation> exportFunctionInformation;
	injectFunctions(types, functions, importFunctionInformation, exportFunctionInformation);

	WasmFuncTypeTranslation const& typeTranslation = translateFuncTypes(types);
	Index const index = createIndex(static_cast<std::uint32_t>(importFunctionInformation.size()), functions);

	writeMagic(out);
	writeVersion(out);
	writeTypeSection(out, typeTranslation);
	writeImportSection(out, importFunctionInformation, typeTranslation);
	writeFunctionSection(out, functions, typeTranslation);
	writeMemorySection(out);
	writeExportSection(out, exportFunctionInformation, index);
	writeCodeSection(out, functions, index);
	return static_cast<bool>(out);
}
}
