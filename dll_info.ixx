module;
#include <dia2.h>
#include <diacreate.h>
export module dll_info;
import std;
import utils;
import filter;

namespace dll_info { 
	export struct TypeInfo;
	export struct InterfaceInfo;
	export struct EnumInfo;
}

std::string bstr_to_utf8(BSTR value) {
	if (value == nullptr) {
		return {};
	}

	const int size = WideCharToMultiByte(
		CP_UTF8,
		0,
		value,
		-1,
		nullptr,
		0,
		nullptr,
		nullptr);
	if (size <= 1) {
		return {};
	}

	std::string result(static_cast<std::size_t>(size - 1), '\0');
	WideCharToMultiByte(
		CP_UTF8,
		0,
		value,
		-1,
		result.data(),
		size,
		nullptr,
		nullptr);
	return result;
}

std::uint64_t array_element_count(IDiaSymbol* array, IDiaSymbol* element) {
	DWORD count{};
	if (array->get_count(&count) == S_OK && count != 0) {
		return count;
	}

	ULONGLONG array_length{};
	ULONGLONG element_length{};
	if (array->get_length(&array_length) == S_OK && array_length != 0 &&
		element->get_length(&element_length) == S_OK && element_length != 0) {
		return array_length / element_length;
	}

	return 0;
}

std::string base_type_name(IDiaSymbol* symbol) {
	DWORD base_type{};
	if (symbol->get_baseType(&base_type) != S_OK) {
		return {};
	}

	ULONGLONG length{};
	symbol->get_length(&length);

	switch (base_type) {
	case btNoType:
		// The only place DIA hands out `btNoType` is the ellipsis of a
		// variadic parameter list.
		return "...";

	case btVoid:
		return "void";

	case btChar:
		return "char";

	case btWChar:
		return "wchar_t";

	case btChar8:
		return "char8_t";

	case btChar16:
		return "char16_t";

	case btChar32:
		return "char32_t";

	case btInt:
		switch (length) {
		case 1:
			return "signed char";

		case 2:
			return "short";

		case 4:
			return "int";

		case 8:
			return "long long";

		case 16:
			return "__int128";

		default:
			return "int";
		}

	case btUInt:
		switch (length) {
		case 1:
			return "unsigned char";

		case 2:
			return "unsigned short";

		case 4:
			return "unsigned int";

		case 8:
			return "unsigned long long";

		case 16:
			return "unsigned __int128";

		default:
			return "unsigned int";
		}

	case btFloat:
		switch (length) {
		case 4:
			return "float";

		case 8:
			return "double";

		case 16:
			return "long double";

		default:
			return "float";
		}

	case btBool:
		return "bool";

	case btLong:
		return length == 8 ? "long long" : "long";

	case btULong:
		return length == 8 ? "unsigned long long" : "unsigned long";

	case btBCD:
		return "BCD";

	case btCurrency:
		return "CURRENCY";

	case btDate:
		return "DATE";

	case btVariant:
		return "VARIANT";

	case btComplex:
		return "complex";

	case btBit:
		return "bit";

	case btBSTR:
		return "BSTR";

	case btHresult:
		return "HRESULT";

	default:
		return {};
	}
}

std::string build_type_name(IDiaSymbol* symbol, const std::string& declarator, int depth) {
	if (symbol == nullptr || depth > 24) {
		return {};
	}

	DWORD tag{};
	if (symbol->get_symTag(&tag) != S_OK) {
		return {};
	}

	// Pointer / reference
	if (tag == SymTagPointerType) {
		IDiaSymbol* pointee = nullptr;
		if (symbol->get_type(&pointee) != S_OK || pointee == nullptr) {
			return {};
		}

		DWORD pointee_tag{};
		const bool has_pointee_tag = pointee->get_symTag(&pointee_tag) == S_OK;

		BOOL rvalue_reference = FALSE;
		BOOL reference = FALSE;
		const bool is_rvalue =
			symbol->get_RValueReference(&rvalue_reference) == S_OK && rvalue_reference == TRUE;
		const bool is_lvalue =
			!is_rvalue && symbol->get_reference(&reference) == S_OK && reference == TRUE;

		std::string next = declarator;
		if (is_rvalue) {
			next.insert(next.begin(), '&');
			next.insert(next.begin(), '&');
		}
		else if (is_lvalue) {
			next.insert(next.begin(), '&');
		}
		else {
			next.insert(next.begin(), '*');
		}

		// `T (*)[N]` / `T (*)(...)` - the declarator has to stay grouped.
		if (has_pointee_tag &&
			(pointee_tag == SymTagArrayType || pointee_tag == SymTagFunctionType)) {
			next = "(" + next + ")";
		}

		std::string result = build_type_name(pointee, next, depth + 1);
		pointee->Release();
		return result;
	}

	// Array
	if (tag == SymTagArrayType) {
		IDiaSymbol* element = nullptr;
		if (symbol->get_type(&element) != S_OK || element == nullptr) {
			return {};
		}

		std::string next = declarator;
		next += '[';
		next += std::to_string(array_element_count(symbol, element));
		next += ']';

		std::string result = build_type_name(element, next, depth + 1);
		element->Release();
		return result;
	}

	// Base type
	if (tag == SymTagBaseType) {
		const std::string name = base_type_name(symbol);
		if (name.empty()) {
			return {};
		}
		return name + declarator;
	}

	// Function type
	if (tag == SymTagFunctionType) {
		IDiaSymbol* return_type = nullptr;
		std::string result;
		if (symbol->get_type(&return_type) == S_OK && return_type != nullptr) {
			result = build_type_name(return_type, {}, depth + 1);
			return_type->Release();
		}
		if (result.empty()) {
			return {};
		}

		std::string arguments;
		IDiaEnumSymbols* args = nullptr;
		if (symbol->findChildren(SymTagFunctionArgType, nullptr, nsNone, &args) == S_OK &&
			args != nullptr) {
			IDiaSymbol* arg = nullptr;
			ULONG fetched = 0;
			while (args->Next(1, &arg, &fetched) == S_OK && fetched == 1) {
				if (!arguments.empty()) {
					arguments += ", ";
				}

				const std::string argument_name = build_type_name(arg, {}, depth + 1);
				arguments += argument_name.empty() ? "<unknown>" : argument_name;

				arg->Release();
				arg = nullptr;
			}
			if (arg != nullptr) {
				arg->Release();
			}
			args->Release();
		}

		if (declarator.empty()) {
			return result + "(" + arguments + ")";
		}

		return result + "(" + declarator + ")(" + arguments + ")";
	}

	BSTR bstr = nullptr;
	std::string own_name;
	if (symbol->get_name(&bstr) == S_OK && bstr != nullptr) {
		own_name = bstr_to_utf8(bstr);
		SysFreeString(bstr);
	}

	// Function argument type: transparent wrapper around the real type
	if (tag == SymTagFunctionArgType) {
		IDiaSymbol* type = nullptr;
		if (symbol->get_type(&type) == S_OK && type != nullptr) {
			std::string result = build_type_name(type, declarator, depth + 1);
			type->Release();
			return result;
		}
		return {};
	}

	// Typedef: prefer the alias, fall back to the underlying type
	if (tag == SymTagTypedef && own_name.empty()) {
		IDiaSymbol* type = nullptr;
		if (symbol->get_type(&type) == S_OK && type != nullptr) {
			std::string result = build_type_name(type, declarator, depth + 1);
			type->Release();
			return result;
		}
		return {};
	}

	// UDT / enum / class / struct / data / ...
	if (own_name.empty()) {
		return {};
	}

	return own_name + declarator;
}

