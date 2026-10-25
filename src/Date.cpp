#include "Date.h"
#include "Exceptions.h"
#include <ctime>
#include <iomanip>
#include <sstream>

namespace TRF {

bool Date::isLeapYear(int y) {
    // Standard Gregorian leap year rule.
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

int Date::daysInMonth(int m, int y) {
    static const int lengths[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m == 2 && isLeapYear(y)) {
        return 29;
    }
    return lengths[m - 1];
}

void Date::validate() const {
    if (month < 1 || month > 12) {
        throw InvalidInputException("Invalid date: month must be between 1 and 12");
    }
    if (year < 1900 || year > 2100) {
        throw InvalidInputException("Invalid date: year must be between 1900 and 2100");
    }
    if (day < 1 || day > daysInMonth(month, year)) {
        throw InvalidInputException("Invalid date: day is out of range for the given month/year");
    }
}

long Date::toDayCount() const {
    long count = 0;
    // Add a full year's worth of days for every year from 1900 up to
    // (but not including) this date's year.
    for (int y = 1900; y < year; ++y) {
        count += isLeapYear(y) ? 366 : 365;
    }
    // Add a full month's worth of days for every month before this one.
    for (int m = 1; m < month; ++m) {
        count += daysInMonth(m, year);
    }
    // Days into the current month. 1900-01-01 itself is day count 0,
    // so the 1st of a month contributes 0, the 2nd contributes 1, etc.
    count += (day - 1);
    return count;
}

void Date::fromDayCount(long count) {
    int y = 1900;
    while (true) {
        int daysThisYear = isLeapYear(y) ? 366 : 365;
        if (count < daysThisYear) {
            break;
        }
        count -= daysThisYear;
        ++y;
    }
    int m = 1;
    while (true) {
        int daysThisMonth = daysInMonth(m, y);
        if (count < daysThisMonth) {
            break;
        }
        count -= daysThisMonth;
        ++m;
    }
    year = y;
    month = m;
    day = static_cast<int>(count) + 1;
}

// Default constructor: "today", via <ctime>.
Date::Date() : day(1), month(1), year(1900) {
    std::time_t t = std::time(nullptr);
    // std::localtime returns a pointer to a static internal struct tm.
    // That would be unsafe to use across threads, but this whole project
    // runs the httplib server with a 1-thread pool (see main.cpp), so
    // there is never more than one thread calling this at a time.
    std::tm* local = std::localtime(&t);
    day = local->tm_mday;
    month = local->tm_mon + 1;
    year = local->tm_year + 1900;
    validate();
}

// Parameterized constructor with a member initializer list.
Date::Date(int d, int m, int y) : day(d), month(m), year(y) {
    validate();
}

// Parses "YYYY-MM-DD". Deliberately strict: wrong length or non-numeric
// parts are reported as InvalidInputException rather than silently
// producing a garbage date.
Date::Date(const std::string& isoDate) : day(1), month(1), year(1900) {
    if (isoDate.size() != 10 || isoDate[4] != '-' || isoDate[7] != '-') {
        throw InvalidInputException("Invalid date string, expected YYYY-MM-DD: " + isoDate);
    }
    try {
        year = std::stoi(isoDate.substr(0, 4));
        month = std::stoi(isoDate.substr(5, 2));
        day = std::stoi(isoDate.substr(8, 2));
    } catch (const std::exception&) {
        throw InvalidInputException("Invalid date string, expected YYYY-MM-DD: " + isoDate);
    }
    validate();
}

// Copy constructor: member-wise copy via initializer list.
Date::Date(const Date& other) : day(other.day), month(other.month), year(other.year) {
}

// Copy assignment: same member-wise copy as the copy constructor, but for
// an already-existing object (e.g. `dueDate = someOtherDate;`). The
// self-assignment check isn't strictly needed here (three ints, nothing
// that breaks if copied onto itself), but it's cheap and is the standard
// shape of a hand-written operator=.
Date& Date::operator=(const Date& other) {
    if (this == &other) {
        return *this;
    }
    day = other.day;
    month = other.month;
    year = other.year;
    return *this;
}

int Date::getDay() const { return day; }
int Date::getMonth() const { return month; }
int Date::getYear() const { return year; }

std::string Date::toString() const {
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(4) << year << "-"
        << std::setfill('0') << std::setw(2) << month << "-"
        << std::setfill('0') << std::setw(2) << day;
    return oss.str();
}

Date Date::operator+(int daysToAdd) const {
    Date result(*this);      // uses copy constructor
    long total = result.toDayCount() + daysToAdd;
    result.fromDayCount(total);
    return result;
}

long Date::operator-(const Date& other) const {
    return this->toDayCount() - other.toDayCount();
}

bool Date::operator<(const Date& other) const {
    return this->toDayCount() < other.toDayCount();
}

bool Date::operator>(const Date& other) const {
    return this->toDayCount() > other.toDayCount();
}

bool Date::operator==(const Date& other) const {
    return day == other.day && month == other.month && year == other.year;
}

bool Date::operator<=(const Date& other) const {
    return *this < other || *this == other;
}

// Friend function: not a member of Date, so it can be called as
// `os << date` with the ostream on the left. Defined here, outside the
// class, but the `friend` declaration in Date.h still gives it access to
// the private day/month/year members.
std::ostream& operator<<(std::ostream& os, const Date& d) {
    os << d.toString();
    return os;
}

} // namespace TRF
