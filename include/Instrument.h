#pragma once
#include <string>
#include <iostream>

namespace TRF {

// enum class (scoped enum): Condition::Damaged, not just Damaged - avoids
// name clashes and accidental implicit conversion to int.
enum class Condition { New, Good, Fair, Damaged };

std::string conditionToString(Condition c);
Condition conditionFromString(const std::string& s); // throws InvalidInputException if unknown

// Abstract base of the Instrument hierarchy.
// Hierarchical inheritance: Instrument -> ElectronicComponent, Tool, PowerDevice.
class Instrument {
protected:
    std::string id;
    std::string name;
    int totalQuantity;
    int availableQuantity;
    Condition condition;

public:
    Instrument(std::string id, std::string name, int totalQuantity, Condition condition);

    // Virtual destructor: Repository<Instrument> deletes objects through
    // an Instrument* base pointer, so the destructor must be virtual for
    // the correct derived destructor to run (same reasoning as Person).
    virtual ~Instrument();

    std::string getId() const;
    std::string getName() const;
    void setName(const std::string& n); // throws InvalidInputException if empty

    int getTotalQuantity() const;
    int getAvailableQuantity() const;
    // Total quantity can only be grown/shrunk down to what's still available.
    void setTotalQuantity(int qty); // throws InvalidInputException on bad values

    Condition getCondition() const;
    void setCondition(Condition c);

    // Reduces availableQuantity by qty when an item is lent out.
    // Validates 1 <= qty <= availableQuantity.
    void issue(int qty); // throws InvalidInputException / ItemUnavailableException
    // Restores availableQuantity by qty when an item is returned.
    void receive(int qty); // throws InvalidInputException

    // --- Pure virtual: category and lending rules differ per subclass,
    // there is no sensible shared default at the Instrument level. ---
    virtual std::string getCategory() const = 0;
    virtual int getMaxLendDays() const = 0;
    virtual double getFinePerDay() const = 0;

    // --- Virtual with a default, PowerDevice overrides calculateFine ---
    virtual double calculateFine(int daysLate) const;
    virtual void display(std::ostream& os) const;
    virtual std::string serialize() const;
    virtual std::string toJson() const;

    // Friend function: lets `std::cout << someInstrument` work with the
    // stream on the left. Declared friend (as the spec asks for, mirroring
    // Date's operator<<) even though it only needs the public display();
    // this keeps the option open for it to read private fields directly
    // without new getters, and matches the pattern used for Date.
    friend std::ostream& operator<<(std::ostream& os, const Instrument& inst);

    // Static members: shared "INS-001" style id sequence for every kind
    // of instrument, and a helper for keeping it in sync after file load.
    static std::string generateId();
    static void ensureCounterAbove(int usedNumber);

private:
    static int nextIdNumber;
};

// ElectronicComponent : public Instrument
class ElectronicComponent : public Instrument {
private:
    std::string voltageRating; // e.g. "5V"

public:
    ElectronicComponent(std::string id, std::string name, int totalQuantity,
                         Condition condition, std::string voltageRating);

    std::string getVoltageRating() const;
    void setVoltageRating(const std::string& v); // throws InvalidInputException if empty

    std::string getCategory() const override;
    int getMaxLendDays() const override;
    double getFinePerDay() const override;

    void display(std::ostream& os) const override;
    std::string serialize() const override;
    std::string toJson() const override;
};

// Tool : public Instrument
class Tool : public Instrument {
private:
    bool requiresTraining;

public:
    Tool(std::string id, std::string name, int totalQuantity,
         Condition condition, bool requiresTraining);

    bool getRequiresTraining() const;
    void setRequiresTraining(bool r);

    std::string getCategory() const override;
    int getMaxLendDays() const override;
    double getFinePerDay() const override;

    void display(std::ostream& os) const override;
    std::string serialize() const override;
    std::string toJson() const override;
};

// PowerDevice : public Instrument
// Safety-sensitive equipment (LiPo batteries, power supplies): fine
// doubles for every day beyond 7 days late.
class PowerDevice : public Instrument {
private:
    std::string capacity; // e.g. "2200mAh"

public:
    PowerDevice(std::string id, std::string name, int totalQuantity,
                Condition condition, std::string capacity);

    std::string getCapacity() const;
    void setCapacity(const std::string& c); // throws InvalidInputException if empty

    std::string getCategory() const override;
    int getMaxLendDays() const override;
    double getFinePerDay() const override;

    // Overrides Instrument's default fine calculation: the first 7 late
    // days are charged at the normal rate, every day after that at double.
    double calculateFine(int daysLate) const override;

    void display(std::ostream& os) const override;
    std::string serialize() const override;
    std::string toJson() const override;
};

} // namespace TRF