std::string symbol_name(IDiaSymbol* symbol) {
	if (symbol == nullptr)
		return {};

	DWORD tag{};
	if (symbol->get_symTag(&tag) != S_OK)
		return {};

	// @DEBUG
	//std::cout
	//	<< "tag=" << tag
	//	<< ", name='" << build_type_name(symbol, {}, 0) << "'";

	// @DEBUG
	//ULONGLONG length{};
	//if (symbol->get_length(&length) == S_OK)
	//	std::cout << ", length=" << length;

	//std::cout << '\n';

	return build_type_name(symbol, {}, 0);
}

std::string class_parent_name(IDiaSymbol* symbol) {
	if (symbol == nullptr) {
		return {};
	}

	IDiaSymbol* parent = nullptr;
	if (symbol->get_classParent(&parent) != S_OK || parent == nullptr) {
		return {};
	}

	std::string result = symbol_name(parent);
	parent->Release();
	return result;
}

std::size_t rfind_scope_operator(const std::string& text) {
	int angle_depth = 0;
	int paren_depth = 0;
	int bracket_depth = 0;

	for (std::size_t index = text.size(); index-- > 0;) {
		switch (text[index]) {
		case '>':
			++angle_depth;
			break;

		case '<':
			if (angle_depth != 0) {
				--angle_depth;
			}
			break;

		case ')':
			++paren_depth;
			break;

		case '(':
			if (paren_depth != 0) {
				--paren_depth;
			}
			break;

		case ']':
			++bracket_depth;
			break;

		case '[':
			if (bracket_depth != 0) {
				--bracket_depth;
			}
			break;

		case ':':
			if (angle_depth == 0 && paren_depth == 0 && bracket_depth == 0 &&
				index != 0 && text[index - 1] == ':') {
				return index - 1;
			}
			break;

		default:
			break;
		}
	}

	return std::string::npos;
}

std::pair<std::string, std::string> split_symbol_name(IDiaSymbol* symbol) {
	const std::string full_name = symbol_name(symbol);
	if (full_name.empty()) {
		return {};
	}

	DWORD tag{};
	const bool has_tag = symbol->get_symTag(&tag) == S_OK;

	// Only a function needs the `classParent` detour: its trailing identifier
	// is the function name and the enclosing class is reported separately, so
	// the namespace has to stop one scope earlier. Every other symbol keeps the
	// whole enclosing scope, which is what lets a nested type come out as
	// `Poco::DateTime::Months` instead of `Poco::Months`.
	if (has_tag && tag == SymTagFunction) {
		const std::string parent_name = class_parent_name(symbol);
		if (!parent_name.empty()) {
			const std::string prefix = parent_name + "::";
			if (full_name.starts_with(prefix)) {
				std::string result_name = full_name.substr(prefix.size());

				const std::size_t scope_separator = rfind_scope_operator(parent_name);
				std::string result_namespace;
				if (scope_separator != std::string::npos) {
					result_namespace = parent_name.substr(0, scope_separator);
				}

				return { std::move(result_name), std::move(result_namespace) };
			}
		}
	}

	const std::size_t separator = rfind_scope_operator(full_name);
	if (separator == std::string::npos) {
		return { full_name, {} };
	}

	return {
		full_name.substr(separator + 2),
		full_name.substr(0, separator)
	};
}

bool is_const_member_function(IDiaSymbol* symbol) {
	if (symbol == nullptr) {
		return false;
	}

	IDiaEnumSymbols* children = nullptr;
	if (symbol->findChildren(SymTagData, nullptr, nsNone, &children) != S_OK ||
		children == nullptr) {
		return false;
	}

	IDiaSymbol* child = nullptr;
	ULONG fetched = 0;
	bool result = false;

	while (!result && children->Next(1, &child, &fetched) == S_OK && fetched == 1) {
		if (symbol_name(child) == "this") {
			IDiaSymbol* this_type = nullptr;
			if (child->get_type(&this_type) == S_OK && this_type != nullptr) {
				IDiaSymbol* object_type = nullptr;
				if (this_type->get_type(&object_type) == S_OK && object_type != nullptr) {
					BOOL value = FALSE;
					if (object_type->get_constType(&value) == S_OK) {
						result = value == TRUE;
					}
					object_type->Release();
				}
				this_type->Release();
			}
		}

		child->Release();
		child = nullptr;
	}

	if (child != nullptr) {
		child->Release();
	}
	children->Release();
	return result;
}

enum class ThisParameter {
	// The symbol carries no parameter data at all - "unknown" must never be
	// treated as "static".
	Unknown,
	Present,
	Missing
};

ThisParameter probe_this_parameter(IDiaSymbol* symbol) {
	if (symbol == nullptr) {
		return ThisParameter::Unknown;
	}

	IDiaEnumSymbols* children = nullptr;
	if (symbol->findChildren(SymTagData, nullptr, nsNone, &children) != S_OK ||
		children == nullptr) {
		return ThisParameter::Unknown;
	}

	IDiaSymbol* child = nullptr;
	ULONG fetched = 0;
	bool any = false;
	bool found = false;

	while (!found && children->Next(1, &child, &fetched) == S_OK && fetched == 1) {
		any = true;
		if (symbol_name(child) == "this") {
			found = true;
		}

		child->Release();
		child = nullptr;
	}

	if (child != nullptr) {
		child->Release();
	}
	children->Release();

	if (found) {
		return ThisParameter::Present;
	}
	return any ? ThisParameter::Missing : ThisParameter::Unknown;
}

