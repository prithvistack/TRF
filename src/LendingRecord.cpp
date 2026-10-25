#include "LendingRecord.h"
#include "Exceptions.h"
#include "JsonUtil.h"
#include <iomanip>
#include <sstream>

namespace TRF {

std::string recordStatusToString(RecordStatus s) {
    switch (s) {
        case RecordStatus::Issued:   return "Issued";
        case RecordStatus::Overdue:  return "Overdue";
        case RecordStatus::Returned: return "Returned";
    }
    return "Unknown";
}

int LendingRecord::nextIdNumber = 1;

LendingRecord::LendingRecord(std::string id, std::string instrumentId, std::string instrumentName,
                              std::string memberId, std::string memberName,
                              std::string lentById, std::string lentByName,
                              int quantity, const Date& issueDate, int daysLent)
    : id(std::move(id)), instrumentId(std::move(instrumentId)), instrumentName(std::move(instrumentName)),
      memberId(std::move(memberId)), memberName(std::move(memberName)),
      lentById(std::move(lentById)), lentByName(std::move(lentByName)),
      quantity(quantity), issueDate(issueDate), daysLent(daysLent),
      dueDate(issueDate + daysLent), // Date::operator+ used right here in the init list
      returnDate(issueDate),          // placeholder until markReturned() is called
      returned(false), fine(0.0), remarks() {
    if (quantity < 1) {
        throw InvalidInputException("Lending quantity must be at least 1");
    }
    if (daysLent < 1) {
        throw InvalidInputException("Number of days lent must be at least 1");
    }
}

LendingRecord::LendingRecord(std::string id, std::string instrumentId, std::string instrumentName,
                              std::string memberId, std::string memberName,
                              std::string lentById, std::string lentByName,
                              int quantity, const Date& issueDate, int daysLent, const Date& dueDate,
                              const Date& returnDate, bool returned, double fine, std::string remarks)
    : id(std::move(id)), instrumentId(std::move(instrumentId)), instrumentName(std::move(instrumentName)),
      memberId(std::move(memberId)), memberName(std::move(memberName)),
      lentById(std::move(lentById)), lentByName(std::move(lentByName)),
      quantity(quantity), issueDate(issueDate), daysLent(daysLent), dueDate(dueDate),
      returnDate(returnDate), returned(returned), fine(fine), remarks(std::move(remarks)) {
}

std::string LendingRecord::getId() const { return id; }
std::string LendingRecord::getInstrumentId() const { return instrumentId; }
std::string LendingRecord::getInstrumentName() const { return instrumentName; }
std::string LendingRecord::getMemberId() const { return memberId; }
std::string LendingRecord::getMemberName() const { return memberName; }
std::string LendingRecord::getLentById() const { return lentById; }
std::string LendingRecord::getLentByName() const { return lentByName; }
int LendingRecord::getQuantity() const { return quantity; }
Date LendingRecord::getIssueDate() const { return issueDate; }
int LendingRecord::getDaysLent() const { return daysLent; }
Date LendingRecord::getDueDate() const { return dueDate; }
Date LendingRecord::getReturnDate() const { return returnDate; }
bool LendingRecord::isReturned() const { return returned; }
double LendingRecord::getFine() const { return fine; }
std::string LendingRecord::getRemarks() const { return remarks; }

void LendingRecord::setRemarks(const std::string& r) {
    if (r.find('|') != std::string::npos) {
        throw InvalidInputException("Remarks cannot contain the '|' character");
    }
    remarks = r;
}

RecordStatus LendingRecord::getStatus(const Date& today) const {
    if (returned) {
        return RecordStatus::Returned;
    }
    if (today > dueDate) {
        return RecordStatus::Overdue;
    }
    return RecordStatus::Issued;
}

int LendingRecord::daysOverdue(const Date& asOf) const {
    if (returned || !(asOf > dueDate)) {
        return 0;
    }
    return static_cast<int>(asOf - dueDate); // Date::operator- : days between two dates
}

void LendingRecord::markReturned(const Date& returnDateIn, double fineIn) {
    if (returnDateIn < issueDate) {
        throw InvalidOperationException("Return date cannot be before the issue date");
    }
    returnDate = returnDateIn;
    fine = fineIn;
    returned = true;
}

void LendingRecord::display(std::ostream& os) const {
    os << id << " | " << instrumentName << " x" << quantity
       << " | Borrower: " << memberName << " | Lent by: " << lentByName
       << " | Issued: " << issueDate << " | Due: " << dueDate;
    if (returned) {
        os << " | Returned: " << returnDate << " | Fine: Rs " << jsonMoney(fine);
    }
}

std::string LendingRecord::serialize() const {
    return id + "|" + instrumentId + "|" + instrumentName + "|" +
           memberId + "|" + memberName + "|" +
           lentById + "|" + lentByName + "|" +
           std::to_string(quantity) + "|" +
           issueDate.toString() + "|" +
           std::to_string(daysLent) + "|" +
           dueDate.toString() + "|" +
           returnDate.toString() + "|" +
           (returned ? "1" : "0") + "|" +
           jsonMoney(fine) + "|" +
           remarks;
}

std::string LendingRecord::toJson(const Date& today, double displayFine) const {
    RecordStatus status = getStatus(today);
    std::string json = "{";
    json += "\"id\":" + jsonString(id) + ",";
    json += "\"instrumentId\":" + jsonString(instrumentId) + ",";
    json += "\"instrumentName\":" + jsonString(instrumentName) + ",";
    json += "\"memberId\":" + jsonString(memberId) + ",";
    json += "\"memberName\":" + jsonString(memberName) + ",";
    json += "\"lentById\":" + jsonString(lentById) + ",";
    json += "\"lentByName\":" + jsonString(lentByName) + ",";
    json += "\"quantity\":" + std::to_string(quantity) + ",";
    json += "\"issueDate\":" + jsonString(issueDate.toString()) + ",";
    json += "\"daysLent\":" + std::to_string(daysLent) + ",";
    json += "\"dueDate\":" + jsonString(dueDate.toString()) + ",";
    json += "\"returnDate\":" + jsonString(returned ? returnDate.toString() : std::string("")) + ",";
    json += "\"returned\":" + std::string(returned ? "true" : "false") + ",";
    // For an active record this is a live estimate ("if returned today"),
    // not yet a charged amount - fineIsProjected tells the frontend which.
    json += "\"fine\":" + jsonMoney(displayFine) + ",";
    json += "\"fineIsProjected\":" + std::string(returned ? "false" : "true") + ",";
    json += "\"remarks\":" + jsonString(remarks) + ",";
    json += "\"status\":" + jsonString(recordStatusToString(status)) + ",";
    json += "\"daysOverdue\":" + std::to_string(daysOverdue(today));
    json += "}";
    return json;
}

std::string LendingRecord::generateId() {
    std::ostringstream oss;
    oss << "REC-" << std::setfill('0') << std::setw(3) << nextIdNumber;
    ++nextIdNumber;
    return oss.str();
}

void LendingRecord::ensureCounterAbove(int usedNumber) {
    if (usedNumber >= nextIdNumber) {
        nextIdNumber = usedNumber + 1;
    }
}

} // namespace TRF
