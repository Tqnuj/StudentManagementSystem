#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  using socket_t = SOCKET;
  static const socket_t INVALID_SOCK = INVALID_SOCKET;
  static void closeSocket(socket_t s) { closesocket(s); }
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
  using socket_t = int;
  static const socket_t INVALID_SOCK = -1;
  static void closeSocket(socket_t s) { close(s); }
#endif

namespace fs = std::filesystem;

struct Student {
    int id = 0;
    std::string name, roll, department, email, phone, status;
    int year = 1;
    double gpa = 0.0;
};

struct StorageCheck {
    bool ready = false;
    bool fileExists = false;
    std::vector<std::string> reasons;
    std::vector<Student> students;
};

struct Request {
    std::string method, path, body;
    std::map<std::string, std::string> headers;
};

std::string upper(std::string s);

static fs::path ROOT;
static fs::path DATA_FILE;
static fs::path RECOVERY_GUIDE;
static const std::string CSV_HEADER = "id,name,roll,department,year,email,phone,gpa,status";

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string jsonEscape(const std::string& s) {
    std::ostringstream out;
    for (unsigned char c : s) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) {
                    const char* hex = "0123456789abcdef";
                    out << "\\u00" << hex[c >> 4] << hex[c & 15];
                } else out << static_cast<char>(c);
        }
    }
    return out.str();
}

std::string jsonString(const std::string& s) { return "\"" + jsonEscape(s) + "\""; }

bool parseCsvLine(const std::string& line, std::vector<std::string>& fields) {
    fields.clear();
    std::string field;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') { field += '"'; ++i; }
                else quoted = false;
            } else field += c;
        } else {
            if (c == ',' && !quoted) { fields.push_back(field); field.clear(); }
            else if (c == '"' && field.empty()) quoted = true;
            else field += c;
        }
    }
    if (quoted) return false;
    fields.push_back(field);
    return true;
}

std::string csvEscape(const std::string& s) {
    if (s.find_first_of(",\"\r\n") == std::string::npos) return s;
    std::string out = "\"";
    for (char c : s) { if (c == '"') out += "\"\""; else out += c; }
    return out + "\"";
}

bool parseInt(const std::string& value, int& out) {
    try {
        size_t used = 0;
        long v = std::stol(value, &used);
        if (used != value.size() || v < INT32_MIN || v > INT32_MAX) return false;
        out = static_cast<int>(v); return true;
    } catch (...) { return false; }
}

bool parseDouble(const std::string& value, double& out) {
    try {
        size_t used = 0;
        out = std::stod(value, &used);
        return used == value.size() && std::isfinite(out);
    } catch (...) { return false; }
}

bool hasControlChars(const std::string& value) {
    for (unsigned char c : value) if (c < 0x20 || c == 0x7f) return true;
    return false;
}

bool validEmail(const std::string& email) {
    auto at = email.find('@');
    if (at == std::string::npos || at == 0 || at + 1 >= email.size()) return false;
    auto dot = email.find('.', at + 2);
    return dot != std::string::npos && dot + 1 < email.size() && email.find(' ') == std::string::npos;
}

bool validateStudent(const Student& s, std::string& why, bool checkId = true) {
    if (checkId && s.id <= 0) { why = "Student ID must be a positive number."; return false; }
    if (trim(s.name).empty() || s.name.size() > 80 || hasControlChars(s.name)) { why = "Name is required and must be 80 characters or fewer."; return false; }
    if (trim(s.roll).empty() || s.roll.size() > 30 || hasControlChars(s.roll)) { why = "Roll number is required and must be 30 characters or fewer."; return false; }
    if (trim(s.department).empty() || s.department.size() > 60 || hasControlChars(s.department)) { why = "Department is required and must be 60 characters or fewer."; return false; }
    if (s.year < 1 || s.year > 4) { why = "Year must be between 1 and 4."; return false; }
    if (!validEmail(s.email) || s.email.size() > 120 || hasControlChars(s.email)) { why = "Enter a valid email address."; return false; }
    if (s.phone.size() < 7 || s.phone.size() > 20 || hasControlChars(s.phone)) { why = "Phone number must contain 7 to 20 characters."; return false; }
    if (!std::all_of(s.phone.begin(), s.phone.end(), [](unsigned char c) { return std::isdigit(c) || c == '+' || c == '-' || c == ' ' || c == '(' || c == ')'; })) { why = "Phone number contains unsupported characters."; return false; }
    if (!std::isfinite(s.gpa) || s.gpa < 0 || s.gpa > 10) { why = "GPA must be between 0.0 and 10.0."; return false; }
    if (s.status != "Active" && s.status != "Inactive") { why = "Status must be Active or Inactive."; return false; }
    return true;
}

