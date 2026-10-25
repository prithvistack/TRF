# TRF Robotics Club — Instrument Lending Management System

A web-based system for the TRF Robotics Club to track instruments/equipment
(Arduinos, sensors, motor drivers, soldering irons, multimeters, LiPo
batteries, power supplies, ...) lent out to members. C++17 backend, plain
HTML/CSS/JS frontend, pipe-separated text files for storage. No database,
no login, no JSON library, no multithreading.

## Folder structure

```
include/   Date.h, Exceptions.h, Person.h, Instrument.h, LendingRecord.h,
           Repository.h, InventorySystem.h, JsonUtil.h
src/       Date.cpp, Person.cpp, Instrument.cpp, LendingRecord.cpp,
           InventorySystem.cpp, main.cpp
lib/       httplib.h            (single-header web server library)
web/       index.html, style.css, app.js
data/      instruments.txt, members.txt, records.txt   (auto-created)
build.bat, run.bat
README.md, VIVA_NOTES.md
```

## Requirements

- Windows 10/11
- A **MinGW-w64** g++ (not the old 32-bit MinGW.org compiler) with C++17,
  `std::thread`/`std::mutex` and `<filesystem>` support. This project was
  built and tested with **WinLibs GCC 16.2.0 (posix threads, UCRT
  runtime)**, installed via:
  ```
  winget install -e --id BrechtSanders.WinLibs.POSIX.UCRT
  ```

### A note on this machine's toolchain

This development machine also has an old 32-bit `MinGW.org GCC 6.3.0` at
`C:\MinGW\bin` earlier on PATH, whose standard library has no
`std::thread`/`std::mutex`/`<filesystem>` at all (`Thread model: win32`).
`httplib.h` cannot compile against it. Because of that:

- **`build.bat`** temporarily prepends the WinLibs `mingw64\bin` folder to
  `PATH` for its own process only (it does not touch your system/user
  PATH), so it always picks the right `g++`, regardless of what a plain
  `g++` in your terminal resolves to.
- The build links with `-static -static-libgcc -static-libstdc++`, so the
  resulting `trf_system.exe` does **not** need the WinLibs DLLs on `PATH`
  at runtime. That's why **`run.bat`** needs nothing special - it just runs
  the exe directly.
- If you set up a normal MinGW-w64 install later (e.g. via MSYS2) and put
  it properly on `PATH`, you can simplify `build.bat` back down to the
  plain command from the spec:
  ```
  g++ -std=c++17 -Wall -Wextra -Iinclude -Ilib src\*.cpp -o trf_system.exe -D_WIN32_WINNT=0x0A00 -lws2_32
  ```

If you open a plain terminal and type `g++ --version` and see
"MinGW.org GCC-6.3.0", that is the old compiler - it is harmless to leave
on PATH (this project's scripts route around it), but it cannot be used to
compile this project directly.

## Build

From the project root (`trf-lending-system/`):

```
build.bat
```

This runs g++ with `-std=c++17 -Wall -Wextra` and produces `trf_system.exe`
in the project root. The build must be completely warning-free.

## Run

**Important: run from the project root**, since the program looks for
`web/` (frontend files) and `data/` (storage) relative to the current
directory.

```
run.bat
```

You should see:

```
Server running at http://localhost:8080
```

Open that URL in Chrome. On the very first run, `data/` doesn't exist yet,
so the system seeds itself with demo data: 8 instruments across all three
categories, 5 members (2 of them core members), and 3 lending records (one
already overdue, so the demo isn't empty). To reset back to that seed
state, close the server and delete the `data/` folder.

Stop the server with Ctrl+C in its terminal window.

## Using the site

- **Dashboard** - key numbers (instruments, units available/issued, active
  lendings, overdue count, total fines collected) plus a quick overdue list.
- **Instruments** - add a new instrument (the extra field changes with the
  category: voltage rating / requires-training / capacity), search by name
  and/or category, update an instrument's condition, delete an instrument
  (blocked while any unit of it is issued).
- **Members** - add a regular or core member (core members get a
  "position" and are the only ones who can appear as the lender when
  issuing an item), list members, delete a member (blocked while they hold
  anything or are the active lender on a record).
- **Lend** - issue an instrument to a borrower (approved by a core member),
  or return an already-issued record (fine is calculated automatically;
  you may optionally record the instrument's condition on return).
- **Records** - all lending records, filterable by status
  (Issued/Overdue/Returned), member id, and/or instrument id. This view
  also serves as "history for one member" or "history for one instrument"
  by filtering on that id alone.

Every form shows the C++ layer's own error message directly underneath it
if a request is rejected (e.g. "MEM-002 already holds 3 active item(s);
the limit is 3"). Deletions ask for confirmation first.

## REST API

All responses are JSON, built by hand (no JSON library). Errors are
`{"error":"message"}`; an unknown id returns HTTP 404, any other business
rule violation returns HTTP 400.

| Method | Path                                             | Purpose                      |
|--------|---------------------------------------------------|------------------------------|
| GET    | `/api/stats`                                      | dashboard numbers + today's date |
| GET    | `/api/instruments?q=&category=`                   | list / search instruments    |
| POST   | `/api/instruments`                                | add an instrument             |
| POST   | `/api/instruments/delete`                         | delete an instrument by id    |
| POST   | `/api/instruments/condition`                      | update an instrument's condition |
| GET    | `/api/members`                                    | list members                  |
| POST   | `/api/members`                                    | add a member (`isCore`, `position`) |
| POST   | `/api/members/delete`                             | delete a member by id         |
| GET    | `/api/records?status=&member=&instrument=`        | list / filter lending records |
| POST   | `/api/issue`                                      | issue (lend) an instrument    |
| POST   | `/api/return`                                     | return an instrument          |

POST bodies are `application/x-www-form-urlencoded`.

## Data files

Plain text, `|`-separated, one record per line, in `data/`:

- `instruments.txt` - one line per instrument, tagged `ELECTRONIC`/`TOOL`/`POWER`
- `members.txt` - one line per person, tagged `MEMBER`/`CORE`
- `records.txt` - one line per lending record

The system re-reads all three on startup and rewrites all three after every
change, so restarting the program never loses data. Static id counters
(`INS-xxx`, `MEM-xxx`, `REC-xxx`) resume after the highest id already found
on disk.

## Testing performed

- `build.bat` compiles cleanly with `-Wall -Wextra` (zero warnings).
- Fresh run seeds 8 instruments / 5 members / 3 records (1 pre-seeded as
  overdue).
- Added an instrument and a member through the API.
- Issued an instrument with a past issue date and confirmed it showed as
  Overdue in `/api/records?status=Overdue`.
- Returned it and an already-overdue seeded record; confirmed fines matched
  hand-calculated values, including the `PowerDevice` doubled-rate-past-
  7-days rule (5 late days -> 75.00 at Rs 15/day; 15 late days -> 345.00 =
  7 x 15 + 8 x 30).
- Verified rejected requests: unknown id -> 404, non-core lender, borrower
  == lender, deleting a member/instrument currently in use -> 400 with a
  readable message.
- Restarted the server process and confirmed every add/issue/return from
  the previous run was still present, and that new ids continued from the
  highest existing number rather than restarting at 1.
