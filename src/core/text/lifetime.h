#pragma once
// HDTRANS_LIFETIMEBOUND on a parameter says that the result may point into it; after the
// parameter list of a member function, into the object. Clang then reports a result
// kept past the end of a temporary argument. Other compilers ignore it.
#if defined(__clang__)
#define HDTRANS_LIFETIMEBOUND [[clang::lifetimebound]]
#else
#define HDTRANS_LIFETIMEBOUND
#endif
