#pragma once
#include "Date.h"
#include <string>
#include <iostream>

namespace TRF {

enum class RecordStatus { Issued, Overdue, Returned };
std::string recordStatusToString(RecordStatus s);

// A single lending transaction: what was borrowed, by whom, who approved
// it, and its current state. LendingRecord does not take part in any
// inheritance hierarchy - there is only one kind of record - so none of
// its member functions need to be virtual.
class LendingRecord {
private:
    std::string id;
    std::string instrumentId;
    std::string instrumentName;   // kept alongside the id so old records
                                   // still show a sensible name even if the
                                   // instrument is later renamed or removed
    std::string memberId;
    std::string memberName;
    std::string lentById;
    std::string lentByName;
    int quantity;
    Date issueDate;
    int daysLent;
    Date dueDate;
    Date returnDate; // only meaningful once `returned` is true
    bool returned;
    double fine;
    std::string remarks;

    static int nextIdNumber;

public:
    // "New lending" constructor: dueDate is computed from issueDate +
    // daysLent using Date::operator+ in the member initializer list.
    LendingRecord(std::string id, std::string instrumentId, std::string instrumentName,
                  std::string memberId, std::string memberName,
                  std::string lentById, std::string lentByName,
                  int quantity, const Date& issueDate, int daysLent);

    // "Reconstruction" constructor: every field is already known (used
    // when reloading a saved record from a data file). Constructor
    // overloading - same class, two different parameter lists picked at
    // compile time based on how many arguments are passed.
    LendingRecord(std::string id, std::string instrumentId, std::string instrumentName,
                  std::string memberId, std::string memberName,
                  std::string lentById, std::string lentByName,
                  int quantity, const Date& issueDate, int daysLent, const Date& dueDate,
                  const Date& returnDate, bool returned, double fine, std::string remarks);

    std::string getId() const;
    std::string getInstrumentId() const;
    std::string getInstrumentName() const;
    std::string getMemberId() const;
    std::string getMemberName() const;
    std::string getLentById() const;
    std::string getLentByName() const;
    int getQuantity() const;
    Date getIssueDate() const;
    int getDaysLent() const;
    Date getDueDate() const;
    Date getReturnDate() const;
    bool isReturned() const;
    double getFine() const;
    std::string getRemarks() const;
    void setRemarks(const std::string& r); // throws InvalidInputException if it contains '|'

    // today/asOf is passed in rather than read internally (e.g. via
    // Date::Date()) so status can be evaluated consistently for a whole
    // batch of records against the exact same "now", and so it's testable
    // with an arbitrary date instead of only the real current date.
    RecordStatus getStatus(const Date& today) const;
    int daysOverdue(const Date& asOf) const; // days asOf is past dueDate, 0 if none/returned
    void markReturned(const Date& returnDate, double fine);

    void display(std::ostream& os) const;
    std::string serialize() const;

    // displayFine is the amount to report as "fine" in the JSON - the
    // finalized amount for a returned record, or (computed by the caller,
    // via InventorySystem::currentFine() - LendingRecord itself has no
    // access to the Instrument needed to calculate it) the live projected
    // amount for a record that's still Issued/Overdue.
    std::string toJson(const Date& today, double displayFine) const;

    static std::string generateId();
    static void ensureCounterAbove(int usedNumber);
};

} // namespace TRF
