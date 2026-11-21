// exam_scheduler.cpp
// University Examination Scheduling System
//  * ExamBST   : Binary Search Tree (hand-written) holding the schedule, ordered by date/time
//  * ExamQueue : Circular-array queue (hand-written) holding exams waiting for a venue (FIFO)
//  * CodeIndex : Hash table (hand-written) mapping course code -> (date, time) for fast search
//  * IdList    : Dynamic array (hand-written) holding the student index numbers of an exam
//  * Venue table: fixed-size array of venues with capacities
// No STL containers (vector, map, queue, ...) are used for the data structures.
//
// Input rules (enforced in the menu AND in the core classes):
//  * Course code : 3-4 letters followed by 3-4 digits (e.g. CSC201)
//  * Title       : 3-60 characters, must contain a letter, no '|' character
//  * Date        : valid YYYY-MM-DD, not in the past
//  * Time        : valid HH:MM (24-hour)
//  * Student ID  : any index number mixing letters and digits (separators / - _ allowed)
//  * Students    : an exam needs at least 1 student; a student cannot have two
//                  exams at the same date and time
//  * Menu choice : whole number, no letters/symbols
//  * Invalid input is re-asked; press Enter on an empty line to cancel.
//  * The schedule is loaded from "exams.txt" at start and saved when you exit.
#include <iostream>
#include <iomanip>
#include <string>
#include <fstream>
#include <ctime>
#include <cctype>
#include <cstdlib>

// ------------------------------------------------------------------ status codes
enum Status {
    OK, ERR_INVALID_INPUT, ERR_DUPLICATE, ERR_NOT_FOUND, ERR_EMPTY,
    ERR_QUEUE_FULL, ERR_VENUE_FULL, ERR_CLASH, ERR_PAST_DATE,
    ERR_STUDENT_EXISTS, ERR_STUDENT_NOT_FOUND, ERR_TOO_MANY_STUDENTS, ERR_LAST_STUDENT
};

inline const char* statusMessage(Status s) {
    switch (s) {
        case OK:                   return "Success.";
        case ERR_INVALID_INPUT:    return "Invalid input.";
        case ERR_DUPLICATE:        return "An examination with that course code already exists.";
        case ERR_NOT_FOUND:        return "No examination with that course code was found.";
        case ERR_EMPTY:            return "The schedule is empty.";
        case ERR_QUEUE_FULL:       return "The pending queue is full (overflow). Allocate venues first.";
        case ERR_VENUE_FULL:       return "The venue table is full (overflow).";
        case ERR_CLASH:            return "Timetable clash: the student already has an exam at that date and time.";
        case ERR_PAST_DATE:        return "The date is in the past.";
        case ERR_STUDENT_EXISTS:   return "That student is already registered for this examination.";
        case ERR_STUDENT_NOT_FOUND:return "That student is not registered for this examination.";
        case ERR_TOO_MANY_STUDENTS:return "Too many students: more than the biggest venue can hold.";
        case ERR_LAST_STUDENT:     return "An examination must keep at least one student. Delete the exam instead.";
    }
    return "Unknown status.";
}

// ------------------------------------------------------------------ helpers
inline std::string toUpper(std::string s) {
    for (size_t i = 0; i < s.size(); ++i) s[i] = (char)std::toupper((unsigned char)s[i]);
    return s;
}

inline std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

inline bool allDigits(const std::string& s, size_t from, size_t to) {
    for (size_t i = from; i < to; ++i)
        if (!std::isdigit((unsigned char)s[i])) return false;
    return true;
}

inline std::string todayDate() {                            // YYYY-MM-DD (system clock)
    std::time_t t = std::time(0);
    std::tm* lt = std::localtime(&t);
    char buf[16];
    std::strftime(buf, sizeof buf, "%Y-%m-%d", lt);
    return std::string(buf);
}

inline bool isValidDate(const std::string& s) {            // YYYY-MM-DD
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
    if (!allDigits(s, 0, 4) || !allDigits(s, 5, 7) || !allDigits(s, 8, 10)) return false;
    int y = std::atoi(s.substr(0, 4).c_str());
    int m = std::atoi(s.substr(5, 2).c_str());
    int d = std::atoi(s.substr(8, 2).c_str());
    if (y < 2000 || y > 2100 || m < 1 || m > 12 || d < 1) return false;
    int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    if (m == 2 && leap) days[1] = 29;
    return d <= days[m - 1];
}

inline bool isValidTime(const std::string& s) {            // HH:MM
    if (s.size() != 5 || s[2] != ':') return false;
    if (!allDigits(s, 0, 2) || !allDigits(s, 3, 5)) return false;
    int h = std::atoi(s.substr(0, 2).c_str());
    int m = std::atoi(s.substr(3, 2).c_str());
    return h >= 0 && h <= 23 && m >= 0 && m <= 59;
}

// Course code: 3-4 letters followed by 3-4 digits (CSC201, INF212, MATH101)
inline bool isValidCode(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && std::isalpha((unsigned char)s[i])) ++i;
    size_t letters = i;
    while (i < s.size() && std::isdigit((unsigned char)s[i])) ++i;
    size_t digits = i - letters;
    return i == s.size() && letters >= 3 && letters <= 4 && digits >= 3 && digits <= 4;
}