StorageCheck checkStorage() {
    StorageCheck result;
    std::error_code ec;
    if (!fs::exists(DATA_FILE, ec) || ec) {
        result.reasons.push_back("Required file data/students.csv is missing. The application will not recreate it automatically.");
        return result;
    }
    result.fileExists = true;
    if (!fs::is_regular_file(DATA_FILE, ec) || ec) {
        result.reasons.push_back("data/students.csv exists but is not a regular file.");
        return result;
    }

    std::ifstream in(DATA_FILE, std::ios::binary);
    if (!in) {
        result.reasons.push_back("data/students.csv cannot be opened for reading.");
        return result;
    }

    std::string line;
    if (!std::getline(in, line)) {
        result.reasons.push_back("data/students.csv is empty. Expected the required CSV header.");
        return result;
    }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line != CSV_HEADER) {
        result.reasons.push_back("CSV header does not match the required schema: " + CSV_HEADER);
        return result;
    }

    std::set<int> ids;
    std::set<std::string> rolls;
    std::set<std::string> emails;
    size_t rowNumber = 1;
    while (std::getline(in, line)) {
        ++rowNumber;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (trim(line).empty()) continue;
        std::vector<std::string> f;
        if (!parseCsvLine(line, f) || f.size() != 9) {
            result.reasons.push_back("Row " + std::to_string(rowNumber) + " is malformed; expected 9 CSV columns.");
            continue;
        }
        Student s;
        int id = 0, year = 0; double gpa = 0;
        if (!parseInt(f[0], id) || !parseInt(f[4], year) || !parseDouble(f[7], gpa)) {
            result.reasons.push_back("Row " + std::to_string(rowNumber) + " has an invalid numeric value.");
            continue;
        }
        s.id = id; s.name = f[1]; s.roll = f[2]; s.department = f[3]; s.year = year;
        s.email = f[5]; s.phone = f[6]; s.gpa = gpa; s.status = f[8];
        std::string why;
        if (!validateStudent(s, why)) {
            result.reasons.push_back("Row " + std::to_string(rowNumber) + ": " + why);
            continue;
        }
        if (!ids.insert(s.id).second) {
            result.reasons.push_back("Row " + std::to_string(rowNumber) + " duplicates student ID " + std::to_string(s.id) + ".");
            continue;
        }
        std::string rollKey = lower(trim(s.roll));
        if (!rolls.insert(rollKey).second) {
            result.reasons.push_back("Row " + std::to_string(rowNumber) + " duplicates roll number " + s.roll + ".");
            continue;
        }
        if (!emails.insert(lower(trim(s.email))).second) {
            result.reasons.push_back("Row " + std::to_string(rowNumber) + " duplicates email address " + s.email + ".");
            continue;
        }
        result.students.push_back(s);
    }
    if (in.bad()) result.reasons.push_back("An I/O error occurred while reading data/students.csv.");
    if (!result.reasons.empty()) {
        result.students.clear(); // Do not use partial data when required-file validation fails.
        return result;
    }
    result.ready = true;
    return result;
}

std::string studentToJson(const Student& s) {
    std::ostringstream out;
    out << "{\"id\":" << s.id
        << ",\"name\":" << jsonString(s.name)
        << ",\"roll\":" << jsonString(s.roll)
        << ",\"department\":" << jsonString(s.department)
        << ",\"year\":" << s.year
        << ",\"email\":" << jsonString(s.email)
        << ",\"phone\":" << jsonString(s.phone)
        << ",\"gpa\":" << std::fixed << std::setprecision(2) << s.gpa
        << ",\"status\":" << jsonString(s.status) << "}";
    return out.str();
}

std::string studentsToJson(const std::vector<Student>& students) {
    std::ostringstream out; out << '[';
    for (size_t i = 0; i < students.size(); ++i) { if (i) out << ','; out << studentToJson(students[i]); }
    out << ']'; return out.str();
}

