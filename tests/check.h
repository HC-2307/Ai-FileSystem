#pragma once
#include <cstdlib>
#include <iostream>

// Unlike assert(), these still fire when NDEBUG is defined.

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::cerr << __FILE__ << ":" << __LINE__                       \
                      << ": CHECK failed: " << #cond << std::endl;         \
            std::exit(1);                                                  \
        }                                                                  \
    } while (0)

#define CHECK_THROWS(expr, ExceptionType)                                  \
    do {                                                                   \
        bool caught = false;                                               \
        try {                                                              \
            expr;                                                          \
        } catch (const ExceptionType&) {                                   \
            caught = true;                                                 \
        } catch (...) {                                                    \
        }                                                                  \
        if (!caught) {                                                     \
            std::cerr << __FILE__ << ":" << __LINE__                       \
                      << ": CHECK_THROWS failed: " << #expr                \
                      << " did not throw " << #ExceptionType << std::endl; \
            std::exit(1);                                                  \
        }                                                                  \
    } while (0)