// Title: 3-60 printable characters with at least one letter, no '|'
inline bool isValidTitle(const std::string& s) {
    if (s.size() < 3 || s.size() > 60) return false;
    bool hasLetter = false;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = (unsigned char)s[i];
        if (std::iscntrl(c) || c == '|') return false;
        if (std::isalpha(c)) hasLetter = true;
    }
    return hasLetter;
}

// Student index number: flexible format. Any mix of letters and digits, with
// optional separators / - _ (e.g. ET/ICT/24/0070, UCC-2024-0381, PS0234521).
// Must have at least one letter AND one digit; 5-25 characters; no symbol at the
// start/end and no two separators in a row.
inline bool isValidStudentId(const std::string& s) {
    if (s.size() < 5 || s.size() > 25) return false;
    bool hasLetter = false, hasDigit = false, prevSep = true;   // true => blocks leading separator
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = (unsigned char)s[i];
        if (std::isalpha(c))      { hasLetter = true; prevSep = false; }
        else if (std::isdigit(c)) { hasDigit  = true; prevSep = false; }
        else if (c == '/' || c == '-' || c == '_') {
            if (prevSep) return false;
            prevSep = true;
        }
        else return false;
    }
    return !prevSep && hasLetter && hasDigit;                   // !prevSep blocks trailing separator
}

// Strict whole-number parser: digits only, no sign, no leading zero, 1..max
inline bool parsePositiveInt(const std::string& s, int maxValue, int& v) {
    if (s.empty() || s.size() > 9) return false;
    if (s[0] == '0') return false;                    // rejects 0, 00, 007 ...
    if (!allDigits(s, 0, s.size())) return false;
    int n = std::atoi(s.c_str());
    if (n < 1 || n > maxValue) return false;
    v = n;
    return true;
}

// Splits s on `sep` into at most maxParts pieces (the last piece keeps the rest).
inline int splitBy(const std::string& s, char sep, std::string* out, int maxParts) {
    int n = 0;
    std::string cur;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == sep && n < maxParts - 1) { out[n++] = cur; cur.clear(); }
        else cur += s[i];
    }
    out[n++] = cur;
    return n;
}

// ------------------------------------------------------------------ Dynamic array of student IDs
class IdList {
    std::string* items;
    int cap, cnt;

    void grow() {
        int nc = cap * 2;
        std::string* n = new std::string[nc];
        for (int i = 0; i < cnt; ++i) n[i] = items[i];
        delete[] items;
        items = n;
        cap = nc;
    }
public:
    IdList() : items(new std::string[4]), cap(4), cnt(0) {}
    IdList(const IdList& o) : items(new std::string[o.cap]), cap(o.cap), cnt(o.cnt) {
        for (int i = 0; i < cnt; ++i) items[i] = o.items[i];
    }
    IdList& operator=(const IdList& o) {
        if (this != &o) {
            std::string* n = new std::string[o.cap];
            for (int i = 0; i < o.cnt; ++i) n[i] = o.items[i];
            delete[] items;
            items = n;
            cap = o.cap;
            cnt = o.cnt;
        }
        return *this;
    }
    ~IdList() { delete[] items; }

    int size() const { return cnt; }
    const std::string& at(int i) const { return items[i]; }

    bool contains(const std::string& id) const {
        for (int i = 0; i < cnt; ++i) if (items[i] == id) return true;
        return false;
    }
    void add(const std::string& id) {
        if (cnt == cap) grow();
        items[cnt++] = id;
    }
    bool removeId(const std::string& id) {
        for (int i = 0; i < cnt; ++i) {
            if (items[i] == id) {
                for (int j = i; j + 1 < cnt; ++j) items[j] = items[j + 1];
                --cnt;
                return true;
            }
        }
        return false;
    }
};

// ------------------------------------------------------------------ data model
struct Exam {
    std::string code;    // e.g. CSC201 (unique)
    std::string title;   // e.g. Data Structures
    std::string date;    // YYYY-MM-DD
    std::string time;    // HH:MM (24-hour)
    std::string venue;   // empty = not yet allocated
    IdList ids;          // student index numbers registered for this exam
};

struct Venue {
    std::string name;
    int capacity;
};

// Ordering key of the BST: date, then time, then course code
inline int compareExam(const Exam& a, const Exam& b) {
    int c = a.date.compare(b.date);
    if (c != 0) return c;
    c = a.time.compare(b.time);
    if (c != 0) return c;
    return a.code.compare(b.code);
}

// ------------------------------------------------------------------ Binary Search Tree
class ExamBST {
    struct Node {
        Exam data;
        Node *left, *right;
        explicit Node(const Exam& e) : data(e), left(0), right(0) {}
    };

    Node* root;
    int count;

    static void destroy(Node* n) {
        if (!n) return;
        destroy(n->left);
        destroy(n->right);
        delete n;
    }

    static Node* insertNode(Node* n, const Exam& e, bool& inserted) {
        if (!n) { inserted = true; return new Node(e); }
        int c = compareExam(e, n->data);
        if (c < 0)      n->left  = insertNode(n->left,  e, inserted);
        else if (c > 0) n->right = insertNode(n->right, e, inserted);
        return n;                                   // equal key: ignore
    }

