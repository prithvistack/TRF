#pragma once
#include "Date.h"
#include "Person.h"
#include "Instrument.h"
#include "LendingRecord.h"
#include "Repository.h"
#include <string>
#include <vector>

namespace TRF {

// Plain aggregate (no invariants, no behaviour) used only to carry the
// dashboard numbers back to the web layer in one call.
struct DashboardStats {
    int totalInstruments;
    int unitsAvailable;
    int unitsIssued;
    int activeLendings;
    int overdueCount;
    double totalFinesCollected;
    Date today;
};

// Owns all persistent state and enforces every business rule. main.cpp's
// HTTP route handlers do nothing but parse form fields, call one method
// here, and turn the result (or a caught exception) into an HTTP response -
// all the actual thinking happens in this class.
class InventorySystem {
private:
    Repository<Instrument> instruments;
    Repository<Person> persons;
    Repository<LendingRecord> records;

    static const std::string INSTRUMENTS_FILE;
    static const std::string MEMBERS_FILE;
    static const std::string RECORDS_FILE;

    void loadInstruments();
    void loadMembers();
    void loadRecords();
    void seedIfEmpty();

    // Free-text fields (names, remarks, etc.) are stored pipe-separated on
    // disk, so a stray '|' typed by a user would silently corrupt the file
    // format on the very next load. Every such field is checked with this
    // before being accepted.
    static void requireNoPipe(const std::string& value, const std::string& fieldLabel);

    Instrument* requireInstrument(const std::string& id) const; // throws InvalidIdException
    Person* requirePerson(const std::string& id) const;         // throws InvalidIdException
    LendingRecord* requireRecord(const std::string& id) const;  // throws InvalidIdException

    // Looks for an existing Member/CoreMember whose PRN (rollNo) matches
    // via Person::getRollNo() - a virtual call, no downcasting - and
    // reuses it if found; otherwise registers a brand new Member with
    // placeholder phone/year, so a borrower's active-record count and
    // borrow limit are tracked correctly across repeat visits instead of a
    // fresh, historyless Person being created every single time. email is
    // optional (needed only for the overdue email-reminder feature) - an
    // empty value is stored as "-", same placeholder used for phone.
    std::string findOrCreateBorrower(const std::string& name, const std::string& surname,
                                      const std::string& branch, const std::string& division,
                                      const std::string& prn, const std::string& email);

public:
    InventorySystem();

    // Destructor of the "system" class. Repository<Instrument>,
    // Repository<Person> and Repository<LendingRecord> are plain (non-
    // pointer) members, so their own destructors already run automatically
    // when an InventorySystem is destroyed, and each of those deletes every
    // T* it owns. Nothing here needs to be freed by hand - but the
    // destructor is still written out explicitly (rather than left
    // implicit) since freeing this system's memory is exactly what it
    // conceptually does, chain-reacting into all three repositories.
    ~InventorySystem();

    void saveAll() const;

    // ---- Instruments ----
    std::string addInstrument(const std::string& category, const std::string& name,
                               int totalQuantity, Condition condition, const std::string& extraField);
    std::vector<Instrument*> listInstruments() const; // sorted by id

    // Compile-time polymorphism: two overloads of the same name, resolved
    // by the compiler based on how many arguments the caller supplies.
    std::vector<Instrument*> searchInstruments(const std::string& keyword) const;
    std::vector<Instrument*> searchInstruments(const std::string& keyword, const std::string& category) const;

    void updateInstrumentCondition(const std::string& instrumentId, Condition newCondition);
    void deleteInstrument(const std::string& instrumentId); // throws InvalidOperationException if any unit is issued

    // ---- Members ----
    // isWalkIn defaults to false for the normal "Add Member" form; only
    // findOrCreateBorrower() ever passes true.
    std::string addMember(const std::string& name, const std::string& phone, const std::string& email,
                           const std::string& rollNo, const std::string& branch, const std::string& division,
                           int year, bool isCore, const std::string& position, bool isWalkIn = false);
    // Registered club members only - excludes anyone auto-registered from
    // the walk-up Lend flow (see Person::isWalkInBorrower()), so the
    // Members tab only ever shows people actually added through it.
    std::vector<Person*> listMembers() const; // sorted by id
    void deleteMember(const std::string& memberId); // throws InvalidOperationException if they hold anything

    // ---- Lending ----
    // Issue to an already-registered person by id (used by nothing in the
    // current UI, kept because it's the "real" business operation everything
    // else funnels into - see the overload below).
    std::string issueItem(const std::string& instrumentId, const std::string& memberId,
                           const std::string& lentById, int quantity, int days, const Date& issueDate);

    // Compile-time polymorphism: overload of issueItem() for the "borrower
    // could be anyone, walk-up lending" flow - takes the borrower's details
    // directly instead of a pre-existing member id, resolves/creates the
    // Person via findOrCreateBorrower(), then calls the overload above.
    std::string issueItem(const std::string& instrumentId, const std::string& borrowerName,
                           const std::string& borrowerSurname, const std::string& borrowerBranch,
                           const std::string& borrowerDivision, const std::string& borrowerPrn,
                           const std::string& borrowerEmail,
                           const std::string& lentById, int quantity, int days, const Date& issueDate);

    void returnItem(const std::string& recordId, const Date& returnDate, bool updateCondition, Condition newCondition);

    // status: "" (any), "Issued", "Overdue", "Returned".
    // memberQuery/instrumentQuery: case-insensitive substring match against
    // the borrower's name / the instrument's name ("" means no filter) -
    // matched against the same names the Records table actually displays,
    // rather than ids that are never shown anywhere in the UI.
    // Sorted by issue date, most recent first.
    std::vector<LendingRecord*> getRecords(const std::string& status, const std::string& memberQuery,
                                            const std::string& instrumentQuery) const;

    // The fine to *display* for a record: for an already-returned record,
    // that's simply the amount finalized in markReturned() at return time.
    // For a still-active (Issued/Overdue) record, there is no stored fine
    // yet - this computes what it would be if returned right now, via the
    // exact same polymorphic Instrument::calculateFine() used by
    // returnItem(), so a PowerDevice's doubled-past-7-days rule applies
    // here identically. Read-only: never mutates the record.
    double currentFine(const LendingRecord* record) const;

    // Nullable, non-throwing lookup - unlike requirePerson(), a missing
    // person here isn't an error, just "nothing extra to show". Used to
    // enrich a lending record's JSON with the borrower's full profile
    // (branch, division, PRN, email, ...) for display in the Records tab.
    Person* findPerson(const std::string& id) const;

    DashboardStats getStats() const;

    Date today() const; // convenience: "now" as understood by the system
};

} // namespace TRF
