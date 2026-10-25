# VIVA_NOTES.md — TRF Instrument Lending System

This document maps every OOP concept used in this project to exactly where
it lives (file + function), why it was used there, and the deep questions
an examiner is likely to ask about it, with answers. Read the "mandatory
deep topics" section first — those are the questions most likely to expose
a shaky understanding.

---

## 1. Mandatory deep topics

### 1.1 vtable / vptr — how a virtual call actually works

**Where it matters:** every `virtual` function in `Person`, `Instrument`,
and every override in `Member`, `CoreMember`, `ElectronicComponent`, `Tool`,
`PowerDevice`. Concretely exercised in `InventorySystem::issueItem()`
(`src/InventorySystem.cpp`), which calls `inst->getMaxLendDays()` and
`borrower->getExtraDays()` through base pointers (`Instrument*`, `Person*`).

**Mechanism:** when a class has at least one `virtual` function, the
compiler gives every object of that class (and its subclasses) a hidden
pointer, the **vptr**, as (conceptually) the first field of the object.
The vptr points to a per-class table of function pointers, the **vtable**.
There is one vtable per class (not per object), and every object of that
class shares a vptr pointing to the same table. Each subclass that
overrides a virtual function gets its own vtable with that slot replaced;
slots it doesn't override just copy the parent's entry.

A call like `inst->getMaxLendDays()`, where `inst` is `Instrument*`, is
compiled into: follow `inst`'s vptr to its vtable, look up the
`getMaxLendDays` slot, call whatever function pointer is stored there. That
lookup happens **at runtime**, using the vptr that was set (by the
constructor) to match the object's *actual* type - `PowerDevice`,
`ElectronicComponent`, whatever `new` actually built - regardless of what
static type (`Instrument*`) is being used to call through. That's the whole
mechanism behind "runtime polymorphism": the pointer's compile-time type
picks *which vtable slot*, the object's run-time type (via its vptr)
decides *whose function* sits in that slot.

**Follow-up Q: does a non-virtual function use the vtable?**
No. A non-virtual function call is resolved entirely at compile time from
the static type of the pointer/reference/expression - no vptr lookup, no
indirection. That's also why non-virtual "overriding" (hiding a base
function with the same name in a derived class, without `virtual`) doesn't
behave polymorphically: calling it through a base pointer runs the base
version, because the compiler decided which function to call before the
program ever ran.

**Follow-up Q: what's the runtime cost?**
One extra pointer per object (the vptr) and one extra indirection per
virtual call (pointer chase through the vtable). Negligible here.

---

### 1.2 Why virtual destructors

**Where:** `Person::~Person()` (`include/Person.h`) and
`Instrument::~Instrument()` (`include/Instrument.h`), both declared
`virtual`.

**Why it's required here specifically:** `Repository<T>` (`include/Repository.h`)
stores `T*` and, in its own destructor, does `for (T* item : items) delete item;`.
`InventorySystem` holds a `Repository<Person>` and a `Repository<Instrument>`
- meaning every `Member`/`CoreMember` is deleted through a `Person*`, and
every `ElectronicComponent`/`Tool`/`PowerDevice` through an `Instrument*`.

If `~Person()` were *not* virtual, `delete somePersonPtr;` where
`somePersonPtr` actually points at a `CoreMember` would only run
`~Person()` - the compiler picks the destructor to call based on the
**static** type of the pointer (`Person*`), not what it actually points at,
exactly like any other non-virtual call. `~Member()` and `~CoreMember()`
would never run. Here none of them free any extra heap memory of their own,
so nothing technically leaks in this specific project, but skipping part of
an object's destruction chain is undefined behaviour by the C++ standard
regardless - relying on "nothing bad happens to leak" is not a safe
argument, and if a derived class *did* later add a `std::string` member or
similar, its destructor being skipped would matter. Declaring the base
destructor `virtual` makes `delete` go through the vtable, just like any
other virtual call, so the *most-derived* destructor always runs first,
then each base destructor up the chain automatically.

**Follow-up Q: does every class need a virtual destructor?**
No - only classes intended to be deleted **polymorphically** (through a
base pointer), i.e. any class with at least one other virtual function that
might be involved in inheritance and heap deletion through the base type.
`Date` and `LendingRecord` have ordinary (non-virtual) destructors because
neither is ever used through a base pointer - `Date` isn't part of any
hierarchy, and `LendingRecord` objects are always deleted through
`LendingRecord*` (via `Repository<LendingRecord>`), never through some base
type.

