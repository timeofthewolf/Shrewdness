#include "net/http.hpp"
#include "savvy/savvy.hpp"
#include "shrewd/asm.hpp"
#include "shrewd/isa.hpp"
#include "shrewd/vm.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using shrewd::Genome;
using Clock = std::chrono::steady_clock;
namespace fs = std::filesystem;

namespace {

std::string json_escape(const std::string &s) {
    std::string o;
    o.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
        case '"':
            o += "\\\"";
            break;
        case '\\':
            o += "\\\\";
            break;
        case '\n':
            o += "\\n";
            break;
        case '\r':
            o += "\\r";
            break;
        case '\t':
            o += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char b[8];
                std::snprintf(b, sizeof b, "\\u%04x", c);
                o += b;
            } else {
                o += c;
            }
        }
    }
    return o;
}

struct JVal {
    enum class T { Null, Bool, Num, Str, Obj, Arr };
    T t = T::Null;
    double num = 0;
    std::string str;
    std::vector<std::string> keys;
    std::vector<JVal> vals;
    std::vector<JVal> arr;

    const JVal *find(const std::string &k) const {
        if (t != T::Obj)
            return nullptr;
        for (std::size_t i = 0; i < keys.size(); ++i)
            if (keys[i] == k)
                return &vals[i];
        return nullptr;
    }
};

struct Json {
    JVal root;

    std::string s(const std::string &k, const std::string &def = "") const {
        const JVal *v = root.find(k);
        return v && v->t == JVal::T::Str ? v->str : def;
    }
    double n(const std::string &k, double def = 0) const {
        const JVal *v = root.find(k);
        if (!v)
            return def;
        if (v->t == JVal::T::Num || v->t == JVal::T::Bool)
            return v->num;
        return def;
    }
    bool has(const std::string &k) const { return root.find(k) != nullptr; }

    static bool parse(const std::string &in, Json &out);
};

struct Cursor {
    const char *p, *e;
    void ws() {
        while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
            ++p;
    }
};

