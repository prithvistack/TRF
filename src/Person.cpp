#include "Person.h"
#include "Exceptions.h"
#include "JsonUtil.h"
#include <iomanip>
#include <sstream>

namespace TRF {

// ===================== Person =====================

Person::Person(std::string id, std::string name, std::string phone, std::string email)
    : id(std::move(id)), name(std::move(name)), phone(std::move(phone)), email(std::move(email)) {
}

// Even though this body is empty, it must be defined (a virtual destructor
// still runs after every derived destructor finishes). An empty virtual
// destructor is normal - there is simply nothing of Person's own to clean
// up (no raw pointers owned here).
Person::~Person() {
}

std::string Person::getId() const { return id; }
std::string Person::getName() const { return name; }

void Person::setName(const std::string& n) {
    if (n.empty()) {
        throw InvalidInputException("Person name cannot be empty");
    }
    name = n;
}

std::string Person::getPhone() const { return phone; }

void Person::setPhone(const std::string& p) {
    if (p.empty()) {
        throw InvalidInputException("Phone number cannot be empty");
    }
    phone = p;
}

std::string Person::getEmail() const { return email; }

void Person::setEmail(const std::string& e) {
    if (e.empty()) {
        throw InvalidInputException("Email cannot be empty");
    }
    email = e;
}

// Default: no bonus days. CoreMember overrides this to return +3.
int Person::getExtraDays() const {
    return 0;
}

// Uses the `this` pointer implicitly to call getRole() - because getRole()
// is virtual, this always prints the ACTUAL role of the object display()
// is being run on, even though display() itself is written once here in
// the base class (run-time polymorphism inside a base-class function).
void Person::display(std::ostream& os) const {
    os << id << " | " << name << " | " << getRole() << " | " << phone << " | " << email;
}

// Base fallback; every concrete subclass (Member, CoreMember) overrides
// this completely because the file line needs a type tag ("MEMBER"/"CORE")
// and subclass-specific fields that Person doesn't know about.
std::string Person::serialize() const {
    return id + "|" + name + "|" + phone + "|" + email;
}

// Default: a plain Person has no roll number. Member overrides this to
// return the real field.
std::string Person::getRollNo() const {
    return "";
}

// Default: a plain Person is never a walk-in borrower. Member overrides
// this to return its own flag.
bool Person::isWalkInBorrower() const {
    return false;
}

std::string Person::toJson() const {
    std::string json = "{";
    json += "\"id\":" + jsonString(id) + ",";
    json += "\"name\":" + jsonString(name) + ",";
    json += "\"role\":" + jsonString(getRole()) + ",";
    json += "\"phone\":" + jsonString(phone) + ",";
    json += "\"email\":" + jsonString(email);
    json += "}";
    return json;
}

// ===================== Member =====================

// Static member definition. Static data members are declared inside the
// class (Person.h) but must be defined exactly once outside it, in a .cpp
// file, or the linker cannot find storage for them.
int Member::nextIdNumber = 1;

Member::Member(std::string id, std::string name, std::string phone, std::string email,
               std::string rollNo, std::string branch, std::string division, int year, bool isWalkIn)
    : Person(std::move(id), std::move(name), std::move(phone), std::move(email)),
      rollNo(std::move(rollNo)), branch(std::move(branch)), division(std::move(division)), year(year),
      isWalkIn(isWalkIn) {
    if (year < 1 || year > 6) {
        throw InvalidInputException("Member year must be between 1 and 6");
    }
}

std::string Member::getRollNo() const { return rollNo; }
void Member::setRollNo(const std::string& r) {
    if (r.empty()) {
        throw InvalidInputException("Roll number cannot be empty");
    }
    rollNo = r;
}

std::string Member::getBranch() const { return branch; }
void Member::setBranch(const std::string& b) {
    if (b.empty()) {
        throw InvalidInputException("Branch cannot be empty");
    }
    branch = b;
}

std::string Member::getDivision() const { return division; }
void Member::setDivision(const std::string& d) {
    if (d.empty()) {
        throw InvalidInputException("Division cannot be empty");
    }
    division = d;
}

int Member::getYear() const { return year; }
void Member::setYear(int y) {
    if (y < 1 || y > 6) {
        throw InvalidInputException("Member year must be between 1 and 6");
    }
    year = y;
}

bool Member::isWalkInBorrower() const { return isWalkIn; }

std::string Member::getRole() const { return "Member"; }
int Member::getBorrowLimit() const { return 3; }
bool Member::canLend() const { return false; }

void Member::display(std::ostream& os) const {
    Person::display(os); // extend the base implementation instead of repeating it
    os << " | Roll: " << rollNo << " | Branch: " << branch << " | Division: " << division
       << " | Year: " << year;
}

std::string Member::serialize() const {
    return "MEMBER|" + id + "|" + name + "|" + phone + "|" + email + "|" +
           rollNo + "|" + branch + "|" + division + "|" + std::to_string(year) + "|" +
           (isWalkIn ? "1" : "0");
}

std::string Member::toJson() const {
    std::string json = "{";
    json += "\"id\":" + jsonString(id) + ",";
    json += "\"name\":" + jsonString(name) + ",";
    json += "\"role\":" + jsonString(getRole()) + ",";
    json += "\"phone\":" + jsonString(phone) + ",";
    json += "\"email\":" + jsonString(email) + ",";
    json += "\"rollNo\":" + jsonString(rollNo) + ",";
    json += "\"branch\":" + jsonString(branch) + ",";
    json += "\"division\":" + jsonString(division) + ",";
    json += "\"year\":" + std::to_string(year) + ",";
    json += "\"borrowLimit\":" + std::to_string(getBorrowLimit()) + ",";
    json += "\"isCore\":false,";
    json += "\"position\":\"\",";
    json += "\"isWalkIn\":" + std::string(isWalkIn ? "true" : "false");
    json += "}";
    return json;
}

std::string Member::generateId() {
    std::ostringstream oss;
    oss << "MEM-" << std::setfill('0') << std::setw(3) << nextIdNumber;
    ++nextIdNumber;
    return oss.str();
}

void Member::ensureCounterAbove(int usedNumber) {
    if (usedNumber >= nextIdNumber) {
        nextIdNumber = usedNumber + 1;
    }
}

// ===================== CoreMember =====================

CoreMember::CoreMember(std::string id, std::string name, std::string phone, std::string email,
                       std::string rollNo, std::string branch, std::string division, int year,
                       std::string position)
    : Member(std::move(id), std::move(name), std::move(phone), std::move(email),
             std::move(rollNo), std::move(branch), std::move(division), year),
      position(std::move(position)) {
    if (this->position.empty()) {
        throw InvalidInputException("Core member position cannot be empty");
    }
}

std::string CoreMember::getPosition() const { return position; }
void CoreMember::setPosition(const std::string& p) {
    if (p.empty()) {
        throw InvalidInputException("Core member position cannot be empty");
    }
    position = p;
}

std::string CoreMember::getRole() const { return "Core Member"; }
int CoreMember::getBorrowLimit() const { return 6; }
bool CoreMember::canLend() const { return true; }
int CoreMember::getExtraDays() const { return 3; }

void CoreMember::display(std::ostream& os) const {
    Member::display(os); // three levels deep: Person::display -> Member::display -> here
    os << " | Position: " << position;
}

std::string CoreMember::serialize() const {
    return "CORE|" + id + "|" + name + "|" + phone + "|" + email + "|" +
           rollNo + "|" + branch + "|" + division + "|" + std::to_string(year) + "|" + position;
}

std::string CoreMember::toJson() const {
    std::string json = "{";
    json += "\"id\":" + jsonString(id) + ",";
    json += "\"name\":" + jsonString(name) + ",";
    json += "\"role\":" + jsonString(getRole()) + ",";
    json += "\"phone\":" + jsonString(phone) + ",";
    json += "\"email\":" + jsonString(email) + ",";
    json += "\"rollNo\":" + jsonString(rollNo) + ",";
    json += "\"branch\":" + jsonString(branch) + ",";
    json += "\"division\":" + jsonString(division) + ",";
    json += "\"year\":" + std::to_string(year) + ",";
    json += "\"borrowLimit\":" + std::to_string(getBorrowLimit()) + ",";
    json += "\"isCore\":true,";
    json += "\"position\":" + jsonString(position) + ",";
    json += "\"isWalkIn\":false"; // a CoreMember is only ever added through the Members tab
    json += "}";
    return json;
}

} // namespace TRF