---

### 1.3 Object slicing — where it could happen here (and doesn't)

**What it is:** assigning or passing a derived object **by value** into a
variable/parameter of the base type copies only the base-class part -
the derived-only fields are "sliced off", and further calls no longer
dispatch to the derived overrides (the vptr in the copy points at the
base's vtable, because the copy genuinely *is* a base object now, not a
polymorphic view of a derived one).

**Where it's avoided in this project:** every place a `Person` or
`Instrument` is handled polymorphically, it's handled through a **pointer**
(`Person*`, `Instrument*`), never by value. `Repository<T>::add(T* item)`
takes a pointer; `InventorySystem::listMembers()` returns
`std::vector<Person*>`, not `std::vector<Person>`; function parameters like
`Instrument& inst` are never used (this project uses pointers throughout
for polymorphic types, matching "raw base-class pointers... with virtual
destructors" from the spec). Because nothing ever copies a `CoreMember`
into a `Person`-by-value variable, slicing never occurs.

**Where it *would* happen if the code were written differently:**
```cpp
void printRole(Person p) { std::cout << p.getRole(); }   // pass BY VALUE
CoreMember c(...);
printRole(c); // slices! p is a plain Person copy, getRole() prints "Member"
```
or
```cpp
Person p = someCoreMember; // copy-constructs a Person from a CoreMember
```
Both compile (a `CoreMember` *is-a* `Person`), and both silently lose the
`CoreMember`-specific data and behaviour. This project sidesteps the whole
problem by never declaring a function parameter or variable of the *plain*
abstract type `Person`/`Instrument` (which would be impossible anyway,
since they're abstract - but the same slicing risk applies one level down,
e.g. a `Member` parameter receiving a `CoreMember` argument) and always
using pointers instead.

**Follow-up Q: would `Person p = someCoreMember;` even compile, given
`Person` is abstract?**
`Person` itself can never be sliced *into* directly, because you cannot
have a variable of an abstract type at all (abstract classes cannot be
instantiated). But `Member` is concrete, so
`Member m = someCoreMemberObject;` (if `CoreMember` objects were ever
available by value) would compile and slice off `position`. Since this
project only ever creates `Member`/`CoreMember` objects with `new` and
manages them via `Person*`, this situation never arises.

---

### 1.4 Shallow vs deep copy — why Repository disables copying

**Where:** `Repository<T>` (`include/Repository.h`) explicitly deletes its
copy constructor and copy assignment operator:
```cpp
Repository(const Repository&) = delete;
Repository& operator=(const Repository&) = delete;
```

**Shallow copy:** copying an object field-by-field, including raw pointers
- the pointer *value* (the address) is copied, not the object it points
to. Two objects end up pointing at the same underlying memory.

**Deep copy:** copying an object such that any owned resource (heap memory,
file handle, etc.) is duplicated too, so each copy owns an independent
resource.

**Why this matters for `Repository<T>`:** its only data member is
`std::vector<T*> items`. If the compiler-generated copy constructor were
allowed, `Repository<Instrument> backup = originalRepo;` would copy the
`vector<T*>` shallowly - `backup.items` and `originalRepo.items` would hold
the *same* pointer values, both "owning" the same `Instrument` objects.
When `originalRepo` is destroyed, its destructor does
`for (T* item : items) delete item;`, freeing every instrument. When
`backup` is later destroyed, it does the exact same loop over the *same*
pointer values - each already-freed. That's a **double delete**, which is
undefined behaviour (commonly a crash, sometimes silent heap corruption).

Deleting the copy constructor and copy assignment operator (`= delete`)
turns any accidental copy attempt into a **compile-time error** instead of
a runtime crash that might not show up until much later. This is
deliberately chosen over writing a deep-copy version, because
`InventorySystem` only ever needs *one* `Repository<Instrument>`, one
`Repository<Person>`, one `Repository<LendingRecord>` - there is no
legitimate reason to copy a repository in this project, so the safest
option is to forbid it outright.

**Follow-up Q: why not just write a proper deep-copy constructor instead?**
It would work, but it's unnecessary complexity for a feature nothing in
this project uses, and it would need equally careful exception-safety (what
happens if the 5th `new T(*originalItems[5])` throws, after 4 were already
allocated?). Deleting the copy operations is simpler, and the comment in
`Repository.h` explains exactly why, which is more defensible in a viva
than silently relying on the default (shallow) behaviour.

---

### 1.5 Why `operator<<` is a friend, not a member

**Where:** `Date::operator<<` (`include/Date.h` / `src/Date.cpp`) and
`Instrument::operator<<` (`include/Instrument.h` / `src/Instrument.cpp`).

**The core reason:** a member operator is always invoked as
`leftOperand.operator@(rightOperand)` - the left operand **must** be an
object of the class the member belongs to. But the natural, idiomatic way
to print is `std::cout << myDate;`, where the left operand is `std::ostream`,
not `Date`. `Date` doesn't own `ostream` and can't add a member function to
it, so `operator<<` for printing a `Date` has to be a **free (non-member)
function**: `operator<<(std::ostream&, const Date&)`.

That free function still needs to read `Date`'s private members
(`day`/`month`/`year` directly, or in `Instrument`'s case, needs to call
the object's `display()`). A free function normally can't see private
members. Declaring it `friend` **inside** the class grants that one
specific function access to the class's private/protected members, without
making those members public to everyone else.

(`Instrument::operator<<` only actually calls the *public* virtual
`display()`, so technically it doesn't strictly need `friend` access - but
it's declared `friend` anyway, matching the `Date` pattern and keeping the
option open to read private fields directly later without adding more
getters.)

**Follow-up Q: could you write `operator<<` as a member instead, just
awkwardly?**
Only if you were willing to write `myDate << std::cout;`, which no one
does and which would break every generic piece of code that expects
`operator<<` to chain left-to-right (`std::cout << a << b << c;`).
Idiomatic stream insertion is only possible as a free function.

---

### 1.6 Static member initialization

**Where:** `Member::nextIdNumber` (`src/Person.cpp`),
`Instrument::nextIdNumber` (`src/Instrument.cpp`),
`LendingRecord::nextIdNumber` (`src/LendingRecord.cpp`), and the file-path
constants `InventorySystem::INSTRUMENTS_FILE` / `MEMBERS_FILE` /
`RECORDS_FILE` (`src/InventorySystem.cpp`).

**Rule:** a `static` data member is *declared* inside the class body (in
the header), but that declaration does not create storage for it - it just
tells the compiler "this class has a member like this, shared by every
object". The member must be **defined** exactly once, in exactly one `.cpp`
file, which is where its actual storage and initial value live:
```cpp
// Person.h
class Member : public Person {
    static int nextIdNumber;   // declaration only - no storage yet
    ...
};

// Person.cpp
int Member::nextIdNumber = 1;  // definition - this is where storage exists
```
If this definition were missing, the code would compile but fail to
**link**, with an "undefined reference to `TRF::Member::nextIdNumber`"
error - because every `.cpp` file that uses `Member::nextIdNumber` just
sees the declaration and trusts that *some* `.cpp` file provides the
storage.

**Why these particular statics matter functionally:** each is the counter
behind an id-generation scheme (`MEM-001`, `INS-001`, `REC-001`, one
counter per hierarchy root - shared across the whole hierarchy, e.g.
`Member` and `CoreMember` both draw from `Member::nextIdNumber` since they
share one id sequence). Being `static` means there is exactly one counter
per class, not one per object - which is the entire point: every `Member`
and `CoreMember` object needs to agree on "the next free id", so that
counter cannot live inside individual objects.

**Follow-up Q: how does the counter "continue after the highest existing
id" after a restart?**
`InventorySystem::loadMembers()` (and the equivalent for instruments and
records) tracks the highest numeric suffix seen while reading
`members.txt`, then calls `Member::ensureCounterAbove(maxNum)` once after
the loop, which does `if (usedNumber >= nextIdNumber) nextIdNumber = usedNumber + 1;`.
So the static counter's *definition* gives it a starting value of 1, but
`InventorySystem`'s constructor (which runs before any new member is ever
added) fast-forwards it past whatever is already on disk.

---

### 1.7 Abstract class vs interface

C++ has no `interface` keyword (unlike Java/C#) - the closest equivalent is
a class with **only** pure virtual functions and no data. This project
deliberately does *not* try to build "interfaces" that way; both `Person`
and `Instrument` are true **abstract classes**: a mix of pure virtual
functions (no default, subclass *must* implement) and ordinary virtual
functions with real, inherited default behaviour, plus real data members
(`id`, `name`, `phone`, `email` in `Person`; `id`, `name`,
`totalQuantity`, `availableQuantity`, `condition` in `Instrument`).

**Where:** `include/Person.h` (`getRole()`, `getBorrowLimit()`, `canLend()`
are pure virtual - `= 0`; `getExtraDays()`, `display()`, `serialize()`,
`toJson()` are virtual with a real body in `Person.cpp`).
`include/Instrument.h` similarly (`getCategory()`, `getMaxLendDays()`,
`getFinePerDay()` pure virtual; `calculateFine()`, `display()`,
`serialize()`, `toJson()` virtual with defaults).

**Key distinction for the viva:**
- An **abstract class** can hold state (data members), can provide real,
  inherited implementations for some of its virtual functions, and a
  subclass automatically gets all of that "for free" via inheritance. It
  becomes non-instantiable the moment it has *at least one* pure virtual
  function left unimplemented.
- An **interface** (as a language concept, even though C++ doesn't have
  the keyword) is *purely* a contract - no data, no shared implementation,
  every method abstract. Multiple interfaces can typically be "implemented"
  by one class without ambiguity, precisely because there's no shared state
  or shared code to conflict.
- This project uses abstract classes on purpose, and single (not multiple)
  inheritance throughout, which is exactly why there's never any diamond-
  inheritance ambiguity to worry about.

**Follow-up Q: is `Person` "fully abstract" or "partially abstract"?**
Partially: it has 3 pure virtual functions (making it non-instantiable) but
also 4 virtual functions with real bodies that `Member`/`CoreMember` can
either inherit as-is or override. `Member` doesn't override
`getExtraDays()` at all (uses `Person`'s `return 0;` default);
`CoreMember` does override it (`return 3;`). That mix is only possible
because `Person` is an abstract *class*, not a pure interface.

---

### 1.8 Overloading vs overriding

**Overloading** (compile-time / static polymorphism): multiple functions
in the *same* scope with the *same name* but *different parameter lists*.
The compiler decides which one to call by looking at the argument types at
the call site - resolved entirely before the program runs, no vtable
involved.

**Where:** `InventorySystem::searchInstruments(const std::string& keyword)`
and `InventorySystem::searchInstruments(const std::string& keyword, const std::string& category)`
(`include/InventorySystem.h` / `src/InventorySystem.cpp`) - same name, one
takes one argument, the other takes two; `main.cpp`'s
`GET /api/instruments` handler picks which one gets called with a plain
`if`, but the *language-level* overload resolution itself happens at the
call site based on argument count. `Date`'s constructors are another
example: `Date()`, `Date(int,int,int)`, `Date(const std::string&)`,
`Date(const Date&)` - four functions named `Date`, disambiguated purely by
parameter list. `Date::operator+`, `operator-`, `operator<`, etc. are
overloaded operators - same operator symbol, different behaviour based on
operand types (this is "compile-time polymorphism" applied to operators
specifically).

**Overriding** (run-time / dynamic polymorphism): a **derived** class
redefining a **virtual** function it inherited from a base class, with the
*exact same signature*. Which version runs is decided at *runtime*, based
on the object's actual type (via the vtable - see 1.1), not the static
type used to call it.

**Where:** `CoreMember::getRole()` overrides `Member::getRole()` overrides
`Person::getRole()` (three levels); `PowerDevice::calculateFine()`
overrides `Instrument::calculateFine()`.

**Follow-up Q: what happens if a derived class writes a function with the
same name but a *different* parameter list than the base's virtual
function - is that overriding or overloading?**
Neither, cleanly - it's called "hiding". The derived function does not
override the base one (signatures differ), and it **hides** every other
overload of that name from the base class in the derived class's scope (a
derived class's member function names hide, rather than overload with,
same-named base members unless you pull them back in with a `using`
declaration). This project avoids that pitfall by always matching the
exact base signature (and marking every intended override with `override`,
so the compiler itself would reject a signature mismatch as an error,
rather than silently compiling a "hiding" function instead).

---

### 1.9 Why `Repository<T>` (and templates generally) must live entirely in the header

**Where:** the whole of `include/Repository.h` - no `Repository.cpp`
exists.

**Reason:** a class template is not itself compiled code - it's a pattern
the compiler uses to *generate* real code, once per distinct type it's
used with (`Repository<Instrument>`, `Repository<Person>`,
`Repository<LendingRecord>` here). That generation (called
**instantiation**) happens wherever the compiler can see *both* the full
template definition *and* a use of it with a specific type. If
`Repository`'s member function bodies were split into a separate
`Repository.cpp`, the compiler would only ever instantiate
`Repository<Instrument>` etc. inside *that* `.cpp` file (if it even
mentions those types, which it wouldn't). Every *other* `.cpp` file (like
`InventorySystem.cpp`, which actually declares and uses
`Repository<Instrument> instruments;`) would only have the *declaration*
available via the header, with no matching compiled definition anywhere -
a linker error ("undefined reference") for every `Repository<T>` member
function it calls.

Keeping the full definitions in the header means every `.cpp` file that
`#include`s `Repository.h` and uses it with some type `T` triggers its own
instantiation of exactly the `Repository<T>` it needs, right there, with
everything the compiler needs already visible.

**Follow-up Q: doesn't that cause the same code to be compiled multiple
times, once per translation unit?**
Yes, potentially - if two different `.cpp` files both instantiate
`Repository<Instrument>`, each compiles its own copy. The linker
recognises these as duplicate template instantiations and keeps only one
copy in the final binary (this is standard, automatic behaviour - "COMDAT
folding" / weak symbols - not something this project's code needs to
manage manually). In this project it's a non-issue anyway: each
`Repository<T>` type is only ever instantiated once, inside
`InventorySystem.h`/`.cpp`.

---

### 1.10 How exceptions unwind the stack

**Where:** the whole `TRFException` hierarchy (`include/Exceptions.h`),
thrown throughout `InventorySystem.cpp` (e.g. `throw InvalidIdException(...)`
in `requireInstrument()`), caught in `main.cpp`'s route handlers
(`catch (const InvalidIdException&)`, `catch (const TRFException&)`).

**Mechanism:** when a `throw` executes, the runtime stops normal execution
and starts searching *outward*, frame by frame, from the function that
threw, through each function that called it, for a matching `catch` block.
As it leaves each stack frame during that search, it **unwinds** it: every
local object with automatic storage duration that was fully constructed in
that frame has its destructor run, in reverse order of construction,
before the frame is popped. This guarantees resources owned by local
objects (file handles, allocated memory wrapped in an owning object, etc.)
get cleaned up even though the function never reaches its normal `return`.
This continues frame by frame until a `catch` clause whose type matches (or
is a base class of) the thrown object's type is found; execution resumes
there, and the exception object is destroyed once that `catch` block
finishes with it.

**Concretely in this project:** in `InventorySystem::issueItem()`, several
checks (`requireInstrument`, `requirePerson`, the borrow-limit check, the
days check) can each `throw`. None of these functions have local objects
with non-trivial destructors that would need special unwinding behaviour
in this specific code path (no raw `new` sitting in a local variable
waiting to leak) - `issueItem()` deliberately calls `inst->issue(quantity)`
(the only state-mutating, "point of no return" step) **only after every
validation check has already passed**, so if any check throws, nothing has
been mutated yet and there's nothing left half-done to clean up. The
exception simply propagates up through `InventorySystem::issueItem()` \-\>
the `svr.Post("/api/issue", ...)` lambda in `main.cpp`, where it's caught
by `catch (const InvalidIdException&)` or `catch (const TRFException&)` and
turned into an HTTP response.

**Follow-up Q: what if a destructor itself throws during unwinding?**
That's a classic problem case (potentially calling `std::terminate` if it
happens while another exception is already propagating) - which is exactly
why the standard advice is "destructors should not throw", and none of the
destructors in this project (`~Person()`, `~Instrument()`,
`~Repository()`, `~InventorySystem()`) do anything that can throw - they
only run other destructors and `delete` raw pointers, neither of which
throws.

---

### 1.11 `const` member functions

**Where:** almost every getter in the project - `Date::getDay() const`,
`Person::getId() const`, `Instrument::getAvailableQuantity() const`,
`LendingRecord::getStatus(const Date& today) const`, etc. - and several
non-getter functions that still don't modify the object, like
`Instrument::display(std::ostream&) const` and every `toJson()`/
`serialize()`.

**What it means:** appending `const` after a member function's parameter
list is a promise, enforced by the compiler, that calling this function
will not modify the object it's called on (concretely: inside a `const`
member function, `this` has type `const ClassName*` instead of
`ClassName*`, so any attempt to assign to a data member, or call a
non-`const` member function on `*this`, is a compile error). It also means
the function can be called on a `const ClassName&` or `const ClassName*` -
without `const`, calling *any* member function on a `const` reference/
pointer would fail to compile.

