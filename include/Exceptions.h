#pragma once
#include <exception>
#include <string>

namespace TRF {

// Base of the project's exception hierarchy.
// Inherits from std::exception so a generic `catch (const std::exception&)`
// at the very outer boundary (e.g. main) still works, while code that wants
// to be specific can catch `const TRFException&` or one of the subclasses
// below. All constructors/what() are one-liners, so they are defined right
// here in the class body instead of a .cpp file: a function defined inside
// a class body is implicitly `inline`, which is required anyway for a
// function that lives in a header included by multiple .cpp files (avoids
// One Definition Rule violations at link time). There would be nothing
// meaningful left in a corresponding Exceptions.cpp.
class TRFException : public std::exception {
protected:
    std::string message;

public:
    explicit TRFException(std::string msg) : message(std::move(msg)) {}

    // override: signature must match std::exception::what() exactly
    // (const, noexcept, returns const char*) or this silently becomes a
    // new function instead of overriding the base virtual - `override`
    // makes the compiler reject the code if that ever happens.
    const char* what() const noexcept override {
        return message.c_str();
    }
};

// Thrown when a lookup id (instrument/person/record) does not exist.
// Mapped to HTTP 404 by the web layer.
class InvalidIdException : public TRFException {
public:
    explicit InvalidIdException(const std::string& msg) : TRFException(msg) {}
};

// Thrown for malformed or out-of-range input: bad date, empty name,
// non-positive quantity, unknown category, etc.
class InvalidInputException : public TRFException {
public:
    explicit InvalidInputException(const std::string& msg) : TRFException(msg) {}
};

// Thrown when trying to issue more units than are available, or an
// instrument whose condition is Damaged.
class ItemUnavailableException : public TRFException {
public:
    explicit ItemUnavailableException(const std::string& msg) : TRFException(msg) {}
};

// Thrown when a borrower already holds their maximum number of active
// (not yet returned) lending records.
class BorrowLimitExceededException : public TRFException {
public:
    explicit BorrowLimitExceededException(const std::string& msg) : TRFException(msg) {}
};

// Thrown for operations that are individually well-formed but not allowed
// in context: lender == borrower, a non-core member trying to lend,
// returning an item before its issue date, deleting something still in use.
class InvalidOperationException : public TRFException {
public:
    explicit InvalidOperationException(const std::string& msg) : TRFException(msg) {}
};

} // namespace TRF
