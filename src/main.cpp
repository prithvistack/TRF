// HTTP server entry point. Deliberately thin: every route parses form/
// query parameters, calls exactly one InventorySystem method, and turns
// the result (or a caught TRFException) into an HTTP response. All the
// actual business logic lives in InventorySystem, not here.
#include "httplib.h"
#include "InventorySystem.h"
#include "Exceptions.h"
#include "JsonUtil.h"
#include "Instrument.h"
#include "Person.h"
#include "LendingRecord.h"
#include <iostream>
#include <string>

using namespace TRF;

namespace {

std::string toJsonArray(const std::vector<Instrument*>& items) {
    std::string json = "[";
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0) json += ",";
        json += items[i]->toJson(); // virtual call: each subclass's own toJson() runs
    }
    json += "]";
    return json;
}

std::string toJsonArray(const std::vector<Person*>& items) {
    std::string json = "[";
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0) json += ",";
        json += items[i]->toJson();
    }
    json += "]";
    return json;
}

std::string toJsonArray(const std::vector<LendingRecord*>& items, const Date& today, const InventorySystem& system) {
    std::string json = "[";
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0) json += ",";
        // currentFine() is the finalized amount for a returned record, or a
        // live "if returned today" estimate for one still Issued/Overdue.
        std::string recordJson = items[i]->toJson(today, system.currentFine(items[i]));

        // Splice in the borrower's full profile (branch/division/PRN/email/...)
        // as a nested object - Person::toJson() is virtual, so this is
        // whatever the borrower's actual subclass (Member/CoreMember)
        // reports, without LendingRecord itself needing to know those fields.
        Person* borrower = system.findPerson(items[i]->getMemberId());
        recordJson.pop_back(); // remove the closing '}' so it can be extended
        recordJson += ",\"borrower\":" + (borrower != nullptr ? borrower->toJson() : std::string("null")) + "}";

        json += recordJson;
    }
    json += "]";
    return json;
}

// Every route's catch block funnels here: maps the TRFException hierarchy
// onto HTTP status codes as required by the spec (InvalidIdException -> 404,
// any other TRFException -> 400), and writes a small {"error":"..."} body.
void sendError(httplib::Response& res, int status, const std::string& message) {
    res.status = status;
    res.set_content("{\"error\":" + jsonString(message) + "}", "application/json");
}

int intParam(const httplib::Request& req, const std::string& key) {
    try {
        return std::stoi(req.get_param_value(key));
    } catch (const std::exception&) {
        throw InvalidInputException("'" + key + "' must be a whole number");
    }
}

Date dateParamOrToday(const httplib::Request& req, const std::string& key, const InventorySystem& system) {
    std::string raw = req.get_param_value(key);
    if (raw.empty()) {
        return system.today();
    }
    return Date(raw); // throws InvalidInputException if malformed
}

} // namespace

