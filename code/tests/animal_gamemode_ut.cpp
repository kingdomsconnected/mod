#include <cstdlib>
#include <stdexcept>
#include <string>

void AnimalGamemodeTests() {
    const std::string command = std::string("\"") + KCDC_TEST_NODE + "\" \"" + KCDC_ANIMAL_POLICY_TEST + "\" \"" + KCDC_ANIMAL_POLICY_MODULE + "\"";
#ifdef _WIN32
    const int result = std::system(("\"" + command + "\"").c_str());
#else
    const int result = std::system(command.c_str());
#endif
    if (result != 0) {
        throw std::runtime_error("Gamemode animal spawner assertions failed; see Node output above.");
    }
}
