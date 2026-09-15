#pragma once
// SelfTest.h — headless verification of the whole data layer against the
// real ~/.zcode/cli data. Run the exe with --selftest.

namespace SelfTest {
// returns process exit code (0 = all critical checks passed)
int run();
}