**Why it matters here specifically:** `Repository<T>::getAll() const`
returns `const std::vector<T*>&`; several `InventorySystem` methods take
`const Date&` parameters and are themselves `const`
(`InventorySystem::getStats() const`, `InventorySystem::today() const`).
Being able to mark these `const` (and mean it) lets read-only code paths
(like every `GET` route in `main.cpp`) be written against `const
InventorySystem&`-style reasoning, and lets the compiler catch a mistake
(accidentally mutating something inside what was meant to be a read-only
query) as a build error instead of a subtle runtime bug.

**Follow-up Q: is `getAvailableQuantity() const` returning `int` by value a
problem for constness?**
No - `const` only restricts what the function may do to `*this` (the
object it's called on). It says nothing about the returned value, which is
a fresh copy of an `int` the caller now owns outright and can do anything
with.

---

### 1.12 The `this` pointer

**Where used explicitly:** `Repository<T>` isn't the clearest example since
it avoids `this->`; the clearest explicit use is
`Date::operator=(const Date& other)` (`src/Date.cpp`):
```cpp
Date& Date::operator=(const Date& other) {
    if (this == &other) { return *this; }
    ...
    return *this;
}
```
**Where used implicitly, constantly:** every member function that reads a
data member (`day`, `month`, `name`, `condition`, ...) without qualifying
it is implicitly writing `this->day` etc. - the compiler inserts that for
you. Every virtual call made *from inside* a base class function that ends
up running a derived override - e.g. `Person::display(std::ostream& os) const`
calling `getRole()` - is really `this->getRole()`; because `this` at that
point holds the address of the *actual* derived object (a `CoreMember`,
say), the virtual dispatch through `this`'s vptr picks
`CoreMember::getRole()`, which is *why* `Person::display()` can be written
once and still print the correct role for every subclass (see 1.1).

**What it is:** inside any non-static member function, `this` is an
implicit parameter - a pointer to the specific object the function was
called on (`ClassName* this`, or `const ClassName* this` inside a `const`
member function). It's how a single piece of compiled function code
(there's only one `Instrument::display()` function in memory, however many
`Instrument` objects exist) knows *which* object's data to operate on for
any given call.

**Why the explicit `this == &other` self-assignment check in
`Date::operator=`:** guards against `d = d;` (assigning an object to
itself) corrupting data in classes where a naive assignment would, for
example, free a resource on the left-hand side before realizing it was
also needed on the right-hand side (relevant for classes managing owned
heap memory). For `Date` specifically (three plain `int`s) it's not
strictly necessary - self-assignment would just copy each `int` onto
itself harmlessly - but it's included as the standard, defensively-correct
shape of a hand-written `operator=`, and is a natural place to explain the
pattern.

**Follow-up Q: why does `this` not appear as a parameter in the function
signature?**
Because it's implicit - the language inserts it automatically for every
non-`static` member function call (`obj.method(args)` compiles down to
something like `method(&obj, args)`), and you're never allowed to declare
it yourself as a parameter. It only becomes explicit if you choose to
write `this->` or dereference it with `*this`, as `operator=` does above.

---

## 2. The rest of the concept map (file + function + why)

### 2.1 Classes, objects, encapsulation

Every domain class (`Date`, `Person`/`Member`/`CoreMember`,
`Instrument`/`ElectronicComponent`/`Tool`/`PowerDevice`, `LendingRecord`,
`Repository<T>`, `InventorySystem`) keeps its data `private` (or
`protected` where a subclass needs direct access, e.g. `Person`'s fields
so `Member`'s constructor can initialize them via the base constructor
call) and exposes `public` getters and setters. Setters validate and
`throw` on bad input rather than silently accepting garbage - e.g.
`Instrument::setName()` (`src/Instrument.cpp`) throws
`InvalidInputException` on an empty string;
`LendingRecord::setRemarks()` (`src/LendingRecord.cpp`) throws if the
remarks contain the file format's `|` delimiter.

**Why:** encapsulation means every class can enforce its own invariants
(an `Instrument` can never end up with a negative `availableQuantity`, a
`Person` can never end up with an empty name) in exactly one place -
the setter/constructor - rather than trusting every caller everywhere to
remember to validate.

### 2.2 Constructors & destructors

- **Default constructor:** `Date::Date()` reads "today" from `<ctime>`.
- **Parameterized constructors:** `Date::Date(int d, int m, int y)`,
  every domain class's main constructor.
- **Copy constructor:** `Date::Date(const Date& other)`
  (`src/Date.cpp`) - explicitly written out, with a comment on why it's
  required to be examinable even though it does what the compiler-
  generated one would.
- **Constructor overloading:** `Date` has four different constructors
  (default / (d,m,y) / from ISO string / copy); `LendingRecord` has two
  (a "new lending" constructor that computes `dueDate` itself, and a
  "reconstruction" constructor used when loading from file that takes
  every field, including `dueDate`, explicitly).
- **Member initializer lists:** used throughout, e.g.
  `LendingRecord`'s "new lending" constructor
  (`src/LendingRecord.cpp`) computes `dueDate(issueDate + daysLent)`
  **in the initializer list itself**, using `Date::operator+`.
- **Destructors:** `InventorySystem::~InventorySystem()`
  (`src/InventorySystem.cpp`) is written out explicitly (even though it
  does nothing beyond what would happen anyway) specifically to be the
  "destructor of the system class [that] frees memory" the spec asks for
  - in practice the freeing happens by **composition**: its
  `Repository<Instrument>`, `Repository<Person>`, `Repository<LendingRecord>`
  members are plain (non-pointer) fields, so *their* destructors run
  automatically as part of `~InventorySystem()` running, and each of
  those frees every `T*` it owns.

### 2.3 Inheritance

- **Hierarchical:** `Instrument` -\> `ElectronicComponent`, `Tool`,
  `PowerDevice` (three siblings sharing one base) - `include/Instrument.h`.
- **Multilevel:** `Person` -\> `Member` -\> `CoreMember` (a chain, three
  levels deep) - `include/Person.h`. `CoreMember::display()`
  demonstrates the chain concretely: it calls `Member::display(os)`,
  which itself calls `Person::display(os)`, and each level appends its
  own extra fields (`src/Person.cpp`).

### 2.4 Polymorphism

- **Compile-time:** function overloading
  (`InventorySystem::searchInstruments`, see 1.8) and operator
  overloading (`Date`, see 2.5).
- **Run-time:** virtual functions and pure virtual functions/abstract
  classes (`Person`, `Instrument`, see 1.7), virtual destructors (see
  1.2), and every `override` in the subclasses.

### 2.5 Operator overloading in `Date`

`include/Date.h` / `src/Date.cpp`: `operator+(int)` (add days -\> new
`Date`, used by `LendingRecord`'s constructor and by
`InventorySystem::seedIfEmpty()` to build a "12 days ago" date),
`operator-(const Date&)` (days between two dates, returns `long`, used by
`LendingRecord::daysOverdue()`), `operator<`, `operator>`, `operator==`,
`operator<=` (used throughout for due-date/overdue comparisons and
sorting), and `operator<<` as a `friend` (see 1.5).

### 2.6 Static members

- Id-generating counters: `Member::nextIdNumber`, `Instrument::nextIdNumber`,
  `LendingRecord::nextIdNumber` (see 1.6), each with a
  `static std::string generateId()` and
  `static void ensureCounterAbove(int)`.
- Static helpers with no counter/state at all: `Date::isLeapYear(int)` and
  `Date::daysInMonth(int, int)` (`include/Date.h` / `src/Date.cpp`) -
  called as `Date::isLeapYear(y)`, no `Date` object needed, because leap-
  year-ness is a property of a *year*, not of any particular `Date`
  instance.

### 2.7 Friend function

`Date::operator<<` and `Instrument::operator<<`, both declared `friend`
inside their class and defined outside it - see 1.5 for the full
reasoning. `Instrument::operator<<` specifically calls the **virtual**
`inst.display(os)`, so `std::cout << someInstrument` always prints using
whichever subclass's `display()` actually applies (run-time polymorphism
happening *inside* a friend function).

