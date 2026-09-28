#pragma once
// Include first in every core and sim .cpp. Turns off floating-point contraction for
// the rest of the translation unit: a fused multiply-add rounds once where the
// source rounds twice, which can flip a noise threshold (fbm2(...) > 0.60f) and
// generate a different maze on another compiler. The build scripts also pass
// -ffp-contract=off; this holds where a build system drops that flag.
//
// GCC ignores these pragmas and honours only the flag (its C++ default is
// -ffp-contract=fast). Clang's -ffast-math and -ffp-contract=fast override the
// pragma, so core must not be built with either (docs/migration.md).
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(_MSC_VER)
#pragma float_control(precise, on)   // before fp_contract: precise turns contraction back on
#pragma fp_contract(off)
#elif !defined(__GNUC__)
#pragma STDC FP_CONTRACT OFF
#endif