std::wstring utf8_to_wide(std::string_view text) {
	if (text.empty()) {
		return {};
	}

	const int length = static_cast<int>(text.size());
	int required = MultiByteToWideChar(
		CP_UTF8,
		MB_ERR_INVALID_CHARS,
		text.data(),
		length,
		nullptr,
		0);
	UINT code_page = CP_UTF8;
	DWORD flags = MB_ERR_INVALID_CHARS;

	if (required <= 0) {
		code_page = CP_ACP;
		flags = 0;
		required = MultiByteToWideChar(code_page, flags, text.data(), length, nullptr, 0);
	}
	if (required <= 0) {
		return {};
	}

	std::wstring result(static_cast<std::size_t>(required), L'\0');
	if (MultiByteToWideChar(code_page, flags, text.data(), length, result.data(), required) != required) {
		return {};
	}
	return result;
}

HRESULT create_dia_source(IDiaDataSource** source) {
	if (source == nullptr) {
		return E_POINTER;
	}
	*source = nullptr;

	HRESULT result = CoCreateInstance(
		__uuidof(DiaSource),
		nullptr,
		CLSCTX_INPROC_SERVER,
		__uuidof(IDiaDataSource),
		reinterpret_cast<void**>(source));
	if (SUCCEEDED(result) && *source != nullptr) {
		return result;
	}

	if (*source != nullptr) {
		(*source)->Release();
		*source = nullptr;
	}
	return NoRegCoCreate(
		L"msdia140.dll",
		__uuidof(DiaSource),
		__uuidof(IDiaDataSource),
		reinterpret_cast<void**>(source));
}

bool resolve_dll_export_function(IDiaSession* session, DWORD export_rva, DWORD& function_rva, IDiaSymbol** function) {
	if (session == nullptr || function == nullptr) {
		return false;
	}
	*function = nullptr;

	DWORD current_rva = export_rva;
	for (int depth = 0; depth < 8; ++depth) {
		IDiaSymbol* direct = nullptr;
		const HRESULT direct_result = session->findSymbolByRVA(
			current_rva,
			SymTagFunction,
			&direct);
		if (SUCCEEDED(direct_result) && direct != nullptr) {
			*function = direct;
			function_rva = current_rva;
			return true;
		}
		if (direct != nullptr) {
			direct->Release();
		}

		IDiaSymbol* thunk = nullptr;
		const HRESULT thunk_result = session->findSymbolByRVA(
			current_rva,
			SymTagThunk,
			&thunk);
		if (FAILED(thunk_result) || thunk == nullptr) {
			return false;
		}

		DWORD target_rva = 0;
		const HRESULT target_result = thunk->get_targetRelativeVirtualAddress(&target_rva);
		thunk->Release();
		if (FAILED(target_result) || target_rva == 0 || target_rva == current_rva) {
			return false;
		}
		current_rva = target_rva;
	}

	return false;
}

struct DllExportInfo {
	std::uint32_t rva{};
	std::uint32_t ordinal{};
	std::string name;
};

struct DllImageMapping {
	HANDLE file = INVALID_HANDLE_VALUE;
	HANDLE mapping = nullptr;
	const std::byte* view = nullptr;

	~DllImageMapping() {
		if (view != nullptr) {
			UnmapViewOfFile(view);
		}
		if (mapping != nullptr) {
			CloseHandle(mapping);
		}
		if (file != INVALID_HANDLE_VALUE) {
			CloseHandle(file);
		}
	}

	std::span<const std::byte> bytes(std::size_t size) const {
		return { view, size };
	}
};

template <class T>
bool read_pe_object(std::span<const std::byte> image, std::size_t offset, T& value) {
	if (offset > image.size() || sizeof(T) > image.size() - offset) {
		return false;
	}
	std::memcpy(&value, image.data() + offset, sizeof(T));
	return true;
}

std::string read_pe_string(std::span<const std::byte> image, std::size_t offset) {
	if (offset >= image.size()) {
		return {};
	}

	std::size_t end = offset;
	while (end < image.size() && image[end] != std::byte{ 0 }) {
		++end;
	}
	return std::string(
		reinterpret_cast<const char*>(image.data() + offset),
		end - offset);
}

bool pe_rva_to_file_offset(std::span<const std::byte> image, DWORD size_of_headers,
	WORD number_of_sections, std::size_t section_table_offset, DWORD rva, DWORD& file_offset) {
	if (rva < size_of_headers && rva < image.size()) {
		file_offset = rva;
		return true;
	}

	for (DWORD index = 0; index < number_of_sections; ++index) {
		IMAGE_SECTION_HEADER section{};
		const std::size_t section_offset =
			section_table_offset + static_cast<std::size_t>(index) * sizeof(section);
		if (!read_pe_object(image, section_offset, section)) {
			return false;
		}

		const std::uint64_t section_start = section.VirtualAddress;
		const std::uint64_t section_size = std::max<std::uint64_t>(
			section.Misc.VirtualSize,
			section.SizeOfRawData);
		if (rva < section_start || static_cast<std::uint64_t>(rva) >= section_start + section_size) {
			continue;
		}

		const std::uint64_t offset =
			static_cast<std::uint64_t>(section.PointerToRawData) +
			(static_cast<std::uint64_t>(rva) - section_start);
		if (offset >= image.size() || offset > (std::numeric_limits<std::uint32_t>::max)()) {
			return false;
		}

		file_offset = static_cast<DWORD>(offset);
		return true;
	}

	return false;
}