std::string reasonsToJson(const std::vector<std::string>& reasons) {
    std::ostringstream out; out << '[';
    for (size_t i = 0; i < reasons.size(); ++i) { if (i) out << ','; out << jsonString(reasons[i]); }
    out << ']'; return out.str();
}

std::string statusJson(const StorageCheck& check) {
    std::error_code ec;
    bool guideAvailable = fs::is_regular_file(RECOVERY_GUIDE, ec) && !ec;
    std::ostringstream out;
    out << "{\"mode\":" << jsonString(check.ready ? "normal" : "read_only_recovery")
        << ",\"canWrite\":" << (check.ready ? "true" : "false")
        << ",\"valid\":" << (check.ready ? "true" : "false")
        << ",\"filePresent\":" << (check.fileExists ? "true" : "false")
        << ",\"recoveryGuideAvailable\":" << (guideAvailable ? "true" : "false")
        << ",\"studentCount\":" << check.students.size()
        << ",\"reasons\":" << reasonsToJson(check.reasons) << '}';
    return out.str();
}

bool writeStudents(const std::vector<Student>& students, std::string& error) {
    // This is only called after the required data file has passed validation.
    fs::path temp = DATA_FILE; temp += ".tmp";
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) { error = "Could not create a temporary data file. No changes were saved."; return false; }
    out << CSV_HEADER << "\n";
    for (const auto& s : students) {
        out << s.id << ',' << csvEscape(s.name) << ',' << csvEscape(s.roll) << ','
            << csvEscape(s.department) << ',' << s.year << ',' << csvEscape(s.email) << ','
            << csvEscape(s.phone) << ',' << std::fixed << std::setprecision(2) << s.gpa << ','
            << csvEscape(s.status) << "\n";
    }
    out.flush();
    if (!out) { out.close(); std::error_code rm; fs::remove(temp, rm); error = "Could not finish writing the temporary file. No changes were saved."; return false; }
    out.close();
#ifdef _WIN32
    if (!MoveFileExW(temp.c_str(), DATA_FILE.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code rm; fs::remove(temp, rm); error = "Could not safely replace the data file. The original was left untouched."; return false;
    }
#else
    std::error_code ec;
    fs::rename(temp, DATA_FILE, ec);
    if (ec) { std::error_code rm; fs::remove(temp, rm); error = "Could not safely replace the data file: " + ec.message(); return false; }
#endif
    return true;
}

std::string getJsonValue(const std::string& body, const std::string& key, bool& found, bool& isString) {
    found = false; isString = false;
    const std::string needle = "\"" + key + "\"";
    size_t p = body.find(needle);
    if (p == std::string::npos) return "";
    p = body.find(':', p + needle.size());
    if (p == std::string::npos) return "";
    ++p; while (p < body.size() && std::isspace(static_cast<unsigned char>(body[p]))) ++p;
    if (p >= body.size()) return "";
    found = true;
    if (body[p] == '"') {
        isString = true; ++p;
        std::string out;
        while (p < body.size()) {
            char c = body[p++];
            if (c == '"') return out;
            if (c == '\\' && p < body.size()) {
                char e = body[p++];
                switch (e) { case 'n': out += '\n'; break; case 'r': out += '\r'; break; case 't': out += '\t'; break;
                    case 'b': out += '\b'; break; case 'f': out += '\f'; break; default: out += e; break; }
            } else out += c;
        }
        found = false; return "";
    }
    size_t start = p;
    while (p < body.size() && body[p] != ',' && body[p] != '}' && !std::isspace(static_cast<unsigned char>(body[p]))) ++p;
    return body.substr(start, p - start);
}