    static Node* removeNode(Node* n, const Exam& key, bool& removed) {
        if (!n) return 0;
        int c = compareExam(key, n->data);
        if (c < 0)      n->left  = removeNode(n->left,  key, removed);
        else if (c > 0) n->right = removeNode(n->right, key, removed);
        else {
            removed = true;
            if (!n->left)  { Node* r = n->right; delete n; return r; }   // 0 or 1 child
            if (!n->right) { Node* l = n->left;  delete n; return l; }
            Node* s = n->right;                      // 2 children: in-order successor
            while (s->left) s = s->left;
            n->data = s->data;
            bool dummy = false;
            n->right = removeNode(n->right, n->data, dummy);
        }
        return n;
    }

    static Exam* findKey(Node* n, const Exam& key) {   // O(height) search by (date, time, code)
        while (n) {
            int c = compareExam(key, n->data);
            if (c == 0) return &n->data;
            n = (c < 0) ? n->left : n->right;
        }
        return 0;
    }

    static int heightOf(Node* n) {
        if (!n) return 0;
        int l = heightOf(n->left), r = heightOf(n->right);
        return 1 + (l > r ? l : r);
    }

    template <class F>
    static void walk(Node* n, F& visit) {           // in-order traversal
        if (!n) return;
        walk(n->left, visit);
        visit(n->data);
        walk(n->right, visit);
    }

    // in-order traversal limited to from <= date <= to (skips subtrees outside the range)
    template <class F>
    static void walkRange(Node* n, const std::string& from, const std::string& to, F& visit) {
        if (!n) return;
        const std::string& d = n->data.date;
        if (from <= d) walkRange(n->left, from, to, visit);     // same date can also be on the left
        if (from <= d && d <= to) visit(n->data);
        if (d <= to) walkRange(n->right, from, to, visit);      // ... or on the right
    }

public:
    ExamBST() : root(0), count(0) {}
    ~ExamBST() { destroy(root); }
    ExamBST(const ExamBST&) = delete;
    ExamBST& operator=(const ExamBST&) = delete;

    bool empty() const { return root == 0; }
    int  size()  const { return count; }
    int  height() const { return heightOf(root); }

    bool insert(const Exam& e) {
        bool inserted = false;
        root = insertNode(root, e, inserted);
        if (inserted) ++count;
        return inserted;
    }

    // Removes the exam with this key (date, time, code). Returns false if none exists.
    bool removeByKey(const Exam& key) {
        bool removed = false;
        root = removeNode(root, key, removed);
        if (removed) --count;
        return removed;
    }

    Exam* findByKey(const Exam& key) { return findKey(root, key); }

    template <class F>
    void inorder(F visit) const { walk(root, visit); }

    template <class F>
    void inorderRange(const std::string& from, const std::string& to, F visit) const {
        walkRange(root, from, to, visit);
    }
};

// ------------------------------------------------------------------ Hash table: course code -> (date, time)
class CodeIndex {
    struct Entry {
        std::string code, date, time;
        Entry* next;
    };
    static const int BUCKETS = 211;
    Entry* table[BUCKETS];

    static int hashOf(const std::string& s) {
        unsigned long h = 5381;
        for (size_t i = 0; i < s.size(); ++i) h = h * 33 + (unsigned char)s[i];
        return (int)(h % BUCKETS);
    }
public:
    CodeIndex() { for (int i = 0; i < BUCKETS; ++i) table[i] = 0; }
    ~CodeIndex() {
        for (int i = 0; i < BUCKETS; ++i) {
            Entry* e = table[i];
            while (e) { Entry* nx = e->next; delete e; e = nx; }
        }
    }
    CodeIndex(const CodeIndex&) = delete;
    CodeIndex& operator=(const CodeIndex&) = delete;

    void put(const std::string& code, const std::string& date, const std::string& time) {
        int h = hashOf(code);
        for (Entry* e = table[h]; e; e = e->next)
            if (e->code == code) { e->date = date; e->time = time; return; }
        Entry* n = new Entry;
        n->code = code; n->date = date; n->time = time;
        n->next = table[h];
        table[h] = n;
    }

    bool get(const std::string& code, std::string& date, std::string& time) const {
        for (Entry* e = table[hashOf(code)]; e; e = e->next)
            if (e->code == code) { date = e->date; time = e->time; return true; }
        return false;
    }

    bool erase(const std::string& code) {
        int h = hashOf(code);
        Entry* prev = 0;
        for (Entry* e = table[h]; e; prev = e, e = e->next) {
            if (e->code == code) {
                if (prev) prev->next = e->next; else table[h] = e->next;
                delete e;
                return true;
            }
        }
        return false;
    }
};

// ------------------------------------------------------------------ Circular queue
class ExamQueue {
    std::string* items;
    int capacity, head, count;
public:
    explicit ExamQueue(int cap = 100) : capacity(cap > 0 ? cap : 1), head(0), count(0) {
        items = new std::string[capacity];
    }
    ~ExamQueue() { delete[] items; }
    ExamQueue(const ExamQueue&) = delete;
    ExamQueue& operator=(const ExamQueue&) = delete;

    bool isEmpty() const { return count == 0; }
    bool isFull()  const { return count == capacity; }
    int  size()    const { return count; }
    int  maxSize() const { return capacity; }