int main() {
    InventorySystem system;
    httplib::Server svr;

    // No multithreading in this project: cap the server's task queue at
    // exactly one worker thread, so requests are always handled strictly
    // one after another.
    svr.new_task_queue = [] { return new httplib::ThreadPool(1); };

    svr.set_mount_point("/", "./web");

    // ---------------- dashboard ----------------
    svr.Get("/api/stats", [&](const httplib::Request&, httplib::Response& res) {
        DashboardStats stats = system.getStats();
        std::string json = "{";
        json += "\"totalInstruments\":" + std::to_string(stats.totalInstruments) + ",";
        json += "\"unitsAvailable\":" + std::to_string(stats.unitsAvailable) + ",";
        json += "\"unitsIssued\":" + std::to_string(stats.unitsIssued) + ",";
        json += "\"activeLendings\":" + std::to_string(stats.activeLendings) + ",";
        json += "\"overdueCount\":" + std::to_string(stats.overdueCount) + ",";
        json += "\"totalFinesCollected\":" + jsonMoney(stats.totalFinesCollected) + ",";
        json += "\"today\":" + jsonString(stats.today.toString());
        json += "}";
        res.set_content(json, "application/json");
    });

    // ---------------- instruments ----------------
    svr.Get("/api/instruments", [&](const httplib::Request& req, httplib::Response& res) {
        std::string q = req.get_param_value("q");
        std::string category = req.get_param_value("category");
        // Overload resolution happens here, at compile time, based on
        // whether a category filter was supplied.
        std::vector<Instrument*> results =
            category.empty() ? system.searchInstruments(q) : system.searchInstruments(q, category);
        res.set_content(toJsonArray(results), "application/json");
    });

    svr.Post("/api/instruments", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string category = req.get_param_value("category");
            std::string name = req.get_param_value("name");
            int totalQuantity = intParam(req, "totalQuantity");
            Condition condition = conditionFromString(req.get_param_value("condition"));
            std::string extraField = req.get_param_value("extraField");

            std::string id = system.addInstrument(category, name, totalQuantity, condition, extraField);
            res.set_content("{\"id\":" + jsonString(id) + "}", "application/json");
        } catch (const InvalidIdException& e) {
            sendError(res, 404, e.what());
        } catch (const TRFException& e) {
            sendError(res, 400, e.what());
        }
    });

    svr.Post("/api/instruments/delete", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            system.deleteInstrument(req.get_param_value("id"));
            res.set_content("{\"ok\":true}", "application/json");
        } catch (const InvalidIdException& e) {
            sendError(res, 404, e.what());
        } catch (const TRFException& e) {
            sendError(res, 400, e.what());
        }
    });

    svr.Post("/api/instruments/condition", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string id = req.get_param_value("id");
            Condition condition = conditionFromString(req.get_param_value("condition"));
            system.updateInstrumentCondition(id, condition);
            res.set_content("{\"ok\":true}", "application/json");
        } catch (const InvalidIdException& e) {
            sendError(res, 404, e.what());
        } catch (const TRFException& e) {
            sendError(res, 400, e.what());
        }
    });

    // ---------------- members ----------------
    svr.Get("/api/members", [&](const httplib::Request&, httplib::Response& res) {
        res.set_content(toJsonArray(system.listMembers()), "application/json");
    });

    svr.Post("/api/members", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string name = req.get_param_value("name");
            std::string phone = req.get_param_value("phone");
            std::string email = req.get_param_value("email");
            std::string rollNo = req.get_param_value("rollNo");
            std::string branch = req.get_param_value("branch");
            std::string division = req.get_param_value("division");
            int year = intParam(req, "year");
            bool isCore = req.get_param_value("isCore") == "true" || req.get_param_value("isCore") == "1";
            std::string position = req.get_param_value("position");

            std::string id = system.addMember(name, phone, email, rollNo, branch, division, year, isCore, position);
            res.set_content("{\"id\":" + jsonString(id) + "}", "application/json");
        } catch (const InvalidIdException& e) {
            sendError(res, 404, e.what());
        } catch (const TRFException& e) {
            sendError(res, 400, e.what());
        }
    });

    svr.Post("/api/members/delete", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            system.deleteMember(req.get_param_value("id"));
            res.set_content("{\"ok\":true}", "application/json");
        } catch (const InvalidIdException& e) {
            sendError(res, 404, e.what());
        } catch (const TRFException& e) {
            sendError(res, 400, e.what());
        }
    });

    // ---------------- lending ----------------
    svr.Post("/api/issue", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string instrumentId = req.get_param_value("instrumentId");
            std::string borrowerName = req.get_param_value("borrowerName");
            std::string borrowerSurname = req.get_param_value("borrowerSurname");
            std::string borrowerBranch = req.get_param_value("borrowerBranch");
            std::string borrowerDivision = req.get_param_value("borrowerDivision");
            std::string borrowerPrn = req.get_param_value("borrowerPrn");
            std::string borrowerEmail = req.get_param_value("borrowerEmail");
            std::string lentById = req.get_param_value("lentById");
            int quantity = intParam(req, "quantity");
            int days = intParam(req, "days");
            Date issueDate = dateParamOrToday(req, "issueDate", system);

            // Overload resolution picks the "borrower could be anyone"
            // version of issueItem() (11 args) over the id-based one (6 args).
            std::string id = system.issueItem(instrumentId, borrowerName, borrowerSurname, borrowerBranch,
                                               borrowerDivision, borrowerPrn, borrowerEmail, lentById,
                                               quantity, days, issueDate);
            res.set_content("{\"id\":" + jsonString(id) + "}", "application/json");
        } catch (const InvalidIdException& e) {
            sendError(res, 404, e.what());
        } catch (const TRFException& e) {
            sendError(res, 400, e.what());
        }
    });

    svr.Post("/api/return", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string recordId = req.get_param_value("recordId");
            Date returnDate = dateParamOrToday(req, "returnDate", system);
            std::string conditionParam = req.get_param_value("condition");
            bool updateCondition = !conditionParam.empty();
            Condition newCondition = updateCondition ? conditionFromString(conditionParam) : Condition::Good;

            system.returnItem(recordId, returnDate, updateCondition, newCondition);
            res.set_content("{\"ok\":true}", "application/json");
        } catch (const InvalidIdException& e) {
            sendError(res, 404, e.what());
        } catch (const TRFException& e) {
            sendError(res, 400, e.what());
        }
    });

    // ---------------- records ----------------
    svr.Get("/api/records", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string status = req.get_param_value("status");
            std::string member = req.get_param_value("member");
            std::string instrument = req.get_param_value("instrument");
            std::vector<LendingRecord*> results = system.getRecords(status, member, instrument);
            res.set_content(toJsonArray(results, system.today(), system), "application/json");
        } catch (const InvalidIdException& e) {
            sendError(res, 404, e.what());
        } catch (const TRFException& e) {
            sendError(res, 400, e.what());
        }
    });

    std::cout << "Server running at http://localhost:8080" << std::endl;
    svr.listen("0.0.0.0", 8080);

    return 0;
}