bool readStudentFromJson(const std::string& body, Student& s, std::string& error, bool requireId) {
    auto getStr = [&](const char* key, std::string& dst, bool required = true) -> bool {
        bool found = false, isString = false; std::string v = getJsonValue(body, key, found, isString);
        if ((!found || !isString) && required) { error = std::string("Missing or invalid field: ") + key + "."; return false; }
        if (found && isString) dst = v;
        return true;
    };
    auto getNum = [&](const char* key, double& dst, bool required = true) -> bool {
        bool found = false, isString = false; std::string v = getJsonValue(body, key, found, isString);
        if (!found || isString) { if (required) error = std::string("Missing or invalid number: ") + key + "."; return !required; }
        if (!parseDouble(v, dst)) { error = std::string("Invalid number: ") + key + "."; return false; }
        return true;
    };
    if (requireId) { double id = 0; if (!getNum("id", id) || std::floor(id) != id || id > INT32_MAX || id < 1) { if (error.empty()) error = "Invalid student ID."; return false; } s.id = static_cast<int>(id); }
    if (!getStr("name", s.name) || !getStr("roll", s.roll) || !getStr("department", s.department) ||
        !getStr("email", s.email) || !getStr("phone", s.phone) || !getStr("status", s.status)) return false;
    double yearValue = 0;
    if (!getNum("year", yearValue) || std::floor(yearValue) != yearValue || yearValue < 1 || yearValue > 4) { if (error.empty()) error = "Year must be an integer from 1 to 4."; return false; }
    s.year = static_cast<int>(yearValue);
    if (!getNum("gpa", s.gpa)) return false;
    if (!validateStudent(s, error, requireId)) return false;
    return true;
}

std::string urlDecode(const std::string& in) {
    std::string out;
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
            auto hexVal = [](char c) -> int { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10; if (c >= 'A' && c <= 'F') return c - 'A' + 10; return -1; };
            int a = hexVal(in[i + 1]), b = hexVal(in[i + 2]);
            if (a >= 0 && b >= 0) { out += static_cast<char>((a << 4) | b); i += 2; }
            else out += in[i];
        } else if (in[i] == '+') out += ' ';
        else out += in[i];
    }
    return out;
}