bool parse_jstring(Cursor &c, std::string &out) {
    if (c.p >= c.e || *c.p != '"')
        return false;
    ++c.p;
    while (c.p < c.e) {
        char ch = *c.p++;
        if (ch == '"')
            return true;
        if (ch != '\\') {
            out += ch;
            continue;
        }
        if (c.p >= c.e)
            return false;
        char esc = *c.p++;
        switch (esc) {
        case 'n':
            out += '\n';
            break;
        case 't':
            out += '\t';
            break;
        case 'r':
            out += '\r';
            break;
        case 'b':
            out += '\b';
            break;
        case 'f':
            out += '\f';
            break;
        case '"':
        case '\\':
        case '/':
            out += esc;
            break;
        case 'u': {
            if (c.e - c.p < 4)
                return false;
            unsigned v = 0;
            for (int i = 0; i < 4; ++i) {
                char h = *c.p++;
                v <<= 4;
                if (h >= '0' && h <= '9')
                    v |= static_cast<unsigned>(h - '0');
                else if (h >= 'a' && h <= 'f')
                    v |= static_cast<unsigned>(h - 'a' + 10);
                else if (h >= 'A' && h <= 'F')
                    v |= static_cast<unsigned>(h - 'A' + 10);
                else
                    return false;
            }
            if (v < 0x80) {
                out += static_cast<char>(v);
            } else if (v < 0x800) {
                out += static_cast<char>(0xC0 | (v >> 6));
                out += static_cast<char>(0x80 | (v & 0x3F));
            } else {
                out += static_cast<char>(0xE0 | (v >> 12));
                out += static_cast<char>(0x80 | ((v >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (v & 0x3F));
            }
            break;
        }
        default:
            return false;
        }
    }
    return false;
}

bool parse_jvalue(Cursor &c, JVal &out, int depth) {
    if (depth > 16)
        return false;
    c.ws();
    if (c.p >= c.e)
        return false;
    if (*c.p == '"') {
        out.t = JVal::T::Str;
        return parse_jstring(c, out.str);
    }
    if (*c.p == '{') {
        ++c.p;
        out.t = JVal::T::Obj;
        c.ws();
        if (c.p < c.e && *c.p == '}') {
            ++c.p;
            return true;
        }
        for (;;) {
            c.ws();
            std::string key;
            if (!parse_jstring(c, key))
                return false;
            c.ws();
            if (c.p >= c.e || *c.p != ':')
                return false;
            ++c.p;
            JVal v;
            if (!parse_jvalue(c, v, depth + 1))
                return false;
            out.keys.push_back(std::move(key));
            out.vals.push_back(std::move(v));
            c.ws();
            if (c.p < c.e && *c.p == ',') {
                ++c.p;
                continue;
            }
            if (c.p < c.e && *c.p == '}') {
                ++c.p;
                return true;
            }
            return false;
        }
    }
    if (*c.p == '[') {
        ++c.p;
        out.t = JVal::T::Arr;
        c.ws();
        if (c.p < c.e && *c.p == ']') {
            ++c.p;
            return true;
        }
        for (;;) {
            JVal v;
            if (!parse_jvalue(c, v, depth + 1))
                return false;
            out.arr.push_back(std::move(v));
            c.ws();
            if (c.p < c.e && *c.p == ',') {
                ++c.p;
                continue;
            }
            if (c.p < c.e && *c.p == ']') {
                ++c.p;
                return true;
            }
            return false;
        }
    }
    if (c.e - c.p >= 4 && !std::strncmp(c.p, "true", 4)) {
        out.t = JVal::T::Bool;
        out.num = 1;
        c.p += 4;
        return true;
    }
    if (c.e - c.p >= 5 && !std::strncmp(c.p, "false", 5)) {
        out.t = JVal::T::Bool;
        out.num = 0;
        c.p += 5;
        return true;
    }
    if (c.e - c.p >= 4 && !std::strncmp(c.p, "null", 4)) {
        out.t = JVal::T::Null;
        c.p += 4;
        return true;
    }
    char *end = nullptr;
    out.num = std::strtod(c.p, &end);
    if (end == c.p || end > c.e)
        return false;
    out.t = JVal::T::Num;
    c.p = end;
    return true;
}

bool Json::parse(const std::string &in, Json &out) {
    Cursor c{in.data(), in.data() + in.size()};
    if (!parse_jvalue(c, out.root, 0))
        return false;
    c.ws();
    return c.p == c.e && out.root.t == JVal::T::Obj;
}

struct BuildErr {
    std::string text;
    std::string file;
    std::size_t line = 0, col = 0;
};

std::string error_json(const BuildErr &e) {
    std::string o = "{\"ok\":false,\"error\":\"" + json_escape(e.text) + "\"";
    if (!e.file.empty())
        o += ",\"file\":\"" + json_escape(e.file) + "\"";
    if (e.line) {
        o += ",\"line\":" + std::to_string(e.line);
        o += ",\"col\":" + std::to_string(e.col ? e.col : 1);
    }
    o += "}";
    return o;
}

bool ends_with(const std::string &s, const char *suffix) {
    const std::size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

std::string fmt_from_name(const std::string &name) {
    if (ends_with(name, ".asm"))
        return "asm";
    if (ends_with(name, ".shrewd"))
        return "genes";
    return "savvy";
}

bool build_genome(const Json &j, const std::string &fb_src,
                  const std::string &fb_fmt, Genome &out, BuildErr &err) {
    std::map<std::string, std::string> files;
    if (const JVal *f = j.root.find("files"); f && f->t == JVal::T::Obj)
        for (std::size_t i = 0; i < f->keys.size(); ++i)
            if (f->vals[i].t == JVal::T::Str)
                files[f->keys[i]] = f->vals[i].str;

    std::string entry = j.s("entry");
    std::string fmt = j.s("fmt", fb_fmt);
    std::string src = j.s("src", fb_src);

    if (!files.empty()) {
        if (entry.empty() && files.size() == 1)
            entry = files.begin()->first;
        const auto it = files.find(entry);
        if (it == files.end()) {
            err.text = "entry file \"" + entry + "\" is not among the files";
            return false;
        }
        src = it->second;
        if (fmt.empty())
            fmt = fmt_from_name(entry);
    }
    if (fmt.empty())
        fmt = "savvy";

    if (fmt == "genes") {
        out.clear();
        const char *p = src.c_str();
        while (*p) {
            for (;;) {
                while (*p && (std::isspace(static_cast<unsigned char>(*p)) ||
                              *p == ',' || *p == '[' || *p == ']'))
                    ++p;
                if (*p == ';' || *p == '#' || (*p == '/' && *(p + 1) == '/')) {
                    while (*p && *p != '\n')
                        ++p;
                    continue;
                }
                break;
            }
            if (!*p)
                break;
            char *end = nullptr;
            long v = std::strtol(p, &end, 10);
            if (end == p) {
                err.text = "not a valid gene list";
                err.file = entry;
                return false;
            }
            out.push_back(static_cast<shrewd::Gene>(v));
            p = end;
        }
        if (out.empty()) {
            err.text = "empty gene list";
            err.file = entry;
            return false;
        }
        return true;
    }

    if (fmt == "asm") {
        std::string e;
        auto g = shrewd::assemble(src, &e);
        if (!g) {
            err.text = e;
            err.file = entry;
            if (e.compare(0, 5, "line ") == 0)
                err.line = std::strtoul(e.c_str() + 5, nullptr, 10);
            return false;
        }
        out = *g;
        return true;
    }

    savvy::Options opts;
    opts.entry = entry.empty() ? "<editor>" : entry;
    if (!files.empty()) {
        opts.resolver =
            [&files](const std::string &name,
                     const std::string &) -> std::optional<savvy::Source> {
            auto it = files.find(name);
            if (it == files.end())
                it = files.find(name + ".savvy");
            if (it == files.end())
                return std::nullopt;
            return savvy::Source{it->first, it->second};
        };
    }
    savvy::Error e;
    auto g = savvy::compile(src, opts, &e);
    if (!g) {
        err.text = e.format();
        err.file = e.file == "<editor>" ? "" : e.file;
        err.line = e.line;
        err.col = e.column;
        return false;
    }
    out = *g;
    return true;
}

std::string genome_views(const Genome &g) {
    std::string o = "\"len\":" + std::to_string(g.size());
    o += ",\"genes\":\"" + json_escape(shrewd::write_genes(g));
    o += "\",\"asm\":\"" + json_escape(shrewd::to_assembly(g));
    o += "\",\"savvy\":\"" + json_escape(savvy::decompile(g));
    o += '"';
    return o;
}

http::Response json_response(std::string body, int status = 200) {
    http::Response r;
    r.status = status;
    r.content_type = "application/json";
    r.body = std::move(body);
    return r;
}

http::Response json_error(const std::string &msg, int status = 400) {
    return json_response(
        "{\"ok\":false,\"error\":\"" + json_escape(msg) + "\"}", status);
}

http::Response tool_trace(const Json &j, std::uint64_t mem_max) {
    Genome g;
    BuildErr err;
    if (!build_genome(j, "", "", g, err))
        return json_response(error_json(err), 400);

    auto clamp_u = [](double v, std::uint64_t def, std::uint64_t hi) {
        if (v <= 0)
            return def;
        std::uint64_t u = static_cast<std::uint64_t>(v);
        return u > hi ? hi : u;
    };
    const std::size_t cap =
        static_cast<std::size_t>(clamp_u(j.n("cap"), 4000, 20000));

    shrewd::Limits L;
    L.max_steps = cap;
    L.memory_size = clamp_u(j.n("mem"), 4096, mem_max);
    L.stack_limit = 100'000;
    L.output_limit = 1u << 16;
    L.max_call_depth = 100'000;
    L.max_child_genes = 200'000;
    L.max_offspring = 16;

    std::vector<shrewd::Value> input;
    for (char ch : j.s("input"))
        input.push_back(static_cast<unsigned char>(ch));

    std::vector<shrewd::TraceStep> trace;
    shrewd::VM vm(L);
    vm.set_trace(&trace, cap, 48);
    const shrewd::Result r =
        vm.run(g, input, static_cast<std::uint64_t>(j.n("seed")));
    vm.set_trace(nullptr);

    std::string o = "{\"ok\":true,\"len\":" + std::to_string(g.size());
    o += ",\"halt\":\"";
    o += shrewd::to_string(r.halt);
    o += "\",\"steps\":" + std::to_string(r.steps);
    o += ",\"output\":[";
    for (std::size_t i = 0; i < r.output.size(); ++i) {
        if (i)
            o += ',';
        o += std::to_string(
            static_cast<int>(static_cast<unsigned char>(r.output[i])));
    }
    o += "]";

    o += ",\"code\":[";
    for (std::size_t pc = 0; pc < g.size(); ++pc) {
        if (pc)
            o += ',';
        const shrewd::Op op = shrewd::decode(g[pc]);
        o += "{\"pc\":" + std::to_string(pc) +
             ",\"op\":" + std::to_string(static_cast<int>(op)) +
             ",\"name\":\"" + std::string(shrewd::info(op).mnemonic) + "\"";
        if (op == shrewd::Op::PUSHI && pc + 1 < g.size())
            o +=
                ",\"arg\":" + std::to_string(static_cast<long long>(g[pc + 1]));
        o += "}";
    }
    o += "],\"trace\":[";
    for (std::size_t s = 0; s < trace.size(); ++s) {
        const shrewd::TraceStep &t = trace[s];
        if (s)
            o += ',';
        o += "{\"pc\":" + std::to_string(t.pc) +
             ",\"d\":" + std::to_string(t.depth) +
             ",\"out\":" + std::to_string(t.out_len) + ",\"r\":[";
        for (int i = 0; i < shrewd::kRegisterCount; ++i) {
            if (i)
                o += ',';
            o += std::to_string(static_cast<long long>(t.regs[i]));
        }
        o += "],\"s\":[";
        for (std::size_t i = 0; i < t.stack.size(); ++i) {
            if (i)
                o += ',';
            o += std::to_string(static_cast<long long>(t.stack[i]));
        }
        o += "]";
        if (t.wrote_addr >= 0)
            o += ",\"wa\":" + std::to_string(t.wrote_addr) + ",\"wv\":" +
                 std::to_string(static_cast<long long>(t.wrote_val));
        o += "}";
    }
    o += "]}";
    return json_response(std::move(o));
}

struct Caps {
    std::uint64_t console_steps_default = 200'000'000;
    std::uint64_t console_steps_max = 1'000'000'000;
    std::uint64_t console_mem_max = 1u << 22;
    std::uint64_t console_output_max = 1u << 20;
    std::uint64_t trace_mem_max = 1u << 20;
    int runs_per_client = 4;
    int runs_global = 16;
    bool rate_limit = false;
};

std::string random_id() {
    static std::mutex m;
    static std::mt19937_64 rng([] {
        std::random_device rd;
        return (static_cast<std::uint64_t>(rd()) << 32) ^ rd() ^
               static_cast<std::uint64_t>(
                   Clock::now().time_since_epoch().count());
    }());
    std::uint64_t a, b;
    {
        std::lock_guard<std::mutex> lk(m);
        a = rng();
        b = rng();
    }
    char buf[33];
    std::snprintf(buf, sizeof buf, "%016llx%016llx",
                  static_cast<unsigned long long>(a),
                  static_cast<unsigned long long>(b));
    return std::string(buf, 32);
}

bool valid_id(const std::string &s) {
    if (s.size() != 32)
        return false;
    for (char c : s)
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            return false;
    return true;
}

class RateLimiter {
  public:
    struct Budget {
        double normal_burst, normal_per_sec, heavy_burst, heavy_per_sec;
    };
    static constexpr Budget kByCookie{240, 20, 12, 0.5};
    static constexpr Budget kByAddress{800, 60, 40, 2.0};

    bool allow(const std::string &cookie, const std::string &addr, bool heavy) {
        const std::string ckey = "c:" + cookie;
        const std::string akey = "a:" + addr;
        const auto now = Clock::now();

        std::lock_guard<std::mutex> lk(mtx_);
        if (map_.size() > 20000)
            sweep_locked(now);

        refill_locked(ckey, kByCookie, now);
        refill_locked(akey, kByAddress, now);
        Entry &ec = map_.at(ckey);
        Entry &ea = map_.at(akey);

        double &bc = heavy ? ec.heavy : ec.normal;
        double &ba = heavy ? ea.heavy : ea.normal;
        if (bc < 1.0 || ba < 1.0)
            return false;
        bc -= 1.0;
        ba -= 1.0;
        return true;
    }

  private:
    struct Entry {
        double normal = 0, heavy = 0;
        Clock::time_point last{};
        const Budget *budget = nullptr;
    };

    void refill_locked(const std::string &key, const Budget &b,
                       Clock::time_point now) {
        Entry &e = map_[key];
        if (!e.budget) {
            e.budget = &b;
            e.normal = b.normal_burst;
            e.heavy = b.heavy_burst;
            e.last = now;
            return;
        }
        const double dt = std::chrono::duration<double>(now - e.last).count();
        e.last = now;
        e.normal = std::min(b.normal_burst, e.normal + dt * b.normal_per_sec);
        e.heavy = std::min(b.heavy_burst, e.heavy + dt * b.heavy_per_sec);
    }

    void sweep_locked(Clock::time_point now) {
        for (auto it = map_.begin(); it != map_.end();) {
            const double idle =
                std::chrono::duration<double>(now - it->second.last).count();
            it = idle > 900 ? map_.erase(it) : std::next(it);
        }
    }

    std::mutex mtx_;
    std::unordered_map<std::string, Entry> map_;
};

class ConsoleSessions {
  public:
    struct Session {
        std::uint64_t id = 0;
        std::string owner;
        std::thread th;
        std::mutex m;
        std::condition_variable cv;
        std::string out;
        std::deque<shrewd::Value> in;
        bool waiting = false;
        bool done = false;
        bool killed = false;
        shrewd::Result res;
        std::size_t out_cap = 1u << 20;
        Clock::time_point touched = Clock::now();
    };

    enum class Refusal { None, PerClient, Global };

    class Io final : public shrewd::Io {
      public:
        explicit Io(Session &s) : s_(s) {}
        shrewd::Value read() override {
            std::unique_lock<std::mutex> lk(s_.m);
            s_.waiting = true;
            s_.cv.wait(lk, [&] { return !s_.in.empty() || s_.killed; });
            s_.waiting = false;
            if (s_.killed)
                return shrewd::kEndOfInput;
            shrewd::Value v = s_.in.front();
            s_.in.pop_front();
            return v;
        }
        void put(char c) override {
            std::lock_guard<std::mutex> lk(s_.m);
            if (s_.out.size() < s_.out_cap)
                s_.out.push_back(c);
        }

      private:
        Session &s_;
    };

    ~ConsoleSessions() {
        std::lock_guard<std::mutex> lk(mtx_);
        for (auto &[id, s] : map_) {
            {
                std::lock_guard<std::mutex> slk(s->m);
                s->killed = true;
            }
            s->cv.notify_all();
            if (s->th.joinable())
                s->th.join();
        }
    }

    ConsoleSessions(int per_client, int global)
        : per_client_(per_client), global_(global) {}

    std::uint64_t start(const std::string &owner, Genome g, shrewd::Limits L,
                        std::uint64_t seed, std::size_t out_cap,
                        Refusal &why) {
        std::lock_guard<std::mutex> lk(mtx_);
        sweep_locked();
        int alive = 0, mine = 0;
        for (auto &[id, s] : map_) {
            std::lock_guard<std::mutex> slk(s->m);
            if (s->done)
                continue;
            ++alive;
            if (s->owner == owner)
                ++mine;
        }
        if (mine >= per_client_) {
            why = Refusal::PerClient;
            return 0;
        }
        if (alive >= global_) {
            why = Refusal::Global;
            return 0;
        }
        why = Refusal::None;
        auto s = std::make_unique<Session>();
        s->id = next_++;
        s->owner = owner;
        s->out_cap = out_cap;
        Session *raw = s.get();
        s->th = std::thread([raw, g = std::move(g), L, seed] {
            shrewd::VM vm(L);
            Io io(*raw);
            shrewd::Result r = vm.run(g, io, seed);
            std::lock_guard<std::mutex> slk(raw->m);
            raw->res = std::move(r);
            raw->done = true;
        });
        std::uint64_t id = s->id;
        map_.emplace(id, std::move(s));
        return id;
    }

    std::string poll_json(const std::string &owner, std::uint64_t id,
                          std::size_t off) {
        std::lock_guard<std::mutex> lk(mtx_);
        sweep_locked();
        auto it = map_.find(id);
        if (it == map_.end() || it->second->owner != owner)
            return "{\"found\":false}";
        Session &s = *it->second;
        std::lock_guard<std::mutex> slk(s.m);
        s.touched = Clock::now();
        const char *state = s.done                      ? "done"
                            : s.waiting && s.in.empty() ? "waiting"
                                                        : "running";
        std::string o = "{\"found\":true,\"state\":\"";
        o += state;
        o += "\",\"off\":" + std::to_string(s.out.size());
        o += ",\"output\":\"";
        if (off < s.out.size())
            o += json_escape(s.out.substr(off));
        o += '"';
        if (s.done) {
            const shrewd::Result &r = s.res;
            o += ",\"steps\":" + std::to_string(r.steps);
            o += ",\"halt\":\"";
            o += shrewd::to_string(r.halt);
            o += "\",\"inputs_read\":" + std::to_string(r.inputs_read);
            o += ",\"registers\":[";
            for (int i = 0; i < shrewd::kRegisterCount; ++i) {
                if (i)
                    o += ',';
                o += std::to_string(r.registers[i]);
            }
            o += "],\"stack\":[";
            const std::size_t first =
                r.stack.size() > 64 ? r.stack.size() - 64 : 0;
            for (std::size_t i = first; i < r.stack.size(); ++i) {
                if (i != first)
                    o += ',';
                o += std::to_string(r.stack[i]);
            }
            o += "],\"stack_size\":" + std::to_string(r.stack.size());
            o +=
                ",\"uncommitted\":" + std::to_string(r.uncommitted_child_genes);
            o += ",\"offspring\":[";
            for (std::size_t i = 0; i < r.offspring.size(); ++i) {
                if (i)
                    o += ',';
                o += '{';
                o += genome_views(r.offspring[i]);
                o += '}';
            }
            o += ']';
        }
        o += '}';
        return o;
    }

    bool input(const std::string &owner, std::uint64_t id,
               const std::string &text, bool eof) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = map_.find(id);
        if (it == map_.end() || it->second->owner != owner)
            return false;
        Session &s = *it->second;
        {
            std::lock_guard<std::mutex> slk(s.m);
            s.touched = Clock::now();
            for (char c : text)
                s.in.push_back(static_cast<unsigned char>(c));
            if (eof)
                s.in.push_back(shrewd::kEndOfInput);
        }
        s.cv.notify_all();
        return true;
    }

    bool kill(const std::string &owner, std::uint64_t id) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = map_.find(id);
        if (it == map_.end() || it->second->owner != owner)
            return false;
        {
            std::lock_guard<std::mutex> slk(it->second->m);
            it->second->killed = true;
        }
        it->second->cv.notify_all();
        return true;
    }

  private:
    void sweep_locked() {
        const auto now = Clock::now();
        for (auto it = map_.begin(); it != map_.end();) {
            Session &s = *it->second;
            bool erase = false;
            {
                std::lock_guard<std::mutex> slk(s.m);
                const double idle =
                    std::chrono::duration<double>(now - s.touched).count();
                if (s.done && idle > 60)
                    erase = true;
                else if (!s.done && idle > 300) {
                    s.killed = true;
                    s.cv.notify_all();
                }
            }
            if (erase) {
                if (s.th.joinable())
                    s.th.join();
                it = map_.erase(it);
            } else {
                ++it;
            }
        }
    }

    int per_client_ = 2;
    int global_ = 8;
    std::mutex mtx_;
    std::map<std::uint64_t, std::unique_ptr<Session>> map_;
    std::uint64_t next_ = 1;
};

const char *kFallbackPage =
    "<!doctype html><meta charset=utf-8><title>Shrewdness</title>"
    "<body style='font:16px/1.6 system-ui;max-width:40rem;margin:4rem auto;"
    "color:#ddd;background:#111'>"
    "<h1>The backend is running</h1>"
    "<p>The API is live on <code>/api/build</code>, but the editor has not "
    "been built. Either:</p>"
    "<pre>cd web && npm install && npm run build</pre>"
    "<p>and reload, or run the whole thing with Docker:</p>"
    "<pre>docker compose up --build</pre>";

const char *mime_for(const std::string &path) {
    auto ends = [&](const char *s) {
        std::size_t n = std::strlen(s);
        return path.size() >= n && path.compare(path.size() - n, n, s) == 0;
    };
    if (ends(".html"))
        return "text/html; charset=utf-8";
    if (ends(".js"))
        return "application/javascript";
    if (ends(".css"))
        return "text/css";
    if (ends(".svg"))
        return "image/svg+xml";
    if (ends(".json") || ends(".map"))
        return "application/json";
    if (ends(".png"))
        return "image/png";
    if (ends(".ico"))
        return "image/x-icon";
    if (ends(".woff2"))
        return "font/woff2";
    if (ends(".txt"))
        return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

bool read_file(const fs::path &p, std::string &out) {
    std::ifstream f(p, std::ios::binary);
    if (!f)
        return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

http::Response serve_static(const std::string &root,
                            const std::string &req_path) {
    http::Response r;
    if (root.empty()) {
        r.content_type = "text/html; charset=utf-8";
        r.body = kFallbackPage;
        return r;
    }
    std::string rel = req_path == "/" ? "/index.html" : req_path;
    if (rel.find("..") != std::string::npos)
        rel = "/index.html";
    fs::path p = fs::path(root) / rel.substr(1);
    std::error_code ec;
    if (!fs::is_regular_file(p, ec)) {
        if (rel.find('.') != std::string::npos) {
            r.status = 404;
            r.body = "not found";
            return r;
        }
        p = fs::path(root) / "index.html";
    }
    if (!read_file(p, r.body)) {
        r.status = 404;
        r.body = "not found";
        return r;
    }
    r.content_type = mime_for(p.string());
    if (rel.compare(0, 8, "/assets/") == 0)
        r.cache_control = "public, max-age=31536000, immutable";
    else if (rel.compare(0, 7, "/logos/") == 0)
        r.cache_control = "public, max-age=86400";
    else
        r.cache_control = "no-cache";
    return r;
}

std::string find_dir(const std::string &flag, const char *env,
                     const char *marker,
                     const std::vector<std::string> &fallbacks) {
    std::vector<std::string> cands;
    if (!flag.empty())
        cands.push_back(flag);
    if (const char *e = std::getenv(env))
        cands.push_back(e);
    cands.insert(cands.end(), fallbacks.begin(), fallbacks.end());
    std::error_code ec;
    fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        for (const std::string &f : fallbacks) {
            cands.push_back((exe.parent_path() / f).string());
            cands.push_back((exe.parent_path().parent_path() / f).string());
        }
    }
    for (const std::string &c : cands)
        if (fs::exists(fs::path(c) / marker, ec))
            return c;
    return "";
}

std::string load_examples_json(const std::string &dir) {
    std::string o = "[";
    if (!dir.empty()) {
        std::vector<fs::path> files;
        std::error_code ec;
        for (const auto &e : fs::directory_iterator(dir, ec))
            if (e.path().extension() == ".savvy")
                files.push_back(e.path());
        std::sort(files.begin(), files.end());
        bool first = true;
        for (const fs::path &p : files) {
            std::string src;
            if (!read_file(p, src))
                continue;
            if (!first)
                o += ',';
            first = false;
            o += "{\"name\":\"" + json_escape(p.stem().string()) +
                 "\",\"src\":\"" + json_escape(src) + "\"}";
        }
    }
    o += ']';
    return o;
}

std::atomic<bool> g_running{true};
void on_sigint(int) { g_running.store(false); }

struct Args {
    int port = 7070;
    bool net = false;
    bool public_mode = false;
    bool secure_cookies = false;
    int workers = 16;
    std::string web;
    std::string origin;
    std::string client_ip_header;
};

Args parse(int argc, char **argv) {
    Args a;
    auto val = [&](int &i) -> const char * {
        if (i + 1 >= argc) {
            std::fprintf(stderr, "missing value for %s\n", argv[i]);
            std::exit(2);
        }
        return argv[++i];
    };
    for (int i = 1; i < argc; ++i) {
        std::string f = argv[i];
        if (f == "--port")
            a.port = std::atoi(val(i));
        else if (f == "--net")
            a.net = true;
        else if (f == "--web")
            a.web = val(i);
        else if (f == "--public")
            a.public_mode = true;
        else if (f == "--trust-proxy")
            a.client_ip_header = "x-forwarded-for";
        else if (f == "--cloudflare") {
            a.client_ip_header = "cf-connecting-ip";
            a.secure_cookies = true;
        } else if (f == "--client-ip-header") {
            a.client_ip_header = val(i);
            for (char &c : a.client_ip_header)
                c = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(c)));
        } else if (f == "--secure-cookies")
            a.secure_cookies = true;
        else if (f == "--workers")
            a.workers = std::atoi(val(i));
        else if (f == "--origin")
            a.origin = val(i);
        else if (f == "--help" || f == "-h") {
            std::printf(
                "usage: shrewdness [options]\n"
                "  the Shrewdness IDE backend: compile / run / trace / "
                "console\n"
                "\n"
                "  --port N        port to listen on (default 7070)\n"
                "  --net           bind 0.0.0.0 instead of 127.0.0.1\n"
                "  --web DIR       where the built front end lives\n"
                "  --workers N     connection threads (default 16)\n"
                "  --public        tighten every per-visitor limit and turn on\n"
                "                  rate limiting; use for anything reachable\n"
                "                  from the internet\n"
                "  --cloudflare    behind a Cloudflare tunnel: read the client\n"
                "                  address from CF-Connecting-IP and mark "
                "cookies\n"
                "                  Secure\n"
                "  --trust-proxy   read the client address from "
                "X-Forwarded-For\n"
                "                  (nginx, Caddy, Traefik)\n"
                "  --client-ip-header NAME\n"
                "                  read it from some other header instead\n"
                "  --secure-cookies\n"
                "                  always mark the session cookie Secure\n"
                "  --origin URL    send Access-Control-Allow-Origin: URL\n"
                "                  (omit entirely for a same-origin deploy)\n"
                "\n"
                "  Only name a client-address header when a proxy you control\n"
                "  is the only thing that can reach this port — otherwise a\n"
                "  client can forge it and evade the rate limiter.\n"
                "\n"
                "  env: SHREWDNESS_WEB (web dir), SHREWDNESS_EXAMPLES "
                "(examples dir)\n");
            std::exit(0);
        } else {
            std::fprintf(stderr, "unknown flag: %s (try --help)\n", f.c_str());
            std::exit(2);
        }
    }
    return a;
}

} // namespace