    bool enqueue(const std::string& code) {          // false on overflow
        if (isFull()) return false;
        items[(head + count) % capacity] = code;
        ++count;
        return true;
    }

    bool dequeue(std::string& out) {                 // false on underflow
        if (isEmpty()) return false;
        out = items[head];
        head = (head + 1) % capacity;
        --count;
        return true;
    }

    std::string at(int i) const { return items[(head + i) % capacity]; }

    bool remove(const std::string& code) {           // keeps FIFO order of the rest
        bool found = false;
        int n = count;
        for (int i = 0; i < n; ++i) {
            std::string c;
            dequeue(c);
            if (!found && c == code) { found = true; continue; }
            enqueue(c);
        }
        return found;
    }
};

// ------------------------------------------------------------------ Scheduler (facade)
class ExamScheduler {
    static const int MAX_VENUES = 10;

    ExamBST   tree;
    CodeIndex index;                  // course code -> (date, time): O(1) lookup, then O(log n) in the tree
    ExamQueue pending;                // exams waiting for a venue
    Venue     venues[MAX_VENUES];
    int       venueCount;
    std::string detail_;              // extra explanation for the last operation

    Exam* findExam(const std::string& code) {
        Exam key;
        key.code = code;
        if (!index.get(code, key.date, key.time)) return 0;
        return tree.findByKey(key);
    }

    int venueCapacity(const std::string& name) const {
        for (int i = 0; i < venueCount; ++i)
            if (venues[i].name == name) return venues[i].capacity;
        return 0;
    }

    bool venueFree(const std::string& name, const std::string& date, const std::string& time) const {
        bool free = true;
        tree.inorder([&](const Exam& e) {
            if (e.venue == name && e.date == date && e.time == time) free = false;
        });
        return free;
    }

    // Best fit: the smallest venue that is big enough and free at that date/time
    int chooseVenue(const Exam& e) const {
        int best = -1;
        for (int i = 0; i < venueCount; ++i) {
            if (venues[i].capacity < e.ids.size()) continue;
            if (!venueFree(venues[i].name, e.date, e.time)) continue;
            if (best == -1 || venues[i].capacity < venues[best].capacity) best = i;
        }
        return best;
    }

    // true if any student of e already has another exam at the same date/time
    bool firstClash(const Exam& e, const std::string& ignoreCode) {
        for (int i = 0; i < e.ids.size(); ++i) {
            std::string other;
            if (studentBusy(e.ids.at(i), e.date, e.time, ignoreCode, other)) {
                detail_ = "Student " + e.ids.at(i) + " already has exam " + other +
                          " on " + e.date + " at " + e.time + ".";
                return true;
            }
        }
        return false;
    }

public:
    explicit ExamScheduler(int queueCapacity = 100) : pending(queueCapacity), venueCount(0) {
        addVenue("NEC", 500);
        addVenue("LT", 300);
        addVenue("LLT", 200);
        addVenue("CALC", 60);
        addVenue("SWLT", 40);
    }

    const std::string& lastDetail() const { return detail_; }

    // ---- venues
    Status addVenue(const std::string& rawName, int capacity) {
        std::string name = trim(rawName);
        if (name.empty() || capacity <= 0) return ERR_INVALID_INPUT;
        if (venueCount >= MAX_VENUES) return ERR_VENUE_FULL;
        venues[venueCount].name = name;
        venues[venueCount].capacity = capacity;
        ++venueCount;
        return OK;
    }

    int maxCapacity() const {
        int m = 0;
        for (int i = 0; i < venueCount; ++i)
            if (venues[i].capacity > m) m = venues[i].capacity;
        return m;
    }

    // Does this student already have an exam (other than ignoreCode) at that date/time?
    bool studentBusy(const std::string& id, const std::string& date, const std::string& time,
                     const std::string& ignoreCode, std::string& otherCode) const {
        bool busy = false;
        tree.inorder([&](const Exam& e) {
            if (!busy && e.code != ignoreCode && e.date == date && e.time == time && e.ids.contains(id)) {
                busy = true;
                otherCode = e.code;
            }
        });
        return busy;
    }

    // ---- Add examination (needs at least one student). allowPast is only for sample/loaded data.
    Status addExam(const std::string& rawCode, const std::string& rawTitle,
                   const std::string& date, const std::string& time,
                   const IdList& students, bool allowPast = false,
                   const std::string& venueName = "") {
        detail_.clear();
        Exam e;
        e.code  = toUpper(trim(rawCode));
        e.title = trim(rawTitle);
        e.date  = trim(date);
        e.time  = trim(time);
        if (!isValidCode(e.code) || !isValidTitle(e.title) || !isValidDate(e.date) ||
            !isValidTime(e.time) || students.size() < 1)
            return ERR_INVALID_INPUT;
        for (int i = 0; i < students.size(); ++i) {
            std::string id = toUpper(trim(students.at(i)));
            if (!isValidStudentId(id)) return ERR_INVALID_INPUT;
            if (e.ids.contains(id)) return ERR_STUDENT_EXISTS;
            e.ids.add(id);
        }
        if (e.ids.size() > maxCapacity()) return ERR_TOO_MANY_STUDENTS;
        if (!allowPast && e.date < todayDate()) return ERR_PAST_DATE;
        if (findExam(e.code)) return ERR_DUPLICATE;
        if (firstClash(e, "")) return ERR_CLASH;

        if (!venueName.empty() && venueCapacity(venueName) >= e.ids.size() &&
            venueFree(venueName, e.date, e.time))
            e.venue = venueName;                     // restored from a saved file

        if (e.venue.empty() && pending.isFull()) return ERR_QUEUE_FULL;
        tree.insert(e);
        index.put(e.code, e.date, e.time);
        if (e.venue.empty()) pending.enqueue(e.code);   // waits for venue allocation
        return OK;
    }