std::vector<DllExportInfo> read_dll_exports(const std::wstring& path) {
	std::vector<DllExportInfo> result;

	DllImageMapping file;
	file.file = CreateFileW(
		path.c_str(),
		GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);
	if (file.file == INVALID_HANDLE_VALUE) {
		return result;
	}

	LARGE_INTEGER file_size{};
	if (!GetFileSizeEx(file.file, &file_size) || file_size.QuadPart <= 0 ||
		static_cast<std::uint64_t>(file_size.QuadPart) > (std::numeric_limits<std::size_t>::max)()) {
		return result;
	}

	file.mapping = CreateFileMappingW(
		file.file,
		nullptr,
		PAGE_READONLY,
		0,
		0,
		nullptr);
	if (file.mapping == nullptr) {
		return result;
	}

	file.view = static_cast<const std::byte*>(MapViewOfFile(
		file.mapping,
		FILE_MAP_READ,
		0,
		0,
		0));
	if (file.view == nullptr) {
		return result;
	}

	const std::size_t image_size = static_cast<std::size_t>(file_size.QuadPart);
	const std::span<const std::byte> image = file.bytes(image_size);

	IMAGE_DOS_HEADER dos_header{};
	if (!read_pe_object(image, 0, dos_header) || dos_header.e_magic != IMAGE_DOS_SIGNATURE ||
		dos_header.e_lfanew < 0) {
		return result;
	}

	const std::size_t nt_offset = static_cast<std::size_t>(dos_header.e_lfanew);
	DWORD signature = 0;
	if (!read_pe_object(image, nt_offset, signature) || signature != IMAGE_NT_SIGNATURE) {
		return result;
	}

	IMAGE_FILE_HEADER file_header{};
	const std::size_t file_header_offset = nt_offset + sizeof(DWORD);
	if (!read_pe_object(image, file_header_offset, file_header)) {
		return result;
	}

	const std::size_t optional_header_offset = file_header_offset + sizeof(IMAGE_FILE_HEADER);
	WORD optional_magic = 0;
	if (!read_pe_object(image, optional_header_offset, optional_magic)) {
		return result;
	}

	IMAGE_DATA_DIRECTORY export_directory{};
	DWORD size_of_headers = 0;
	if (optional_magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
		IMAGE_OPTIONAL_HEADER32 optional_header{};
		if (!read_pe_object(image, optional_header_offset, optional_header) ||
			optional_header.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXPORT) {
			return result;
		}
		export_directory = optional_header.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
		size_of_headers = optional_header.SizeOfHeaders;
	}
	else if (optional_magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
		IMAGE_OPTIONAL_HEADER64 optional_header{};
		if (!read_pe_object(image, optional_header_offset, optional_header) ||
			optional_header.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXPORT) {
			return result;
		}
		export_directory = optional_header.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
		size_of_headers = optional_header.SizeOfHeaders;
	}
	else {
		return result;
	}

	if (export_directory.VirtualAddress == 0 || export_directory.Size == 0) {
		return result;
	}

	DWORD export_offset = 0;
	const std::size_t section_table_offset =
		optional_header_offset + file_header.SizeOfOptionalHeader;
	if (!pe_rva_to_file_offset(
		image,
		size_of_headers,
		file_header.NumberOfSections,
		section_table_offset,
		export_directory.VirtualAddress,
		export_offset)) {
		return result;
	}

	IMAGE_EXPORT_DIRECTORY export_header{};
	if (!read_pe_object(image, export_offset, export_header)) {
		return result;
	}

	const DWORD function_count = export_header.NumberOfFunctions;
	const DWORD name_count = export_header.NumberOfNames;
	if (function_count == 0) {
		return result;
	}

	DWORD functions_offset = 0;
	if (!pe_rva_to_file_offset(
		image,
		size_of_headers,
		file_header.NumberOfSections,
		section_table_offset,
		export_header.AddressOfFunctions,
		functions_offset)) {
		return result;
	}

	DWORD names_offset = 0;
	DWORD ordinals_offset = 0;
	if (name_count != 0 &&
		(!pe_rva_to_file_offset(
			image,
			size_of_headers,
			file_header.NumberOfSections,
			section_table_offset,
			export_header.AddressOfNames,
			names_offset) ||
			!pe_rva_to_file_offset(
				image,
				size_of_headers,
				file_header.NumberOfSections,
				section_table_offset,
				export_header.AddressOfNameOrdinals,
				ordinals_offset))) {
		return result;
	}

	std::vector<std::uint32_t> name_rvas(function_count, 0);
	for (DWORD index = 0; index < name_count; ++index) {
		DWORD name_rva = 0;
		WORD ordinal_index = 0;
		if (!read_pe_object(
			image,
			static_cast<std::size_t>(names_offset) +
			static_cast<std::size_t>(index) * sizeof(DWORD),
			name_rva) ||
			!read_pe_object(
				image,
				static_cast<std::size_t>(ordinals_offset) +
				static_cast<std::size_t>(index) * sizeof(WORD),
				ordinal_index)) {
			return result;
		}

		if (ordinal_index < function_count && name_rvas[ordinal_index] == 0) {
			name_rvas[ordinal_index] = name_rva;
		}
	}

	const std::uint64_t forwarder_begin = export_directory.VirtualAddress;
	const std::uint64_t forwarder_end = forwarder_begin + export_directory.Size;
	result.reserve(function_count);

	for (DWORD index = 0; index < function_count; ++index) {
		DWORD function_rva = 0;
		if (!read_pe_object(
			image,
			static_cast<std::size_t>(functions_offset) +
			static_cast<std::size_t>(index) * sizeof(DWORD),
			function_rva)) {
			return result;
		}
		if (function_rva == 0) {
			continue;
		}

		if (function_rva >= forwarder_begin && function_rva < forwarder_end) {
			continue;
		}

		DllExportInfo exported;
		exported.rva = function_rva;
		exported.ordinal = export_header.Base + index;

		if (name_rvas[index] != 0) {
			DWORD name_offset = 0;
			if (pe_rva_to_file_offset(
				image,
				size_of_headers,
				file_header.NumberOfSections,
				section_table_offset,
				name_rvas[index],
				name_offset)) {
				exported.name = read_pe_string(image, name_offset);
			}
		}

		result.push_back(std::move(exported));
	}

	return result;
}

void release_symbol(IDiaSymbol* symbol) {
	if (symbol != nullptr) {
		symbol->Release();
	}
}