int main(int argc, char **argv) {
    Args a = parse(argc, argv);
    std::signal(SIGINT, on_sigint);

    const std::string web_root =
        find_dir(a.web, "SHREWDNESS_WEB", "index.html", {"web/dist", "web"});
    const std::string examples_json = load_examples_json(
        find_dir("", "SHREWDNESS_EXAMPLES", "replicator.savvy", {"examples"}));

    Caps caps;
    if (a.public_mode) {
        caps.console_steps_default = 50'000'000;  // ~0.15 s of CPU
        caps.console_steps_max = 200'000'000;     // ~0.6 s of CPU
        caps.console_mem_max = 1u << 20;
        caps.console_output_max = 1u << 18;
        caps.trace_mem_max = 1u << 18;
        caps.runs_per_client = 2;
        caps.runs_global = 8;
        caps.rate_limit = true;
    }

    ConsoleSessions consoles(caps.runs_per_client, caps.runs_global);
    RateLimiter limiter;

    auto route = [&](const http::Request &req,
                     const std::string &client) -> http::Response {
        const std::string &path = req.path;
        if (path.compare(0, 5, "/api/") != 0)
            return serve_static(web_root, path);

        const bool heavy =
            path == "/api/trace" || path == "/api/console/start";
        if (caps.rate_limit && !limiter.allow(client, req.client_ip, heavy))
            return json_error(heavy ? "slow down — too many runs started"
                                    : "slow down — too many requests",
                              429);

        Json j;
        if (!req.body.empty() && !Json::parse(req.body, j))
            return json_error("request body is not a flat JSON object");
        auto ps = [&](const char *k, const std::string &def = "") {
            if (j.has(k))
                return j.s(k);
            std::string q = req.param(k);
            return q.empty() ? def : q;
        };
        auto pn = [&](const char *k, double def = 0) -> double {
            if (j.has(k))
                return j.n(k, def);
            std::string q = req.param(k);
            return q.empty() ? def : std::strtod(q.c_str(), nullptr);
        };

        if (path == "/api/build")
            return [&] {
                Genome g;
                BuildErr err;
                if (!build_genome(j, req.param("src"), req.param("fmt"), g,
                                  err))
                    return json_response(error_json(err), 400);
                return json_response("{\"ok\":true," + genome_views(g) + '}');
            }();

        if (path == "/api/trace")
            return tool_trace(j, caps.trace_mem_max);

        if (path == "/api/console/start") {
            Genome g;
            BuildErr berr;
            if (!build_genome(j, "", "", g, berr))
                return json_response(error_json(berr), 400);
            auto clamp_u = [](double v, std::uint64_t def, std::uint64_t hi) {
                if (v <= 0)
                    return def;
                std::uint64_t u = static_cast<std::uint64_t>(v);
                return u > hi ? hi : u;
            };
            shrewd::Limits L;
            L.max_steps = clamp_u(j.n("steps"), caps.console_steps_default,
                                  caps.console_steps_max);
            L.memory_size = clamp_u(j.n("mem"), 65536, caps.console_mem_max);
            L.stack_limit = 1'000'000;
            L.output_limit = caps.console_output_max;
            L.max_call_depth = 100'000;
            L.max_child_genes = 200'000;
            L.max_offspring = 16;
            ConsoleSessions::Refusal why{};
            std::uint64_t id = consoles.start(
                client, std::move(g), L, static_cast<std::uint64_t>(j.n("seed")),
                caps.console_output_max, why);
            if (!id)
                return json_error(
                    why == ConsoleSessions::Refusal::PerClient
                        ? "too many programs running — stop one first"
                        : "the server is busy running other people's "
                          "programs — try again in a moment",
                    429);
            return json_response("{\"ok\":true,\"id\":" + std::to_string(id) +
                                 '}');
        }

        if (path == "/api/console/poll") {
            return json_response(
                consoles.poll_json(client, static_cast<std::uint64_t>(pn("id")),
                                   static_cast<std::size_t>(pn("off"))));
        }

        if (path == "/api/console/input") {
            if (!consoles.input(client, static_cast<std::uint64_t>(pn("id")),
                                ps("text"), pn("eof") != 0))
                return json_error("no such program", 404);
            return json_response("{\"ok\":true}");
        }

        if (path == "/api/console/kill") {
            consoles.kill(client, static_cast<std::uint64_t>(pn("id")));
            return json_response("{\"ok\":true}");
        }

        if (path == "/api/isa") {
            static const std::string isa = [] {
                std::string o = "[";
                for (int i = 0; i < shrewd::kOpCount; ++i) {
                    const auto &inf = shrewd::info(static_cast<shrewd::Op>(i));
                    if (i)
                        o += ',';
                    o += "{\"op\":" + std::to_string(i) + ",\"name\":\"" +
                         std::string(inf.mnemonic) +
                         "\",\"size\":" + std::to_string(inf.size) +
                         ",\"doc\":\"" + json_escape(std::string(inf.doc)) +
                         "\"}";
                }
                o += ']';
                return o;
            }();
            return json_response(isa);
        }

        if (path == "/api/examples")
            return json_response(examples_json);

        return json_error("no such endpoint", 404);
    };

    auto handler = [&](const http::Request &req) -> http::Response {
        std::string sid = req.cookie("sid");
        const bool fresh = !valid_id(sid);
        if (fresh)
            sid = random_id();

        http::Response r = route(req, sid);

        if (fresh) {
            const bool https =
                a.secure_cookies ||
                (!a.client_ip_header.empty() &&
                 req.header("x-forwarded-proto") == "https");
            std::string c = "Set-Cookie: sid=" + sid +
                            "; Path=/; Max-Age=31536000; HttpOnly; "
                            "SameSite=Lax";
            if (https)
                c += "; Secure";
            r.extra_headers.push_back(std::move(c));
        }
        return r;
    };

    http::Options opts;
    opts.port = a.port;
    opts.loopback_only = !a.net;
    opts.workers = a.workers;
    opts.client_ip_header = a.client_ip_header;
    opts.allow_origin = a.origin;

    http::Server server(opts, handler);
    if (!server.start()) {
        std::fprintf(stderr,
                     "could not bind port %d (in use?). Try --port N.\n",
                     a.port);
        return 1;
    }

    std::printf("Shrewdness — IDE backend\n");
    std::printf("  http://%s:%d/%s\n", a.net ? "0.0.0.0" : "127.0.0.1", a.port,
                web_root.empty() ? "  (frontend not built — see /)" : "");
    std::printf("  %d worker threads%s\n", a.workers,
                a.public_mode ? ", public limits, rate limiting on" : "");
    if (!a.client_ip_header.empty())
        std::printf("  client address from %s\n", a.client_ip_header.c_str());
    if (a.net && !a.public_mode)
        std::printf("  note: --net without --public leaves desktop-sized "
                    "limits on a public port\n");
    if (a.public_mode && a.client_ip_header.empty())
        std::printf(
            "  warning: no client-address header configured, so every visitor\n"
            "           arriving through a proxy shares one rate-limit bucket.\n"
            "           Behind Cloudflare pass --cloudflare; behind nginx or\n"
            "           Caddy pass --trust-proxy.\n");
    std::printf("  compile · run · trace · console. Ctrl-C to stop.\n");
    std::fflush(stdout);

    while (g_running.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    server.stop();
    std::printf("\nstopped.\n");
    return 0;
}