    // ---- Delete examination
    Status deleteExam(const std::string& rawCode) {
        detail_.clear();
        if (tree.empty()) return ERR_EMPTY;
        std::string code = toUpper(trim(rawCode));
        Exam* cur = findExam(code);
        if (!cur) return ERR_NOT_FOUND;
        Exam key;
        key.code = cur->code; key.date = cur->date; key.time = cur->time;
        tree.removeByKey(key);
        index.erase(code);
        pending.remove(code);
        return OK;
    }

    // ---- Update examination date/time. The key changes, so remove + re-insert.
    Status updateExam(const std::string& rawCode, const std::string& date, const std::string& time) {
        detail_.clear();
        if (tree.empty()) return ERR_EMPTY;
        std::string code = toUpper(trim(rawCode));
        Exam* cur = findExam(code);
        if (!cur) return ERR_NOT_FOUND;
        Exam e = *cur;
        e.date = trim(date);
        e.time = trim(time);
        e.venue.clear();                             // must be re-allocated
        if (!isValidDate(e.date) || !isValidTime(e.time)) return ERR_INVALID_INPUT;
        if (e.date < todayDate()) return ERR_PAST_DATE;
        if (firstClash(e, code)) return ERR_CLASH;
        bool wasAllocated = !cur->venue.empty();
        if (wasAllocated && pending.isFull()) return ERR_QUEUE_FULL;

        Exam key;
        key.code = cur->code; key.date = cur->date; key.time = cur->time;
        tree.removeByKey(key);                       // cur is invalid after this line
        pending.remove(code);
        tree.insert(e);
        index.put(code, e.date, e.time);
        pending.enqueue(code);                       // a slot is free, cannot overflow
        return OK;
    }

    // ---- Students of an examination
    Status addStudent(const std::string& rawCode, const std::string& rawId) {
        detail_.clear();
        if (tree.empty()) return ERR_EMPTY;
        Exam* e = findExam(toUpper(trim(rawCode)));
        if (!e) return ERR_NOT_FOUND;
        std::string id = toUpper(trim(rawId));
        if (!isValidStudentId(id)) return ERR_INVALID_INPUT;
        if (e->ids.contains(id)) return ERR_STUDENT_EXISTS;
        if (e->ids.size() + 1 > maxCapacity()) return ERR_TOO_MANY_STUDENTS;
        std::string other;
        if (studentBusy(id, e->date, e->time, e->code, other)) {
            detail_ = "Student " + id + " already has exam " + other + " on " + e->date +
                      " at " + e->time + ".";
            return ERR_CLASH;
        }
        bool needMove = !e->venue.empty() && venueCapacity(e->venue) < e->ids.size() + 1;
        if (needMove && pending.isFull()) return ERR_QUEUE_FULL;
        e->ids.add(id);
        if (needMove) {
            e->venue.clear();
            pending.enqueue(e->code);
            detail_ = "The venue is now too small, so the exam went back to the pending queue.";
        }
        return OK;
    }

    Status removeStudent(const std::string& rawCode, const std::string& rawId) {
        detail_.clear();
        if (tree.empty()) return ERR_EMPTY;
        Exam* e = findExam(toUpper(trim(rawCode)));
        if (!e) return ERR_NOT_FOUND;
        std::string id = toUpper(trim(rawId));
        if (!e->ids.contains(id)) return ERR_STUDENT_NOT_FOUND;
        if (e->ids.size() == 1) return ERR_LAST_STUDENT;
        e->ids.removeId(id);
        return OK;
    }

    // ---- Search examination
    Status searchExam(const std::string& rawCode, Exam& out) {
        detail_.clear();
        if (tree.empty()) return ERR_EMPTY;
        Exam* e = findExam(toUpper(trim(rawCode)));
        if (!e) return ERR_NOT_FOUND;
        out = *e;
        return OK;
    }

    int searchByDate(const std::string& date, std::ostream& os = std::cout) const {
        return searchByDateRange(date, date, os);
    }

    // all exams with from <= date <= to (in date/time order)
    int searchByDateRange(const std::string& from, const std::string& to,
                          std::ostream& os = std::cout) const {
        int n = 0;
        tree.inorderRange(from, to, [&](const Exam& e) { printRow(e, os); ++n; });
        return n;
    }

    // ---- Allocate examination venues (process the FIFO queue)
    int allocateVenues(std::ostream& os = std::cout) {
        int allocated = 0;
        int n = pending.size();
        for (int i = 0; i < n; ++i) {
            std::string code;
            pending.dequeue(code);
            Exam* e = findExam(code);
            if (!e) continue;
            int v = chooseVenue(*e);
            if (v >= 0) {
                e->venue = venues[v].name;
                os << "  Allocated " << e->code << " (" << e->ids.size() << " students) -> "
                   << e->venue << "\n";
                ++allocated;
            } else {
                os << "  No free venue for " << e->code << " (" << e->ids.size()
                   << " students) on " << e->date << " " << e->time << " - kept in queue\n";
                pending.enqueue(code);
            }
        }
        return allocated;
    }