std::int64_t variant_to_int64(const VARIANT& value) {
	switch (value.vt) {
	case VT_I1:
		return value.cVal;

	case VT_UI1:
		return value.bVal;

	case VT_I2:
		return value.iVal;

	case VT_UI2:
		return value.uiVal;

	case VT_I4:
		return value.lVal;

	case VT_UI4:
		return value.ulVal;

	case VT_I8:
		return value.llVal;

	case VT_UI8:
		return static_cast<std::int64_t>(value.ullVal);

	case VT_INT:
		return value.intVal;

	case VT_UINT:
		return static_cast<std::int64_t>(value.uintVal);

	case VT_BOOL:
		return value.boolVal == VARIANT_TRUE ? 1 : 0;

	case VT_R4:
		return static_cast<std::int64_t>(value.fltVal);

	case VT_R8:
		return static_cast<std::int64_t>(value.dblVal);

	default:
		return 0;
	}
}

void ensure_com_initialized() {
	CoInitializeEx(nullptr, COINIT_MULTITHREADED);
}

std::vector<dll_info::TypeInfo> get_base_types(const dll_info::TypeInfo& type);
dll_info::EnumInfo build_enum_info(IDiaSymbol* symbol);

export namespace dll_info {

	// 枚举成员的两种拼写都封装成 EnumValue：
	//   完整拼写 Poco::Message::Priority::PRIO_FATAL
	//       -> namespace_name = "Poco::Message", class_name = "Priority"
	//   裸拼写   Poco::Message::PRIO_FATAL（unscoped 枚举成员泄漏到外层作用域）
	//       -> namespace_name = "Poco",        class_name = "Message"
	// class_name 是成员的直接限定段：完整拼写下就是枚举名，裸拼写下是枚举所在的类。
	struct EnumValue {
		std::string name;
		std::string namespace_name;
		std::string class_name;
		std::int64_t value{};

		// 由拼写字符串构造：最后一次 :: 切出成员名，作用域末段是 class_name，其余是 namespace_name
		explicit EnumValue(std::string spelling, std::int64_t member_value = 0) : value(member_value) {
			const std::size_t member_separator = rfind_scope_operator(spelling);
			if (member_separator == std::string::npos) {
				name = std::move(spelling);
				return;
			}

			name = spelling.substr(member_separator + 2);
			std::string scope = spelling.substr(0, member_separator);
			const std::size_t scope_separator = rfind_scope_operator(scope);
			if (scope_separator == std::string::npos) {
				class_name = std::move(scope);
			}
			else {
				class_name = scope.substr(scope_separator + 2);
				namespace_name = scope.substr(0, scope_separator);
			}
		}

		std::string string() const {
			std::string ret{};
			if (!namespace_name.empty())
				ret += namespace_name + "::";

			if (!class_name.empty())
				ret += class_name + "::";
			ret += name.empty() ? "<unknown>" : name;
			return ret;
		}

		bool operator == (const EnumValue& other) const {
			if (name != other.name) {
				return false;
			}

			const std::string scope = namespace_name.empty()
				? class_name
				: namespace_name + "::" + class_name;
			const std::string other_scope = other.namespace_name.empty()
				? other.class_name
				: other.namespace_name + "::" + other.class_name;

			// 作用域完全一致：同一枚举的同名成员
			if (scope == other_scope) {
				return true;
			}

			// unscoped 泄漏：完整拼写比裸拼写恰好多一层枚举名段，
			// 即一边的完整作用域等于另一边的外层作用域
			return scope == other.namespace_name || other_scope == namespace_name;
		}
	};

	struct EnumInfo {
		std::string name;
		std::string namespace_name;
		std::size_t size{};  // size in bytes of the underlying type
		std::vector<EnumValue> values;
	};

	struct TypeInfo {
		// Owning: valid for as long as the TypeInfo (or any of its copies)
		// lives, so interfaces returning TypeInfos keep usable handles.
		std::shared_ptr<IDiaSymbol> handle;

		std::string name;
		std::string namespace_name;

		enum class type {
			Unknown,
			Enum,
			Union,
			Struct,
			Class
		} type{ type::Unknown };

		bool is_const{};      // const T / const T* / const T& - qualifies `name`
		bool is_top_const{};  // T* const / T& const - qualifies the pointer itself
		bool is_pointer{};
		bool is_ref{};
		bool is_left_ref{};
		bool is_right_ref{};

		explicit TypeInfo(IDiaSymbol* hd) : handle(hd, release_symbol) {
			if (handle == nullptr) {
				return;
			}

			DWORD tag = 0;
			if (handle->get_symTag(&tag) != S_OK) {
				return;
			}

			if (tag == SymTagPointerType) {
				BOOL reference = FALSE;
				BOOL rvalue_reference = FALSE;

				const HRESULT reference_result = handle->get_reference(&reference);
				const HRESULT rvalue_result = handle->get_RValueReference(&rvalue_reference);

				is_right_ref = rvalue_result == S_OK && rvalue_reference == TRUE;
				is_left_ref = reference_result == S_OK && reference == TRUE && !is_right_ref;
				is_ref = is_left_ref || is_right_ref;
				is_pointer = !is_ref;
			}

			IDiaSymbol* name_symbol = handle.get();
			IDiaSymbol* target_symbol = nullptr;
			if (tag == SymTagPointerType && handle->get_type(&target_symbol) == S_OK &&
				target_symbol != nullptr) {
				name_symbol = target_symbol;
				name_symbol->get_symTag(&tag);
			}

			auto [resolved_name, resolved_namespace] = split_symbol_name(name_symbol);
			name = std::move(resolved_name);
			namespace_name = std::move(resolved_namespace);

			// `const T*` / `const T&` / `const T`: the qualification DIA reports on
			// the named type. For a pointer the named type is the pointee, so this
			// is the pointee qualification - which is what a signature cares about.
			BOOL const_type = FALSE;
			if (name_symbol->get_constType(&const_type) == S_OK && const_type == TRUE) {
				is_const = true;
			}

			// `T* const`: the qualification sits on the pointer symbol itself.
			if (name_symbol != handle.get() &&
				handle->get_constType(&const_type) == S_OK && const_type == TRUE) {
				is_top_const = true;
			}

			if (tag == SymTagEnum) {
				type = type::Enum;
			}
			else if (tag == SymTagUDT) {
				DWORD udt_kind = 0;
				if (name_symbol->get_udtKind(&udt_kind) == S_OK) {
					switch (udt_kind) {
					case UdtStruct:
						type = type::Struct;
						break;
					case UdtClass:
					case UdtInterface:
						type = type::Class;
						break;
					case UdtUnion:
					case UdtTaggedUnion:
						type = type::Union;
						break;
					default:
						break;
					}
				}
			}

			if (target_symbol != nullptr) {
				target_symbol->Release();
			}


			// @TODO DEBUG
			if (name.empty()) {
				DWORD tag{};
				handle->get_symTag(&tag);

				std::println(
					"UNRESOLVED: tag={}, name='{}'",
					tag,
					symbol_name(handle.get())
				);
			}
		}