bool sendAll(socket_t sock, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
#ifdef _WIN32
        int n = send(sock, data + sent, static_cast<int>(std::min<size_t>(len - sent, INT32_MAX)), 0);
#else
        ssize_t n = send(sock, data + sent, len - sent, 0);
#endif
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

std::string reasonPhrase(int status) {
    switch (status) { case 200: return "OK"; case 201: return "Created"; case 400: return "Bad Request";
        case 404: return "Not Found"; case 405: return "Method Not Allowed"; case 409: return "Conflict";
        case 413: return "Payload Too Large"; case 423: return "Locked"; case 500: return "Internal Server Error";
        case 503: return "Service Unavailable"; default: return "OK"; }
}

void respond(socket_t sock, int status, const std::string& body, const std::string& type = "application/json; charset=utf-8") {
    std::ostringstream head;
    head << "HTTP/1.1 " << status << ' ' << reasonPhrase(status) << "\r\n"
         << "Content-Type: " << type << "\r\n"
         << "Content-Length: " << body.size() << "\r\n"
         << "Cache-Control: no-store\r\n"
         << "X-Content-Type-Options: nosniff\r\n"
         << "X-Frame-Options: DENY\r\n"
         << "Referrer-Policy: no-referrer\r\n"
         << "Connection: close\r\n\r\n";
    std::string h = head.str();
    sendAll(sock, h.data(), h.size()); sendAll(sock, body.data(), body.size());
}

void respondError(socket_t sock, int status, const std::string& message) {
    respond(sock, status, "{\"error\":" + jsonString(message) + "}");
}

bool readRequest(socket_t sock, Request& req) {
    std::string raw;
    char buffer[4096];
    size_t headerEnd = std::string::npos;
    while ((headerEnd = raw.find("\r\n\r\n")) == std::string::npos) {
#ifdef _WIN32
        int n = recv(sock, buffer, sizeof(buffer), 0);
#else
        ssize_t n = recv(sock, buffer, sizeof(buffer), 0);
#endif
        if (n <= 0) return false;
        raw.append(buffer, static_cast<size_t>(n));
        if (raw.size() > 65536) return false;
    }
    std::string headerPart = raw.substr(0, headerEnd);
    std::istringstream headers(headerPart);
    std::string firstLine;
    if (!std::getline(headers, firstLine)) return false;
    if (!firstLine.empty() && firstLine.back() == '\r') firstLine.pop_back();
    std::istringstream requestLine(firstLine);
    std::string version;
    if (!(requestLine >> req.method >> req.path >> version)) return false;
    req.method = upper(req.method);
    size_t q = req.path.find('?'); if (q != std::string::npos) req.path.resize(q);
    std::string line;
    while (std::getline(headers, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto sep = line.find(':');
        if (sep != std::string::npos) req.headers[lower(trim(line.substr(0, sep)))] = trim(line.substr(sep + 1));
    }
    size_t contentLength = 0;
    if (req.headers.count("content-length")) {
        try { contentLength = static_cast<size_t>(std::stoul(req.headers["content-length"])); } catch (...) { return false; }
    }
    if (contentLength > 1024 * 1024) return false;
    req.body = raw.substr(headerEnd + 4);
    while (req.body.size() < contentLength) {
#ifdef _WIN32
        int n = recv(sock, buffer, sizeof(buffer), 0);
#else
        ssize_t n = recv(sock, buffer, sizeof(buffer), 0);
#endif
        if (n <= 0) return false;
        req.body.append(buffer, static_cast<size_t>(n));
        if (req.body.size() > contentLength) break;
    }
    if (req.body.size() > contentLength) req.body.resize(contentLength);
    return true;
}

// Defined separately to avoid locale-dependent URL parsing behaviour.
std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

void handleApi(socket_t sock, const Request& req) {
    if (req.path == "/api/status" && req.method == "GET") {
        auto check = checkStorage(); respond(sock, 200, statusJson(check)); return;
    }
    if (req.path == "/api/students" && req.method == "GET") {
        auto check = checkStorage();
        if (!check.ready) { respond(sock, 503, "{\"error\":\"Student data is not ready. The app is in read-only recovery mode.\",\"reasons\":" + reasonsToJson(check.reasons) + "}"); return; }
        respond(sock, 200, studentsToJson(check.students)); return;
    }
    if (req.path == "/api/students" && req.method == "POST") {
        auto check = checkStorage();
        if (!check.ready) { respondError(sock, 423, "Writes are disabled because required data validation/recovery is not complete. See the recovery notice on the dashboard."); return; }
        Student s; std::string err;
        if (!readStudentFromJson(req.body, s, err, false)) { respondError(sock, 400, err); return; }
        for (const auto& old : check.students) {
            if (lower(trim(old.roll)) == lower(trim(s.roll))) { respondError(sock, 409, "That roll number already exists."); return; }
            if (lower(trim(old.email)) == lower(trim(s.email))) { respondError(sock, 409, "That email address already exists."); return; }
        }
        int maxId = 0;
        for (const auto& old : check.students) maxId = std::max(maxId, old.id);
        if (maxId == INT32_MAX) { respondError(sock, 409, "No more student IDs are available in the supported range."); return; }
        s.id = maxId + 1;
        auto updated = check.students; updated.push_back(s);
        if (!writeStudents(updated, err)) { respondError(sock, 500, err); return; }
        respond(sock, 201, studentToJson(s)); return;
    }
    const std::string prefix = "/api/students/";
    if (req.path.rfind(prefix, 0) == 0) {
        std::string idText = req.path.substr(prefix.size()); int id = 0;
        if (!parseInt(idText, id) || id <= 0) { respondError(sock, 400, "Student ID is invalid."); return; }
        auto check = checkStorage();
        if (!check.ready) { respondError(sock, 423, "Writes are disabled because required data validation/recovery is not complete. See the recovery notice on the dashboard."); return; }
        auto it = std::find_if(check.students.begin(), check.students.end(), [&](const Student& s) { return s.id == id; });
        if (it == check.students.end()) { respondError(sock, 404, "Student not found."); return; }
        if (req.method == "PUT") {
            Student replacement; replacement.id = id; std::string err;
            if (!readStudentFromJson(req.body, replacement, err, false)) { respondError(sock, 400, err); return; }
            for (const auto& old : check.students) {
                if (old.id == id) continue;
                if (lower(trim(old.roll)) == lower(trim(replacement.roll))) { respondError(sock, 409, "That roll number already exists."); return; }
                if (lower(trim(old.email)) == lower(trim(replacement.email))) { respondError(sock, 409, "That email address already exists."); return; }
            }
            *it = replacement;
            if (!writeStudents(check.students, err)) { respondError(sock, 500, err); return; }
            respond(sock, 200, studentToJson(replacement)); return;
        }
        if (req.method == "DELETE") {
            check.students.erase(it);
            std::string err;
            if (!writeStudents(check.students, err)) { respondError(sock, 500, err); return; }
            respond(sock, 200, "{\"message\":\"Student deleted.\"}"); return;
        }
        respondError(sock, 405, "Method not allowed for this endpoint."); return;
    }
    respondError(sock, 404, "API endpoint not found.");
}

void serveStatic(socket_t sock, const Request& req) {
    if (req.method != "GET") { respondError(sock, 405, "Only GET is supported for web assets."); return; }
    std::string rel;
    if (req.path == "/" || req.path == "/index.html") rel = "web/index.html";
    else if (req.path == "/styles.css") rel = "web/styles.css";
    else if (req.path == "/app.js") rel = "web/app.js";
    else if (req.path == "/favicon.svg") rel = "web/favicon.svg";
    else if (req.path == "/recovery-guide") rel = "docs/RECOVERY_GUIDE.md";
    else { respondError(sock, 404, "Page not found."); return; }
    fs::path path = ROOT / rel;
    std::ifstream in(path, std::ios::binary);
    if (!in) { respondError(sock, 404, "The requested web file is missing."); return; }
    std::ostringstream contents; contents << in.rdbuf();
    std::string type = "text/plain; charset=utf-8";
    if (rel.size() >= 4 && rel.substr(rel.size() - 4) == ".css") type = "text/css; charset=utf-8";
    if (rel.size() >= 3 && rel.substr(rel.size() - 3) == ".js") type = "application/javascript; charset=utf-8";
    if (rel.size() >= 5 && rel.substr(rel.size() - 5) == ".html") type = "text/html; charset=utf-8";
    if (rel.size() >= 4 && rel.substr(rel.size() - 4) == ".svg") type = "image/svg+xml";
    respond(sock, 200, contents.str(), type);
}

int main(int argc, char** argv) {
    ROOT = fs::current_path();
    if (argc >= 2) ROOT = fs::absolute(argv[1]);
    int port = 8080;
    if (argc >= 3) {
        try { port = std::stoi(argv[2]); } catch (...) { std::cerr << "Invalid port. Use a number from 1 to 65535.\n"; return 1; }
    }
    if (port < 1 || port > 65535) { std::cerr << "Invalid port. Use a number from 1 to 65535.\n"; return 1; }
    DATA_FILE = ROOT / "data" / "students.csv";
    RECOVERY_GUIDE = ROOT / "docs" / "RECOVERY_GUIDE.md";

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { std::cerr << "Could not initialize Windows sockets.\n"; return 1; }
#endif
    socket_t server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == INVALID_SOCK) { std::cerr << "Could not create server socket.\n"; return 1; }
    int opt = 1;
#ifdef _WIN32
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // local machine only
    if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        std::cerr << "Could not bind to 127.0.0.1:" << port << ". Close another app using that port or start with a different port number.\n";
        closeSocket(server);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }
    if (listen(server, 16) != 0) { std::cerr << "Could not start listening.\n"; closeSocket(server); return 1; }

    auto startup = checkStorage();
    std::cout << "\nStudent Management System\n"
              << "-------------------------\n"
              << "Open: http://127.0.0.1:" << port << "\n"
              << "Project folder: " << ROOT.string() << "\n";
    if (startup.ready) std::cout << "Storage: ready (" << startup.students.size() << " students)\n";
    else {
        std::cout << "Storage: READ-ONLY RECOVERY MODE\n";
        for (const auto& why : startup.reasons) std::cout << " - " << why << "\n";
        std::cout << "Normal writes are disabled. Read docs/RECOVERY_GUIDE.md.\n";
    }
    std::cout << "Press Ctrl+C to stop the server.\n\n" << std::flush;

    while (true) {
        sockaddr_in clientAddr{};
#ifdef _WIN32
        int addrLen = sizeof(clientAddr);
#else
        socklen_t addrLen = sizeof(clientAddr);
#endif
        socket_t client = accept(server, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
        if (client == INVALID_SOCK) continue;
        Request req;
        if (!readRequest(client, req)) {
            respondError(client, 400, "Could not read HTTP request. Check that the request is complete and under 1 MB.");
        } else if (req.path.rfind("/api/", 0) == 0) handleApi(client, req);
        else serveStatic(client, req);
        closeSocket(client);
    }

    closeSocket(server);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
