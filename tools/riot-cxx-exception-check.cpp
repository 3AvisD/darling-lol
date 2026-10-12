#include <stdexcept>
#include <string>
#include <stdio.h>
#include <stdlib.h>
int main() {
    bool caught = false;
    try { std::stoi(""); }
    catch (std::out_of_range&) { fprintf(stderr, "FAIL: wrong exception type\n"); return 1; }
    catch (std::invalid_argument value) { caught = true; }
    if (!caught) return 1;
    try { std::stoi("99999999999999999999999999"); }
    catch (std::invalid_argument&) { fprintf(stderr, "FAIL: distinct types matched\n"); return 1; }
    catch (std::out_of_range&) { printf("PASS: duplicate exception RTTI catches matching type only\n"); return 0; }
    return 1;
}