    // ---- Save / load (plain text, one exam per line)
    bool saveToFile(const std::string& path) const {
        std::ofstream out(path.c_str());
        if (!out) return false;
        tree.inorder([&](const Exam& e) {
            out << "EXAM|" << e.code << "|" << e.title << "|" << e.date << "|" << e.time
                << "|" << e.venue << "|";
            for (int i = 0; i < e.ids.size(); ++i) out << (i ? "," : "") << e.ids.at(i);
            out << "\n";
        });
        return out.good();
    }

    // Returns the number of exams loaded, or -1 if the file does not exist.
    int loadFromFile(const std::string& path, int& skipped) {
        skipped = 0;
        std::ifstream in(path.c_str());
        if (!in) return -1;
        int loaded = 0;
        std::string line;
        while (std::getline(in, line)) {
            line = trim(line);
            if (line.empty()) continue;
            std::string f[7];                         // EXAM|code|title|date|time|venue|ids
            if (splitBy(line, '|', f, 7) != 7 || f[0] != "EXAM") { ++skipped; continue; }
            IdList ids;
            std::string cur;
            for (size_t i = 0; i <= f[6].size(); ++i) {
                if (i == f[6].size() || f[6][i] == ',') {
                    if (!trim(cur).empty()) ids.add(trim(cur));
                    cur.clear();
                } else cur += f[6][i];
            }
            if (addExam(f[1], f[2], f[3], f[4], ids, true, f[5]) == OK) ++loaded;
            else ++skipped;
        }
        return loaded;
    }

    // ---- Display
    static void printHeader(std::ostream& os) {
        os << std::left << std::setw(11) << "Code" << std::setw(30) << "Title"
           << std::setw(12) << "Date" << std::setw(7) << "Time"
           << std::setw(10) << "Students" << "Venue\n"
           << std::string(88, '-') << "\n";
    }

    static void printRow(const Exam& e, std::ostream& os) {
        os << std::left << std::setw(11) << e.code
           << std::setw(30) << e.title.substr(0, 28)
           << std::setw(12) << e.date << std::setw(7) << e.time
           << std::setw(10) << e.ids.size()
           << (e.venue.empty() ? "(pending)" : e.venue) << "\n";
    }

    static void printStudents(const Exam& e, std::ostream& os) {
        os << "Students (" << e.ids.size() << "):\n";
        for (int i = 0; i < e.ids.size(); ++i)
            os << "  " << (i + 1) << ". " << e.ids.at(i) << "\n";
    }

    void displaySchedule(std::ostream& os = std::cout) const {
        if (tree.empty()) { os << "No examinations scheduled.\n"; return; }
        printHeader(os);
        tree.inorder([&](const Exam& e) { printRow(e, os); });
        os << "Total: " << tree.size() << " examination(s)\n";
    }

    void displayQueue(std::ostream& os = std::cout) const {
        if (pending.isEmpty()) { os << "The pending queue is empty.\n"; return; }
        os << "Pending queue (front -> back), " << pending.size() << "/" << pending.maxSize() << ":\n";
        for (int i = 0; i < pending.size(); ++i)
            os << "  " << (i + 1) << ". " << pending.at(i) << "\n";
    }

    void displayVenues(std::ostream& os = std::cout) const {
        os << std::left << std::setw(20) << "Venue" << "Capacity\n" << std::string(30, '-') << "\n";
        for (int i = 0; i < venueCount; ++i)
            os << std::setw(20) << venues[i].name << venues[i].capacity << "\n";
    }

    // ---- information helpers (used by tests)
    int examCount()   const { return tree.size(); }
    int queueSize()   const { return pending.size(); }
    std::string orderedCodes() const {
        std::string s;
        tree.inorder([&](const Exam& e) { if (!s.empty()) s += " "; s += e.code; });
        return s;
    }
};

// ------------------------------------------------------------------ Menu (main)
#ifndef EXAM_NO_MAIN
static const char* DATA_FILE = "exams.txt";

enum Ask { ASK_OK, ASK_CANCEL, ASK_EOF };

static bool readLine(const std::string& prompt, std::string& out) {
    std::cout << prompt;
    if (!std::getline(std::cin, out)) return false;      // end of input
    out = trim(out);
    return true;
}

static void report(Status s, const ExamScheduler& sch) {
    std::cout << (s == OK ? "[OK] " : "[ERROR] ") << statusMessage(s) << "\n";
    if (!sch.lastDetail().empty()) std::cout << "      " << sch.lastDetail() << "\n";
}

// Keeps asking until the value passes `valid`. Empty line = cancel.
static Ask askText(const std::string& prompt, std::string& out,
                   bool (*valid)(const std::string&), const char* hint, bool upper = false) {
    while (true) {
        if (!readLine(prompt, out)) return ASK_EOF;
        if (out.empty()) { std::cout << "Cancelled.\n"; return ASK_CANCEL; }
        if (upper) out = toUpper(out);
        if (valid(out)) return ASK_OK;
        std::cout << "[ERROR] " << hint << " (press Enter on an empty line to cancel)\n";
    }
}

