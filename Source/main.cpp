#include "Application.hpp"
#include "Core/Logger.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

int main() {
	Kaamoo::Application app{};

	try {
		app.run();
	}
	catch (const std::exception& e) {
		Kaamoo::Logger::Error(e.what());
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}