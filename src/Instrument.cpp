#include "Instrument.h"
#include "Exceptions.h"
#include "JsonUtil.h"
#include <iomanip>
#include <sstream>

namespace TRF {

std::string conditionToString(Condition c) {
    switch (c) {
        case Condition::New:     return "New";
        case Condition::Good:    return "Good";
        case Condition::Fair:    return "Fair";
        case Condition::Damaged: return "Damaged";
    }
    return "Unknown"; // unreachable, silences -Wreturn-type for some compilers
}

Condition conditionFromString(const std::string& s) {
    if (s == "New")     return Condition::New;
    if (s == "Good")    return Condition::Good;
    if (s == "Fair")    return Condition::Fair;
    if (s == "Damaged") return Condition::Damaged;
    throw InvalidInputException("Unknown instrument condition: " + s);
}

// ===================== Instrument =====================

int Instrument::nextIdNumber = 1;

Instrument::Instrument(std::string id, std::string name, int totalQuantity, Condition condition)
    : id(std::move(id)), name(std::move(name)),
      totalQuantity(totalQuantity), availableQuantity(totalQuantity), condition(condition) {
    if (totalQuantity < 1) {
        throw InvalidInputException("Total quantity must be at least 1");
    }
}

Instrument::~Instrument() {
}

std::string Instrument::getId() const { return id; }
std::string Instrument::getName() const { return name; }

void Instrument::setName(const std::string& n) {
    if (n.empty()) {
        throw InvalidInputException("Instrument name cannot be empty");
    }
    name = n;
}

int Instrument::getTotalQuantity() const { return totalQuantity; }
int Instrument::getAvailableQuantity() const { return availableQuantity; }

void Instrument::setTotalQuantity(int qty) {
    int issuedCount = totalQuantity - availableQuantity;
    if (qty < issuedCount) {
        throw InvalidInputException("Total quantity cannot be less than the number of units currently issued");
    }
    availableQuantity += (qty - totalQuantity);
    totalQuantity = qty;
}

Condition Instrument::getCondition() const { return condition; }
void Instrument::setCondition(Condition c) { condition = c; }

void Instrument::issue(int qty) {
    if (qty < 1) {
        throw InvalidInputException("Quantity to issue must be at least 1");
    }
    if (qty > availableQuantity) {
        throw ItemUnavailableException(
            "Only " + std::to_string(availableQuantity) + " unit(s) of '" + name + "' available");
    }
    availableQuantity -= qty;
}

void Instrument::receive(int qty) {
    int issuedCount = totalQuantity - availableQuantity;
    if (qty < 1) {
        throw InvalidInputException("Quantity to receive must be at least 1");
    }
    if (qty > issuedCount) {
        throw InvalidInputException("Cannot receive more units than are currently issued");
    }
    availableQuantity += qty;
}

// Default fine: simply daysLate * the per-day rate. PowerDevice overrides
// this with a rule that doubles the rate beyond 7 late days.
double Instrument::calculateFine(int daysLate) const {
    if (daysLate <= 0) {
        return 0.0;
    }
    return daysLate * getFinePerDay();
}

// Uses `this` implicitly through getCategory()/getMaxLendDays()/getFinePerDay():
// because those are virtual (or in the case of subclasses that override
// display, called on the derived display itself), the values printed are
// always the calling object's real, dynamic-type values.
void Instrument::display(std::ostream& os) const {
    os << id << " | " << name << " | " << getCategory()
       << " | Qty: " << availableQuantity << "/" << totalQuantity
       << " | Condition: " << conditionToString(condition);
}

std::string Instrument::serialize() const {
    return id + "|" + name + "|" + std::to_string(totalQuantity) + "|" +
           std::to_string(availableQuantity) + "|" + conditionToString(condition);
}

std::string Instrument::toJson() const {
    std::string json = "{";
    json += "\"id\":" + jsonString(id) + ",";
    json += "\"name\":" + jsonString(name) + ",";
    json += "\"category\":" + jsonString(getCategory()) + ",";
    json += "\"totalQuantity\":" + std::to_string(totalQuantity) + ",";
    json += "\"availableQuantity\":" + std::to_string(availableQuantity) + ",";
    json += "\"condition\":" + jsonString(conditionToString(condition)) + ",";
    json += "\"maxLendDays\":" + std::to_string(getMaxLendDays()) + ",";
    json += "\"finePerDay\":" + jsonMoney(getFinePerDay());
    json += "}";
    return json;
}

// Friend function, defined outside the class. It does exactly one thing:
// call the virtual display(). Because `inst` is a reference and display()
// is virtual, this line runs the override belonging to whatever concrete
// type `inst` actually is at runtime (ElectronicComponent/Tool/PowerDevice)
// even though this function only ever mentions the base type Instrument.
std::ostream& operator<<(std::ostream& os, const Instrument& inst) {
    inst.display(os);
    return os;
}

std::string Instrument::generateId() {
    std::ostringstream oss;
    oss << "INS-" << std::setfill('0') << std::setw(3) << nextIdNumber;
    ++nextIdNumber;
    return oss.str();
}

void Instrument::ensureCounterAbove(int usedNumber) {
    if (usedNumber >= nextIdNumber) {
        nextIdNumber = usedNumber + 1;
    }
}

// ===================== ElectronicComponent =====================

ElectronicComponent::ElectronicComponent(std::string id, std::string name, int totalQuantity,
                                          Condition condition, std::string voltageRating)
    : Instrument(std::move(id), std::move(name), totalQuantity, condition),
      voltageRating(std::move(voltageRating)) {
    if (this->voltageRating.empty()) {
        throw InvalidInputException("Voltage rating cannot be empty");
    }
}

std::string ElectronicComponent::getVoltageRating() const { return voltageRating; }
void ElectronicComponent::setVoltageRating(const std::string& v) {
    if (v.empty()) {
        throw InvalidInputException("Voltage rating cannot be empty");
    }
    voltageRating = v;
}

std::string ElectronicComponent::getCategory() const { return "Electronic Component"; }
int ElectronicComponent::getMaxLendDays() const { return 14; }
double ElectronicComponent::getFinePerDay() const { return 10.0; }

void ElectronicComponent::display(std::ostream& os) const {
    Instrument::display(os);
    os << " | Voltage: " << voltageRating;
}

std::string ElectronicComponent::serialize() const {
    return "ELECTRONIC|" + Instrument::serialize() + "|" + voltageRating;
}

std::string ElectronicComponent::toJson() const {
    // Build the shared part via the base class, then splice in the extra
    // field just before the closing brace.
    std::string base = Instrument::toJson();
    base.pop_back(); // remove trailing '}'
    base += ",\"voltageRating\":" + jsonString(voltageRating) + "}";
    return base;
}

// ===================== Tool =====================

Tool::Tool(std::string id, std::string name, int totalQuantity,
           Condition condition, bool requiresTraining)
    : Instrument(std::move(id), std::move(name), totalQuantity, condition),
      requiresTraining(requiresTraining) {
}

bool Tool::getRequiresTraining() const { return requiresTraining; }
void Tool::setRequiresTraining(bool r) { requiresTraining = r; }

std::string Tool::getCategory() const { return "Tool"; }
int Tool::getMaxLendDays() const { return 7; }
double Tool::getFinePerDay() const { return 20.0; }

void Tool::display(std::ostream& os) const {
    Instrument::display(os);
    os << " | Training required: " << (requiresTraining ? "Yes" : "No");
}

std::string Tool::serialize() const {
    return "TOOL|" + Instrument::serialize() + "|" + (requiresTraining ? "1" : "0");
}

std::string Tool::toJson() const {
    std::string base = Instrument::toJson();
    base.pop_back();
    base += ",\"requiresTraining\":" + std::string(requiresTraining ? "true" : "false") + "}";
    return base;
}

// ===================== PowerDevice =====================

PowerDevice::PowerDevice(std::string id, std::string name, int totalQuantity,
                          Condition condition, std::string capacity)
    : Instrument(std::move(id), std::move(name), totalQuantity, condition),
      capacity(std::move(capacity)) {
    if (this->capacity.empty()) {
        throw InvalidInputException("Capacity cannot be empty");
    }
}

std::string PowerDevice::getCapacity() const { return capacity; }
void PowerDevice::setCapacity(const std::string& c) {
    if (c.empty()) {
        throw InvalidInputException("Capacity cannot be empty");
    }
    capacity = c;
}

std::string PowerDevice::getCategory() const { return "Power Device"; }
int PowerDevice::getMaxLendDays() const { return 5; }
double PowerDevice::getFinePerDay() const { return 15.0; }

// PowerDevice is safety-sensitive (batteries/power supplies), so days
// beyond 7 late are charged at DOUBLE the normal per-day rate. This is a
// clean example of runtime polymorphism overriding a virtual function
// that has a default: Instrument::calculateFine() is never called here -
// this replaces it entirely for any PowerDevice object.
double PowerDevice::calculateFine(int daysLate) const {
    if (daysLate <= 0) {
        return 0.0;
    }
    const int normalDaysThreshold = 7;
    if (daysLate <= normalDaysThreshold) {
        return daysLate * getFinePerDay();
    }
    int extraDays = daysLate - normalDaysThreshold;
    return (normalDaysThreshold * getFinePerDay()) + (extraDays * getFinePerDay() * 2);
}

void PowerDevice::display(std::ostream& os) const {
    Instrument::display(os);
    os << " | Capacity: " << capacity;
}

std::string PowerDevice::serialize() const {
    return "POWER|" + Instrument::serialize() + "|" + capacity;
}

std::string PowerDevice::toJson() const {
    std::string base = Instrument::toJson();
    base.pop_back();
    base += ",\"capacity\":" + jsonString(capacity) + "}";
    return base;
}

} // namespace TRF