		std::string string() const {
			std::string result;
			if (is_const) {
				result += "const ";
			}
			if (!namespace_name.empty()) {
				result += namespace_name + "::";
			}
			if (!name.empty()) {
				result += name;
			}
			else {
				result += "<unknown>";
			}
			if (is_ref) {
				if (is_left_ref)
					result += " &";
				else if (is_right_ref)
					result += " &&";
			}
			if (is_pointer)
				result += "*";
			if (is_top_const)
				result += " const";
			return result;
		}

		// Same rendering as `string()` but with all cv-qualifiers stripped:
		// both the pointee/type `const` and the top-level one go, while
		// `&` / `&&` / `*` stay. Useful for cv-insensitive comparisons.
		std::string remove_cv_string() const {
			std::string result;
			if (!namespace_name.empty()) {
				result += namespace_name + "::";
			}
			if (!name.empty()) {
				result += name;
			}
			else {
				result += "<unknown>";
			}
			if (is_ref) {
				if (is_left_ref)
					result += " &";
				else if (is_right_ref)
					result += " &&";
			}
			if (is_pointer)
				result += "*";
			return result;
		}

		// Same as `remove_cv_string()` but references go too (`&` / `&&`),
		// mirroring `std::remove_cvref_t`: cv + reference stripped, pointers
		// stay (a pointer is part of the type, not a qualifier). Renders the
		// bare type identity, ignoring how it is passed around.
		std::string remove_cvref_string() const {
			std::string result;
			if (!namespace_name.empty()) {
				result += namespace_name + "::";
			}
			if (!name.empty()) {
				result += name;
			}
			else {
				result += "<unknown>";
			}
			if (is_pointer)
				result += "*";
			return result;
		}

		bool operator == (const TypeInfo& other) const {
			// 如果类型替换后相同，则类型完全相同（cv 与引用都不参与比较）
			if (utils::replace(remove_cvref_string(), filter::filters) ==
				utils::replace(other.remove_cvref_string(), filter::filters)) {
				return true;
			}
			// 否则需要比较基类是否相同（整条祖先链，不只是直接基类）
			const std::vector<TypeInfo> bases = get_base_types(*this);
			const std::vector<TypeInfo> other_bases = get_base_types(other);

			// 一个类型是另一个类型的基类，则认为它们是相同的
			bool this_is_base = std::ranges::any_of(other_bases, [this](const TypeInfo& cur) {
				return utils::replace(remove_cvref_string(), filter::filters) ==
					utils::replace(cur.remove_cvref_string(), filter::filters);
			});
			bool other_is_base = std::ranges::any_of(bases, [&other](const TypeInfo& cur) {
				return utils::replace(other.remove_cvref_string(), filter::filters) ==
					utils::replace(cur.remove_cvref_string(), filter::filters);
			});

			return this_is_base || other_is_base;
		}
	};

	struct InterfaceInfo {
		// Owning, same semantics as TypeInfo::handle: adopted from the caller,
		// released when the last copy dies. Valid for the InterfaceInfo's
		// lifetime, not just its constructor.
		std::shared_ptr<IDiaSymbol> handle;

		bool is_const{};
		bool is_static{};
		std::string name;
		std::shared_ptr<TypeInfo> class_info;
		std::string class_name;
		std::string namespace_name;

		std::shared_ptr<TypeInfo> return_info;
		std::vector<std::shared_ptr<TypeInfo>> args_info;

		explicit InterfaceInfo(IDiaSymbol* hd) : handle(hd, release_symbol) {
			if (handle == nullptr) {
				return;
			}

			auto [resolved_name, resolved_namespace] = split_symbol_name(handle.get());
			name = std::move(resolved_name);
			namespace_name = std::move(resolved_namespace);

			IDiaSymbol* class_parent = nullptr;
			if (handle->get_classParent(&class_parent) == S_OK && class_parent != nullptr) {
				class_info = std::make_shared<TypeInfo>(class_parent);
				class_name = class_info->name;
				if (!class_info->namespace_name.empty()) {
					namespace_name = class_info->namespace_name;
				}
			}

			is_const = is_const_member_function(handle.get());
			is_static = class_info != nullptr &&
				probe_this_parameter(handle.get()) == ThisParameter::Missing;

			IDiaSymbol* function_type = nullptr;
			if (handle->get_type(&function_type) == S_OK && function_type != nullptr) {
				IDiaSymbol* return_type = nullptr;
				if (function_type->get_type(&return_type) == S_OK && return_type != nullptr) {
					return_info = std::make_shared<TypeInfo>(return_type);
				}

				IDiaEnumSymbols* args = nullptr;
				if (function_type->findChildren(SymTagFunctionArgType, nullptr, nsNone, &args) == S_OK &&
					args != nullptr) {
					IDiaSymbol* arg = nullptr;
					ULONG fetched = 0;
					while (args->Next(1, &arg, &fetched) == S_OK && fetched == 1) {
						IDiaSymbol* arg_type = nullptr;
						if (arg->get_type(&arg_type) == S_OK && arg_type != nullptr) {
							args_info.push_back(std::make_shared<TypeInfo>(arg_type));
						}
						arg->Release();						
						arg = nullptr;
					}
					args->Release();
				}

			function_type->Release();
		}
	}

		std::string string() const {
			std::string result;
			if (!namespace_name.empty()) {
				result += namespace_name + "::";
			}
			if (!class_name.empty()) {
				result += class_name + "::";
			}
			result += name.empty() ? "<unknown>" : name;

			result += "(";
			for (std::size_t i = 0; i < args_info.size(); ++i) {
				if (i != 0) {
					result += ", ";
				}
				result += args_info[i]->string();
			}
			result += ")";

			if (is_const) {
				result += " const";
			}
			return result;
		}

