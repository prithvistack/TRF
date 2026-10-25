#pragma once
#include <iostream>
#include <string>

namespace TRF {

// Represents a calendar date (day/month/year) and supports the arithmetic
// the lending system needs: adding days to get a due date, and subtracting
// two dates to get the number of days between them.
class Date {
private:
    int day;
    int month;
    int year;

    // Internal helper: turns this date into a single running count of days
    // since 1900-01-01 (which is day 0). Implemented with plain loops over
    // whole years and whole months (no closed-form formula) so every step
    // is easy to trace in a debugger and easy to explain in the viva.
    long toDayCount() const;

    // Inverse of toDayCount(): walks forward year by year, then month by
    // month, from 1900-01-01 until `count` days have been consumed, and
    // sets day/month/year accordingly.
    void fromDayCount(long count);

    // Throws InvalidInputException if day/month/year is not a real date.
    void validate() const;

public:
    // Default constructor: "today", read from <ctime>.
    Date();

    // Parameterized constructor: validates before accepting.
    Date(int d, int m, int y);

    // Constructs from an ISO date string "YYYY-MM-DD" (used when reading
    // dates back from data files, and from HTML form fields).
    explicit Date(const std::string& isoDate);

    // Copy constructor (explicitly required by the spec; the compiler-
    // generated one would do the same thing here since Date has no
    // pointer members, but writing it out makes the concept examinable).
    Date(const Date& other);

    // Rule of Three: a class that declares its own copy constructor should
    // also declare its own copy assignment operator (and destructor, which
    // here is fine left implicit since there is nothing to free). Without
    // this, `existingDate = otherDate;` would still compile using the
    // compiler-generated version, but newer GCC warns that mixing a
    // hand-written copy constructor with an implicit assignment operator
    // is error-prone, since the two could silently drift apart if a member
    // were added later and only one of them were updated.
    Date& operator=(const Date& other);

    int getDay() const;
    int getMonth() const;
    int getYear() const;

    std::string toString() const; // "YYYY-MM-DD", zero-padded

    // --- Operator overloading (compile-time polymorphism) ---

    Date operator+(int daysToAdd) const;        // date + n days -> new Date
    long operator-(const Date& other) const;    // days between two dates
    bool operator<(const Date& other) const;
    bool operator>(const Date& other) const;
    bool operator==(const Date& other) const;
    bool operator<=(const Date& other) const;

    // operator<< must be a free function (left operand is ostream, not
    // Date), but it needs access to day/month/year, which are private.
    // Declaring it `friend` grants that access without making the members
    // public. See Date.cpp for the definition.
    friend std::ostream& operator<<(std::ostream& os, const Date& d);

    // --- Static members: belong to the class, not any one object ---
    static bool isLeapYear(int y);
    static int daysInMonth(int m, int y);
};

} // namespace TRF