static bool vCode(const std::string& s)  { return isValidCode(s); }
static bool vTitle(const std::string& s) { return isValidTitle(s); }
static bool vDate(const std::string& s)  { return isValidDate(s); }
static bool vStudentId(const std::string& s) { return isValidStudentId(s); }
static bool vTime(const std::string& s)  { return isValidTime(s); }

static const char* H_CODE  = "Course code must be 3-4 letters followed by 3-4 digits (e.g. CSC201)";
static const char* H_TITLE = "Title must be 3-60 characters, contain a letter and not contain '|'";
static const char* H_DATE  = "Enter a real date as YYYY-MM-DD (e.g. 2026-12-07)";
static const char* H_SID   = "Index number must mix letters and digits (5-25 chars), separators / - _ allowed, e.g. ET/ICT/24/0070";
static const char* H_TIME  = "Enter a real time as HH:MM, 24-hour (e.g. 09:00)";

// Reads student index numbers one per line. Empty line finishes (needs at least one).
static Ask askStudentIds(ExamScheduler& sch, const std::string& date, const std::string& time,
                         IdList& ids) {
    std::string line;
    while (true) {
        std::cout << "Student index number #" << (ids.size() + 1)
                  << (ids.size() == 0 ? " (Enter to cancel): " : " (Enter when finished): ");
        if (!std::getline(std::cin, line)) return ASK_EOF;
        line = toUpper(trim(line));
        if (line.empty()) {
            if (ids.size() == 0) { std::cout << "Cancelled.\n"; return ASK_CANCEL; }
            return ASK_OK;
        }
        if (!isValidStudentId(line)) { std::cout << "[ERROR] " << H_SID << "\n"; continue; }
        if (ids.contains(line))      { std::cout << "[ERROR] That index number is already in this list.\n"; continue; }
        std::string other;
        if (sch.studentBusy(line, date, time, "", other)) {
            std::cout << "[ERROR] Clash: " << line << " already has exam " << other
                      << " on " << date << " at " << time << ".\n";
            continue;
        }
        ids.add(line);
        if (ids.size() >= sch.maxCapacity()) {
            std::cout << "Maximum number of students for one exam reached.\n";
            return ASK_OK;
        }
    }
}

static IdList idsOf(const char* a, const char* b = 0, const char* c = 0) {
    IdList l;
    l.add(a);
    if (b) l.add(b);
    if (c) l.add(c);
    return l;
}

static void loadSample(ExamScheduler& sch) {
    int added = 0;
    added += sch.addExam("CSC201", "Data Structures",   "2026-12-07", "09:00",
                         idsOf("ET/ICT/24/0070", "ET/ICT/24/0071", "ET/ICT/24/0072"), true) == OK;
    added += sch.addExam("CSC205", "Operating Systems", "2026-12-07", "09:00",
                         idsOf("ET/ICT/24/0073", "ET/ICT/24/0074"), true) == OK;
    added += sch.addExam("MTH101", "Calculus I",        "2026-12-03", "14:00",
                         idsOf("ET/ICT/24/0070", "ET/ICT/24/0075"), true) == OK;
    added += sch.addExam("PHY110", "General Physics",   "2026-12-03", "09:00",
                         idsOf("ET/ICT/24/0071"), true) == OK;
    added += sch.addExam("ENG102", "Technical Writing", "2026-12-10", "09:00",
                         idsOf("ET/ICT/24/0072", "ET/ICT/24/0073"), true) == OK;
    std::cout << "Sample data loaded (" << added << " added, " << sch.examCount()
              << " examinations in schedule).\n";
}

static void printMenu() {
    std::cout << "\n===== UNIVERSITY EXAMINATION SCHEDULING SYSTEM =====\n"
              << " 1. Add examination\n"
              << " 2. Delete examination\n"
              << " 3. Update examination (date/time)\n"
              << " 4. Add student to examination\n"
              << " 5. Remove student from examination\n"
              << " 6. Search examination (by course code)\n"
              << " 7. Search examinations (by date)\n"
              << " 8. Search examinations (date range)\n"
              << " 9. Display schedule\n"
              << "10. Allocate examination venues\n"
              << "11. Manage examination queue (view pending)\n"
              << "12. Display venues\n"
              << "13. Load sample data\n"
              << "14. Save to file now\n"
              << " 0. Exit (saves automatically)\n";
}