### 2.8 Templates

`Repository<T>` (`include/Repository.h`) - see 1.9 for why it's
header-only. Provides `add`, `findById`, `exists`, `remove`, `getAll`,
`size`, and a `filter` member that is itself a template
(`template <typename Predicate> std::vector<T*> filter(Predicate pred) const`),
letting `InventorySystem` pass arbitrary lambdas
(`instruments.filter([&](const Instrument* i){ ... })`) without
`Repository` needing to know anything `Instrument`-specific.

### 2.9 STL

- `std::vector<T*>` inside `Repository<T>` and everywhere lists of
  instruments/members/records are passed around.
- `std::map<std::string, RecordStatus>` in
  `InventorySystem::getRecords()` (`src/InventorySystem.cpp`) -
  translates the `status` query-string filter ("Issued"/"Overdue"/
  "Returned") into the matching enum value via a lookup, instead of an
  `if`/`else if` chain of string comparisons.
- `std::sort` with a lambda comparator - e.g.
  `InventorySystem::listInstruments()` sorts by id;
  `InventorySystem::getRecords()` sorts by issue date, most recent first,
  using `Date::operator>` inside the comparator lambda.
- `std::find_if` - inside `Repository<T>::findById()`/`remove()`, and in
  `InventorySystem::deleteMember()` to check whether any record still
  references the member being deleted.
