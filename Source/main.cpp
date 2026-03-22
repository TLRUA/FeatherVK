#include "Application.hpp"
#include "Core/Logger.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

int main() {
	FeatherVK::Application app{};

	try {
		app.run();
	}
	catch (const std::exception& e) {
		FeatherVK::Logger::Error(e.what());
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