static void run(ExamScheduler& sch) {
    std::string line;
    while (true) {
        printMenu();
        if (!readLine("Choose an option: ", line)) return;

        if (line == "0") { std::cout << "Goodbye.\n"; return; }

        int choice = 0;
        if (!parsePositiveInt(line, 14, choice)) {
            std::cout << "[ERROR] Please enter a number from the menu (0-14).\n";
            continue;
        }

        std::string code, title, date, time, date2, sid;
        Ask a;
        switch (choice) {
        case 1: {
            if ((a = askText("Course code (e.g. CSC201): ", code, vCode, H_CODE, true)) == ASK_EOF) return;
            if (a == ASK_CANCEL) break;
            if ((a = askText("Course title: ", title, vTitle, H_TITLE)) == ASK_EOF) return;
            if (a == ASK_CANCEL) break;
            while (true) {
                if ((a = askText("Date (YYYY-MM-DD): ", date, vDate, H_DATE)) != ASK_OK) break;
                if (date >= todayDate()) break;
                std::cout << "[ERROR] The date is in the past (today is " << todayDate() << ").\n";
            }
            if (a == ASK_EOF) return;
            if (a == ASK_CANCEL) break;
            if ((a = askText("Time (HH:MM, 24-hour): ", time, vTime, H_TIME)) == ASK_EOF) return;
            if (a == ASK_CANCEL) break;
            IdList ids;
            if ((a = askStudentIds(sch, date, time, ids)) == ASK_EOF) return;
            if (a == ASK_CANCEL) break;
            report(sch.addExam(code, title, date, time, ids), sch);
            break;
        }
        case 2:
            if ((a = askText("Course code to delete: ", code, vCode, H_CODE, true)) == ASK_EOF) return;
            if (a == ASK_CANCEL) break;
            report(sch.deleteExam(code), sch);
            break;
        case 3:
            if ((a = askText("Course code to update: ", code, vCode, H_CODE, true)) == ASK_EOF) return;
            if (a == ASK_CANCEL) break;
            while (true) {
                if ((a = askText("New date (YYYY-MM-DD): ", date, vDate, H_DATE)) != ASK_OK) break;
                if (date >= todayDate()) break;
                std::cout << "[ERROR] The date is in the past (today is " << todayDate() << ").\n";
            }
            if (a == ASK_EOF) return;
            if (a == ASK_CANCEL) break;
            if ((a = askText("New time (HH:MM): ", time, vTime, H_TIME)) == ASK_EOF) return;
            if (a == ASK_CANCEL) break;
            report(sch.updateExam(code, date, time), sch);
            break;
        case 4:
            if ((a = askText("Course code: ", code, vCode, H_CODE, true)) == ASK_EOF) return;
            if (a == ASK_CANCEL) break;
            if ((a = askText("Student index number to add: ", sid, vStudentId, H_SID, true)) == ASK_EOF) return;
            if (a == ASK_CANCEL) break;
            report(sch.addStudent(code, sid), sch);
            break;
        case 5:
            if ((a = askText("Course code: ", code, vCode, H_CODE, true)) == ASK_EOF) return;
            if (a == ASK_CANCEL) break;
            if ((a = askText("Student index number to remove: ", sid, vStudentId, H_SID, true)) == ASK_EOF) return;
            if (a == ASK_CANCEL) break;
            report(sch.removeStudent(code, sid), sch);
            break;
        case 6: {
            if ((a = askText("Course code to search: ", code, vCode, H_CODE, true)) == ASK_EOF) return;
            if (a == ASK_CANCEL) break;
            Exam e;
            Status s = sch.searchExam(code, e);
            if (s == OK) {
                ExamScheduler::printHeader(std::cout);
                ExamScheduler::printRow(e, std::cout);
                ExamScheduler::printStudents(e, std::cout);
            } else report(s, sch);
            break;
        }
        case 7:
            if ((a = askText("Date to search (YYYY-MM-DD): ", date, vDate, H_DATE)) == ASK_EOF) return;
            if (a == ASK_CANCEL) break;
            ExamScheduler::printHeader(std::cout);
            if (sch.searchByDate(date) == 0) std::cout << "No examinations on that date.\n";
            break;
        case 8:
            if ((a = askText("From date (YYYY-MM-DD): ", date, vDate, H_DATE)) == ASK_EOF) return;
            if (a == ASK_CANCEL) break;
            while (true) {
                if ((a = askText("To date (YYYY-MM-DD): ", date2, vDate, H_DATE)) != ASK_OK) break;
                if (date2 >= date) break;
                std::cout << "[ERROR] The end date must not be before the start date.\n";
            }
            if (a == ASK_EOF) return;
            if (a == ASK_CANCEL) break;
            ExamScheduler::printHeader(std::cout);
            if (sch.searchByDateRange(date, date2) == 0) std::cout << "No examinations in that range.\n";
            break;
        case 9: sch.displaySchedule(); break;
        case 10:
            if (sch.queueSize() == 0) { std::cout << "Nothing to allocate: the pending queue is empty.\n"; break; }
            {
                int n = sch.allocateVenues();
                std::cout << "Allocated " << n << " examination(s).\n";
            }
            break;
        case 11: sch.displayQueue(); break;
        case 12: sch.displayVenues(); break;
        case 13: loadSample(sch); break;
        case 14:
            std::cout << (sch.saveToFile(DATA_FILE) ? "[OK] Saved to " : "[ERROR] Could not save to ")
                      << DATA_FILE << "\n";
            break;
        default: std::cout << "[ERROR] Unknown option.\n";
        }
    }
}

int main() {
    ExamScheduler sch;
    int skipped = 0;
    int loaded = sch.loadFromFile(DATA_FILE, skipped);
    if (loaded >= 0) {
        std::cout << "Loaded " << loaded << " examination(s) from " << DATA_FILE;
        if (skipped > 0) std::cout << " (" << skipped << " invalid line(s) skipped)";
        std::cout << ".\n";
    }
    run(sch);
    if (!sch.saveToFile(DATA_FILE)) std::cout << "[ERROR] Could not save to " << DATA_FILE << "\n";
    return 0;
}
#endif
