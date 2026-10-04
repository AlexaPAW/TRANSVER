#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace std;

static constexpr int ASCII = 128;
static constexpr int EPS = -1;

struct TokenSpec {
    string name;
    string regex;
    bool skip = false; // пробельные символы/комментарии
    bool error = false; // лексическая ошибка
    int priority = 0;
};

// ребро НКА
struct NFAEdge {
    int to;
    int ch; // EPS or 0..127
};

struct NFAState {
    vector<NFAEdge> edges;
    int acceptToken = -1; // -1 = не конечное состояние
};

// фрагмент автомата Томпсона
struct Fragment {
    int start;
    int end;
};

// внутреннее представление токена регулярного выражения
// SET — множество возможных символов, OP — оператор regex
struct RToken {
    enum Kind { SET, OP } kind;
    vector<int> chars;
    char op = 0;
};

static string trim(const string &s) {
    size_t b = 0, e = s.size();
    while (b < e && isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

static vector<string> splitTab(const string &line) {
    vector<string> fields;
    size_t pos = 0;
    while (true) {
        size_t tabPos = line.find('\t', pos);
        if (tabPos == string::npos) { fields.push_back(line.substr(pos)); break; }
        fields.push_back(line.substr(pos, tabPos - pos));
        pos = tabPos + 1;
    }
    return fields;
}

// работает при выводе результата для json
static string jsonEscape(const string &s) {
    ostringstream output;
    output << '"';
    for (unsigned char ch : s) {
        switch (ch) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (ch < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", ch);
                    output << buf;
                } else output << ch;
        }
    }
    output << '"';
    return output.str();
}

// работает с тестовыми данными
// \t -> TAB, \r -> CR, \n -> LF
static string decodeEscapes(const string &s) {
    string decoded;
    for (size_t i = 0; i < s.size(); ++i) {
        char ch = s[i];
        if (ch != '\\') { decoded.push_back(ch); continue; }
        if (i + 1 >= s.size()) throw runtime_error("dangling escape in test input");
        char escapeChar = s[++i];
        switch (escapeChar) {
            case 'n': decoded.push_back('\n'); break;
            case 'r': decoded.push_back('\r'); break;
            case 't': decoded.push_back('\t'); break;
            case '\\': decoded.push_back('\\'); break;
            case '"': decoded.push_back('"'); break;
            case '\'': decoded.push_back('\''); break;
            case 'x': {
                if (i + 2 >= s.size()) throw runtime_error("bad \\x escape");
                auto hv = [](char h)->int {
                    if (h >= '0' && h <= '9') return h - '0';
                    if (h >= 'a' && h <= 'f') return h - 'a' + 10;
                    if (h >= 'A' && h <= 'F') return h - 'A' + 10;
                    return -1;
                };
                int highNibble = hv(s[i + 1]), lowNibble = hv(s[i + 2]);
                if (highNibble < 0 || lowNibble < 0) throw runtime_error("bad \\x escape");
                decoded.push_back(static_cast<char>(highNibble * 16 + lowNibble));
                i += 2;
                break;
            }
            default: decoded.push_back(escapeChar); break;
        }
    }
    return decoded;
}

class RegexParser {
public:
    explicit RegexParser(string s) : src(move(s)) {}

    vector<RToken> toPostfix() {
        vector<RToken> raw;
        for (size_t i = 0; i < src.size();) {
            char ch = src[i];
            if (isspace(static_cast<unsigned char>(ch))) {
                ++i;
                continue;
            }
            if (ch == '\\') {
                int ch = parseEscape(i);
                raw.push_back(setToken({ch}));
            } else if (ch == '[') {
                raw.push_back(setToken(parseClass(i)));
            } else if (ch == '(' || ch == ')' || ch == '|' || ch == '*' || ch == '+' || ch == '?') {
                raw.push_back(opToken(ch));
                ++i;
            } else if (ch == '.') {
                raw.push_back(setToken({'.'})); // обычная точка
                ++i;
            } else {
                raw.push_back(setToken({static_cast<unsigned char>(ch)}));
                ++i;
            }
        }

        // вставка точки между двумя выражениями (конкатенация)
        vector<RToken> withConcat;
        for (size_t i = 0; i < raw.size(); ++i) {
            if (!withConcat.empty() && canEnd(withConcat.back()) && canBegin(raw[i])) {
                withConcat.push_back(opToken('.'));
            }
            withConcat.push_back(raw[i]);
        }

        vector<RToken> out;
        vector<char> ops;

        // ab|c = a b . c |     т.к. понимается как (ab)|c
        auto prec = [](char op) {
            if (op == '|') return 1;
            if (op == '.') return 2;
            return 0;
        };
        for (const auto &t : withConcat) {
            if (t.kind == RToken::SET) {
                out.push_back(t);
            } else {
                char op = t.op;
                if (op == '(') {
                    ops.push_back(op);
                } else if (op == ')') {
                    bool found = false;
                    while (!ops.empty()) {
                        char x = ops.back(); ops.pop_back();
                        if (x == '(') { found = true; break; }
                        out.push_back(opToken(x));
                    }
                    if (!found) throw runtime_error("unmatched ')' in regex: " + src);
                } else if (op == '*' || op == '+' || op == '?') {
                    out.push_back(opToken(op));
                } else {
                    while (!ops.empty() && ops.back() != '(' && prec(ops.back()) >= prec(op)) {
                        out.push_back(opToken(ops.back()));
                        ops.pop_back();
                    }
                    ops.push_back(op);
                }
            }
        }
        while (!ops.empty()) {
            if (ops.back() == '(') throw runtime_error("unmatched '(' in regex: " + src);
            out.push_back(opToken(ops.back()));
            ops.pop_back();
        }
        if (out.empty()) throw runtime_error("empty regex");
        return out;
    }

private:
    string src;

    static RToken setToken(initializer_list<int> xs) {
        RToken t; t.kind = RToken::SET; t.chars.assign(xs); return t;
    }
    static RToken setToken(vector<int> xs) {
        sort(xs.begin(), xs.end());
        xs.erase(unique(xs.begin(), xs.end()), xs.end());
        RToken t; t.kind = RToken::SET; t.chars = move(xs); return t;
    }
    static RToken opToken(char op) { RToken t; t.kind = RToken::OP; t.op = op; return t; }
    
    static bool canEnd(const RToken &t) {
        return t.kind == RToken::SET || (t.kind == RToken::OP && (t.op == ')' || t.op == '*' || t.op == '+' || t.op == '?'));
    }
    static bool canBegin(const RToken &t) {
        return t.kind == RToken::SET || (t.kind == RToken::OP && t.op == '(');
    }

    // работает с регулярными выражениями из token.regex
    int parseEscape(size_t &i) {
        ++i;
        if (i >= src.size()) throw runtime_error("dangling escape in regex: " + src);
        char escapeChar = src[i++];
        switch (escapeChar) {
            case 'n': return '\n';
            case 'r': return '\r';
            case 't': return '\t';
            case '\\': return '\\';
            case '(': return '(';
            case ')': return ')';
            case '[': return '[';
            case ']': return ']';
            case '|': return '|';
            case '*': return '*';
            case '+': return '+';
            case '?': return '?';
            case '.': return '.';
            case '-': return '-';
            case '^': return '^';
            // \xHH: две шестнадцатеричные цифры превращаются в один байт
            case 'x': {
                if (i + 2 > src.size()) throw runtime_error("bad \\x in regex: " + src);
                auto hv = [](char h)->int {
                    if (h >= '0' && h <= '9') return h - '0';
                    if (h >= 'a' && h <= 'f') return h - 'a' + 10;
                    if (h >= 'A' && h <= 'F') return h - 'A' + 10;
                    return -1;
                };
                if (i + 1 >= src.size()) throw runtime_error("bad \\x in regex: " + src);
                int a = hv(src[i]), b = hv(src[i + 1]);
                if (a < 0 || b < 0) throw runtime_error("bad \\x in regex: " + src);
                i += 2;
                return a * 16 + b;
            }
            default: return static_cast<unsigned char>(escapeChar);
        }
    }

    vector<int> parseClass(size_t &i) {
        ++i; // i указывает на '['
        bool neg = false;
        if (i < src.size() && src[i] == '^') { neg = true; ++i; }
        vector<int> chars;
        bool have = false;
        while (i < src.size() && src[i] != ']') {
            int rangeStart;
            if (src[i] == '\\') rangeStart = parseEscape(i);
            else rangeStart = static_cast<unsigned char>(src[i++]);
            int rangeEnd = rangeStart;
            // Если после символа стоит '-' и далее не ']', считаем это диапазоном rangeStart-rangeEnd
            if (i < src.size() - 1 && src[i] == '-' && src[i + 1] != ']') {
                ++i;
                if (i >= src.size()) throw runtime_error("unterminated class: " + src);
                if (src[i] == '\\') rangeEnd = parseEscape(i);
                else rangeEnd = static_cast<unsigned char>(src[i++]);
                if (rangeStart > rangeEnd) throw runtime_error("descending class range in regex: " + src);
            }
            if (rangeStart < 0 || rangeStart >= ASCII || rangeEnd < 0 || rangeEnd >= ASCII) throw runtime_error("regex contains non-ASCII class member: " + src);
            for (int ch = rangeStart; ch <= rangeEnd; ++ch) chars.push_back(ch);
            have = true;
        }
        if (i >= src.size() || src[i] != ']') throw runtime_error("unterminated class: " + src);
        ++i;
        if (!have && !neg) throw runtime_error("empty character class: " + src);
        sort(chars.begin(), chars.end());
        chars.erase(unique(chars.begin(), chars.end()), chars.end());
        // [^...]:
        if (neg) {
            vector<int> all;
            vector<char> in(ASCII, 0);
            for (int c : chars) in[c] = 1;
            for (int ch = 0; ch < ASCII; ++ch) if (!in[ch]) all.push_back(ch);
            chars.swap(all);
        }
        return chars;
    }
};

// НКА по Томпсону
// для каждого regex создаётся фрагмент с одним start/end
// все фрагменты потм соединяются общим epsilon стартом
class AutomataBuilder {
public:
    vector<NFAState> nfa;
    int globalStart = -1;

    int newState() { nfa.push_back({}); return static_cast<int>(nfa.size()) - 1; }
    void addEdge(int from, int to, int ch) { nfa[from].edges.push_back({to, ch}); }

    Fragment compile(const vector<RToken> &postfix) {
        vector<Fragment> st;
        // на каждый символ 2 состояния и 1 ребро
        for (const auto &t : postfix) {
            if (t.kind == RToken::SET) {
                int s = newState(), e = newState();
                for (int c : t.chars) addEdge(s, e, c);
                st.push_back({s, e});
            } else {
                char op = t.op;
                if (op == '.') {
                    if (st.size() < 2) throw runtime_error("bad regex concat");
                    auto b = st.back(); st.pop_back();
                    auto a = st.back(); st.pop_back();
                    addEdge(a.end, b.start, EPS);
                    st.push_back({a.start, b.end});
                } else if (op == '|') {
                    if (st.size() < 2) throw runtime_error("bad regex alternation");
                    auto b = st.back(); st.pop_back();
                    auto a = st.back(); st.pop_back();
                    int s = newState(), e = newState();
                    addEdge(s, a.start, EPS); addEdge(s, b.start, EPS);
                    addEdge(a.end, e, EPS); addEdge(b.end, e, EPS);
                    st.push_back({s, e});
                }
                //   * — 0+ раз
                //   + — 1+ раз
                //   ? — 0 или 1 раз
                else if (op == '*' || op == '+' || op == '?') {
                    if (st.empty()) throw runtime_error("bad unary regex op");
                    auto a = st.back(); st.pop_back();
                    int s = newState(), e = newState();
                    if (op == '*' || op == '?') addEdge(s, e, EPS);
                    addEdge(s, a.start, EPS);
                    if (op == '*' ) addEdge(a.end, a.start, EPS);
                    addEdge(a.end, e, EPS);
                    st.push_back({s, e});
                } else throw runtime_error("unknown regex op");
            }
        }
        if (st.size() != 1) throw runtime_error("invalid regular expression");
        return st.back();
    }

    void build(const vector<TokenSpec> &specs) {
        nfa.clear();
        globalStart = newState();
        for (size_t i = 0; i < specs.size(); ++i) {
            auto postfix = RegexParser(specs[i].regex).toPostfix();
            Fragment f = compile(postfix);
            nfa[f.end].acceptToken = static_cast<int>(i);
            addEdge(globalStart, f.start, EPS);
        }
    }
};

// ДКА
struct DFA {
    int start = 0;
    int trap = -1;
    vector<array<int, ASCII>> trans;
    vector<int> accept;
    vector<vector<int>> subsets; // subsets[q] — множество состояний исходного НКА, которому соответствует q
};

// НКА -> ДКА
class SubsetBuilder {
public:
    static DFA build(const AutomataBuilder &b, const vector<TokenSpec> &specs) {
        DFA d;
        const int N = static_cast<int>(b.nfa.size());
        // epsilon-замыкание
        auto closure = [&](vector<int> s) { 
            vector<char> seen(N, 0);
            vector<int> st = s, out;
            for (int x : s) seen[x] = 1;
            while (!st.empty()) {
                int u = st.back(); st.pop_back();
                out.push_back(u);
                for (auto e : b.nfa[u].edges) if (e.ch == EPS && !seen[e.to]) {
                    seen[e.to] = 1; st.push_back(e.to);
                }
            }
            sort(out.begin(), out.end());
            return out;
        };
        auto move = [&](const vector<int> &s, int c) {
            vector<int> out;
            vector<char> seen(N, 0);
            for (int u : s) for (auto e : b.nfa[u].edges) {
                if (e.ch == c && !seen[e.to]) { seen[e.to] = 1; out.push_back(e.to); }
            }
            sort(out.begin(), out.end());
            return out;
        };
        // если несколько конечных состояний, то нужен с min приоритетом
        auto acceptOf = [&](const vector<int> &s) {
            int best = -1;
            for (int u : s) {
                int t = b.nfa[u].acceptToken;
                if (t >= 0 && (best < 0 || specs[t].priority < specs[best].priority)) best = t;
            }
            return best;
        };

        vector<int> startSet = closure({b.globalStart});
        vector<int> empty;
        map<vector<int>, int> id;
        // ДКА для subset, если оно ещё не встречалось
        auto intern = [&](const vector<int> &subset)->int {
            auto it = id.find(subset);
            if (it != id.end()) return it->second;
            int x = static_cast<int>(d.subsets.size());
            id.emplace(subset, x);
            d.subsets.push_back(subset);
            d.trans.emplace_back();
            d.trans.back().fill(0);
            d.accept.push_back(acceptOf(subset));
            return x;
        };

        int s0 = intern(startSet);
        (void)s0;
        // пустое множество состояний НКА это ловушка ДКА
        int trap = intern(empty);
        d.trap = trap;
        queue<int> q;
        vector<char> queued;
        queued.resize(2, 0);
        q.push(d.start = 0);
        q.push(trap);
        queued[0] = queued[trap] = 1;
        while (!q.empty()) {
            int u = q.front(); q.pop();
            if (u == trap) {
                for (int c = 0; c < ASCII; ++c) d.trans[u][c] = trap;
                continue;
            }
            for (int c = 0; c < ASCII; ++c) {
                auto m = move(d.subsets[u], c);
                auto cl = closure(m);
                int v = intern(cl);
                d.trans[u][c] = v;
                if (static_cast<int>(queued.size()) <= v) queued.resize(v + 1, 0);
                if (!queued[v]) { queued[v] = 1; q.push(v); }
            }
        }
        return d;
    }
};

// мин. ДКА
struct MinDFA {
    int start = 0;
    int trap = -1;
    vector<array<int, ASCII>> trans;
    vector<int> accept;
    vector<int> oldToNew;
};

// минимизация ДКА алгоритмом Хопкрофта
class Hopcroft {
public:
    static MinDFA minimize(const DFA &d) {
        const int n = static_cast<int>(d.trans.size());
        map<int, vector<int>> byAccept;
        for (int q = 0; q < n; ++q) byAccept[d.accept[q]].push_back(q);
        vector<vector<int>> P;
        for (auto &kv : byAccept) P.push_back(kv.second);

        vector<int> cls(n, -1);
        for (int i = 0; i < static_cast<int>(P.size()); ++i) for (int q : P[i]) cls[q] = i;

        vector<int> work;
        for (int i = 0; i < static_cast<int>(P.size()); ++i) work.push_back(i);

        // Берём класс A и по каждому символу ищем состояния, которые переходят в A
        while (!work.empty()) {
            int Aidx = work.back(); work.pop_back();
            vector<char> inA(n, 0);
            for (int q : P[Aidx]) inA[q] = 1;

            for (int c = 0; c < ASCII; ++c) {
                vector<char> X(n, 0);
                for (int q = 0; q < n; ++q) if (inA[d.trans[q][c]]) X[q] = 1;

                int pcount = static_cast<int>(P.size());
                for (int Yidx = 0; Yidx < pcount; ++Yidx) {
                    // inter = Y ∩ X, diff = Y \ X, ксли обе непустые, Y надо расколоть
                    vector<int> inter, diff;
                    for (int q : P[Yidx]) (X[q] ? inter : diff).push_back(q);
                    if (inter.empty() || diff.empty()) continue;
                    P[Yidx] = move(inter);
                    int Zidx = static_cast<int>(P.size());
                    P.push_back(move(diff));
                    for (int q : P[Yidx]) cls[q] = Yidx;
                    for (int q : P[Zidx]) cls[q] = Zidx;

                    // если расколотый класс уже есть в work, заменяем его одной половиной и
                    // добавляем вторую, иначе по Хопкрофту выгоднее добавить меньшую половину
                    auto replaceInWork = [&](int oldIdx, int a, int z) {
                        bool found = false;
                        for (int &w : work) {
                            if (w == oldIdx) {
                                w = a;
                                found = true;
                                break;
                            }
                        }
                        if (found) work.push_back(z);
                        else {
                            if (P[a].size() <= P[z].size()) work.push_back(a);
                            else work.push_back(z);
                        }
                    };
                    replaceInWork(Yidx, Yidx, Zidx);
                }
            }
        }

        // сначала стартовый класс, потом ловушка, потом остальные
        vector<int> classIds(P.size(), -1);
        int next = 0;
        classIds[cls[d.start]] = next++;
        if (cls[d.trap] != cls[d.start]) classIds[cls[d.trap]] = next++;
        for (size_t i = 0; i < P.size(); ++i) if (classIds[i] < 0) classIds[i] = next++;

        MinDFA m;
        m.trans.resize(P.size());
        m.accept.resize(P.size(), -1);
        for (size_t i = 0; i < P.size(); ++i) {
            int rep = P[i][0];
            m.accept[classIds[i]] = d.accept[rep];
            for (int c = 0; c < ASCII; ++c) m.trans[classIds[i]][c] = classIds[cls[d.trans[rep][c]]];
        }
        m.start = classIds[cls[d.start]];
        m.trap = classIds[cls[d.trap]];
        m.oldToNew.resize(n);
        for (int q = 0; q < n; ++q) m.oldToNew[q] = classIds[cls[q]];

        return m;
    }
};

struct Lexeme {
    string token;
    string text;
    size_t pos = 0;
};

// итог scan()
struct LexResult {
    vector<Lexeme> tokens;
    bool ok = true;
    size_t errorPos = 0;
    string errorText;
    string errorReason;
};

class Lexer {
public:
    Lexer(const MinDFA &d, const vector<TokenSpec> &specs) : dfa(d), specs(specs) {}

    LexResult scan(const string &input) const {
        LexResult r;
        size_t pos = 0;
        while (pos < input.size()) {
            int state = dfa.start;
            int lastAccept = -1;
            size_t lastPos = pos;
            size_t cursor = pos;
            while (cursor < input.size()) {
                unsigned char byte = static_cast<unsigned char>(input[cursor]);
                if (byte >= ASCII) break;
                int nextState = dfa.trans[state][byte];
                if (nextState == dfa.trap) break;
                state = nextState;
                ++cursor;
                if (dfa.accept[state] >= 0) {
                    lastAccept = dfa.accept[state];
                    lastPos = cursor;
                }
            }
            if (lastAccept < 0) {
                r.ok = false;
                r.errorPos = pos;
                if (static_cast<unsigned char>(input[pos]) >= ASCII) {
                    r.errorText = input.substr(pos, 1);
                    r.errorReason = "non-ASCII byte outside the fixed ASCII alphabet";
                } else {
                    r.errorText = input.substr(pos, min<size_t>(1, input.size() - pos));
                    r.errorReason = "no token matches at this position";
                }
                return r;
            }

            string lexeme = input.substr(pos, lastPos - pos);
            const auto &spec = specs[lastAccept];
            if (spec.error) {
                r.ok = false;
                r.errorPos = pos;
                r.errorText = lexeme;
                r.errorReason = "matched lexical error rule " + spec.name;
                return r;
            }
            if (!spec.skip) r.tokens.push_back({spec.name, lexeme, pos});
            pos = lastPos;
        }
        return r;
    }

private:
    const MinDFA &dfa;
    const vector<TokenSpec> &specs;
};


static vector<TokenSpec> readSpecs(const string &path) {
    ifstream in(path);
    if (!in) throw runtime_error("cannot open spec: " + path);
    vector<TokenSpec> s;
    string line;
    while (getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        auto f = splitTab(line);
        if (f.size() != 4) throw runtime_error("spec line must have 4 TAB-separated fields: name\\tflags\\tpriority\\tregex");
        TokenSpec x;
        x.name = f[0];
        string flags = f[1];
        x.skip = flags.find('S') != string::npos;
        x.error = flags.find('E') != string::npos;
        x.priority = stoi(f[2]);
        x.regex = f[3];
        s.push_back(move(x));
    }
    if (s.empty()) throw runtime_error("empty token spec");
    return s;
}

static void exportJson(const string &path, const vector<TokenSpec> &specs, const AutomataBuilder &nb,
                       const DFA &d, const MinDFA &m) {
    ofstream out(path);
    if (!out) throw runtime_error("cannot write " + path);
    out << "{\n";
    out << "  \"alphabet\": {\"kind\": \"ASCII\", \"size\": 128},\n";
    out << "  \"tokens\": [\n";
    for (size_t i = 0; i < specs.size(); ++i) {
        out << "    {\"id\": " << i << ", \"name\": " << jsonEscape(specs[i].name)
            << ", \"regex\": " << jsonEscape(specs[i].regex)
            << ", \"skip\": " << (specs[i].skip ? "true" : "false")
            << ", \"error\": " << (specs[i].error ? "true" : "false")
            << ", \"priority\": " << specs[i].priority << "}" << (i + 1 == specs.size() ? "" : ",") << "\n";
    }
    out << "  ],\n";
    out << "  \"nfa_states\": " << nb.nfa.size() << ",\n";
    out << "  \"dfa_states\": " << d.trans.size() << ",\n";
    out << "  \"min_dfa_states\": " << m.trans.size() << ",\n";
    out << "  \"start_state\": " << m.start << ",\n";
    out << "  \"trap_state\": " << m.trap << ",\n";
    out << "  \"accepting\": {\n";
    bool first = true;
    for (size_t i = 0; i < m.accept.size(); ++i) if (m.accept[i] >= 0) {
        if (!first) out << ",\n";
        first = false;
        out << "    " << jsonEscape(to_string(i)) << ": " << jsonEscape(specs[m.accept[i]].name);
    }
    out << "\n  },\n";
    out << "  \"transitions\": [\n";
    for (size_t q = 0; q < m.trans.size(); ++q) {
        out << "    [";
        for (int c = 0; c < ASCII; ++c) {
            if (c) out << ", ";
            out << m.trans[q][c];
        }
        out << "]" << (q + 1 == m.trans.size() ? "" : ",") << "\n";
    }
    out << "  ]\n}" << "\n";
}

static void exportCsv(const string &path, const MinDFA &m) {
    ofstream out(path);
    if (!out) throw runtime_error("cannot write " + path);
    out << "state,accept,trap";
    for (int c = 0; c < ASCII; ++c) out << "," << c;
    out << "\n";
    for (size_t q = 0; q < m.trans.size(); ++q) {
        out << q << "," << m.accept[q] << "," << (static_cast<int>(q) == m.trap ? 1 : 0);
        for (int c = 0; c < ASCII; ++c) out << "," << m.trans[q][c];
        out << "\n";
    }
}

struct TestCase {
    string inputEsc; // запись как в файле
    string expected;
    string input; // декодированная запись
};

static vector<TestCase> readTests(const string &path) {
    ifstream in(path);
    if (!in) throw runtime_error("cannot open tests: " + path);
    vector<TestCase> ts;
    string line;
    int lineno = 0;
    while (getline(in, line)) {
        ++lineno;
        if (line.empty() || line[0] == '#') continue;
        auto f = splitTab(line);
        if (f.size() != 2) throw runtime_error("test line must be input\\texpected at line " + to_string(lineno));
        ts.push_back({f[0], f[1], decodeEscapes(f[0])});
    }
    return ts;
}

// Сводит LexResult к формату с expected из tests.tsv
static string actualToString(const LexResult &r) {
    if (!r.ok) return "ERROR";
    if (r.tokens.empty()) return "<EMPTY>";
    ostringstream o;
    for (size_t i = 0; i < r.tokens.size(); ++i) {
        if (i) o << ' ';
        o << r.tokens[i].token;
    }
    return o.str();
}

static string tokensWithLexemes(const LexResult &r) {
    ostringstream o;
    for (size_t i = 0; i < r.tokens.size(); ++i) {
        if (i) o << ' ';
        o << r.tokens[i].token << '(' << r.tokens[i].text << ')';
    }
    if (!r.ok) o << " ERROR(pos=" << r.errorPos << ", text=" << jsonEscape(r.errorText) << ", reason=" << r.errorReason << ')';
    return o.str();
}

static void printStats(const vector<TokenSpec> &specs, const AutomataBuilder &nb, const DFA &d, const MinDFA &m) {
    cout << "tokens: " << specs.size() << "\n";
    cout << "NFA states: " << nb.nfa.size() << "\n";
    cout << "DFA states: " << d.trans.size() << "\n";
    cout << "minimized DFA states: " << m.trans.size() << "\n";
    cout << "start state: " << m.start << "\n";
    cout << "trap state: " << m.trap << "\n";
}

static void usage(const char *prog) {
    cerr << "Usage:\n"
         << "  " << prog << " --run-tests [--spec tokens.regex] [--tests tests/tests.tsv] [--out DIR]\n"
         << "  " << prog << " --dump [--spec tokens.regex] [--out DIR]\n"
         << "  " << prog << " --scan <string> [--spec tokens.regex] [--out DIR]\n";
}

int main(int argc, char **argv) {
    try {
        bool runTests = false, dump = false, scan = false;
        string specPath = "tokens.regex";
        string testsPath = "tests/tests.tsv";
        string outDir = "out";
        string scanInput;

        for (int i = 1; i < argc; ++i) {
            string a = argv[i];
            if (a == "--run-tests") runTests = true;
            else if (a == "--dump") dump = true;
            else if (a == "--scan") {
                if (i + 1 >= argc) throw runtime_error("--scan needs an input string");
                scan = true; scanInput = argv[++i];
            } else if (a == "--spec") {
                if (i + 1 >= argc) throw runtime_error("--spec needs a path");
                specPath = argv[++i];
            } else if (a == "--tests") {
                if (i + 1 >= argc) throw runtime_error("--tests needs a path");
                testsPath = argv[++i];
            } else if (a == "--out") {
                if (i + 1 >= argc) throw runtime_error("--out needs a directory");
                outDir = argv[++i];
            } else if (a == "--help" || a == "-h") { usage(argv[0]); return 0; }
            else throw runtime_error("unknown option: " + a);
        }
        if (!runTests && !dump && !scan) dump = true;

        vector<TokenSpec> specs = readSpecs(specPath);
        AutomataBuilder nb; // регулярные выражения -> НКА Томпсона
        nb.build(specs);
        DFA d = SubsetBuilder::build(nb, specs); // НКА -> полный ДКА
        MinDFA m = Hopcroft::minimize(d); // ДКА -> минимальный ДКА
        Lexer lexer(m, specs);

        std::filesystem::create_directories(outDir);
        exportJson(outDir + "/dfa.json", specs, nb, d, m);
        exportCsv(outDir + "/dfa.csv", m);
        {
            ofstream h(outDir + "/dfa_table.hpp");
            if (!h) throw runtime_error("cannot write " + outDir + "/dfa_table.hpp");
            h << "#pragma once\n#include <array>\n#include <cstddef>\nnamespace funny_dfa {\n";
            h << "inline constexpr std::size_t kStateCount = " << m.trans.size() << ";\n";
            h << "inline constexpr int kAlphabetSize = 128;\n";
            h << "inline constexpr int kStartState = " << m.start << ";\n";
            h << "inline constexpr int kTrapState = " << m.trap << ";\n";
            h << "inline constexpr std::array<int, kStateCount> kAccept = {";
            for (size_t i = 0; i < m.accept.size(); ++i) { if (i) h << ","; h << m.accept[i]; }
            h << "};\n";
            h << "inline constexpr std::array<std::array<int, kAlphabetSize>, kStateCount> kTransition = {{\n";
            for (size_t q = 0; q < m.trans.size(); ++q) {
                h << "  std::array<int, kAlphabetSize>{";
                for (int c = 0; c < ASCII; ++c) { if (c) h << ","; h << m.trans[q][c]; }
                h << "}" << (q + 1 == m.trans.size() ? "\n" : ",\n");
            }
            h << "}};\n} // namespace funny_dfa\n";
        }

        if (dump) {
            printStats(specs, nb, d, m);
            cout << "exports: " << outDir << "/dfa.json, " << outDir << "/dfa.csv\n";
        }
        if (scan) {
            LexResult r = lexer.scan(scanInput);
            cout << (r.ok ? "OK " : "ERROR ") << tokensWithLexemes(r) << "\n";
            return r.ok ? 0 : 2;
        }
        if (runTests) {
            vector<TestCase> tests = readTests(testsPath);
            ofstream resultFile(outDir + "/test_results.tsv");
            if (!resultFile) throw runtime_error("cannot write test_results.tsv");
            resultFile << "index\tinput\texpected\tgot\tstatus\n";
            int pass = 0;
            for (size_t i = 0; i < tests.size(); ++i) {
                auto r = lexer.scan(tests[i].input);
                string got = actualToString(r);
                bool ok = got == tests[i].expected;
                cout << (ok ? "PASS" : "FAIL") << "\t#" << (i + 1)
                     << "\tinput=" << jsonEscape(tests[i].input)
                     << "\texpected=" << tests[i].expected
                     << "\tgot=" << got;
                resultFile << (i + 1) << "\t" << jsonEscape(tests[i].input) << "\t"
                           << tests[i].expected << "\t" << got << "\t" << (ok ? "PASS" : "FAIL") << "\n";
                if (!ok && !r.ok) cout << "\terror=" << r.errorReason << " at " << r.errorPos;
                cout << "\n";
                if (ok) ++pass;
            }
            int asciiPass = 0;
            for (int c = 0; c < ASCII; ++c) {
                string one(1, static_cast<char>(c));
                (void)lexer.scan(one);
                ++asciiPass;
            }
            // Отдельная проверка выхода за ASCII
            string nonAscii; nonAscii.push_back(static_cast<char>(0xC3)); nonAscii.push_back(static_cast<char>(0xA9));
            auto nr = lexer.scan(nonAscii);
            bool nonAsciiOk = !nr.ok;
            cout << "ascii transition coverage: " << asciiPass << "/128 columns exercised\n";
            cout << "non-ASCII trap check: " << (nonAsciiOk ? "PASS" : "FAIL") << "\n";
            cout << "summary: " << pass << "/" << tests.size() << " passed\n";
            bool allOk = pass == static_cast<int>(tests.size()) && nonAsciiOk;
            return allOk ? 0 : 1;
        }
        return 0;
    } catch (const exception &e) {
        cerr << "error: " << e.what() << "\n";
        return 2;
    }
}
