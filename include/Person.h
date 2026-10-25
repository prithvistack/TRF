#pragma once
#include <string>
#include <iostream>

namespace TRF {

// Abstract base of the Person hierarchy.
// Multilevel inheritance: Person -> Member -> CoreMember.
// Person itself is never instantiated (it has pure virtual functions),
// but a Person* is what Repository<Person> and InventorySystem work with,
// so that the same code handles both Member and CoreMember objects.
class Person {
protected:
    std::string id;
    std::string name;
    std::string phone;
    std::string email;

public:
    Person(std::string id, std::string name, std::string phone, std::string email);

    // Virtual destructor: Repository<Person> deletes objects through a
    // Person* base pointer. If this destructor were not virtual, deleting
    // a CoreMember* through a Person* would only run ~Person(), skipping
    // ~Member() and ~CoreMember() - undefined behaviour / leaked resources.
    // A virtual destructor makes delete look up the real (dynamic) type
    // through the vtable, just like any other virtual call.
    virtual ~Person();

    // --- Encapsulation: private-ish (protected) data, public accessors ---
    std::string getId() const;
    std::string getName() const;
    void setName(const std::string& n);   // throws InvalidInputException if empty
    std::string getPhone() const;
    void setPhone(const std::string& p);  // throws InvalidInputException if empty
    std::string getEmail() const;
    void setEmail(const std::string& e);  // throws InvalidInputException if empty

    // --- Pure virtual: no meaningful default exists at the Person level,
    // every concrete subclass MUST supply its own. This is what makes
    // Person an abstract class (cannot be instantiated on its own). ---
    virtual std::string getRole() const = 0;       // "Member" / "Core Member"
    virtual int getBorrowLimit() const = 0;        // max simultaneous active records
    virtual bool canLend() const = 0;               // may this person be the "lent by" party?

    // --- Virtual with a real default, subclasses may override if their
    // behaviour differs (run-time polymorphism resolved via the vtable) ---
    virtual int getExtraDays() const;                  // bonus days on top of an instrument's max; default 0
    virtual void display(std::ostream& os) const;       // human-readable summary
    virtual std::string serialize() const;               // one line for the data file
    virtual std::string toJson() const;                   // one JSON object

    // Default "" (a plain Person has no roll number). Member overrides this
    // to return the real value. Declared here - instead of downcasting with
    // dynamic_cast, which this project deliberately avoids - so
    // InventorySystem can ask "does an existing Person already have this
    // PRN?" via ordinary virtual dispatch on a Person*, without caring
    // whether it actually points at a Member or a CoreMember.
    virtual std::string getRollNo() const;

    // Default false. Member overrides this to report whether it was
    // auto-registered from the walk-up Lend flow (see
    // InventorySystem::findOrCreateBorrower()) rather than added through
    // the Members tab - such a Person still exists in the repository (so
    // its borrow limit is still tracked correctly), it's just excluded
    // from InventorySystem::listMembers(). Same reasoning as getRollNo():
    // a virtual query instead of downcasting.
    virtual bool isWalkInBorrower() const;
};

// Member : public Person (level 1 of the multilevel chain).
// A regular robotics club member who can only borrow, never lend.
class Member : public Person {
protected:
    std::string rollNo;   // PRN (Permanent Registration Number)
    std::string branch;
    std::string division; // e.g. "A"
    int year; // academic year, e.g. 2
    bool isWalkIn; // true only if auto-registered via the walk-up Lend flow

    // Static counter shared by every Member AND CoreMember, since both
    // use the same "MEM-xxx" id sequence. protected so CoreMember's own
    // static methods (inherited, not overridden - statics don't have
    // per-object dispatch) can still reach it through Member::.
    static int nextIdNumber;

public:
    Member(std::string id, std::string name, std::string phone, std::string email,
           std::string rollNo, std::string branch, std::string division, int year,
           bool isWalkIn = false);

    std::string getRollNo() const override;
    void setRollNo(const std::string& r);
    std::string getBranch() const;
    void setBranch(const std::string& b);
    std::string getDivision() const;
    void setDivision(const std::string& d);
    int getYear() const;
    void setYear(int y); // throws InvalidInputException if out of a sane range
    bool isWalkInBorrower() const override;

    // Runtime polymorphism: overriding Person's pure virtuals.
    std::string getRole() const override;
    int getBorrowLimit() const override;
    bool canLend() const override;

    void display(std::ostream& os) const override;
    std::string serialize() const override;
    std::string toJson() const override;

    // Static members: generate/track "MEM-001" style ids. Not tied to any
    // one object - called as Member::generateId(), no instance needed.
    static std::string generateId();
    // Called once at startup after loading existing members from file, so
    // new ids continue after the highest one already on disk.
    static void ensureCounterAbove(int usedNumber);
};

// CoreMember : public Member (level 2 - completes the multilevel chain).
// A club core team member: has a higher borrow limit, extra grace days,
// and is the only kind of Person allowed to be the "lent by" party.
class CoreMember : public Member {
private:
    std::string position; // e.g. "Electronics Head"

public:
    CoreMember(std::string id, std::string name, std::string phone, std::string email,
               std::string rollNo, std::string branch, std::string division, int year, std::string position);

    std::string getPosition() const;
    void setPosition(const std::string& p); // throws InvalidInputException if empty

    // Overrides Member's overrides again - each level can replace the
    // behaviour of the level below it.
    std::string getRole() const override;
    int getBorrowLimit() const override;
    bool canLend() const override;
    int getExtraDays() const override;

    void display(std::ostream& os) const override;
    std::string serialize() const override;
    std::string toJson() const override;
};

} // namespace TRF