		std::string remove_cv_string() const {
			std::string result;
			if (!namespace_name.empty()) {
				result += namespace_name + "::";
			}
			if (!class_name.empty()) {
				result += class_name + "::";
			}
			result += name.empty() ? "<unknown>" : name;

			result += "(";
			for (std::size_t i = 0; i < args_info.size(); ++i) {
				if (i != 0) {
					result += ", ";
				}
			result += args_info[i]->remove_cv_string();
		}
		result += ")";
		return result;
	}

	// Same as `remove_cv_string()` but the arguments render through
	// `TypeInfo::remove_cvref_string()`: cv + references stripped from
	// every parameter, pointers stay. The function name part never had
	// qualifiers, so only the argument list changes.
	std::string remove_cvref_string() const {
		std::string result;
		if (!namespace_name.empty()) {
			result += namespace_name + "::";
		}
		if (!class_name.empty()) {
			result += class_name + "::";
		}
		result += name.empty() ? "<unknown>" : name;

		result += "(";
		for (std::size_t i = 0; i < args_info.size(); ++i) {
			if (i != 0) {
				result += ", ";
			}
			result += args_info[i]->remove_cvref_string();
		}
		result += ")";
		return result;
	}

		bool operator == (const InterfaceInfo& other) const {
			// 先比较接口名称，如果不同则不进行后续比较
			if (name != other.name)
				return false;

			// 类：一边有类一边没有 → 不同；都有 → filter 相同或互为基类才继续
			//（TypeInfo::operator== 已包含 filter 归一化 + 基类链两层语义）
			if (!class_info || !other.class_info)
				return class_info == other.class_info;
			if (!(*class_info == *other.class_info))
				return false;

			// 参数：ranges::equal 自带长度检查与短路，解引用走深比较
			if (!std::ranges::equal(args_info, other.args_info,
				[](const auto& a, const auto& b) { return *a == *b; }))
				return false;

			// 返回值：深比较
			if (!return_info || !other.return_info)
				return return_info == other.return_info;
			return *return_info == *other.return_info;
		}
};

	std::vector<InterfaceInfo> parse_interface(std::string_view path) {

	if (!std::filesystem::exists(path))
		throw utils::format_runtime_error("File not found: {}", path);

	std::vector<dll_info::InterfaceInfo> result;

	const std::wstring wide_path = utf8_to_wide(path);
	if (wide_path.empty()) {
		return result;
	}

	const std::vector<DllExportInfo> exports = read_dll_exports(wide_path);
	if (exports.empty()) {
		return result;
	}

	ensure_com_initialized();
	IDiaDataSource* source = nullptr;
	if (SUCCEEDED(create_dia_source(&source)) && source != nullptr) {
		if (SUCCEEDED(source->loadDataForExe(wide_path.c_str(), nullptr, nullptr))) {
			IDiaSession* session = nullptr;
			if (SUCCEEDED(source->openSession(&session)) && session != nullptr) {
				std::vector<std::uint32_t> loaded_rvas;
				loaded_rvas.reserve(exports.size());

				for (const DllExportInfo& exported : exports) {
					DWORD function_rva = 0;
					IDiaSymbol* function = nullptr;
					if (!resolve_dll_export_function(
						session,
						exported.rva,
						function_rva,
						&function)) {
						continue;
					}

					if (std::find(loaded_rvas.begin(), loaded_rvas.end(), function_rva) ==
						loaded_rvas.end()) {
						result.emplace_back(function);
						loaded_rvas.push_back(function_rva);
					}
					else {
						// Not handed to an InterfaceInfo - still ours to release.
						function->Release();
					}
				}

				session->Release();
			}
		}
		source->Release();
	}

	return result;
}

	std::vector<EnumInfo> parse_enum(std::string_view path) {
	if (!std::filesystem::exists(path))
		throw utils::format_runtime_error("File not found: {}", path);

	std::vector<dll_info::EnumInfo> result;
	const std::wstring wide_path = utf8_to_wide(path);
	if (wide_path.empty()) {
		return result;
	}

	ensure_com_initialized();
	IDiaDataSource* source = nullptr;
	if (SUCCEEDED(create_dia_source(&source)) && source != nullptr &&
		SUCCEEDED(source->loadDataFromPdb(wide_path.c_str()))) {
		IDiaSession* session = nullptr;
		if (SUCCEEDED(source->openSession(&session)) && session != nullptr) {
			IDiaSymbol* global_scope = nullptr;
			if (SUCCEEDED(session->get_globalScope(&global_scope)) && global_scope != nullptr) {
				IDiaEnumSymbols* enums = nullptr;
				if (SUCCEEDED(global_scope->findChildren(
					SymTagEnum,
					nullptr,
					nsNone,
					&enums)) &&
					enums != nullptr) {
					// The same enumeration is reported once per compiland
					// that uses it, so identical ones have to be folded.
					std::set<std::string> seen;
					IDiaSymbol* symbol = nullptr;
					ULONG fetched = 0;
					while (SUCCEEDED(enums->Next(1, &symbol, &fetched)) && fetched == 1) {
						dll_info::EnumInfo info = build_enum_info(symbol);

						std::string key = info.namespace_name + "::" + info.name +
							"|" + std::to_string(info.size);
						for (const EnumValue& value : info.values) {
							key += "|" + value.name + "=" + std::to_string(value.value);
						}

						if (seen.insert(std::move(key)).second) {
							result.push_back(std::move(info));
						}

						symbol->Release();
						symbol = nullptr;
					}
					if (symbol != nullptr) {
						symbol->Release();
					}
					enums->Release();
				}
				global_scope->Release();
			}
			session->Release();
		}
		source->Release();
	}

	return result;
}

