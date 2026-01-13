#include "Application.hpp"
#include "Core/Logger.hpp"

#ifdef _MSC_VER
#include <Windows.h>
#include <crtdbg.h>
#endif

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

std::mutex g_diagnosticLogMutex;

std::filesystem::path GetDiagnosticLogPath() {
#ifdef _MSC_VER
	char modulePath[MAX_PATH]{};
	const DWORD length = GetModuleFileNameA(nullptr, modulePath, static_cast<DWORD>(std::size(modulePath)));
	if (length > 0) {
		return std::filesystem::path(modulePath).parent_path() / "startup_assert.log";
	}
#endif
	return std::filesystem::current_path() / "startup_assert.log";
}

void AppendDiagnosticLog(const std::string& message) {
	std::lock_guard<std::mutex> lock(g_diagnosticLogMutex);
	std::cerr << message;
	std::cerr.flush();

	std::ofstream logFile(GetDiagnosticLogPath(), std::ios::app);
	if (logFile.is_open()) {
		logFile << message;
		logFile.flush();
	}

#ifdef _MSC_VER
	OutputDebugStringA(message.c_str());
#endif
}

std::string ToHexString(unsigned long long value) {
	std::ostringstream stream;
	stream << "0x" << std::hex << std::uppercase << value;
	return stream.str();
}

#ifdef _MSC_VER
std::string NarrowOrPlaceholder(const wchar_t* text) {
	if (text == nullptr || *text == L'\0') {
		return "<null>";
	}

	const int required = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
	if (required <= 1) {
		return "<conversion-failed>";
	}

	std::string narrow(static_cast<size_t>(required - 1), '\0');
	WideCharToMultiByte(CP_UTF8, 0, text, -1, narrow.data(), required, nullptr, nullptr);
	return narrow;
}

int __cdecl CrtReportHook(int reportType, char* message, int* returnValue) {
	std::string typeLabel = "CRT";
	switch (reportType) {
	case _CRT_WARN:
		typeLabel = "CRT_WARN";
		break;
	case _CRT_ERROR:
		typeLabel = "CRT_ERROR";
		break;
	case _CRT_ASSERT:
		typeLabel = "CRT_ASSERT";
		break;
	default:
		break;
	}

	AppendDiagnosticLog("[" + typeLabel + "] " + (message != nullptr ? std::string(message) : std::string("<null>")));
	if (returnValue != nullptr) {
		*returnValue = 0;
	}
	return FALSE;
}

void __cdecl InvalidParameterHandler(
	const wchar_t* expression,
	const wchar_t* function,
	const wchar_t* file,
	unsigned int line,
	uintptr_t) {
	AppendDiagnosticLog(
		"[CRT_INVALID_PARAMETER] expression=" + NarrowOrPlaceholder(expression) +
		" function=" + NarrowOrPlaceholder(function) +
		" file=" + NarrowOrPlaceholder(file) +
		" line=" + std::to_string(line) + "\n");
}

LONG WINAPI UnhandledExceptionLogger(EXCEPTION_POINTERS* exceptionInfo) {
	const DWORD code = exceptionInfo != nullptr && exceptionInfo->ExceptionRecord != nullptr
		? exceptionInfo->ExceptionRecord->ExceptionCode
		: 0;
	AppendDiagnosticLog("[UNHANDLED_EXCEPTION] code=" + ToHexString(static_cast<unsigned long long>(code)) + "\n");
	return EXCEPTION_CONTINUE_SEARCH;
}

void InstallStartupDiagnostics() {
	const auto logPath = GetDiagnosticLogPath();
	{
		std::ofstream clearFile(logPath, std::ios::trunc);
		clearFile << "[STARTUP] Installing diagnostic hooks\n";
	}

	_set_error_mode(_OUT_TO_STDERR);
	_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
	_CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
	_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
	_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
	_CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
	_CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
	_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
	_CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, &CrtReportHook);
	_set_invalid_parameter_handler(&InvalidParameterHandler);
	SetUnhandledExceptionFilter(&UnhandledExceptionLogger);
	AppendDiagnosticLog("[STARTUP] Diagnostic hooks installed. Log file: " + logPath.string() + "\n");
}
#else
void InstallStartupDiagnostics() {}
#endif

}  // namespace

int main() {
	InstallStartupDiagnostics();

	try {
		FeatherVK::Application app{};
		app.run();
	}
	catch (const std::exception& e) {
		FeatherVK::Logger::Error(e.what());
		AppendDiagnosticLog("[STD_EXCEPTION] " + std::string(e.what()) + "\n");
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