- `std::count_if` - `InventorySystem::issueItem()` (counting a borrower's
  current active records against their limit) and
  `InventorySystem::getStats()` (active lending count, overdue count).
- **Lambdas** - the predicate/comparator argument in essentially every
  call above.

### 2.10 Exception handling

Hierarchy in `include/Exceptions.h`: `TRFException : public std::exception`,
with `InvalidIdException`, `InvalidInputException`,
`ItemUnavailableException`, `BorrowLimitExceededException`,
`InvalidOperationException` all deriving from it. Thrown throughout
`InventorySystem.cpp` with a message describing exactly what went wrong;
caught in `main.cpp`'s route handlers and mapped to HTTP status codes:
```cpp
catch (const InvalidIdException& e) { sendError(res, 404, e.what()); }
catch (const TRFException& e)       { sendError(res, 400, e.what()); }
```
Catching the more-derived `InvalidIdException` **before** the more-general
`TRFException` matters: `catch` clauses are tried top-to-bottom, and since
`InvalidIdException` *is a* `TRFException`, a `catch (const TRFException&)`
placed first would swallow it and every other subclass, and the
`InvalidIdException`-specific 404 branch would never run.

### 2.11 File handling

Every domain class has `serialize() const` (`Date` doesn't need one -
`toString()` already gives the pipe-safe textual form used inside other
classes' `serialize()`). `InventorySystem::saveAll() const`
(`src/InventorySystem.cpp`) writes all three files after every mutating
operation; `loadInstruments()`/`loadMembers()`/`loadRecords()` (also in
`InventorySystem.cpp`) parse them back on startup, and
`InventorySystem::seedIfEmpty()` populates fresh demo data if both
`instruments` and `persons` repositories come up empty after loading (i.e.
first run).

### 2.12 Small but examinable things

- **`this` pointer:** see 1.12.
- **`const` member functions:** see 1.11.
- **`enum class`:** `Condition` (`New`/`Good`/`Fair`/`Damaged`,
  `include/Instrument.h`) and `RecordStatus`
  (`Issued`/`Overdue`/`Returned`, `include/LendingRecord.h`). Using
  `enum class` instead of a plain `enum` means values must be written as
  `Condition::Damaged`, not bare `Damaged` - avoiding any name clash (e.g.
  if some other enum also had a `New` value) and preventing accidental
  implicit conversion to `int` (`Condition::Good + 1` is a compile error,
  where a plain `enum`'s value would silently convert).
- **`namespace TRF`:** every single class/function in this project lives
  inside `namespace TRF { ... }`. Keeps this project's names (`Date`,
  `Member`, `Instrument`, ...) from clashing with anything else linked
  into the same program (including `httplib`'s own names, or the standard
  library) - `main.cpp` opts in with `using namespace TRF;` for
  convenience, but every header still qualifies things properly, so
  another file including these headers without that `using` directive
  would still need to write `TRF::Date`, `TRF::Instrument`, etc.

---

## 3. Quick reference: exception hierarchy -\> HTTP status

| Exception thrown                     | HTTP status | Example situation |
|---------------------------------------|-------------|--------------------|
| `InvalidIdException`                  | 404         | Unknown instrument/member/record id |
| `InvalidInputException`               | 400         | Empty name, bad date string, quantity out of range |
| `ItemUnavailableException`            | 400         | Not enough units available, or instrument is Damaged |
| `BorrowLimitExceededException`        | 400         | Borrower already at their active-record limit |
| `InvalidOperationException`           | 400         | Borrower == lender, non-core lender, deleting something in use, returning before the issue date |

All five derive from `TRFException`, which derives from `std::exception` -
so a generic `catch (const std::exception&)` anywhere in the call chain
(there isn't one in this project's routes, but there could be, e.g. around
`main()` itself) would still catch any of them, as a last-resort safety
net.