	std::vector<TypeInfo> parse_class(std::string_view path) {
	if (!std::filesystem::exists(path))
		throw utils::format_runtime_error("File not found: {}", path);

	std::vector<dll_info::TypeInfo> result;
	const std::wstring wide_path = utf8_to_wide(path);
	if (wide_path.empty()) {
		return result;
	}

	ensure_com_initialized();
	IDiaDataSource* source = nullptr;
	if (SUCCEEDED(create_dia_source(&source)) && source != nullptr &&
		SUCCEEDED(source->loadDataFromPdb(wide_path.c_str()))) {
		IDiaSession* session = nullptr;
		if (SUCCEEDED(source->openSession(&session)) && session != nullptr) {
			IDiaSymbol* global_scope = nullptr;
			if (SUCCEEDED(session->get_globalScope(&global_scope)) && global_scope != nullptr) {
				IDiaEnumSymbols* classes = nullptr;
				if (SUCCEEDED(global_scope->findChildren(
					SymTagUDT,
					nullptr,
					nsNone,
					&classes)) &&
					classes != nullptr) {
					std::set<std::string> seen;
					IDiaSymbol* symbol = nullptr;
					ULONG fetched = 0;
					while (SUCCEEDED(classes->Next(1, &symbol, &fetched)) && fetched == 1) {
						// TypeInfo adopts the reference: on a dedup hit the
						// moved-out local releases it, on a miss the local
						// destructs here - exactly one Release either way.
						dll_info::TypeInfo info(symbol);

						std::string key = info.namespace_name.empty()
							? info.name
							: info.namespace_name + "::" + info.name;
						if (seen.insert(std::move(key)).second) {
							result.push_back(std::move(info));
						}

						symbol = nullptr;
					}
					classes->Release();
				}
				global_scope->Release();
			}
			session->Release();
		}
		source->Release();
	}

	return result;
}

	// 
	std::vector<InterfaceInfo> filter(const std::vector<InterfaceInfo>& interfaces, std::string filter = "") {
		std::vector<InterfaceInfo> ret{};
		for (const auto& cur : interfaces) {
			if (cur.string().find(filter) != std::string::npos) {
				ret.push_back(cur);
			}
		}
		return ret;
	}
	std::vector<TypeInfo> filter(const std::vector<TypeInfo>& types, std::string filter = "") {
		std::vector<TypeInfo> ret{};
		for (const auto& type : types) {
			if (type.string().find(filter) != std::string::npos) {
				ret.push_back(type);
			}
		}
		return ret;
	}
	std::vector<EnumInfo> filter(const std::vector<EnumInfo>& enums, std::string filter = "") {
		std::vector<EnumInfo> ret{};
		for (const auto& enum_info : enums) {
			// 与另外两个 filter 的语义对齐：对完整限定名做子串匹配，
			// 否则 "Poco" 只会去裸类型名里找，namespace_name 全部漏掉
			const std::string full = enum_info.namespace_name.empty()
				? enum_info.name
				: enum_info.namespace_name + "::" + enum_info.name;
			if (full.find(filter) != std::string::npos) {
				ret.push_back(enum_info);
			}
		}
		return ret;
	}

	std::vector<InterfaceInfo> parse_interface_filter(std::string_view path, std::string filter = "") {
		auto interfaces = parse_interface(path);
		return dll_info::filter(interfaces, filter);
	}
	std::vector<TypeInfo> parse_class_filter(std::string_view path, std::string filter = "") {
		auto types = parse_class(path);
		return dll_info::filter(types, filter);
	}
	std::vector<EnumInfo> parse_enum_filter(std::string_view path, std::string filter = "") {
		auto enums = parse_enum(path);
		return dll_info::filter(enums, filter);
	}
} // namespace dll_info

// ---- Internal helper definitions -------------------------------------------
// Declared before the namespace (TypeInfo::operator== and parse_enum call
// them from inline code); defined here because the bodies need the complete
// types that only exist inside the namespace.

std::vector<dll_info::TypeInfo> get_base_type(const dll_info::TypeInfo& type) {
	std::vector<dll_info::TypeInfo> result;

	if (type.handle == nullptr) {
		return result;
	}

	DWORD tag = 0;
	if (type.handle->get_symTag(&tag) != S_OK || tag != SymTagUDT) {
		return result;
	}

	IDiaEnumSymbols* bases = nullptr;
	if (FAILED(type.handle->findChildren(SymTagBaseClass, nullptr, nsNone, &bases)) ||
		bases == nullptr) {
		return result;
	}

	IDiaSymbol* base = nullptr;
	ULONG fetched = 0;
	while (SUCCEEDED(bases->Next(1, &base, &fetched)) && fetched == 1) {
		IDiaSymbol* base_type_symbol = nullptr;
		if (base->get_type(&base_type_symbol) == S_OK && base_type_symbol != nullptr) {
			result.emplace_back(base_type_symbol);
		}

		base->Release();
		base = nullptr;
	}
	if (base != nullptr) {
		base->Release();
	}

	bases->Release();
	return result;
}

void collect_base_types(const dll_info::TypeInfo& type, std::vector<dll_info::TypeInfo>& chain, std::set<std::string>& seen) {
	for (const dll_info::TypeInfo& base : get_base_type(type)) {
		if (seen.insert(base.remove_cv_string()).second) {
			chain.push_back(base);
			collect_base_types(base, chain, seen);
		}
	}
}

std::vector<dll_info::TypeInfo> get_base_types(const dll_info::TypeInfo& type) {
	std::vector<dll_info::TypeInfo> chain;
	std::set<std::string> seen;
	collect_base_types(type, chain, seen);
	return chain;
}

dll_info::EnumInfo build_enum_info(IDiaSymbol* symbol) {
	dll_info::EnumInfo info;

	auto [resolved_name, resolved_namespace] = split_symbol_name(symbol);
	info.name = std::move(resolved_name);
	info.namespace_name = std::move(resolved_namespace);

	ULONGLONG length{};
	if (symbol->get_length(&length) == S_OK) {
		info.size = length;
	}

	IDiaEnumSymbols* members = nullptr;
	if (SUCCEEDED(symbol->findChildren(SymTagData, nullptr, nsNone, &members)) &&
		members != nullptr) {
		IDiaSymbol* member = nullptr;
		ULONG fetched = 0;
		while (SUCCEEDED(members->Next(1, &member, &fetched)) && fetched == 1) {
			std::string member_name;
			std::int64_t member_value = 0;

			BSTR bstr = nullptr;
			if (member->get_name(&bstr) == S_OK && bstr != nullptr) {
				member_name = bstr_to_utf8(bstr);
				SysFreeString(bstr);
			}

			VARIANT value{};
			if (member->get_value(&value) == S_OK) {
				member_value = variant_to_int64(value);
			}

			// 成员的完整拼写 = 枚举完整限定名 + 成员名，两种比较形态由 EnumValue 内部承载
			info.values.emplace_back(
				info.namespace_name.empty()
					? info.name + "::" + member_name
					: info.namespace_name + "::" + info.name + "::" + member_name,
				member_value);

			member->Release();
			member = nullptr;
		}
		if (member != nullptr) {
			member->Release();
		}
		members->Release();
	}

	return info;
}
