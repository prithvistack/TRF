#include "InventorySystem.h"
#include "Exceptions.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>

namespace TRF {

const std::string InventorySystem::INSTRUMENTS_FILE = "data/instruments.txt";
const std::string InventorySystem::MEMBERS_FILE = "data/members.txt";
const std::string InventorySystem::RECORDS_FILE = "data/records.txt";

// ---- small file-local helpers (not part of the public API) ----
namespace {

// Splits on '|', keeping empty trailing fields (e.g. an empty remarks
// field at the end of a record line). A naive `while (getline(iss, tok, '|'))`
// loop silently drops a trailing empty token because getline() fails once
// it hits end-of-stream with nothing left to extract, so this is done by
// hand with find() instead.
std::vector<std::string> splitPipe(const std::string& line) {
    std::vector<std::string> tokens;
    std::size_t start = 0;
    while (true) {
        std::size_t pos = line.find('|', start);
        if (pos == std::string::npos) {
            tokens.push_back(line.substr(start));
            break;
        }
        tokens.push_back(line.substr(start, pos - start));
        start = pos + 1;
    }
    return tokens;
}

// Pulls the trailing number out of an id like "INS-007" -> 7. Used while
// loading, to work out where each static id counter should resume.
int idSuffix(const std::string& id) {
    std::size_t dash = id.find('-');
    if (dash == std::string::npos) {
        return 0;
    }
    try {
        return std::stoi(id.substr(dash + 1));
    } catch (const std::exception&) {
        return 0;
    }
}

std::string toLower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

} // anonymous namespace

// ===================== construction / destruction =====================

InventorySystem::InventorySystem() {
    std::filesystem::create_directories("data");
    loadInstruments();
    loadMembers();
    loadRecords();
    seedIfEmpty();
}

InventorySystem::~InventorySystem() {
    // Nothing to free by hand: instruments/persons/records are plain
    // (non-pointer) Repository<T> members, so their destructors run
    // automatically right here, and each of those deletes every T* it
    // owns. This is exactly what "the system's destructor frees memory"
    // means in practice - it happens by composition, not by an explicit
    // loop of `delete` calls in this function body.
}

// ===================== file loading =====================

void InventorySystem::loadInstruments() {
    std::ifstream file(INSTRUMENTS_FILE);
    if (!file.is_open()) {
        return; // no file yet - first run, seedIfEmpty() will populate it
    }
    std::string line;
    int maxNum = 0;
    while (std::getline(file, line)) {
        if (line.empty()) {
            continue;
        }
        try {
            std::vector<std::string> f = splitPipe(line);
            if (f.size() < 7) {
                throw InvalidInputException("expected at least 7 fields, found " + std::to_string(f.size()));
            }
            const std::string& type = f[0];
            const std::string& id = f[1];
            const std::string& name = f[2];
            int total = std::stoi(f[3]);
            int avail = std::stoi(f[4]);
            Condition cond = conditionFromString(f[5]);
            const std::string& extra = f[6];

            Instrument* inst = nullptr;
            if (type == "ELECTRONIC") {
                inst = new ElectronicComponent(id, name, total, cond, extra);
            } else if (type == "TOOL") {
                inst = new Tool(id, name, total, cond, extra == "1");
            } else if (type == "POWER") {
                inst = new PowerDevice(id, name, total, cond, extra);
            } else {
                throw InvalidInputException("unknown instrument type tag '" + type + "'");
            }

            // The constructor always starts availableQuantity == totalQuantity;
            // "use up" the units that were already out on disk by issuing them.
            int issuedCount = total - avail;
            if (issuedCount > 0) {
                inst->issue(issuedCount);
            }

            instruments.add(inst);
            maxNum = std::max(maxNum, idSuffix(id));
        } catch (const std::exception& e) {
            std::cerr << "Warning: skipping malformed instrument line (" << e.what() << "): " << line << "\n";
        }
    }
    Instrument::ensureCounterAbove(maxNum);
}

void InventorySystem::loadMembers() {
    std::ifstream file(MEMBERS_FILE);
    if (!file.is_open()) {
        return;
    }
    std::string line;
    int maxNum = 0;
    while (std::getline(file, line)) {
        if (line.empty()) {
            continue;
        }
        try {
            std::vector<std::string> f = splitPipe(line);
            if (f.size() < 9) {
                throw InvalidInputException("expected at least 9 fields, found " + std::to_string(f.size()));
            }
            const std::string& type = f[0];
            const std::string& id = f[1];
            const std::string& name = f[2];
            const std::string& phone = f[3];
            const std::string& email = f[4];
            const std::string& rollNo = f[5];
            const std::string& branch = f[6];
            const std::string& division = f[7];
            int year = std::stoi(f[8]);

            Person* p = nullptr;
            if (type == "MEMBER") {
                bool isWalkIn;
                if (f.size() >= 10) {
                    isWalkIn = (f[9] == "1");
                } else {
                    // f.size() == 9: this line predates the isWalkIn field.
                    // Infer it instead of defaulting to false for everyone -
                    // only findOrCreateBorrower()'s auto-create path ever
                    // saves the "-"/"-" placeholder phone/email (the Add
                    // Member form requires real values), so a line with
                    // both still equal to "-" reliably identifies a
                    // walk-in borrower saved by an older build.
                    isWalkIn = (phone == "-" && email == "-");
                }
                p = new Member(id, name, phone, email, rollNo, branch, division, year, isWalkIn);
            } else if (type == "CORE") {
                if (f.size() < 10) {
                    throw InvalidInputException("core member line missing position field");
                }
                p = new CoreMember(id, name, phone, email, rollNo, branch, division, year, f[9]);
            } else {
                throw InvalidInputException("unknown person type tag '" + type + "'");
            }

            persons.add(p);
            maxNum = std::max(maxNum, idSuffix(id));
        } catch (const std::exception& e) {
            std::cerr << "Warning: skipping malformed member line (" << e.what() << "): " << line << "\n";
        }
    }
    Member::ensureCounterAbove(maxNum);
}

void InventorySystem::loadRecords() {
    std::ifstream file(RECORDS_FILE);
    if (!file.is_open()) {
        return;
    }
    std::string line;
    int maxNum = 0;
    while (std::getline(file, line)) {
        if (line.empty()) {
            continue;
        }
        try {
            std::vector<std::string> f = splitPipe(line);
            if (f.size() < 15) {
                throw InvalidInputException("expected 15 fields, found " + std::to_string(f.size()));
            }
            LendingRecord* r = new LendingRecord(
                f[0], f[1], f[2], f[3], f[4], f[5], f[6],
                std::stoi(f[7]), Date(f[8]), std::stoi(f[9]), Date(f[10]),
                Date(f[11]), f[12] == "1", std::stod(f[13]), f[14]);
            records.add(r);
            maxNum = std::max(maxNum, idSuffix(f[0]));
        } catch (const std::exception& e) {
            std::cerr << "Warning: skipping malformed record line (" << e.what() << "): " << line << "\n";
        }
    }
    LendingRecord::ensureCounterAbove(maxNum);
}

void InventorySystem::seedIfEmpty() {
    if (!instruments.getAll().empty() || !persons.getAll().empty()) {
        return; // real data already on disk - never overwrite or duplicate it
    }

    // 8 instruments across all three categories.
    std::string arduino    = addInstrument("Electronic Component", "Arduino Uno R3", 6, Condition::Good, "5V");
    addInstrument("Electronic Component", "ESP32 Dev Board", 5, Condition::New, "3.3V");
    addInstrument("Electronic Component", "IR Proximity Sensor", 10, Condition::Good, "5V");
    addInstrument("Electronic Component", "L298N Motor Driver", 4, Condition::Fair, "12V");
    std::string solderIron = addInstrument("Tool", "Soldering Iron 40W", 3, Condition::Good, "true");
    addInstrument("Tool", "Digital Multimeter", 4, Condition::New, "false");
    std::string lipoBatt    = addInstrument("Power Device", "LiPo Battery 2200mAh", 8, Condition::Good, "2200mAh");
    addInstrument("Power Device", "Bench Power Supply", 2, Condition::Good, "30V 5A");

    // 5 members: 3 regular, 2 core.
    std::string m1 = addMember("Aarav Sharma", "9800000001", "aarav@example.com", "21ME101", "Mechanical", "A", 3, false, "");
    std::string m2 = addMember("Priya Nair", "9800000002", "priya@example.com", "21EC045", "Electronics", "B", 2, false, "");
    addMember("Rohan Gupta", "9800000003", "rohan@example.com", "22CS077", "Computer Science", "A", 1, false, "");
    std::string c1 = addMember("Sanya Iyer", "9800000004", "sanya@example.com", "20EC010", "Electronics", "A", 4, true, "Electronics Head");
    std::string c2 = addMember("Kabir Mehta", "9800000005", "kabir@example.com", "20ME020", "Mechanical", "B", 4, true, "Mechanical Head");

    // 3 records: two currently active, one deliberately overdue (issued
    // 12 days ago on a 7-day-max Tool, so today is well past its due date).
    issueItem(arduino, m1, c1, 1, 10, today());
    issueItem(lipoBatt, m2, c1, 2, 5, today());
    Date twelveDaysAgo = today() + (-12);
    issueItem(solderIron, m1, c2, 1, 7, twelveDaysAgo);
}

void InventorySystem::requireNoPipe(const std::string& value, const std::string& fieldLabel) {
    if (value.find('|') != std::string::npos) {
        throw InvalidInputException(fieldLabel + " cannot contain the '|' character");
    }
}

Instrument* InventorySystem::requireInstrument(const std::string& id) const {
    Instrument* inst = instruments.findById(id);
    if (inst == nullptr) {
        throw InvalidIdException("No instrument with id " + id);
    }
    return inst;
}

Person* InventorySystem::requirePerson(const std::string& id) const {
    Person* p = persons.findById(id);
    if (p == nullptr) {
        throw InvalidIdException("No member with id " + id);
    }
    return p;
}

LendingRecord* InventorySystem::requireRecord(const std::string& id) const {
    LendingRecord* r = records.findById(id);
    if (r == nullptr) {
        throw InvalidIdException("No lending record with id " + id);
    }
    return r;
}

void InventorySystem::saveAll() const {
    std::ofstream instFile(INSTRUMENTS_FILE, std::ios::trunc);
    for (const Instrument* inst : instruments.getAll()) {
        instFile << inst->serialize() << "\n";
    }

    std::ofstream memFile(MEMBERS_FILE, std::ios::trunc);
    for (const Person* p : persons.getAll()) {
        memFile << p->serialize() << "\n";
    }

    std::ofstream recFile(RECORDS_FILE, std::ios::trunc);
    for (const LendingRecord* r : records.getAll()) {
        recFile << r->serialize() << "\n";
    }
}

// ===================== instruments =====================

std::string InventorySystem::addInstrument(const std::string& category, const std::string& name,
                                            int totalQuantity, Condition condition, const std::string& extraField) {
    requireNoPipe(name, "Instrument name");
    requireNoPipe(extraField, "Instrument extra field");

    // The id is generated up front. If construction below throws (bad
    // extra field, bad quantity), that id number is simply never used
    // again - the same way an auto-increment database column skips a
    // value on a failed insert. Harmless, and much simpler than trying to
    // "give the number back".
    std::string id = Instrument::generateId();

    Instrument* inst = nullptr;
    if (category == "Electronic Component") {
        inst = new ElectronicComponent(id, name, totalQuantity, condition, extraField);
    } else if (category == "Tool") {
        bool requiresTraining = (extraField == "true" || extraField == "1");
        inst = new Tool(id, name, totalQuantity, condition, requiresTraining);
    } else if (category == "Power Device") {
        inst = new PowerDevice(id, name, totalQuantity, condition, extraField);
    } else {
        throw InvalidInputException("Unknown instrument category: " + category);
    }

    instruments.add(inst);
    saveAll();
    return id;
}

std::vector<Instrument*> InventorySystem::listInstruments() const {
    std::vector<Instrument*> result = instruments.getAll(); // copies the pointer vector, not the objects
    std::sort(result.begin(), result.end(),
               [](const Instrument* a, const Instrument* b) { return a->getId() < b->getId(); });
    return result;
}

// Compile-time polymorphism, overload 1 of 2: search by name only.
std::vector<Instrument*> InventorySystem::searchInstruments(const std::string& keyword) const {
    std::string needle = toLower(keyword);
    std::vector<Instrument*> result = instruments.filter([&needle](const Instrument* inst) {
        return needle.empty() || toLower(inst->getName()).find(needle) != std::string::npos;
    });
    std::sort(result.begin(), result.end(),
               [](const Instrument* a, const Instrument* b) { return a->getId() < b->getId(); });
    return result;
}

// Compile-time polymorphism, overload 2 of 2: search by name AND category.
// The compiler picks this one instead of the single-argument version
// purely based on the number of arguments at the call site - resolved at
// compile time, unlike the run-time virtual dispatch used elsewhere.
std::vector<Instrument*> InventorySystem::searchInstruments(const std::string& keyword, const std::string& category) const {
    std::string needle = toLower(keyword);
    std::vector<Instrument*> result = instruments.filter([&needle, &category](const Instrument* inst) {
        bool nameMatches = needle.empty() || toLower(inst->getName()).find(needle) != std::string::npos;
        bool categoryMatches = category.empty() || inst->getCategory() == category;
        return nameMatches && categoryMatches;
    });
    std::sort(result.begin(), result.end(),
               [](const Instrument* a, const Instrument* b) { return a->getId() < b->getId(); });
    return result;
}

void InventorySystem::updateInstrumentCondition(const std::string& instrumentId, Condition newCondition) {
    Instrument* inst = requireInstrument(instrumentId);
    inst->setCondition(newCondition);
    saveAll();
}

void InventorySystem::deleteInstrument(const std::string& instrumentId) {
    Instrument* inst = requireInstrument(instrumentId);
    if (inst->getAvailableQuantity() < inst->getTotalQuantity()) {
        throw InvalidOperationException("Cannot delete '" + inst->getName() + "': some units are currently issued");
    }
    instruments.remove(instrumentId);
    saveAll();
}

// ===================== members =====================

std::string InventorySystem::addMember(const std::string& name, const std::string& phone, const std::string& email,
                                        const std::string& rollNo, const std::string& branch,
                                        const std::string& division, int year,
                                        bool isCore, const std::string& position, bool isWalkIn) {
    requireNoPipe(name, "Member name");
    requireNoPipe(phone, "Phone");
    requireNoPipe(email, "Email");
    requireNoPipe(rollNo, "Roll number");
    requireNoPipe(branch, "Branch");
    requireNoPipe(division, "Division");
    if (isCore) {
        requireNoPipe(position, "Position");
    }

    std::string id = Member::generateId(); // shared MEM-xxx sequence for both Member and CoreMember

    Person* p = nullptr;
    if (isCore) {
        p = new CoreMember(id, name, phone, email, rollNo, branch, division, year, position);
    } else {
        p = new Member(id, name, phone, email, rollNo, branch, division, year, isWalkIn);
    }

    persons.add(p);
    saveAll();
    return id;
}

// Reuses an existing Person if one already has this PRN (so repeat
// borrowers keep a single accumulating history and their borrow limit is
// enforced correctly), otherwise registers a brand-new Member on the fly.
// Phone/year aren't part of the walk-up lending form, so they get harmless
// placeholders - Person's constructor doesn't require them to be non-empty
// (only the setPhone()/setEmail() setters validate that, and they're never
// called here). email IS optionally collected on that form (needed for the
// overdue email-reminder feature) - an empty value is stored as "-".
std::string InventorySystem::findOrCreateBorrower(const std::string& name, const std::string& surname,
                                                    const std::string& branch, const std::string& division,
                                                    const std::string& prn, const std::string& email) {
    if (name.empty()) {
        throw InvalidInputException("Borrower name cannot be empty");
    }
    if (prn.empty()) {
        throw InvalidInputException("Borrower PRN cannot be empty");
    }
    requireNoPipe(name, "Borrower name");
    requireNoPipe(surname, "Borrower surname");
    requireNoPipe(branch, "Borrower branch");
    requireNoPipe(division, "Borrower division");
    requireNoPipe(prn, "Borrower PRN");
    requireNoPipe(email, "Borrower email");

    // Virtual call through Person* - no dynamic_cast needed to check a
    // Member-specific field on an arbitrary Person.
    const std::vector<Person*>& all = persons.getAll();
    auto it = std::find_if(all.begin(), all.end(),
                             [&prn](const Person* p) { return p->getRollNo() == prn; });
    if (it != all.end()) {
        return (*it)->getId();
    }

    std::string fullName = surname.empty() ? name : name + " " + surname;
    std::string emailToStore = email.empty() ? "-" : email;
    return addMember(fullName, "-", emailToStore, prn, branch, division, 1, false, "", /*isWalkIn=*/true);
}

std::vector<Person*> InventorySystem::listMembers() const {
    // Excludes walk-in borrowers (see Person::isWalkInBorrower()) - the
    // Members tab should only ever show people actually added through it,
    // not anyone who was just typed into the Lend form.
    std::vector<Person*> result = persons.filter([](const Person* p) { return !p->isWalkInBorrower(); });
    std::sort(result.begin(), result.end(),
               [](const Person* a, const Person* b) { return a->getId() < b->getId(); });
    return result;
}

void InventorySystem::deleteMember(const std::string& memberId) {
    Person* p = requirePerson(memberId);

    const std::vector<LendingRecord*>& all = records.getAll();
    bool holdsSomething = std::find_if(all.begin(), all.end(), [&memberId](const LendingRecord* r) {
                               return r->getMemberId() == memberId && !r->isReturned();
                           }) != all.end();
    // A core member currently listed as "lent by" on an active record is
    // also protected - deleting them would leave that record pointing at
    // a lender who no longer exists.
    bool isActiveLender = std::find_if(all.begin(), all.end(), [&memberId](const LendingRecord* r) {
                               return r->getLentById() == memberId && !r->isReturned();
                           }) != all.end();

    if (holdsSomething || isActiveLender) {
        throw InvalidOperationException("Cannot delete '" + p->getName() + "': linked to an active lending record");
    }

    persons.remove(memberId);
    saveAll();
}

// ===================== lending =====================

std::string InventorySystem::issueItem(const std::string& instrumentId, const std::string& memberId,
                                        const std::string& lentById, int quantity, int days, const Date& issueDate) {
    Instrument* inst = requireInstrument(instrumentId);
    Person* borrower = requirePerson(memberId);
    Person* lender = requirePerson(lentById);

    if (memberId == lentById) {
        throw InvalidOperationException("Borrower and lender cannot be the same person");
    }
    // Virtual call: only a CoreMember's canLend() returns true. No RTTI
    // (no dynamic_cast) is used to find that out.
    if (!lender->canLend()) {
        throw InvalidOperationException(lender->getName() + " is not authorised to lend items (core members only)");
    }
    if (inst->getCondition() == Condition::Damaged) {
        throw ItemUnavailableException("'" + inst->getName() + "' is marked Damaged and cannot be issued");
    }
    if (quantity < 1 || quantity > inst->getAvailableQuantity()) {
        throw ItemUnavailableException("Requested quantity is not available for '" + inst->getName() + "'");
    }

    const std::vector<LendingRecord*>& all = records.getAll();
    int activeCount = static_cast<int>(std::count_if(all.begin(), all.end(), [&memberId](const LendingRecord* r) {
        return r->getMemberId() == memberId && !r->isReturned();
    }));
    if (activeCount >= borrower->getBorrowLimit()) {
        throw BorrowLimitExceededException(
            borrower->getName() + " already holds " + std::to_string(activeCount) +
            " active item(s); the limit is " + std::to_string(borrower->getBorrowLimit()));
    }

    // Virtual calls: getMaxLendDays()/getExtraDays() run the real
    // subclass's version even though inst/borrower are base-class pointers.
    int maxDays = inst->getMaxLendDays() + borrower->getExtraDays();
    if (days < 1 || days > maxDays) {
        throw InvalidInputException(
            "Requested " + std::to_string(days) + " day(s) exceeds the maximum of " +
            std::to_string(maxDays) + " allowed for this borrower/instrument combination");
    }

    inst->issue(quantity); // mutate state only after every rule above has passed

    std::string recordId = LendingRecord::generateId();
    LendingRecord* record = new LendingRecord(recordId, inst->getId(), inst->getName(),
                                               borrower->getId(), borrower->getName(),
                                               lender->getId(), lender->getName(),
                                               quantity, issueDate, days);
    records.add(record);
    saveAll();
    return recordId;
}

// Overload for the "borrower could be anyone" walk-up lending flow: resolve
// (or silently register) the borrower first, then defer to the id-based
// overload above for every actual business rule (borrow limit, damaged
// check, days check, ...), so those rules only ever need to be written once.
std::string InventorySystem::issueItem(const std::string& instrumentId, const std::string& borrowerName,
                                        const std::string& borrowerSurname, const std::string& borrowerBranch,
                                        const std::string& borrowerDivision, const std::string& borrowerPrn,
                                        const std::string& borrowerEmail,
                                        const std::string& lentById, int quantity, int days, const Date& issueDate) {
    std::string memberId = findOrCreateBorrower(borrowerName, borrowerSurname, borrowerBranch,
                                                 borrowerDivision, borrowerPrn, borrowerEmail);
    return issueItem(instrumentId, memberId, lentById, quantity, days, issueDate);
}

void InventorySystem::returnItem(const std::string& recordId, const Date& returnDate,
                                  bool updateCondition, Condition newCondition) {
    LendingRecord* record = requireRecord(recordId);
    if (record->isReturned()) {
        throw InvalidOperationException("Record " + recordId + " has already been returned");
    }
    Instrument* inst = requireInstrument(record->getInstrumentId());

    int daysLate = record->daysOverdue(returnDate);
    // Polymorphic call: PowerDevice::calculateFine doubles the rate past 7
    // late days; every other Instrument uses the Instrument base default.
    double fine = inst->calculateFine(daysLate);

    record->markReturned(returnDate, fine); // also validates returnDate >= issueDate
    inst->receive(record->getQuantity());

    if (updateCondition) {
        inst->setCondition(newCondition);
    }

    saveAll();
}

std::vector<LendingRecord*> InventorySystem::getRecords(const std::string& status, const std::string& memberQuery,
                                                          const std::string& instrumentQuery) const {
    // STL map: translates the query-string status filter into the enum
    // value it represents, instead of an if/else-if chain of string
    // comparisons.
    static const std::map<std::string, RecordStatus> statusLookup = {
        {"Issued", RecordStatus::Issued},
        {"Overdue", RecordStatus::Overdue},
        {"Returned", RecordStatus::Returned},
    };

    bool filterByStatus = !status.empty();
    RecordStatus wantedStatus = RecordStatus::Issued;
    if (filterByStatus) {
        auto it = statusLookup.find(status);
        if (it == statusLookup.end()) {
            throw InvalidInputException("Unknown status filter: " + status);
        }
        wantedStatus = it->second;
    }

    // Matched by name, not id: the Records table never shows raw
    // member/instrument ids anywhere, only names, so filtering by id would
    // ask the user for something they can never actually see.
    std::string memberNeedle = toLower(memberQuery);
    std::string instrumentNeedle = toLower(instrumentQuery);

    Date asOf = today();
    std::vector<LendingRecord*> result = records.filter([&](const LendingRecord* r) {
        if (filterByStatus && r->getStatus(asOf) != wantedStatus) {
            return false;
        }
        if (!memberNeedle.empty() && toLower(r->getMemberName()).find(memberNeedle) == std::string::npos) {
            return false;
        }
        if (!instrumentNeedle.empty() &&
            toLower(r->getInstrumentName()).find(instrumentNeedle) == std::string::npos) {
            return false;
        }
        return true;
    });

    // Most recent lending first - uses Date::operator> from Phase 1.
    std::sort(result.begin(), result.end(),
               [](const LendingRecord* a, const LendingRecord* b) { return a->getIssueDate() > b->getIssueDate(); });
    return result;
}

double InventorySystem::currentFine(const LendingRecord* record) const {
    if (record->isReturned()) {
        return record->getFine(); // finalized at return time, based on the actual return date
    }
    Instrument* inst = instruments.findById(record->getInstrumentId());
    if (inst == nullptr) {
        return 0.0; // defensive: shouldn't happen, deleteInstrument() is blocked while any unit is issued
    }
    int daysLate = record->daysOverdue(today());
    // Polymorphic call - PowerDevice's doubled-past-7-days rule applies
    // here exactly as it would if this record were returned right now.
    return inst->calculateFine(daysLate);
}

Person* InventorySystem::findPerson(const std::string& id) const {
    return persons.findById(id);
}

DashboardStats InventorySystem::getStats() const {
    DashboardStats stats{};
    stats.today = today();
    stats.totalInstruments = static_cast<int>(instruments.size());

    int totalUnits = 0;
    int availableUnits = 0;
    for (const Instrument* inst : instruments.getAll()) {
        totalUnits += inst->getTotalQuantity();
        availableUnits += inst->getAvailableQuantity();
    }
    stats.unitsAvailable = availableUnits;
    stats.unitsIssued = totalUnits - availableUnits;

    const std::vector<LendingRecord*>& all = records.getAll();
    stats.activeLendings = static_cast<int>(
        std::count_if(all.begin(), all.end(), [](const LendingRecord* r) { return !r->isReturned(); }));
    stats.overdueCount = static_cast<int>(std::count_if(all.begin(), all.end(), [&stats](const LendingRecord* r) {
        return r->getStatus(stats.today) == RecordStatus::Overdue;
    }));

    double totalFines = 0.0;
    for (const LendingRecord* r : all) {
        if (r->isReturned()) {
            totalFines += r->getFine();
        }
    }
    stats.totalFinesCollected = totalFines;

    return stats;
}

Date InventorySystem::today() const {
    return Date();
}

} // namespace TRF
