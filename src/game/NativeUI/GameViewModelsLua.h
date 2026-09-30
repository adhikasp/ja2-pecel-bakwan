#pragma once
// ja2.viewModel(name), ja2.viewModelCommand(name, command, ...), ja2.viewModels() for automation and tests.

#include <sol/sol.hpp>

void RegisterViewModelApi(sol::state& lua, sol::table ja2);
