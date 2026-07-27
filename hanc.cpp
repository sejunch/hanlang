// hanc.cpp — 한랭(.han) -> x86-64 어셈블리 컴파일러 (v4)
// v3: 소수 타입, 에러 캐럿 표시, 0나누기 가드, 함수 타입 추론, 참/거짓
// v4: 목록 (리터럴 / N번째 / 개수 / 넣다 / 범위 검사)
// v5: 불러오기, 내장 값(제곱근/절댓값/반올림/무작위), 문자 개수·N번째
// v6: 다형 함수(타입 조합마다 단형화), 몫 연산자 ~/, 문자 사전순 비교
// v7: 격자 2차원(N행 M열), 반복 제어(멈추다/계속하다)
// v8: 입력을 getline 으로 (줄 길이 제한 없앰). 공백 나누기는 lib/문자.han 에
// v9: 출력 끝맺음 옵션  "안녕"을 끝" " 말하다.
// v10: 비트 연산(& | ^ ~ << >>), 문자↔코드값, 묶음(구조체). 타입 인코딩 재설계
// 빌드: g++ -std=c++17 -O2 -o hanc hanc.cpp
// 사용: ./hanc prog.han > prog.s   또는   ./hanc prog.han -o prog
#include <bits/stdc++.h>
using namespace std;

// ==================== UTF-8 ====================
static uint32_t decodeUtf8(const string& s, size_t& i) {
    unsigned char c = s[i];
    if (c < 0x80)        { i += 1; return c; }
    if ((c >> 5) == 0x6) { uint32_t r = ((c & 0x1F) << 6) | (s[i+1] & 0x3F); i += 2; return r; }
    if ((c >> 4) == 0xE) { uint32_t r = ((c & 0x0F) << 12) | ((s[i+1] & 0x3F) << 6) | (s[i+2] & 0x3F); i += 3; return r; }
    uint32_t r = ((c & 0x07) << 18) | ((s[i+1] & 0x3F) << 12) | ((s[i+2] & 0x3F) << 6) | (s[i+3] & 0x3F);
    i += 4; return r;
}
static bool isHangul(uint32_t cp) {
    return (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0x1100 && cp <= 0x11FF) || (cp >= 0x3130 && cp <= 0x318F);
}
static bool isIdentChar(uint32_t cp)  { return isHangul(cp) || cp == '_' || (cp < 0x80 && isalnum((int)cp)); }
static bool isIdentStart(uint32_t cp) { return isHangul(cp) || cp == '_' || (cp < 0x80 && isalpha((int)cp)); }
static int dispWidth(uint32_t cp) { return isHangul(cp) ? 2 : 1; }

// ==================== 에러 표시 ====================
// 불러오기 때문에 파일이 여러 개 -> 토큰/노드가 파일 번호를 들고 다닌다.
struct SrcFile { string name; vector<string> lines; };
// 함수 몸통은 인자 타입 조합마다 따로 검사된다. 그래서 몸통에서 난 에러는
// "어느 호출 때문에 그 조합이 만들어졌는지"를 같이 보여줘야 원인을 알 수 있다.
static vector<string> errCtx;
struct Diag {
    vector<SrcFile> files;
    int addFile(const string& name, const string& src) {
        SrcFile sf; sf.name = name;
        string ln;
        for (char c : src) {
            if (c == '\n') { sf.lines.push_back(ln); ln.clear(); }
            else if (c != '\r') ln += c;
        }
        sf.lines.push_back(ln);
        files.push_back(move(sf));
        return (int)files.size() - 1;
    }
    [[noreturn]] void error(int file, int line, int col, int blen, const string& m) {
        cerr << "[오류] ";
        if (files.size() > 1 && file >= 0 && file < (int)files.size())
            cerr << files[file].name << " ";
        cerr << line << "행: " << m << "\n";
        static const vector<string> none;
        const vector<string>& lines =
            (file >= 0 && file < (int)files.size()) ? files[file].lines : none;
        if (line >= 1 && line <= (int)lines.size() && col >= 0) {
            const string& L = lines[line - 1];
            string num = to_string(line);
            cerr << "  " << num << " | " << L << "\n";
            cerr << "  " << string(num.size(), ' ') << " | ";
            size_t i = 0;
            while (i < (size_t)col && i < L.size()) {
                uint32_t c = decodeUtf8(L, i);
                cerr << string(dispWidth(c), ' ');
            }
            if ((size_t)col >= L.size()) cerr << "^";
            else {
                size_t end = min(L.size(), (size_t)col + (size_t)max(blen, 1));
                while (i < end) {
                    uint32_t c = decodeUtf8(L, i);
                    cerr << string(dispWidth(c), '^');
                }
            }
            cerr << "\n";
        }
        for (size_t k = errCtx.size(); k-- > 0; ) cerr << errCtx[k] << "\n";
        exit(1);
    }
};
static Diag diag;
[[noreturn]] static void err(int file, int line, const string& m) { diag.error(file, line, -1, 0, m); }

// ==================== 토큰 ====================
enum class TK { Num, Flt, Str, Ident, Particle, Keyword, Op, End };
struct Token {
    TK t; string s, particle;
    long long n = 0; double f = 0;
    int file = 0, line = 1, col = -1, blen = 0;
};
[[noreturn]] static void terr(const Token& t, const string& m) { diag.error(t.file, t.line, t.col, t.blen, m); }

static const vector<string> PARTICLES = {
    "이라고","라고","으로","에서","에게","를","을","는","은","이","가","의","에","로","와","과"
};
static const set<string> DECL_PARTICLES = { "는", "은" };
// "9의 제곱근" 꼴 내장 값들 (값을 주는 것 = 명사형, 동작 = 동사형 규칙)
static const set<string> BUILTIN_NOUNS = {
    "제곱근","절댓값","반올림","올림","버림","무작위","코드","글자"
};
// "판의 행수" 꼴. 키워드가 아니라 known 에 넣어 '의' 뒤에서만 특별 해석 (이름 충돌 회피)
static const set<string> GRID_NOUNS = { "행수", "열수" };
static const set<string> KEYWORDS = {
    "만약","라면","이라면","아니면","인","동안","번","반복하다",
    "말하다","돌려주다","란","이란","이다","듣다","바꾸다",
    "정수","소수","문자","참","거짓","번째","넣다",
    "행","열","채우다","멈추다","계속하다","묶음","새"
};

// ==================== 1단계: 렉서 ====================
struct Lexer {
    string src; size_t i = 0, lineStart = 0; int line = 1; int fileIdx = 0;
    Lexer(string s, int fi) : src(move(s)), fileIdx(fi) {}
    int col() const { return (int)(i - lineStart); }

    vector<Token> run() {
        vector<Token> out;
        while (true) {
            skipSpace();
            if (i >= src.size()) {
                Token e; e.t = TK::End; e.s = "<끝>"; e.line = line; e.col = col(); e.file = fileIdx;
                out.push_back(e); break;
            }
            size_t save = i;
            int c0 = col();
            uint32_t cp = decodeUtf8(src, i);

            if (cp == '"') {
                string val;
                while (i < src.size() && src[i] != '"') {
                    if (src[i] == '\\' && i + 1 < src.size()) {
                        i++; char e = src[i++];
                        val += (e == 'n' ? '\n' : e == 't' ? '\t' : e);
                    } else {
                        if (src[i] == '\n') { line++; lineStart = i + 1; }
                        val += src[i++];
                    }
                }
                if (i >= src.size()) err(fileIdx, line, "문자열이 닫히지 않음");
                i++;
                Token t; t.t = TK::Str; t.s = val; t.line = line; t.col = c0; t.blen = (int)(i - save);
                out.push_back(t);
            }
            else if (cp < 0x80 && isdigit((int)cp)) {
                long long v = cp - '0';
                while (i < src.size() && isdigit((unsigned char)src[i])) v = v * 10 + (src[i++] - '0');
                // 소수 리터럴: 숫자 '.' 숫자   ('3.' 은 숫자 3 + 문장 끝)
                if (i + 1 < src.size() && src[i] == '.' && isdigit((unsigned char)src[i+1])) {
                    i++;
                    while (i < src.size() && isdigit((unsigned char)src[i])) i++;
                    Token t; t.t = TK::Flt;
                    t.f = strtod(src.substr(save, i - save).c_str(), nullptr);
                    t.line = line; t.col = c0; t.blen = (int)(i - save);
                    out.push_back(t);
                } else {
                    Token t; t.t = TK::Num; t.n = v; t.line = line; t.col = c0; t.blen = (int)(i - save);
                    out.push_back(t);
                }
            }
            else if (isIdentStart(cp)) {
                size_t st = save;
                while (i < src.size()) {
                    size_t pk = i; uint32_t c2 = decodeUtf8(src, pk);
                    if (!isIdentChar(c2)) break;
                    i = pk;
                }
                Token t; t.t = TK::Ident; t.s = src.substr(st, i - st);
                t.line = line; t.col = c0; t.blen = (int)(i - st);
                out.push_back(t);
            }
            else {
                i = save;
                // 순서 중요: && 가 & 보다, ~/ 가 ~ 보다 먼저 잡혀야 한다
                static const vector<string> OPS2 = {">=","<=","==","!=","&&","||","~/","<<",">>"};
                string two = src.substr(i, 2);
                bool hit = false;
                for (auto& o : OPS2) if (two == o) {
                    Token t; t.t = TK::Op; t.s = o; t.line = line; t.col = c0; t.blen = 2;
                    out.push_back(t); i += 2; hit = true; break;
                }
                if (hit) continue;
                if (string("+-*/%<>=(){}.,![]&|^~").find((char)cp) == string::npos)
                    diag.error(fileIdx, line, c0, 1, "알 수 없는 문자");
                Token t; t.t = TK::Op; t.s = string(1,(char)cp); t.line = line; t.col = c0; t.blen = 1;
                out.push_back(t);
                i++;
            }
        }
        for (auto& t : out) t.file = fileIdx;
        return out;
    }

    void skipSpace() {
        while (i < src.size()) {
            if (src[i] == '\n') { line++; i++; lineStart = i; }
            else if (isspace((unsigned char)src[i])) i++;
            else if (src.compare(i, 2, "//") == 0) { while (i < src.size() && src[i] != '\n') i++; }
            else if (src.compare(i, 2, "/*") == 0) {
                int st = line; i += 2;
                while (i + 1 < src.size() && src.compare(i, 2, "*/") != 0) {
                    if (src[i]=='\n') { line++; lineStart = i + 1; }
                    i++;
                }
                if (i + 1 >= src.size()) err(fileIdx, st, "주석 /* 가 닫히지 않음");
                i += 2;
            }
            else break;
        }
    }
};

static bool endsWith(const string& w, const string& p) {
    return w.size() > p.size() && w.compare(w.size() - p.size(), p.size(), p) == 0;
}

// ==================== 1.5단계: 불러오기 ====================
// "수학.han"을 불러오다.  -> 그 자리에 그 파일의 토큰을 끼워 넣는다 (C의 #include).
// 토큰마다 파일 번호가 붙어 있어서 에러 메시지의 행 번호는 원래 파일 기준으로 나온다.
static string hancDir;                       // 컴파일러가 있는 폴더 (표준 라이브러리 검색용)

static string dirOf(const string& p) {
    size_t s = p.find_last_of('/');
    return s == string::npos ? string(".") : p.substr(0, s);
}
static bool readFile(const string& p, string& out) {
    ifstream f(p);
    if (!f) return false;
    stringstream ss; ss << f.rdbuf();
    out = ss.str();
    return true;
}
static string canonical(const string& p) {
    char buf[PATH_MAX];
    if (realpath(p.c_str(), buf)) return string(buf);
    return p;
}

struct Loader {
    set<string> loaded;                      // 이미 불러온 파일 (순환/중복 방지)

    // 파일 하나를 토큰으로. <끝> 토큰은 뺀 채 돌려준다.
    vector<Token> load(const string& path, const string& shownName) {
        string src;
        if (!readFile(path, src)) { cerr << "파일 못 엶: " << path << "\n"; exit(1); }
        int fi = diag.addFile(shownName, src);
        vector<Token> raw = Lexer(src, fi).run();
        if (!raw.empty() && raw.back().t == TK::End) raw.pop_back();
        return expand(raw, path);
    }

    // 토큰 열에서 불러오기 문장을 찾아 재귀 치환
    vector<Token> expand(const vector<Token>& in, const string& ownerPath) {
        vector<Token> out;
        for (size_t j = 0; j < in.size(); j++) {
            if (!(in[j].t == TK::Ident && in[j].s == "불러오다")) { out.push_back(in[j]); continue; }
            // 앞으로 되짚어 "파일이름" + 을/를 꼴인지 확인
            if (j < 2 || in[j-2].t != TK::Str ||
                in[j-1].t != TK::Ident || (in[j-1].s != "을" && in[j-1].s != "를"))
                terr(in[j], "불러오기는 \"파일이름\"을 불러오다. 꼴로 써야 함");
            if (j + 1 >= in.size() || !(in[j+1].t == TK::Op && in[j+1].s == "."))
                terr(in[j], "문장은 '.' 로 끝나야 함");
            out.pop_back();                  // 을/를
            Token nameTok = out.back(); out.pop_back();   // "파일이름"
            j++;                             // '.' 건너뜀

            string want = nameTok.s;
            // 찾는 순서. 설치본은 hanc 가 /usr/local/bin 에, 라이브러리는 /usr/local/lib/hanlang 에 있다.
            vector<string> cand;
            cand.push_back(dirOf(ownerPath) + "/" + want);          // 부르는 파일 옆
            if (const char* env = getenv("HAN_LIB"))
                cand.push_back(string(env) + "/" + want);           // 직접 지정한 곳
            cand.push_back(hancDir + "/lib/" + want);               // 소스 트리에서 바로 실행할 때
            cand.push_back(hancDir + "/../lib/hanlang/" + want);    // 설치본
            cand.push_back("/usr/local/lib/hanlang/" + want);
            cand.push_back("/usr/lib/hanlang/" + want);
            string tried;
            for (auto& c : cand) if (ifstream(c)) { tried = c; break; }
            if (tried.empty())
                terr(nameTok, "'" + want + "' 를 찾을 수 없음. 같은 폴더나 라이브러리 폴더에 있어야 함");
            string key = canonical(tried);
            if (loaded.count(key)) continue;              // 이미 불러옴 -> 조용히 건너뜀
            loaded.insert(key);
            vector<Token> sub = load(tried, want);
            for (auto& t : sub) out.push_back(t);
        }
        return out;
    }
};

// ==================== 2단계: 조사 분리기 ====================
struct Resolver {
    set<string> known;
    set<string> funcNames;

    void prescan(const vector<Token>& in) {
        for (size_t j = 0; j < in.size(); j++) {
            if (in[j].t != TK::Ident || (in[j].s != "란" && in[j].s != "이란")) continue;
            size_t st = 0;
            for (size_t k = j; k-- > 0; ) {
                if (in[k].t == TK::Op && (in[k].s == "." || in[k].s == "{" || in[k].s == "}")) { st = k + 1; break; }
            }
            vector<size_t> words;
            for (size_t k = st; k < j; k++) if (in[k].t == TK::Ident) words.push_back(k);
            if (words.empty()) terr(in[j], "'란' 앞에 함수 이름이 없음");
            funcNames.insert(in[words.back()].s);
            for (size_t x = 0; x + 1 < words.size(); x++) {
                string w = in[words[x]].s;
                for (auto& p : PARTICLES) if (endsWith(w, p)) { w = w.substr(0, w.size()-p.size()); break; }
                known.insert(w);
            }
        }
    }

    vector<Token> run(vector<Token> in) {
        // "X의 ○○" 꼴 내장 이름들. KEYWORDS 가 아니라 known 에 넣는 이유:
        // 키워드로 만들면 전역 이름을 먹어서 '절댓값' 같은 변수를 못 만들게 된다.
        // known 에 넣으면 통짜로 지나가서 Parser 가 '의' 뒤에서만 특별히 해석한다.
        known.insert("개수");
        for (auto& b : BUILTIN_NOUNS) known.insert(b);
        for (auto& b : GRID_NOUNS) known.insert(b);
        prescan(in);
        vector<Token> out;
        for (auto& tk : in) {
            if (tk.t != TK::Ident) { out.push_back(tk); continue; }
            const string& w = tk.s;

            if (KEYWORDS.count(w))   { tk.t = TK::Keyword; out.push_back(tk); continue; }
            if (funcNames.count(w))  { out.push_back(tk); continue; }
            if (known.count(w))      { out.push_back(tk); continue; }

            bool pw = false;
            for (auto& p : PARTICLES) if (w == p) { pw = true; break; }
            if (pw) { tk.t = TK::Particle; out.push_back(tk); continue; }

            bool done = false;
            for (auto& p : PARTICLES)                       // 아는 변수 + 조사
                if (endsWith(w, p) && known.count(w.substr(0, w.size()-p.size()))) {
                    tk.s = w.substr(0, w.size()-p.size()); tk.particle = p;
                    tk.blen = (int)tk.s.size();
                    out.push_back(tk); done = true; break;
                }
            if (done) continue;
            for (auto& p : PARTICLES)                       // 키워드 + 조사 ("정수로")
                if (endsWith(w, p) && KEYWORDS.count(w.substr(0, w.size()-p.size()))) {
                    tk.t = TK::Keyword;
                    tk.s = w.substr(0, w.size()-p.size()); tk.particle = p;
                    tk.blen = (int)tk.s.size();
                    out.push_back(tk); done = true; break;
                }
            if (done) continue;
            // "나이이다" -> 나이 + 이다  ("이다" 먼저: "나이다" 는 나이+다)
            for (const char* suf : {"이다", "다"}) {
                if (endsWith(w, suf)) {
                    string stem = w.substr(0, w.size() - strlen(suf));
                    if (known.count(stem)) {
                        Token v = tk; v.s = stem; v.particle = ""; v.blen = (int)stem.size();
                        out.push_back(v);
                        Token k; k.t = TK::Keyword; k.s = "이다"; k.file = tk.file; k.line = tk.line;
                        k.col = tk.col + (int)stem.size(); k.blen = (int)(w.size() - stem.size());
                        out.push_back(k);
                        done = true; break;
                    }
                }
            }
            if (done) continue;
            // "번호번째" / "가번째는" / "앞번째를" -> 어간 + 번째 + (조사)
            // 모르는 어간도 쪼갠다: "정의되지 않은 변수" 쪽 진단이 훨씬 정확하기 때문
            {
                string body = w, part;
                for (auto& p : PARTICLES)
                    if (endsWith(w, p) && endsWith(w.substr(0, w.size()-p.size()), "번째")) {
                        body = w.substr(0, w.size()-p.size()); part = p; break;
                    }
                string stem = endsWith(body, "번째")
                            ? body.substr(0, body.size() - strlen("번째")) : string();
                // stem 이 비면 낱말이 그냥 "번째..." 라 아래 키워드+조사 규칙이 처리한다
                if (!stem.empty()) {
                    Token v = tk; v.s = stem; v.particle = ""; v.blen = (int)stem.size();
                    if (KEYWORDS.count(stem)) v.t = TK::Keyword;
                    out.push_back(v);
                    Token k; k.t = TK::Keyword; k.s = "번째"; k.particle = part;
                    k.file = tk.file; k.line = tk.line;
                    k.col = tk.col + (int)stem.size(); k.blen = (int)strlen("번째");
                    out.push_back(k);
                    continue;
                }
            }
            // "행번호행", "열번호열은" -> 어간 + 행/열 + (조사)
            // 어간이 '아는 변수'일 때만 쪼갠다. 안 그러면 여행·배열·진행 같은 낱말이 박살난다.
            done = false;
            for (const char* dep : {"행", "열"}) {
                string body = w, part;
                for (auto& p : PARTICLES)
                    if (endsWith(w, p) && endsWith(w.substr(0, w.size()-p.size()), dep)) {
                        body = w.substr(0, w.size()-p.size()); part = p; break;
                    }
                if (!endsWith(body, dep)) continue;
                string stem = body.substr(0, body.size() - strlen(dep));
                if (stem.empty() || !known.count(stem)) continue;
                Token v = tk; v.s = stem; v.particle = ""; v.blen = (int)stem.size();
                out.push_back(v);
                Token k; k.t = TK::Keyword; k.s = dep; k.particle = part;
                k.file = tk.file; k.line = tk.line;
                k.col = tk.col + (int)stem.size(); k.blen = (int)strlen(dep);
                out.push_back(k);
                done = true; break;
            }
            if (done) continue;
            // "개수번" -> 개수 + 번
            if (endsWith(w, "번") && known.count(w.substr(0, w.size() - strlen("번")))) {
                string stem = w.substr(0, w.size() - strlen("번"));
                Token v = tk; v.s = stem; v.particle = ""; v.blen = (int)stem.size();
                out.push_back(v);
                Token k; k.t = TK::Keyword; k.s = "번"; k.file = tk.file; k.line = tk.line;
                k.col = tk.col + (int)stem.size(); k.blen = (int)(w.size() - stem.size());
                out.push_back(k);
                continue;
            }
            // 모르는 낱말이라도 이다/다 로 끝나면 쪼갬:
            // "미지수이다" -> 미지수 + 이다. 어차피 둘 다 에러지만
            // "정의되지 않은 변수 '미지수'" 쪽이 훨씬 정확한 진단임
            for (const char* suf : {"이다", "다"}) {
                if (endsWith(w, suf)) {
                    string stem = w.substr(0, w.size() - strlen(suf));
                    Token v = tk; v.s = stem; v.particle = ""; v.blen = (int)stem.size();
                    if (KEYWORDS.count(stem)) v.t = TK::Keyword;   // "참이다" -> Kw참 + Kw이다
                    out.push_back(v);
                    Token k; k.t = TK::Keyword; k.s = "이다"; k.file = tk.file; k.line = tk.line;
                    k.col = tk.col + (int)stem.size(); k.blen = (int)(w.size() - stem.size());
                    out.push_back(k);
                    done = true; break;
                }
            }
            if (done) continue;
            for (auto& p : DECL_PARTICLES)                  // 는/은 = 새 변수 선언
                if (endsWith(w, p)) {
                    string stem = w.substr(0, w.size()-p.size());
                    known.insert(stem);
                    tk.s = stem; tk.particle = p;
                    tk.blen = (int)stem.size();
                    out.push_back(tk); done = true; break;
                }
            if (done) continue;

            known.insert(w);
            out.push_back(tk);
        }
        return out;
    }
};

// ==================== AST ====================
// 타입 인코딩
//   0,1,2       정수 / 문자 / 소수
//   3           빈 목록 (원소 타입 미정)
//   16+i        i번째 구조체
//   1024+원소   그 원소의 목록  ("학생 목록" 처럼 구조체 목록도 자연히 표현된다)
// 예전엔 목록을 3,4,5 로 붙여 썼는데 구조체를 넣으니 번호가 겹쳤다. 그래서 자리를 떼어놨다.
enum { TINT = 0, TSTR = 1, TFLT = 2, TLU = 3, TSTRUCT = 16, TLIST = 1024 };
static bool isListTy(int t)   { return t >= TLIST || t == TLU; }
static bool isStructTy(int t) { return t >= TSTRUCT && t < TLIST; }
static int  listOf(int e)     { return TLIST + e; }
static int  elemOf(int t)     { return t - TLIST; }

static vector<string> structNames;          // 구조체 이름 (TSTRUCT + i 로 찾음)
static string tyName(int t) {
    if (t == TINT) return "정수";
    if (t == TSTR) return "문자";
    if (t == TFLT) return "소수";
    if (t == TLU)  return "목록";
    if (isStructTy(t)) {
        size_t i = (size_t)(t - TSTRUCT);
        return i < structNames.size() ? structNames[i] : "묶음";
    }
    if (t >= TLIST) return tyName(elemOf(t)) + " 목록";
    return "???";
}

struct Node; using P = unique_ptr<Node>;
// kind: num flt str var un bin call input conv | assign print ret if while repeat block exprstmt
struct Node {
    string kind, str;
    long long num = 0;
    double f = 0;
    int file = 0, line = 0, col = -1, blen = 0;
    int ty = TINT;
    vector<P> kid;
};
static P mk(string k, string s = "", long long n = 0) {
    auto p = make_unique<Node>(); p->kind = move(k); p->str = move(s); p->num = n; return p;
}
static void at(Node* n, const Token& t) { n->file = t.file; n->line = t.line; n->col = t.col; n->blen = t.blen; }
[[noreturn]] static void nerr(const Node* n, const string& m) { diag.error(n->file, n->line, n->col, n->blen, m); }
struct Func { string name; vector<string> params; P body; int file = 0, line = 0; bool used = false; };

// 구조체(묶음). 모든 칸이 8바이트라 값 표현이 목록과 똑같다 — 포인터 하나.
struct Field { string name; int ty; };
struct Struct { string name; vector<Field> fields; int file = 0, line = 0; };
static vector<Struct> structs;
static int findField(int sty, const string& f) {
    if (!isStructTy(sty)) return -1;
    size_t si = (size_t)(sty - TSTRUCT);
    if (si >= structs.size()) return -1;
    for (size_t i = 0; i < structs[si].fields.size(); i++)
        if (structs[si].fields[i].name == f) return (int)i;
    return -1;
}

// ==================== 3단계: 파서 ====================
struct Parser {
    vector<Token> tk; size_t p = 0;
    set<string>& funcNames;
    map<string, Func> funcs;
    string pending;
    bool noGrid = false;       // 참이면 "N행 M열" 을 격자 만들기로 읽지 않음
    set<string> fieldNames;    // 모든 묶음의 칸 이름 (파싱 때 '의' 뒤를 가리기 위함)

    Parser(vector<Token> t, set<string>& fn) : tk(move(t)), funcNames(fn) {}

    Token& cur() { return tk[p]; }
    bool isOp(const string& s)  { return cur().t == TK::Op && cur().s == s; }
    bool isKw(const string& s)  { return cur().t == TK::Keyword && cur().s == s; }
    bool isFunc() { return cur().t == TK::Ident && funcNames.count(cur().s); }
    void expectOp(const string& s, const string& msg = "") {
        if (!isOp(s)) terr(cur(), msg.empty() ? "'" + s + "' 가 필요한데 '" + cur().s + "' 가 있음" : msg);
        p++;
    }
    void expectKw(const string& s, const string& msg = "") {
        if (!isKw(s)) terr(cur(), msg.empty() ? "'" + s + "' 가 필요한데 '" + cur().s + "' 가 있음" : msg);
        p++;
    }

    // ---- 식 ----
    P expr() { pending.clear(); return orE(); }

    P lvl(const vector<string>& ops, P (Parser::*next)()) {
        P l = (this->*next)();
        while (pending.empty() && cur().t == TK::Op &&
               find(ops.begin(), ops.end(), cur().s) != ops.end()) {
            Token op = cur(); p++;
            P r = (this->*next)();
            auto n = mk("bin", op.s); at(n.get(), op);
            n->kid.push_back(move(l)); n->kid.push_back(move(r));
            l = move(n);
        }
        return l;
    }
    // 우선순위는 C와 같은 순서로 둔다 (|| < && < | < ^ < & < ==,!= < 비교 < 시프트 < +- < */)
    P orE()   { return lvl({"||"}, &Parser::andE); }
    P andE()  { return lvl({"&&"}, &Parser::borE); }
    P borE()  { return lvl({"|"}, &Parser::bxorE); }
    P bxorE() { return lvl({"^"}, &Parser::bandE); }
    P bandE() { return lvl({"&"}, &Parser::eqE); }
    P eqE()  { return lvl({"==","!="}, &Parser::cmpE); }
    P cmpE() { return lvl({"<",">","<=",">="}, &Parser::shiftE); }
    P shiftE(){ return lvl({"<<",">>"}, &Parser::addE); }
    P addE() { return lvl({"+","-"}, &Parser::mulE); }
    P mulE() { return lvl({"*","/","%","~/"}, &Parser::unary); }

    P unary() {
        if (isOp("~")) {
            Token op = cur(); p++;
            auto n = mk("un", "~"); at(n.get(), op);
            n->kid.push_back(unary());
            return n;
        }
        if (isOp("-") || isOp("!")) {
            Token op = cur();
            // '-' 바로 뒤가 숫자면 음수 리터럴로 접는다.
            // 안 그러면 "-5의 절댓값" 이 -(5의 절댓값) = -5 로 읽혀서 뜻이 뒤집힌다.
            if (op.s == "-" && (tk[p+1].t == TK::Num || tk[p+1].t == TK::Flt)) {
                p++;
                Token num = cur();
                P lit;
                if (num.t == TK::Num) lit = mk("num", "", -num.n);
                else { lit = mk("flt"); lit->f = -num.f; }
                at(lit.get(), num);
                p++; grabParticle();
                while (pending == "의") { pending.clear(); lit = possessive(move(lit)); }
                return lit;
            }
            p++;
            auto n = mk("un", op.s); at(n.get(), op);
            n->kid.push_back(unary());
            return n;
        }
        return primary();
    }

    // primary = atom 뒤에 "행 열"(격자 만들기) / "의 ○○" 사슬
    P primary() {
        P e = atom();
        // "3행 4열" = 격자 만들기. 단 '의' 뒤 색인 자리에서는 격자 '접근'이라 noGrid 로 막는다.
        if (!noGrid && pending.empty() && isKw("행")) {
            p++;
            P cols = gridIdx();
            if (!isKw("열")) terr(cur(), "'N행' 다음에는 'M열' 이 와야 함");
            auto g = mk("grid"); at(g.get(), cur());
            pending = cur().particle; p++;
            if (pending.empty()) grabParticle();
            g->kid.push_back(move(e));
            g->kid.push_back(move(cols));
            e = move(g);
        }
        while (pending == "의") { pending.clear(); e = possessive(move(e)); }
        return e;
    }

    // 행/열 번호 자리: 여기서 또 "N행 M열" 을 격자 만들기로 읽으면 안 된다
    P gridIdx() {
        bool saved = noGrid; noGrid = true;
        P e = expr();
        noGrid = saved;
        return e;
    }

    // base 뒤에 '의' 가 붙은 상태에서 호출됨
    P possessive(P base) {
        // 묶음의 칸:  철수의 이름
        // 내장 이름(개수/제곱근/행수…)을 먼저 보므로 칸 이름이 그것들과 겹치면 내장이 이긴다.
        if (cur().t == TK::Ident && fieldNames.count(cur().s) &&
            cur().s != "개수" && !BUILTIN_NOUNS.count(cur().s) && !GRID_NOUNS.count(cur().s)) {
            auto n = mk("field", cur().s); at(n.get(), cur());
            pending = cur().particle; p++;
            if (pending.empty()) grabParticle();
            n->kid.push_back(move(base));
            return n;
        }
        if (cur().t == TK::Ident &&
            (cur().s == "개수" || BUILTIN_NOUNS.count(cur().s) || GRID_NOUNS.count(cur().s))) {
            const string& w = cur().s;
            string k = (w == "개수") ? "len" : (w == "행수") ? "gridrows"
                     : (w == "열수") ? "gridcols" : "builtin";
            auto n = mk(k, w);
            at(n.get(), cur());
            pending = cur().particle; p++;
            if (pending.empty()) grabParticle();
            n->kid.push_back(move(base));
            return n;
        }
        P idx = gridIdx();
        if (isKw("행")) {                       // 판의 2행 3열
            p++;
            P col = gridIdx();
            if (!isKw("열")) terr(cur(), "'N행' 다음에는 'M열' 이 와야 함");
            auto n = mk("gridget"); at(n.get(), cur());
            pending = cur().particle; p++;
            if (pending.empty()) grabParticle();
            n->kid.push_back(move(base));
            n->kid.push_back(move(idx));
            n->kid.push_back(move(col));
            return n;
        }
        if (!isKw("번째"))
            terr(cur(), "'의' 다음에는 '(몇)번째', '(몇)행 (몇)열', '개수' 가 와야 함");
        auto n = mk("index"); at(n.get(), cur());
        pending = cur().particle; p++;
        if (pending.empty()) grabParticle();
        n->kid.push_back(move(base));
        n->kid.push_back(move(idx));
        return n;
    }

    P atom() {
        Token t = cur();
        if (t.t == TK::Num)  { auto n = mk("num","",t.n); at(n.get(),t); p++; grabParticle(); return n; }
        if (t.t == TK::Flt)  { auto n = mk("flt"); n->f = t.f; at(n.get(),t); p++; grabParticle(); return n; }
        if (t.t == TK::Str)  { auto n = mk("str",t.s); at(n.get(),t); p++; grabParticle(); return n; }
        if (isKw("참"))  { auto n = mk("num","",1); at(n.get(),t); p++; grabParticle(); return n; }
        if (isKw("거짓")){ auto n = mk("num","",0); at(n.get(),t); p++; grabParticle(); return n; }
        if (t.t == TK::Ident) {
            if (funcNames.count(t.s))
                terr(t, "함수 '" + t.s + "' 를 값으로 쓰려면 ( ) 로 감싸야 함");
            auto n = mk("var", t.s); at(n.get(), t);
            pending = t.particle;
            p++;
            return n;
        }
        if (isKw("새")) {                       // (새 학생) — 묶음 하나 만들기
            p++;
            if (cur().t != TK::Ident) terr(cur(), "'새' 다음에는 묶음 이름이 와야 함");
            auto n = mk("new", cur().s); at(n.get(), cur());
            p++; grabParticle();
            return n;
        }
        if (isOp("[")) {
            p++;
            auto n = mk("list"); at(n.get(), t);
            if (!isOp("]")) {
                while (true) {
                    n->kid.push_back(expr());
                    if (isOp(",")) { p++; continue; }
                    break;
                }
            }
            pending.clear();
            expectOp("]", "목록은 ']' 로 닫아야 함");
            grabParticle();
            return n;
        }
        if (isOp("(")) { p++; P e = paren(); expectOp(")"); grabParticle(); return e; }
        terr(t, "여기에는 값(숫자, 문자, 변수)이 와야 함");
    }
    void grabParticle() {
        if (cur().t == TK::Particle) { pending = cur().s; p++; }
    }

    // 괄호 안: 묶은 식 / 값으로 쓰는 함수 호출 / X를 정수로 바꾸다
    P paren() {
        vector<P> args; string save;
        while (true) {
            P e = expr(); save = pending;
            args.push_back(move(e));
            if (isFunc()) {
                auto n = mk("call", cur().s); at(n.get(), cur()); p++;
                for (auto& a : args) n->kid.push_back(move(a));
                pending.clear();
                return n;
            }
            if (isKw("채우다")) {                  // (3행 4열을 0으로 채우다)
                Token tt = cur(); p++;
                int gi = -1;
                for (size_t k = 0; k < args.size(); k++)
                    if (args[k]->kind == "grid") gi = (int)k;
                if (args.size() != 2 || gi < 0)
                    terr(tt, "'N행 M열을 값으로 채우다' 꼴이어야 함");
                auto n = mk("fill"); at(n.get(), tt);
                n->kid.push_back(move(args[gi]));
                n->kid.push_back(move(args[1 - gi]));
                pending.clear();
                return n;
            }
            if (isKw("정수") || isKw("문자") || isKw("소수")) {
                Token tt = cur(); p++;
                expectKw("바꾸다", "'" + tt.s + "로' 다음에는 '바꾸다' 가 와야 함");
                if (args.size() != 1) terr(tt, "바꾸다 는 값 하나만 받음");
                auto n = mk("conv", tt.s); at(n.get(), tt);
                n->kid.push_back(move(args[0]));
                pending.clear();
                return n;
            }
            if (save.empty()) break;
        }
        if (args.size() != 1) terr(cur(), "괄호 안을 읽을 수 없음");
        pending.clear();
        return move(args[0]);
    }

    // ---- 문장 ----
    P block() {
        expectOp("{");
        auto b = mk("block");
        while (!isOp("}")) {
            if (cur().t == TK::End) terr(cur(), "'}' 로 닫아야 함");
            b->kid.push_back(stmt());
        }
        expectOp("}");
        return b;
    }

    P stmt() {
        Token first = cur();

        if (isKw("만약")) {
            p++;
            expectOp("(", "'만약' 다음에는 '(' 로 조건을 감싸야 함");
            auto n = mk("if"); at(n.get(), first);
            n->kid.push_back(paren());   // expr()이 아니라 paren(): 조건에 함수 호출을 쓸 수 있어야 함
            expectOp(")");
            if (isKw("라면") || isKw("이라면")) p++;
            else terr(cur(), "'만약 (조건)' 뒤에는 '라면' 이 와야 함");
            n->kid.push_back(block());
            if (isKw("아니면")) { p++; n->kid.push_back(block()); }
            return n;
        }
        // 대입:  X는 <식>이다.  /  X는 듣다.
        if (isKw("멈추다") || isKw("계속하다")) {
            auto n = mk(cur().s == "멈추다" ? "break" : "continue");
            at(n.get(), cur()); p++;
            expectOp(".", "문장은 '.' 로 끝나야 함");
            return n;
        }
        if (cur().t == TK::Ident && DECL_PARTICLES.count(cur().particle)) {
            auto n = mk("assign", cur().s); at(n.get(), cur()); p++;
            if (isKw("듣다")) {
                Token dt = cur(); p++;
                expectOp(".", "문장은 '.' 로 끝나야 함");
                auto in = mk("input"); at(in.get(), dt);
                n->kid.push_back(move(in));
                return n;
            }
            n->kid.push_back(expr());
            expectKw("이다", "값 뒤에 '이다' 가 와야 함");
            expectOp(".", "문장은 '.' 로 끝나야 함");
            return n;
        }
        // 나머지는 전부 "인자들 + 서술어" 꼴
        vector<P> args; vector<string> parts;   // parts = 각 인자에 붙은 조사
        string endWith; bool hasEnd = false;    // 끝" " 말하다 — 끝맺음 옵션
        auto atPredicate = [&] {
            return isKw("반복하다") || isKw("말하다") || isKw("돌려주다") || isKw("인") || isKw("넣다") || isFunc();
        };
        while (!atPredicate()) {
            // 조사와 서술어 사이의 옵션:  "안녕"을 끝" " 말하다.
            // '끝' 을 키워드로 만들지 않은 이유 — 변수 이름으로 흔하다(lib/목록.han 이 쓴다).
            // 조사가 안 붙은 '끝' 바로 뒤에 문자열이 올 때만 옵션으로 읽는다.
            if (cur().t == TK::Ident && cur().s == "끝" && cur().particle.empty() &&
                tk[p+1].t == TK::Str) {
                if (hasEnd) terr(cur(), "'끝' 은 한 번만 쓸 수 있음");
                endWith = tk[p+1].s; hasEnd = true;
                p += 2;
                continue;
            }
            args.push_back(expr());
            // 원소 대입:  점수의 2번째는 <식>이다.
            if (args.size() == 1 && (pending == "는" || pending == "은")) {
                Node* h = args[0].get();
                if (h->kind == "field") {                 // 철수의 이름은 "철수"이다.
                    auto n = mk("fieldset", h->str);
                    n->file = h->file; n->line = h->line; n->col = h->col; n->blen = h->blen;
                    n->kid.push_back(move(h->kid[0]));
                    if (isKw("듣다")) {
                        Token dt = cur(); p++;
                        auto in = mk("input"); at(in.get(), dt);
                        n->kid.push_back(move(in));
                    } else {
                        n->kid.push_back(expr());
                        expectKw("이다", "값 뒤에 '이다' 가 와야 함");
                    }
                    expectOp(".", "문장은 '.' 로 끝나야 함");
                    return n;
                }
                if (h->kind == "index" || h->kind == "gridget") {
                    bool g = (h->kind == "gridget");
                    auto n = mk(g ? "gridset" : "idxset");
                    n->file = h->file; n->line = h->line; n->col = h->col; n->blen = h->blen;
                    n->kid.push_back(move(h->kid[0]));
                    n->kid.push_back(move(h->kid[1]));
                    if (g) n->kid.push_back(move(h->kid[2]));
                    if (isKw("듣다")) {
                        Token dt = cur(); p++;
                        auto in = mk("input"); at(in.get(), dt);
                        n->kid.push_back(move(in));
                    } else {
                        n->kid.push_back(expr());
                        expectKw("이다", "값 뒤에 '이다' 가 와야 함");
                    }
                    expectOp(".", "문장은 '.' 로 끝나야 함");
                    return n;
                }
                if (h->kind == "len")
                    nerr(h, "목록의 '개수' 에 직접 값을 넣을 수는 없음. '넣다' 로 늘리기");
            }
            parts.push_back(pending);
            if (isKw("번")) { p++; pending = "번"; parts.back() = "번"; }
            if (pending.empty() && !atPredicate())
                terr(cur(), "문장을 끝맺을 서술어(말하다, 돌려주다 등)를 찾을 수 없음");
        }
        if (isKw("넣다")) {
            Token nt = cur(); p++;
            expectOp(".", "문장은 '.' 로 끝나야 함");
            if (args.size() != 2) terr(first, "'목록에 값을 넣다' 꼴이어야 함");
            int bi = -1;                       // '에' 가 붙은 쪽이 목록
            for (int i = 0; i < 2; i++) if (parts[i] == "에") bi = i;
            if (bi == -1) terr(nt, "어느 목록에 넣을지 '에' 로 표시해야 함 (예: 점수에 10을 넣다.)");
            auto n = mk("push"); at(n.get(), nt);
            n->kid.push_back(move(args[bi]));
            n->kid.push_back(move(args[1 - bi]));
            return n;
        }
        if (isKw("말하다")) {
            p++; expectOp(".", "문장은 '.' 로 끝나야 함");
            if (args.size() != 1) terr(first, "말하다 는 값 하나만 받음");
            auto n = mk("print", endWith, hasEnd ? 1 : 0);
            at(n.get(), first); n->kid.push_back(move(args[0]));
            return n;
        }
        if (hasEnd) terr(first, "'끝' 은 '말하다' 에만 쓸 수 있음");
        if (isKw("돌려주다")) {
            p++; expectOp(".", "문장은 '.' 로 끝나야 함");
            if (args.size() != 1) terr(first, "돌려주다 는 값 하나만 받음");
            auto n = mk("ret"); at(n.get(), first); n->kid.push_back(move(args[0]));
            return n;
        }
        if (isKw("반복하다")) {
            p++;
            if (args.size() != 1) terr(first, "'N번 반복하다' 꼴이어야 함");
            auto n = mk("repeat"); at(n.get(), first);
            n->kid.push_back(move(args[0]));
            n->kid.push_back(block());
            return n;
        }
        if (isKw("인")) {
            p++; expectKw("동안", "'인' 다음에는 '동안' 이 와야 함");
            if (args.size() != 1) terr(first, "조건이 하나여야 함");
            auto n = mk("while"); at(n.get(), first);
            n->kid.push_back(move(args[0]));
            n->kid.push_back(block());
            return n;
        }
        if (isFunc()) {
            auto n = mk("call", cur().s); at(n.get(), cur()); p++;
            for (auto& a : args) n->kid.push_back(move(a));
            expectOp(".", "문장은 '.' 로 끝나야 함");
            auto s = mk("exprstmt"); s->file = n->file; s->line = n->line; s->kid.push_back(move(n));
            return s;
        }
        terr(first, "이 문장을 읽을 수 없음");
    }

    bool atFuncDef() {
        for (size_t k = p; k < tk.size(); k++) {
            if (tk[k].t == TK::Keyword && (tk[k].s == "란" || tk[k].s == "이란")) {
                // "학생 이란 묶음 {" 은 함수가 아니라 구조체 정의
                return !(k + 1 < tk.size() && tk[k+1].t == TK::Keyword && tk[k+1].s == "묶음");
            }
            if (tk[k].t == TK::Op && (tk[k].s == "." || tk[k].s == "{" || tk[k].s == "}")) return false;
            if (tk[k].t == TK::End) return false;
        }
        return false;
    }
    bool atStructDef() {
        for (size_t k = p; k < tk.size(); k++) {
            if (tk[k].t == TK::Keyword && (tk[k].s == "란" || tk[k].s == "이란"))
                return k + 1 < tk.size() && tk[k+1].t == TK::Keyword && tk[k+1].s == "묶음";
            if (tk[k].t == TK::Op && (tk[k].s == "." || tk[k].s == "{" || tk[k].s == "}")) return false;
            if (tk[k].t == TK::End) return false;
        }
        return false;
    }

    // 학생 이란 묶음 { 이름은 문자. 점수는 정수. }
    void structDef() {
        Token nameTok = cur();
        if (cur().t != TK::Ident) terr(cur(), "묶음 이름이 있어야 함");
        Struct s; s.name = cur().s; s.file = cur().file; s.line = cur().line;
        p++;
        if (!(isKw("란") || isKw("이란"))) terr(cur(), "묶음 이름 뒤에는 '이란' 이 와야 함");
        p++;
        expectKw("묶음");
        for (auto& old : structs)
            if (old.name == s.name)
                terr(nameTok, "'" + s.name + "' 묶음이 이미 있음 (" + to_string(old.line) + "행)");
        expectOp("{", "묶음 내용은 '{' 로 열어야 함");
        while (!isOp("}")) {
            if (cur().t == TK::End) terr(cur(), "'}' 로 닫아야 함");
            if (cur().t != TK::Ident || !DECL_PARTICLES.count(cur().particle))
                terr(cur(), "묶음 안은 '이름은 정수.' 꼴이어야 함");
            Field f; f.name = cur().s;
            Token ft = cur(); p++;
            if      (isKw("정수")) f.ty = TINT;
            else if (isKw("문자")) f.ty = TSTR;
            else if (isKw("소수")) f.ty = TFLT;
            else terr(cur(), "칸 타입은 정수, 소수, 문자 중 하나여야 함");
            p++;
            expectOp(".", "칸 하나마다 '.' 로 끝내야 함");
            for (auto& o : s.fields)
                if (o.name == f.name) terr(ft, "'" + f.name + "' 칸이 이 묶음에 이미 있음");
            s.fields.push_back(f);
        }
        expectOp("}");
        if (s.fields.empty()) err(s.file, s.line, "'" + s.name + "' 묶음에 칸이 하나도 없음");
        structNames.push_back(s.name);
        structs.push_back(move(s));
    }

    void funcDef() {
        Func f; f.file = cur().file; f.line = cur().line;
        vector<string> names;
        while (!(cur().t == TK::Keyword && (cur().s == "란" || cur().s == "이란"))) {
            if (cur().t != TK::Ident) terr(cur(), "함수 정의 형식이 잘못됨");
            names.push_back(cur().s); p++;
        }
        p++;
        if (names.empty()) err(f.file, f.line, "함수 이름이 없음");
        f.name = names.back(); names.pop_back();
        f.params = names;
        f.body = block();
        // 불러오기로 같은 이름이 두 번 들어오면 조용히 덮어써서 디버깅이 지옥이 된다
        if (funcs.count(f.name)) {
            const Func& old = funcs[f.name];
            string where = (old.file != f.file && old.file < (int)diag.files.size())
                         ? diag.files[old.file].name + " " : string();
            err(f.file, f.line, "'" + f.name + "' 함수가 이미 있음 (" + where +
                to_string(old.line) + "행). 이름을 다르게 짓기");
        }
        funcs[f.name] = move(f);
    }

    // 칸 이름을 미리 모아둔다. '철수의 이름' 을 파싱하려면 '이름' 이 칸 이름인 걸 알아야 하는데,
    // 파서는 타입을 모르므로 이름으로만 판단한다.
    void prescanStructs() {
        for (size_t k = 0; k + 1 < tk.size(); k++) {
            if (!(tk[k].t == TK::Keyword && (tk[k].s == "란" || tk[k].s == "이란"))) continue;
            if (!(tk[k+1].t == TK::Keyword && tk[k+1].s == "묶음")) continue;
            size_t j = k + 2;
            if (!(j < tk.size() && tk[j].t == TK::Op && tk[j].s == "{")) continue;
            for (j++; j < tk.size() && !(tk[j].t == TK::Op && tk[j].s == "}"); j++)
                if (tk[j].t == TK::Ident && DECL_PARTICLES.count(tk[j].particle))
                    fieldNames.insert(tk[j].s);
        }
    }

    P program() {
        prescanStructs();
        auto top = mk("block");
        while (cur().t != TK::End) {
            if (atStructDef())    structDef();
            else if (atFuncDef()) funcDef();
            else top->kid.push_back(stmt());
        }
        return top;
    }
};

// ==================== 4단계: 타입 검사기 ====================
// 변수: 첫 대입에서 타입 확정, 이후 고정.
// 함수: 인자 타입 조합마다 따로 찍어낸다 (C++ 템플릿의 단형화와 같은 방식).
//   같은 함수를 정수 목록으로도 소수 목록으로도 쓸 수 있는 이유.
//   타입 주석(n->ty)이 조합마다 달라야 하므로 몸통 AST를 조합마다 복제한다.
struct Inst {
    vector<int> pt;                 // 인자 타입 조합
    int rt = -1;                    // 돌려주는 타입
    int st = 0;                     // 0 미검사 1 검사중 2 완료
    P body;                         // 이 조합 전용 몸통 복제본
    string label;                   // 어셈블리 라벨
};

static P cloneNode(const Node* n) {
    auto c = make_unique<Node>();
    c->kind = n->kind; c->str = n->str; c->num = n->num; c->f = n->f;
    c->file = n->file; c->line = n->line; c->col = n->col; c->blen = n->blen;
    c->ty = n->ty;
    for (auto& k : n->kid) c->kid.push_back(cloneNode(k.get()));
    return c;
}

struct Checker {
    map<string,Func>* funcs;
    // deque 를 쓰는 이유: 몸통을 검사하는 도중 같은 함수의 새 조합이 추가될 수 있는데
    // vector 면 재할당으로 curRet 포인터가 죽는다. deque 는 참조가 안 깨진다.
    map<string, deque<Inst>> insts;
    map<string,int> varTy;
    int* curRet = nullptr;
    int labelId = 0;
    int loopDepth = 0;
    bool inFunc = false;
    string curName;

    static string typeList(const vector<int>& t) {
        string s;
        for (size_t i = 0; i < t.size(); i++) { if (i) s += ", "; s += tyName(t[i]); }
        return s.empty() ? string("없음") : s;
    }

    // 이 조합의 인스턴스를 찾거나 새로 찍어낸다. 돌려주는 타입 반환, site->num 에 조합 번호 기록.
    int typeFunc(const string& name, const vector<int>& argTy, Node* site) {
        Func& f = funcs->at(name);
        if (f.params.size() != argTy.size()) {
            if (site) nerr(site, "'" + name + "' 는 인자 " + to_string(f.params.size()) +
                           "개가 필요한데 " + to_string(argTy.size()) + "개를 넣음");
            err(f.file, f.line, "'" + name + "' 인자 개수가 안 맞음");
        }
        deque<Inst>& list = insts[name];
        for (size_t i = 0; i < list.size(); i++) {
            if (list[i].pt != argTy) continue;
            if (list[i].st == 1 && list[i].rt == -1)
                nerr(site, "재귀 호출보다 앞에서 '돌려주다' 로 무엇을 돌려주는지 한 번은 알려줘야 함");
            if (site) site->num = (long long)i;
            return list[i].rt;
        }
        if (list.size() >= 200)
            nerr(site, "'" + name + "' 를 서로 다른 타입으로 너무 많이 부름");

        size_t idx = list.size();
        list.emplace_back();
        Inst& in = list[idx];
        in.pt = argTy; in.st = 1;
        in.body = cloneNode(f.body.get());
        in.label = "fn" + to_string(labelId++);
        if (site) site->num = (long long)idx;

        // 몸통 안에서 에러가 나면 어디서 부른 건지 같이 보여준다 (템플릿 에러 대책)
        bool ctx = (site != nullptr);
        if (ctx) {
            string where = (diag.files.size() > 1 && site->file < (int)diag.files.size())
                         ? diag.files[site->file].name + " " : string();
            errCtx.push_back("  '" + name + "' 를 " + where + to_string(site->line) +
                             "행에서 부름 (인자: " + typeList(argTy) + ")");
        }
        auto savedVar = move(varTy); varTy.clear();
        int* savedRet = curRet; bool savedIn = inFunc; string savedName = curName;
        int savedLoop = loopDepth; loopDepth = 0;   // 함수 몸통은 반복 밖에서 시작
        for (size_t i = 0; i < f.params.size(); i++) varTy[f.params[i]] = argTy[i];
        curRet = &in.rt; inFunc = true; curName = name;
        chkStmt(in.body.get());
        in.st = 2;
        if (in.rt == -1) in.rt = TINT;
        varTy = move(savedVar); curRet = savedRet; inFunc = savedIn; curName = savedName;
        loopDepth = savedLoop;
        if (ctx) errCtx.pop_back();
        return in.rt;
    }

    void chkExpr(Node* n) {
        if (n->kind == "num")   { n->ty = TINT; return; }
        if (n->kind == "flt")   { n->ty = TFLT; return; }
        if (n->kind == "str")   { n->ty = TSTR; return; }
        if (n->kind == "input") { n->ty = TSTR; return; }
        if (n->kind == "var") {
            auto it = varTy.find(n->str);
            if (it == varTy.end()) nerr(n, "정의되지 않은 변수 '" + n->str + "'");
            n->ty = it->second;
            return;
        }
        if (n->kind == "conv") {
            chkExpr(n->kid[0].get());
            if (isListTy(n->kid[0]->ty)) nerr(n, "목록은 '바꾸다' 로 바꿀 수 없음");
            if (isStructTy(n->kid[0]->ty)) nerr(n, "묶음은 '바꾸다' 로 바꿀 수 없음. 칸을 하나 꺼내 쓰기");
            n->ty = (n->str == "정수") ? TINT : (n->str == "소수") ? TFLT : TSTR;
            return;
        }
        if (n->kind == "list") {
            if (n->kid.empty()) { n->ty = TLU; return; }
            for (auto& k : n->kid) chkExpr(k.get());
            int e = n->kid[0]->ty;
            if (isListTy(e)) nerr(n->kid[0].get(), "목록 안에 목록은 아직 만들 수 없음");
            for (size_t i = 1; i < n->kid.size(); i++)
                if (n->kid[i]->ty != e)
                    nerr(n->kid[i].get(), "목록 안 값은 타입이 모두 같아야 함. 1번째가 " + tyName(e) +
                         "인데 이 값은 " + tyName(n->kid[i]->ty));
            n->ty = listOf(e);
            return;
        }
        if (n->kind == "index") {
            Node *b = n->kid[0].get(), *ix = n->kid[1].get();
            chkExpr(b); chkExpr(ix);
            if (!isListTy(b->ty) && b->ty != TSTR)
                nerr(n, "'의 N번째' 는 목록이나 문자에만 쓸 수 있음 (지금은 " + tyName(b->ty) + ")");
            if (b->ty == TLU)
                nerr(n, "빈 목록이라 원소 타입을 알 수 없음. 먼저 '목록에 값을 넣다.' 로 값을 넣기");
            if (ix->ty != TINT) {
                string m = "'번째' 앞은 정수여야 함";
                if (ix->ty == TFLT) m += ". 소수라면 '(값을 정수로 바꾸다)' 쓰기";
                nerr(ix, m);
            }
            n->ty = (b->ty == TSTR) ? TSTR : elemOf(b->ty);   // 문자의 N번째 = 글자 하나(문자)
            return;
        }
        if (n->kind == "len") {
            Node* b = n->kid[0].get();
            chkExpr(b);
            if (!isListTy(b->ty) && b->ty != TSTR)
                nerr(n, "'의 개수' 는 목록이나 문자에만 쓸 수 있음 (지금은 " + tyName(b->ty) + ")");
            n->ty = TINT;
            return;
        }
        if (n->kind == "new") {                  // (새 학생)
            for (size_t i = 0; i < structs.size(); i++)
                if (structs[i].name == n->str) {
                    n->ty = TSTRUCT + (int)i;
                    n->num = (long long)i;
                    return;
                }
            nerr(n, "'" + n->str + "' 이라는 묶음이 없음");
        }
        if (n->kind == "field") {                // 철수의 이름
            Node* b = n->kid[0].get();
            chkExpr(b);
            if (!isStructTy(b->ty))
                nerr(n, "'" + n->str + "' 은 묶음의 칸인데 " + tyName(b->ty) + " 에 쓰려고 함");
            int fi = findField(b->ty, n->str);
            if (fi < 0)
                nerr(n, "'" + tyName(b->ty) + "' 묶음에 '" + n->str + "' 칸이 없음");
            n->num = fi;
            n->ty = structs[(size_t)(b->ty - TSTRUCT)].fields[(size_t)fi].ty;
            return;
        }
        if (n->kind == "grid") {                 // 3행 4열  (그냥 쓰면 정수 격자, 전부 0)
            chkExpr(n->kid[0].get()); chkExpr(n->kid[1].get());
            if (n->kid[0]->ty != TINT || n->kid[1]->ty != TINT)
                nerr(n, "'N행 M열' 의 N 과 M 은 정수여야 함");
            n->ty = listOf(TINT);
            return;
        }
        if (n->kind == "fill") {                 // (3행 4열을 0.0으로 채우다)
            chkExpr(n->kid[0].get());            // 안쪽 grid 노드
            chkExpr(n->kid[1].get());
            int v = n->kid[1]->ty;
            if (isListTy(v)) nerr(n->kid[1].get(), "격자를 목록으로 채울 수는 없음");
            n->ty = listOf(v);
            return;
        }
        if (n->kind == "gridget") {              // 판의 2행 3열
            Node* b = n->kid[0].get();
            chkExpr(b); chkExpr(n->kid[1].get()); chkExpr(n->kid[2].get());
            if (!isListTy(b->ty))
                nerr(n, "'N행 M열' 은 격자에만 쓸 수 있음 (지금은 " + tyName(b->ty) + ")");
            if (b->ty == TLU) nerr(n, "빈 목록이라 원소 타입을 알 수 없음");
            if (n->kid[1]->ty != TINT) nerr(n->kid[1].get(), "'행' 앞은 정수여야 함");
            if (n->kid[2]->ty != TINT) nerr(n->kid[2].get(), "'열' 앞은 정수여야 함");
            n->ty = elemOf(b->ty);
            return;
        }
        if (n->kind == "gridrows" || n->kind == "gridcols") {
            Node* b = n->kid[0].get();
            chkExpr(b);
            if (!isListTy(b->ty))
                nerr(n, "'의 " + n->str + "' 는 격자에만 쓸 수 있음 (지금은 " +
                     tyName(b->ty) + ")");
            n->ty = TINT;
            return;
        }
        if (n->kind == "builtin") {
            Node* b = n->kid[0].get();
            chkExpr(b);
            const string& f = n->str;
            if (f == "코드") {                                  // "가"의 코드 -> 정수
                if (b->ty != TSTR)
                    nerr(b, "'의 코드' 는 문자에만 쓸 수 있음 (지금은 " + tyName(b->ty) + ")");
                n->ty = TINT;
                return;
            }
            if (f == "글자") {                                  // 65의 글자 -> "A"
                if (b->ty != TINT)
                    nerr(b, "'의 글자' 는 정수에만 쓸 수 있음 (지금은 " + tyName(b->ty) + ")");
                n->ty = TSTR;
                return;
            }
            if (b->ty == TSTR || isListTy(b->ty) || isStructTy(b->ty))
                nerr(b, "'의 " + f + "' 은 숫자에만 쓸 수 있음 (지금은 " + tyName(b->ty) + ")");
            if (f == "제곱근")      n->ty = TFLT;                 // 정수든 소수든 결과는 소수
            else if (f == "절댓값") n->ty = b->ty;                // 넣은 타입 그대로
            else if (f == "무작위") {
                if (b->ty != TINT) nerr(b, "'의 무작위' 앞은 정수여야 함 (예: (6의 무작위))");
                n->ty = TINT;
            }
            else n->ty = TINT;                                    // 반올림/올림/버림 -> 정수
            return;
        }
        if (n->kind == "un") {
            chkExpr(n->kid[0].get());
            int t = n->kid[0]->ty;
            if (isListTy(t))   nerr(n, "목록에는 '" + n->str + "' 를 쓸 수 없음");
            if (isStructTy(t)) nerr(n, "묶음에는 '" + n->str + "' 를 쓸 수 없음");
            if (n->str == "-") {
                if (t == TSTR) nerr(n, "문자에는 '-' 를 쓸 수 없음");
                n->ty = t;
            } else if (n->str == "~") {
                if (t != TINT) nerr(n, "'~' (비트 뒤집기) 는 정수에만 쓸 수 있음");
                n->ty = TINT;
            } else {
                if (t != TINT) nerr(n, "'!' 는 정수(참/거짓)에만 쓸 수 있음");
                n->ty = TINT;
            }
            return;
        }
        if (n->kind == "call") {
            if (!funcs->count(n->str)) nerr(n, "없는 함수 '" + n->str + "'");
            vector<int> at;
            for (auto& a : n->kid) { chkExpr(a.get()); at.push_back(a->ty); }
            n->ty = typeFunc(n->str, at, n);
            return;
        }
        if (n->kind == "bin") {
            Node *a = n->kid[0].get(), *b = n->kid[1].get();
            chkExpr(a); chkExpr(b);
            if (isListTy(a->ty) || isListTy(b->ty))
                nerr(n, "목록은 연산에 바로 쓸 수 없음. (목록의 1번째) 처럼 원소를 꺼내 쓰기");
            if (isStructTy(a->ty) || isStructTy(b->ty))
                nerr(n, "묶음은 연산에 바로 쓸 수 없음. (철수의 점수) 처럼 칸을 꺼내 쓰기");
            const string& o = n->str;
            bool num2 = (a->ty != TSTR && b->ty != TSTR);
            if (o == "+") {
                if (a->ty == TSTR && b->ty == TSTR) { n->ty = TSTR; return; }
                if (num2) { n->ty = (a->ty == TFLT || b->ty == TFLT) ? TFLT : TINT; return; }
                nerr(n, "숫자와 문자는 + 로 못 섞음. '정수로 바꾸다' 나 '문자로 바꾸다' 를 쓰기");
            }
            if (o == "==" || o == "!=") {
                if ((a->ty == TSTR) != (b->ty == TSTR)) nerr(n, "문자와 숫자는 비교할 수 없음");
                n->ty = TINT;
                return;
            }
            if (o == "-" || o == "*") {
                if (!num2) nerr(n, "'" + o + "' 는 숫자끼리만 됨");
                n->ty = (a->ty == TFLT || b->ty == TFLT) ? TFLT : TINT;
                return;
            }
            if (o == "/") {
                if (!num2) nerr(n, "'/' 는 숫자끼리만 됨");
                n->ty = TFLT;   // 나눗셈은 항상 소수 (7 / 2 = 3.5)
                return;
            }
            if (o == "&" || o == "|" || o == "^" || o == "<<" || o == ">>") {
                if (a->ty != TINT || b->ty != TINT)
                    nerr(n, "'" + o + "' (비트 연산) 은 정수끼리만 됨");
                n->ty = TINT;
                return;
            }
            if (o == "%" || o == "~/") {
                if (a->ty != TINT || b->ty != TINT) {
                    string what = (o == "%") ? "'%' (나머지)" : "'~/' (몫)";
                    string m = what + " 는 정수끼리만 됨";
                    if (a->ty == TFLT || b->ty == TFLT) m += ". 소수라면 '(값을 정수로 바꾸다)' 쓰기";
                    nerr(n, m);
                }
                n->ty = TINT;
                return;
            }
            if (o == "<" || o == ">" || o == "<=" || o == ">=") {
                if (a->ty == TSTR && b->ty == TSTR) { n->ty = TINT; return; }  // 사전순(가나다)
                if (!num2) nerr(n, "'" + o + "' 는 숫자끼리 또는 문자끼리만 됨");
                n->ty = TINT;
                return;
            }
            // && ||
            if (a->ty != TINT || b->ty != TINT)
                nerr(n, "'" + o + "' 는 정수(참/거짓)끼리만 됨");
            n->ty = TINT;
            return;
        }
        nerr(n, "알 수 없는 식");
    }

    void chkStmt(Node* n) {
        if (n->kind == "block") { for (auto& k : n->kid) chkStmt(k.get()); return; }
        if (n->kind == "assign") {
            chkExpr(n->kid[0].get());
            int t = n->kid[0]->ty;
            auto it = varTy.find(n->str);
            if (it == varTy.end()) varTy[n->str] = t;
            else if (it->second == TLU && isListTy(t)) it->second = t;      // 빈 목록 -> 타입 확정
            else if (isListTy(it->second) && t == TLU) { /* 빈 목록 재대입: 타입 유지 */ }
            else if (it->second != t) {
                string m = "'" + n->str + "' 는 " + tyName(it->second) + " 변수인데 " +
                           tyName(t) + "를 넣으려 함";
                if (it->second == TINT && t == TFLT)
                    m += ". 처음부터 소수로 쓰려면 0.0 처럼 만들기";
                nerr(n, m);
            }
            n->ty = t;
            return;
        }
        if (n->kind == "exprstmt") { chkExpr(n->kid[0].get()); return; }
        if (n->kind == "print") {
            chkExpr(n->kid[0].get());
            if (isListTy(n->kid[0]->ty))
                nerr(n->kid[0].get(), "목록은 통째로 말할 수 없음. (목록의 1번째) 처럼 하나씩 말하기");
            if (isStructTy(n->kid[0]->ty))
                nerr(n->kid[0].get(), "묶음은 통째로 말할 수 없음. (철수의 이름) 처럼 칸을 하나씩 말하기");
            return;
        }
        if (n->kind == "fieldset") {             // 철수의 이름은 "철수"이다.
            Node *b = n->kid[0].get(), *v = n->kid[1].get();
            chkExpr(b); chkExpr(v);
            if (!isStructTy(b->ty))
                nerr(n, "'" + n->str + "' 은 묶음의 칸인데 " + tyName(b->ty) + " 에 쓰려고 함");
            int fi = findField(b->ty, n->str);
            if (fi < 0)
                nerr(n, "'" + tyName(b->ty) + "' 묶음에 '" + n->str + "' 칸이 없음");
            int ft = structs[(size_t)(b->ty - TSTRUCT)].fields[(size_t)fi].ty;
            if (v->ty != ft)
                nerr(v, "'" + n->str + "' 칸은 " + tyName(ft) + " 인데 " + tyName(v->ty) + "를 넣으려 함");
            n->num = fi;
            return;
        }
        if (n->kind == "gridset") {              // 판의 2행 3열은 7이다.
            Node *b = n->kid[0].get(), *v = n->kid[3].get();
            chkExpr(b); chkExpr(n->kid[1].get()); chkExpr(n->kid[2].get()); chkExpr(v);
            if (!isListTy(b->ty))
                nerr(b, "'N행 M열' 은 격자에만 쓸 수 있음 (지금은 " + tyName(b->ty) + ")");
            if (n->kid[1]->ty != TINT) nerr(n->kid[1].get(), "'행' 앞은 정수여야 함");
            if (n->kid[2]->ty != TINT) nerr(n->kid[2].get(), "'열' 앞은 정수여야 함");
            if (isListTy(v->ty)) nerr(v, "격자 칸에 목록은 넣을 수 없음");
            if (b->ty == TLU) {
                if (b->kind == "var") { varTy[b->str] = listOf(v->ty); b->ty = listOf(v->ty); }
                else nerr(b, "빈 목록의 원소 타입을 알 수 없음");
            }
            if (v->ty != elemOf(b->ty))
                nerr(v, "이 격자는 " + tyName(elemOf(b->ty)) + "만 담을 수 있는데 " +
                     tyName(v->ty) + "를 넣으려 함");
            return;
        }
        if (n->kind == "idxset" || n->kind == "push") {
            bool isSet = (n->kind == "idxset");
            Node *b = n->kid[0].get();
            Node *ix = isSet ? n->kid[1].get() : nullptr;
            Node *v = n->kid[isSet ? 2 : 1].get();
            chkExpr(b);
            if (ix) chkExpr(ix);
            chkExpr(v);
            if (!isListTy(b->ty))
                nerr(b, (isSet ? string("'의 N번째' 는") : string("'에 넣다' 는")) +
                     " 목록에만 쓸 수 있음 (지금은 " + tyName(b->ty) + ")");
            if (ix && ix->ty != TINT) nerr(ix, "'번째' 앞은 정수여야 함");
            if (isListTy(v->ty)) nerr(v, "목록 안에 목록은 아직 넣을 수 없음");
            if (b->ty == TLU) {                       // 빈 목록: 첫 값이 원소 타입 확정
                if (b->kind == "var") { varTy[b->str] = listOf(v->ty); b->ty = listOf(v->ty); }
                else nerr(b, "빈 목록의 원소 타입을 알 수 없음");
            }
            if (v->ty != elemOf(b->ty))
                nerr(v, "이 목록은 " + tyName(b->ty) + "이라 " + tyName(elemOf(b->ty)) +
                     "만 넣을 수 있는데 " + tyName(v->ty) + "를 넣으려 함");
            return;
        }
        if (n->kind == "ret") {
            if (!inFunc) nerr(n, "'돌려주다' 는 함수 안에서만 쓸 수 있음");
            chkExpr(n->kid[0].get());
            int t = n->kid[0]->ty;
            if (*curRet == -1) *curRet = t;
            else if (*curRet != t)
                nerr(n, "'" + curName + "' 는 " + tyName(*curRet) + "를 돌려주기로 했는데 " +
                     tyName(t) + "를 돌려주려 함");
            return;
        }
        if (n->kind == "break" || n->kind == "continue") {
            if (loopDepth == 0)
                nerr(n, "'" + string(n->kind == "break" ? "멈추다" : "계속하다") +
                        "' 는 반복(반복하다 / 인 동안) 안에서만 쓸 수 있음");
            return;
        }
        if (n->kind == "if" || n->kind == "while" || n->kind == "repeat") {
            chkExpr(n->kid[0].get());
            if (n->kid[0]->ty != TINT)
                nerr(n->kid[0].get(), n->kind == "repeat" ? "반복 횟수는 정수여야 함" : "조건은 정수(참/거짓)여야 함");
            bool isLoop = (n->kind != "if");
            if (isLoop) loopDepth++;
            chkStmt(n->kid[1].get());
            if (isLoop) loopDepth--;
            if (n->kid.size() == 3) chkStmt(n->kid[2].get());
            return;
        }
        nerr(n, "알 수 없는 문장");
    }

    void run(Node* top, map<string,Func>& fs) {
        funcs = &fs;
        chkStmt(top);
        // 내 파일에 있는데 한 번도 안 불린 함수는 검사해준다 (인자는 전부 정수로 가정).
        // 불러온 파일(file != 0)의 안 쓴 함수는 건너뛴다 — 라이브러리엔 문자/목록을
        // 받는 함수가 잔뜩 있어서 정수로 가정하면 죄다 엉뚱한 에러가 난다.
        for (auto& kv : fs) {
            if (insts[kv.first].empty() && kv.second.file == 0)
                typeFunc(kv.first, vector<int>(kv.second.params.size(), TINT), nullptr);
        }
    }
};

// ==================== 5단계: 코드 생성 (x86-64, AT&T) ====================
// 값 표현: 모든 값은 64비트로 rax/스택을 지나간다.
//   정수 = 그대로, 문자 = 포인터, 소수 = double 비트 패턴.
//   소수 연산 때만 xmm 으로 옮겼다가 돌아온다.
//   -> 사용자 함수 호출이 타입과 무관하게 같은 규약(rdi, rsi, ...)을 쓴다.
struct Codegen {
    ostringstream text, rodata;
    map<string,int> slot;
    int frame = 0, labelId = 0, strId = 0;
    string retLabel;
    map<string,Func>* funcs;
    map<string, deque<Inst>>* insts;
    vector<pair<int,int>> loops;        // {멈추다 라벨, 계속하다 라벨}

    int newLabel() { return labelId++; }

    void collectVars(Node* n) {
        if (!n) return;
        if (n->kind == "assign" && !slot.count(n->str)) { frame += 8; slot[n->str] = -frame; }
        if (n->kind == "repeat") { frame += 8; n->num = -frame; }
        for (auto& k : n->kid) collectVars(k.get());
    }

    void push()  { text << "    subq $16, %rsp\n    movq %rax, (%rsp)\n"; }
    void popTo(const string& r) { text << "    movq (%rsp), " << r << "\n    addq $16, %rsp\n"; }
    void toF()   { text << "    cvtsi2sdq %rax, %xmm0\n    movq %xmm0, %rax\n"; }  // 정수 -> 소수 비트

    void genExpr(Node* n) {
        if (n->kind == "num") { text << "    movq $" << n->num << ", %rax\n"; return; }
        if (n->kind == "flt") {
            uint64_t bits; memcpy(&bits, &n->f, 8);
            text << "    movabsq $" << bits << ", %rax\n";
            return;
        }
        if (n->kind == "str") {
            int id = strId++;
            rodata << ".Lstr" << id << ":\n    .asciz \"" << escape(n->str) << "\"\n";
            text << "    leaq .Lstr" << id << "(%rip), %rax\n";
            return;
        }
        if (n->kind == "var") {
            if (!slot.count(n->str)) nerr(n, "정의되지 않은 변수 '" + n->str + "'");
            text << "    movq " << slot[n->str] << "(%rbp), %rax\n";
            return;
        }
        if (n->kind == "input") { text << "    call __han_input\n"; return; }
        if (n->kind == "list") {
            text << "    movl $" << (n->kid.empty() ? 1 : (int)n->kid.size()) << ", %edi\n"
                    "    call __han_lnew\n";
            push();                                    // 핸들을 스택에 두고 원소마다 lpush
            for (auto& e : n->kid) {
                genExpr(e.get());
                text << "    movq %rax, %rsi\n    movq (%rsp), %rdi\n    call __han_lpush\n";
            }
            popTo("%rax");
            return;
        }
        if (n->kind == "index") {
            bool str = (n->kid[0]->ty == TSTR);
            genExpr(n->kid[0].get()); push();
            genExpr(n->kid[1].get());
            text << "    movq %rax, %rsi\n";
            popTo("%rdi");
            text << "    movq $" << n->line << ", %rdx\n"
                 << "    call " << (str ? "__han_schar" : "__han_lget") << "\n";
            return;
        }
        if (n->kind == "len") {
            genExpr(n->kid[0].get());
            if (n->kid[0]->ty == TSTR) text << "    movq %rax, %rdi\n    call __han_slen\n";
            else                       text << "    movq (%rax), %rax\n";
            return;
        }
        if (n->kind == "new") {
            const Struct& s = structs[(size_t)n->num];
            text << "    movl $" << (s.fields.size() * 8) << ", %edi\n    call malloc\n";
            for (size_t i = 0; i < s.fields.size(); i++) {
                // 문자 칸은 빈 문자로 시작한다 (0 이면 널 포인터라 찍는 순간 죽는다)
                if (s.fields[i].ty == TSTR)
                    text << "    leaq .Lempty(%rip), %rcx\n    movq %rcx, " << (i*8) << "(%rax)\n";
                else
                    text << "    movq $0, " << (i*8) << "(%rax)\n";
            }
            return;
        }
        if (n->kind == "field") {
            genExpr(n->kid[0].get());
            text << "    movq " << (n->num * 8) << "(%rax), %rax\n";
            return;
        }
        if (n->kind == "grid" || n->kind == "fill") {
            // fill 의 kid[0] 은 grid 노드 — 그걸 다시 만들면 두 번 할당되므로 크기만 꺼내 쓴다
            Node* g = (n->kind == "fill") ? n->kid[0].get() : n;
            genExpr(g->kid[0].get()); push();          // 행
            genExpr(g->kid[1].get()); push();          // 열
            if (n->kind == "fill") genExpr(n->kid[1].get());
            else                   text << "    movq $0, %rax\n";
            text << "    movq %rax, %rdx\n";           // 채울 값
            popTo("%rsi");                             // 열
            popTo("%rdi");                             // 행
            text << "    movq $" << n->line << ", %rcx\n    call __han_gnew\n";
            return;
        }
        if (n->kind == "gridget") {
            genExpr(n->kid[0].get()); push();
            genExpr(n->kid[1].get()); push();
            genExpr(n->kid[2].get());
            text << "    movq %rax, %rdx\n";
            popTo("%rsi");
            popTo("%rdi");
            text << "    movq $" << n->line << ", %rcx\n    call __han_gget\n";
            return;
        }
        if (n->kind == "gridrows" || n->kind == "gridcols") {
            genExpr(n->kid[0].get());
            text << "    movq %rax, %rdi\n    movq $" << n->line << ", %rsi\n"
                 << "    call " << (n->kind == "gridrows" ? "__han_grows" : "__han_gcols") << "\n";
            return;
        }
        if (n->kind == "builtin") {
            Node* a = n->kid[0].get();
            const string& f = n->str;
            genExpr(a);
            if (f == "무작위") {
                text << "    movq %rax, %rdi\n    movq $" << n->line << ", %rsi\n"
                        "    call __han_rand\n";
                return;
            }
            if (f == "코드" || f == "글자") {
                text << "    movq %rax, %rdi\n    movq $" << n->line << ", %rsi\n"
                     << "    call " << (f == "코드" ? "__han_ord" : "__han_chr") << "\n";
                return;
            }
            if (f == "절댓값") {
                if (a->ty == TFLT)
                    text << "    movabsq $9223372036854775807, %rcx\n    andq %rcx, %rax\n"; // 부호 비트만 끔
                else
                    text << "    cqto\n    xorq %rdx, %rax\n    subq %rdx, %rax\n";
                return;
            }
            if (a->ty == TINT) toF();                       // 아래는 전부 소수 연산
            if (f == "제곱근") {
                text << "    movq %rax, %xmm0\n"
                        "    pxor %xmm1, %xmm1\n"
                        "    ucomisd %xmm1, %xmm0\n"
                        "    jae 1f\n"
                        "    movl $" << n->line << ", %edi\n    call __han_negsqrt\n1:\n"
                        "    sqrtsd %xmm0, %xmm0\n    movq %xmm0, %rax\n";
                return;
            }
            // 반올림 / 올림 / 버림 -> 정수
            const char* fn = (f == "반올림") ? "round" : (f == "올림") ? "ceil" : "floor";
            text << "    movq %rax, %xmm0\n    call " << fn << "\n"
                    "    cvttsd2si %xmm0, %rax\n";
            return;
        }
        if (n->kind == "conv") {
            Node* a = n->kid[0].get();
            genExpr(a);
            if (a->ty == n->ty) return;
            if (n->ty == TINT) {
                if (a->ty == TFLT) text << "    movq %rax, %xmm0\n    cvttsd2si %xmm0, %rax\n";
                else               text << "    movq %rax, %rdi\n    call atoll\n";
            } else if (n->ty == TFLT) {
                if (a->ty == TINT) toF();
                else               text << "    movq %rax, %rdi\n    call atof\n    movq %xmm0, %rax\n";
            } else { // TSTR
                if (a->ty == TINT) text << "    movq %rax, %rdi\n    call __han_tostr\n";
                else               text << "    movq %rax, %rdi\n    call __han_ftostr\n";
            }
            return;
        }
        if (n->kind == "un") {
            genExpr(n->kid[0].get());
            if (n->str == "-") {
                if (n->ty == TFLT)
                    text << "    movabsq $-9223372036854775808, %rcx\n    xorq %rcx, %rax\n"; // 부호 비트
                else
                    text << "    negq %rax\n";
            } else if (n->str == "~") {
                text << "    notq %rax\n";
            } else {
                text << "    cmpq $0, %rax\n    sete %al\n    movzbq %al, %rax\n";
            }
            return;
        }
        if (n->kind == "call") {
            for (auto& a : n->kid) { genExpr(a.get()); push(); }
            static const char* REG[6] = {"%rdi","%rsi","%rdx","%rcx","%r8","%r9"};
            for (int i = (int)n->kid.size() - 1; i >= 0; i--) popTo(REG[i]);
            // n->num = 검사기가 정해준 인자타입 조합 번호
            text << "    call " << (*insts)[n->str][(size_t)n->num].label << "\n";
            return;
        }
        if (n->kind == "bin") {
            const string& o = n->str;
            Node *a = n->kid[0].get(), *b = n->kid[1].get();
            if (o == "&&" || o == "||") {
                int L = newLabel();
                genExpr(a);
                text << "    cmpq $0, %rax\n";
                text << (o == "&&" ? "    je .L" : "    jne .L") << L << "\n";
                genExpr(b);
                text << "    cmpq $0, %rax\n    setne %al\n    movzbq %al, %rax\n";
                int E = newLabel();
                text << "    jmp .L" << E << "\n.L" << L << ":\n";
                text << "    movq $" << (o == "&&" ? 0 : 1) << ", %rax\n";
                text << ".L" << E << ":\n";
                return;
            }
            if (o == "+" && n->ty == TSTR) {
                genExpr(a); push();
                genExpr(b);
                text << "    movq %rax, %rsi\n";
                popTo("%rdi");
                text << "    call __han_concat\n";
                return;
            }
            if (a->ty == TSTR && (o == "==" || o == "!=" || o == "<" ||
                                  o == ">"  || o == "<=" || o == ">=")) {
                genExpr(a); push();
                genExpr(b);
                text << "    movq %rax, %rsi\n";
                popTo("%rdi");
                string cc = o == "==" ? "sete" : o == "!=" ? "setne" : o == "<" ? "setl"
                          : o == ">"  ? "setg" : o == "<=" ? "setle" : "setge";
                // strcmp 는 int(eax)를 준다. rax 위쪽 32비트는 쓰레기일 수 있으므로 cmpl 로 비교.
                text << "    call strcmp\n    cmpl $0, %eax\n    " << cc
                     << " %al\n    movzbq %al, %rax\n";
                return;
            }
            bool flt = (a->ty == TFLT || b->ty == TFLT || o == "/");
            if (flt) {
                genExpr(a); if (a->ty == TINT) toF();
                push();
                genExpr(b); if (b->ty == TINT) toF();
                text << "    movq %rax, %xmm1\n";
                popTo("%rax");
                text << "    movq %rax, %xmm0\n";
                if (o == "/") {
                    text << "    pxor %xmm2, %xmm2\n    ucomisd %xmm2, %xmm1\n"
                            "    jp 1f\n    jne 1f\n"
                            "    movl $" << n->line << ", %edi\n    call __han_div0\n1:\n";
                }
                if (o == "+") text << "    addsd %xmm1, %xmm0\n    movq %xmm0, %rax\n";
                else if (o == "-") text << "    subsd %xmm1, %xmm0\n    movq %xmm0, %rax\n";
                else if (o == "*") text << "    mulsd %xmm1, %xmm0\n    movq %xmm0, %rax\n";
                else if (o == "/") text << "    divsd %xmm1, %xmm0\n    movq %xmm0, %rax\n";
                else {
                    string cc = o == "<" ? "setb" : o == ">" ? "seta" : o == "<=" ? "setbe"
                              : o == ">=" ? "setae" : o == "==" ? "sete" : "setne";
                    text << "    comisd %xmm1, %xmm0\n    " << cc << " %al\n    movzbq %al, %rax\n";
                }
                return;
            }
            genExpr(a); push();
            genExpr(b);
            text << "    movq %rax, %rcx\n";
            popTo("%rax");
            if (o == "+") text << "    addq %rcx, %rax\n";
            else if (o == "-") text << "    subq %rcx, %rax\n";
            else if (o == "*") text << "    imulq %rcx, %rax\n";
            else if (o == "&") text << "    andq %rcx, %rax\n";
            else if (o == "|") text << "    orq %rcx, %rax\n";
            else if (o == "^") text << "    xorq %rcx, %rax\n";
            else if (o == "<<" || o == ">>") {
                // 0~63 밖으로 밀면 x86 이 6비트로 잘라버려서 결과가 엉뚱해진다. 미리 막는다.
                // 부호 없는 비교라 음수도 한 번에 걸린다 (음수는 아주 큰 수로 보임)
                text << "    cmpq $63, %rcx\n    jbe 1f\n"
                        "    movl $" << n->line << ", %edi\n"
                        "    movq %rcx, %rsi\n    call __han_shift\n1:\n";
                text << (o == "<<" ? "    shlq %cl, %rax\n" : "    sarq %cl, %rax\n");
            }
            else if (o == "%" || o == "~/") {
                text << "    testq %rcx, %rcx\n    jne 1f\n"
                        "    movl $" << n->line << ", %edi\n    call __han_div0\n1:\n";
                text << "    cqto\n    idivq %rcx\n";
                if (o == "%") text << "    movq %rdx, %rax\n";   // 나머지, 몫은 이미 rax
            }
            else {
                string cc = o == "<" ? "setl" : o == ">" ? "setg" : o == "<=" ? "setle"
                          : o == ">=" ? "setge" : o == "==" ? "sete" : "setne";
                text << "    cmpq %rcx, %rax\n    " << cc << " %al\n    movzbq %al, %rax\n";
            }
            return;
        }
        nerr(n, "알 수 없는 식");
    }

    void genStmt(Node* n) {
        if (n->kind == "block") { for (auto& k : n->kid) genStmt(k.get()); return; }
        if (n->kind == "assign") {
            genExpr(n->kid[0].get());
            text << "    movq %rax, " << slot[n->str] << "(%rbp)\n";
            return;
        }
        if (n->kind == "exprstmt") { genExpr(n->kid[0].get()); return; }
        if (n->kind == "idxset") {
            genExpr(n->kid[0].get()); push();
            genExpr(n->kid[1].get()); push();
            genExpr(n->kid[2].get());
            text << "    movq %rax, %rdx\n";
            popTo("%rsi");
            popTo("%rdi");
            text << "    movq $" << n->line << ", %rcx\n    call __han_lset\n";
            return;
        }
        if (n->kind == "fieldset") {
            genExpr(n->kid[0].get()); push();
            genExpr(n->kid[1].get());
            text << "    movq %rax, %rdx\n";
            popTo("%rax");
            text << "    movq %rdx, " << (n->num * 8) << "(%rax)\n";
            return;
        }
        if (n->kind == "gridset") {
            genExpr(n->kid[0].get()); push();
            genExpr(n->kid[1].get()); push();
            genExpr(n->kid[2].get()); push();
            genExpr(n->kid[3].get());
            text << "    movq %rax, %rcx\n";
            popTo("%rdx");
            popTo("%rsi");
            popTo("%rdi");
            text << "    movq $" << n->line << ", %r8\n    call __han_gset\n";
            return;
        }
        if (n->kind == "push") {
            genExpr(n->kid[0].get()); push();
            genExpr(n->kid[1].get());
            text << "    movq %rax, %rsi\n";
            popTo("%rdi");
            text << "    call __han_lpush\n";
            return;
        }
        if (n->kind == "print") {
            Node* a = n->kid[0].get();
            genExpr(a);
            if (n->num == 0) {                       // 기본: 줄바꿈으로 끝
                if (a->ty == TSTR)
                    text << "    movq %rax, %rdi\n    call puts\n";
                else if (a->ty == TFLT)
                    text << "    movq %rax, %xmm0\n    leaq .LfmtF(%rip), %rdi\n"
                            "    movl $1, %eax\n    call printf\n";
                else
                    text << "    movq %rax, %rsi\n    leaq .Lfmt(%rip), %rdi\n"
                            "    xorl %eax, %eax\n    call printf\n";
                return;
            }
            // 끝"..." 옵션: 값만 찍고 정해준 문자로 끝맺는다
            if (a->ty == TSTR)
                text << "    movq %rax, %rsi\n    leaq .LfmtS(%rip), %rdi\n"
                        "    xorl %eax, %eax\n    call printf\n";
            else if (a->ty == TFLT)
                text << "    movq %rax, %xmm0\n    leaq .LfmtG(%rip), %rdi\n"
                        "    movl $1, %eax\n    call printf\n";
            else
                text << "    movq %rax, %rsi\n    leaq .Lfmt2(%rip), %rdi\n"
                        "    xorl %eax, %eax\n    call printf\n";
            if (!n->str.empty()) {
                int id = strId++;
                rodata << ".Lstr" << id << ":\n    .asciz \"" << escape(n->str) << "\"\n";
                // 사용자 문자열을 형식 문자열로 쓰면 안 되므로 %s 로 넘긴다
                text << "    leaq .Lstr" << id << "(%rip), %rsi\n"
                        "    leaq .LfmtS(%rip), %rdi\n"
                        "    xorl %eax, %eax\n    call printf\n";
            }
            return;
        }
        if (n->kind == "ret") {
            genExpr(n->kid[0].get());
            text << "    jmp " << retLabel << "\n";
            return;
        }
        if (n->kind == "if") {
            int Lelse = newLabel(), Lend = newLabel();
            genExpr(n->kid[0].get());
            text << "    cmpq $0, %rax\n    je .L" << Lelse << "\n";
            genStmt(n->kid[1].get());
            text << "    jmp .L" << Lend << "\n.L" << Lelse << ":\n";
            if (n->kid.size() == 3) genStmt(n->kid[2].get());
            text << ".L" << Lend << ":\n";
            return;
        }
        if (n->kind == "while") {
            int Ltop = newLabel(), Lend = newLabel();
            text << ".L" << Ltop << ":\n";
            genExpr(n->kid[0].get());
            text << "    cmpq $0, %rax\n    je .L" << Lend << "\n";
            loops.push_back({Lend, Ltop});      // 계속하다 = 조건 다시 보기
            genStmt(n->kid[1].get());
            loops.pop_back();
            text << "    jmp .L" << Ltop << "\n.L" << Lend << ":\n";
            return;
        }
        if (n->kind == "repeat") {
            int off = (int)n->num, Ltop = newLabel(), Lend = newLabel(), Lcont = newLabel();
            genExpr(n->kid[0].get());
            text << "    movq %rax, " << off << "(%rbp)\n";
            text << ".L" << Ltop << ":\n";
            text << "    cmpq $0, " << off << "(%rbp)\n    jle .L" << Lend << "\n";
            // 계속하다 는 Ltop 이 아니라 Lcont 로 가야 한다. 안 그러면 횟수가 안 줄어 무한 반복.
            loops.push_back({Lend, Lcont});
            genStmt(n->kid[1].get());
            loops.pop_back();
            text << ".L" << Lcont << ":\n";
            text << "    decq " << off << "(%rbp)\n    jmp .L" << Ltop << "\n";
            text << ".L" << Lend << ":\n";
            return;
        }
        if (n->kind == "break")    { text << "    jmp .L" << loops.back().first  << "\n"; return; }
        if (n->kind == "continue") { text << "    jmp .L" << loops.back().second << "\n"; return; }
        nerr(n, "알 수 없는 문장");
    }

    static string escape(const string& s) {
        string r;
        for (char c : s) {
            if (c == '"' || c == '\\') { r += '\\'; r += c; }
            else if (c == '\n') r += "\\n";
            else if (c == '\t') r += "\\t";
            else r += c;
        }
        return r;
    }

    void genFunc(const string& label, Func* f, Node* body) {
        slot.clear(); frame = 0;
        if (f) for (auto& prm : f->params) { frame += 8; slot[prm] = -frame; }
        collectVars(body);
        int aligned = (frame + 15) / 16 * 16;
        retLabel = ".Lret_" + label;

        text << "\n.globl " << label << "\n" << label << ":\n";
        text << "    pushq %rbp\n    movq %rsp, %rbp\n";
        if (aligned) text << "    subq $" << aligned << ", %rsp\n";
        if (f) {
            static const char* REG[6] = {"%rdi","%rsi","%rdx","%rcx","%r8","%r9"};
            for (size_t i = 0; i < f->params.size(); i++)
                text << "    movq " << REG[i] << ", " << slot[f->params[i]] << "(%rbp)\n";
        }
        genStmt(body);
        if (!f) text << "    xorl %eax, %eax\n";
        text << retLabel << ":\n    leave\n    ret\n";
    }

    static const char* runtime() {
        return
// getline 을 쓴다. 예전엔 256바이트 고정 버퍼 + fgets 라서 긴 줄이 조용히 잘렸다.
// getline 은 필요한 만큼 버퍼를 늘려주므로 줄 길이 제한이 없다.
"__han_input:\n"
"    pushq %rbp\n    movq %rsp, %rbp\n"
"    leaq .Lgline(%rip), %rdi\n"
"    leaq .Lgcap(%rip), %rsi\n"
"    movq stdin(%rip), %rdx\n"
"    call getline\n"
"    testq %rax, %rax\n"
"    jg .Lin_ok\n"
"    leaq .Lempty(%rip), %rdi\n"        // 입력 끝 -> 빈 문자
"    call strdup\n"
"    leave\n    ret\n"
".Lin_ok:\n"
"    movq .Lgline(%rip), %rdi\n"
"    leaq .Lnl(%rip), %rsi\n"
"    call strcspn\n"                    // 줄바꿈(\\r 도) 자리 찾기
"    movq .Lgline(%rip), %rdi\n"
"    movb $0, (%rdi,%rax)\n"
"    call strdup\n"
"    leave\n    ret\n"
"\n__han_tostr:\n"
"    pushq %rbp\n    movq %rsp, %rbp\n"
"    subq $16, %rsp\n"
"    movq %rdi, 8(%rsp)\n"
"    movl $40, %edi\n"
"    call malloc\n"
"    movq %rax, 0(%rsp)\n"
"    movq %rax, %rdi\n"
"    leaq .Lfmt2(%rip), %rsi\n"
"    movq 8(%rsp), %rdx\n"
"    xorl %eax, %eax\n"
"    call sprintf\n"
"    movq 0(%rsp), %rax\n"
"    leave\n    ret\n"
"\n__han_ftostr:\n"
"    pushq %rbp\n    movq %rsp, %rbp\n"
"    subq $16, %rsp\n"
"    movq %rdi, 8(%rsp)\n"
"    movl $40, %edi\n"
"    call malloc\n"
"    movq %rax, 0(%rsp)\n"
"    movq %rax, %rdi\n"
"    leaq .LfmtG(%rip), %rsi\n"
"    movq 8(%rsp), %xmm0\n"
"    movl $1, %eax\n"
"    call sprintf\n"
"    movq 0(%rsp), %rax\n"
"    leave\n    ret\n"
"\n__han_concat:\n"
"    pushq %rbp\n    movq %rsp, %rbp\n"
"    subq $32, %rsp\n"
"    movq %rdi, 0(%rsp)\n"
"    movq %rsi, 8(%rsp)\n"
"    call strlen\n"
"    movq %rax, 16(%rsp)\n"
"    movq 8(%rsp), %rdi\n"
"    call strlen\n"
"    addq 16(%rsp), %rax\n"
"    leaq 1(%rax), %rdi\n"
"    call malloc\n"
"    movq %rax, 16(%rsp)\n"
"    movq %rax, %rdi\n"
"    movq 0(%rsp), %rsi\n"
"    call strcpy\n"
"    movq 16(%rsp), %rdi\n"
"    movq 8(%rsp), %rsi\n"
"    call strcat\n"
"    movq 16(%rsp), %rax\n"
"    leave\n    ret\n"
"\n__han_div0:\n"
"    pushq %rbp\n    movq %rsp, %rbp\n"
"    movl %edi, %esi\n"
"    leaq .Ldiv0(%rip), %rdi\n"
"    xorl %eax, %eax\n"
"    call printf\n"
"    movl $1, %edi\n"
"    call exit\n"
// 목록 = 핸들 24바이트 { len, cap, data* }. 핸들 주소는 성장(realloc) 후에도 안 변함.
"\n__han_lnew:\n"                       // rdi = 처음 용량(>=1) -> rax = 핸들
"    pushq %rbp\n    movq %rsp, %rbp\n"
"    subq $16, %rsp\n"
"    movq %rdi, 0(%rsp)\n"
"    movl $32, %edi\n"                  // 핸들 32바이트: len, cap, data*, cols
"    call malloc\n"
"    movq %rax, 8(%rsp)\n"
"    movq $0, 0(%rax)\n"
"    movq $0, 24(%rax)\n"               // cols = 0 -> 평범한 목록 (격자 아님)

"    movq 0(%rsp), %rcx\n"
"    movq %rcx, 8(%rax)\n"
"    leaq 0(,%rcx,8), %rdi\n"
"    call malloc\n"
"    movq 8(%rsp), %rcx\n"
"    movq %rax, 16(%rcx)\n"
"    movq %rcx, %rax\n"
"    leave\n    ret\n"
"\n__han_lpush:\n"                      // rdi = 핸들, rsi = 값
"    pushq %rbp\n    movq %rsp, %rbp\n"
"    subq $16, %rsp\n"
"    cmpq $0, 24(%rdi)\n"               // 격자는 칸 수가 고정이라 늘리면 안 됨
"    jne .Lpush_grid\n"
"    movq %rdi, 0(%rsp)\n"
"    movq %rsi, 8(%rsp)\n"
"    movq 0(%rdi), %rax\n"
"    cmpq 8(%rdi), %rax\n"
"    jne 1f\n"
"    movq 8(%rdi), %rcx\n"              // 꽉 참: 용량 2배
"    addq %rcx, %rcx\n"
"    movq %rcx, 8(%rdi)\n"
"    leaq 0(,%rcx,8), %rsi\n"
"    movq 16(%rdi), %rdi\n"
"    call realloc\n"
"    movq 0(%rsp), %rdi\n"
"    movq %rax, 16(%rdi)\n"
"1:\n"
"    movq 0(%rdi), %rax\n"
"    movq 16(%rdi), %rcx\n"
"    movq 8(%rsp), %rdx\n"
"    movq %rdx, (%rcx,%rax,8)\n"
"    incq 0(%rdi)\n"
"    leave\n    ret\n"
"\n__han_lget:\n"                       // rdi = 핸들, rsi = N(1부터), rdx = 행번호
"    cmpq $1, %rsi\n"
"    jl 1f\n"
"    cmpq 0(%rdi), %rsi\n"
"    jg 1f\n"
"    movq 16(%rdi), %rcx\n"
"    movq -8(%rcx,%rsi,8), %rax\n"
"    ret\n"
"1:\n"
"    pushq %rbp\n    movq %rsp, %rbp\n"
"    movq 0(%rdi), %rcx\n"
"    movq %rsi, %r8\n"
"    movq %rdx, %rsi\n"
"    movq %r8, %rdx\n"
"    leaq .Loob(%rip), %rdi\n"
"    xorl %eax, %eax\n"
"    call printf\n"
"    movl $1, %edi\n"
"    call exit\n"
"\n__han_lset:\n"                       // rdi = 핸들, rsi = N(1부터), rdx = 값, rcx = 행번호
"    cmpq $1, %rsi\n"
"    jl 1f\n"
"    cmpq 0(%rdi), %rsi\n"
"    jg 1f\n"
"    movq 16(%rdi), %r8\n"
"    movq %rdx, -8(%r8,%rsi,8)\n"
"    ret\n"
"1:\n"
"    pushq %rbp\n    movq %rsp, %rbp\n"
"    movq 0(%rdi), %r8\n"
"    movq %rsi, %rdx\n"
"    movq %rcx, %rsi\n"
"    movq %r8, %rcx\n"
"    leaq .Loob(%rip), %rdi\n"
"    xorl %eax, %eax\n"
"    call printf\n"
"    movl $1, %edi\n"
"    call exit\n"
"\n__han_negsqrt:\n"                    // edi = 행번호
"    pushq %rbp\n    movq %rsp, %rbp\n"
"    movl %edi, %esi\n"
"    leaq .Lnegsqrt(%rip), %rdi\n"
"    xorl %eax, %eax\n"
"    call printf\n"
"    movl $1, %edi\n"
"    call exit\n"
"\n__han_rand:\n"                       // rdi = N -> 1..N 중 하나, rsi = 행번호
"    pushq %rbp\n    movq %rsp, %rbp\n"
"    subq $16, %rsp\n"
"    movq %rdi, 0(%rsp)\n"
"    movq %rsi, 8(%rsp)\n"
"    cmpq $1, %rdi\n"
"    jl .Lrand_err\n"
"    movl .Lseeded(%rip), %eax\n"       // 첫 호출에만 씨앗 심기
"    testl %eax, %eax\n"
"    jne .Lrand_go\n"
"    movl $1, %eax\n"
"    movl %eax, .Lseeded(%rip)\n"
"    xorl %edi, %edi\n"
"    call time\n"
"    movl %eax, %edi\n"
"    call srand\n"
".Lrand_go:\n"
"    call rand\n"
"    cltq\n"
"    cqto\n"
"    idivq 0(%rsp)\n"
"    movq %rdx, %rax\n"
"    incq %rax\n"
"    leave\n    ret\n"
".Lrand_err:\n"
"    movq 0(%rsp), %rdx\n"
"    movq 8(%rsp), %rsi\n"
"    leaq .Lrand0(%rip), %rdi\n"
"    xorl %eax, %eax\n"
"    call printf\n"
"    movl $1, %edi\n"
"    call exit\n"
"\n__han_slen:\n"                       // rdi = 문자 -> rax = 글자수 (UTF-8 이어붙임 바이트는 안 셈)
"    xorl %eax, %eax\n"
".Lsl_loop:\n"
"    movzbl (%rdi), %ecx\n"
"    testb %cl, %cl\n"
"    je .Lsl_done\n"
"    incq %rdi\n"
"    andb $192, %cl\n"
"    cmpb $128, %cl\n"
"    je .Lsl_loop\n"
"    incq %rax\n"
"    jmp .Lsl_loop\n"
".Lsl_done:\n"
"    ret\n"
"\n__han_schar:\n"                      // rdi = 문자, rsi = N(1부터), rdx = 행번호 -> 글자 하나
"    pushq %rbp\n    movq %rsp, %rbp\n"
"    subq $48, %rsp\n"
"    movq %rdi, 0(%rsp)\n"
"    movq %rsi, 8(%rsp)\n"
"    movq %rdx, 16(%rsp)\n"
"    cmpq $1, %rsi\n"
"    jl .Lsc_err\n"
"    movq %rdi, %rcx\n"
"    movq %rsi, %r8\n"
"    decq %r8\n"                        // 앞의 (N-1) 글자 건너뛰기
".Lsc_skip:\n"
"    testq %r8, %r8\n"
"    je .Lsc_at\n"
"    movzbl (%rcx), %eax\n"
"    testb %al, %al\n"
"    je .Lsc_err\n"
"    incq %rcx\n"
".Lsc_tail:\n"
"    movzbl (%rcx), %eax\n"
"    testb %al, %al\n"
"    je .Lsc_next\n"
"    andb $192, %al\n"
"    cmpb $128, %al\n"
"    jne .Lsc_next\n"
"    incq %rcx\n"
"    jmp .Lsc_tail\n"
".Lsc_next:\n"
"    decq %r8\n"
"    jmp .Lsc_skip\n"
".Lsc_at:\n"                            // rcx = N번째 글자 시작
"    movzbl (%rcx), %eax\n"
"    testb %al, %al\n"
"    je .Lsc_err\n"
"    movq %rcx, %r9\n"
"    incq %r9\n"
".Lsc_len:\n"
"    movzbl (%r9), %eax\n"
"    testb %al, %al\n"
"    je .Lsc_cut\n"
"    andb $192, %al\n"
"    cmpb $128, %al\n"
"    jne .Lsc_cut\n"
"    incq %r9\n"
"    jmp .Lsc_len\n"
".Lsc_cut:\n"
"    subq %rcx, %r9\n"                  // r9 = 이 글자의 바이트 수
"    movq %rcx, 24(%rsp)\n"
"    movq %r9, 32(%rsp)\n"
"    leaq 1(%r9), %rdi\n"
"    call malloc\n"
"    movq %rax, 40(%rsp)\n"
"    movq %rax, %rdi\n"
"    movq 24(%rsp), %rsi\n"
"    movq 32(%rsp), %rdx\n"
"    call memcpy\n"
"    movq 40(%rsp), %rax\n"
"    movq 32(%rsp), %rcx\n"
"    movb $0, (%rax,%rcx)\n"
"    leave\n    ret\n"
".Lsc_err:\n"
"    movq 0(%rsp), %rdi\n"
"    call __han_slen\n"
"    movq %rax, %rcx\n"
"    movq 16(%rsp), %rsi\n"
"    movq 8(%rsp), %rdx\n"
"    leaq .Lsoob(%rip), %rdi\n"
"    xorl %eax, %eax\n"
"    call printf\n"
"    movl $1, %edi\n"
"    call exit\n"
".Lpush_grid:\n"
"    leaq .Lgpush(%rip), %rdi\n"
"    xorl %eax, %eax\n"
"    call printf\n"
"    movl $1, %edi\n"
"    call exit\n"
// ---- 격자 ----
// 격자도 그냥 목록이다. 핸들의 cols 가 0이 아니면 격자로 취급하고 (행-1)*cols+(열-1) 로 접는다.
"\n__han_gnew:\n"                       // rdi=행수, rsi=열수, rdx=채울값, rcx=행번호
"    pushq %rbp\n    movq %rsp, %rbp\n"
"    subq $48, %rsp\n"
"    movq %rdi, 0(%rsp)\n"
"    movq %rsi, 8(%rsp)\n"
"    movq %rdx, 16(%rsp)\n"
"    movq %rcx, 24(%rsp)\n"
"    cmpq $1, %rdi\n"
"    jl .Lgn_bad\n"
"    cmpq $1, %rsi\n"
"    jl .Lgn_bad\n"
"    movq %rdi, %rax\n"
"    imulq %rsi, %rax\n"
"    movq %rax, 32(%rsp)\n"             // 전체 칸 수
"    movl $32, %edi\n"
"    call malloc\n"
"    movq %rax, 40(%rsp)\n"
"    movq 32(%rsp), %rcx\n"
"    movq %rcx, 0(%rax)\n"
"    movq %rcx, 8(%rax)\n"
"    movq 8(%rsp), %rdx\n"
"    movq %rdx, 24(%rax)\n"             // cols
"    leaq 0(,%rcx,8), %rdi\n"
"    call malloc\n"
"    movq 40(%rsp), %rcx\n"
"    movq %rax, 16(%rcx)\n"
"    movq 32(%rsp), %rdx\n"
"    movq 16(%rsp), %r8\n"
"    xorq %r9, %r9\n"
".Lgn_loop:\n"
"    cmpq %rdx, %r9\n"
"    jge .Lgn_done\n"
"    movq %r8, (%rax,%r9,8)\n"
"    incq %r9\n"
"    jmp .Lgn_loop\n"
".Lgn_done:\n"
"    movq 40(%rsp), %rax\n"
"    leave\n    ret\n"
".Lgn_bad:\n"
"    movq 24(%rsp), %rsi\n"
"    movq 0(%rsp), %rdx\n"
"    movq 8(%rsp), %rcx\n"
"    leaq .Lgsize(%rip), %rdi\n"
"    xorl %eax, %eax\n"
"    call printf\n"
"    movl $1, %edi\n"
"    call exit\n"
"\n__han_gget:\n"                       // rdi=핸들, rsi=행, rdx=열, rcx=행번호
"    movq 24(%rdi), %r8\n"
"    testq %r8, %r8\n"
"    je .Lgg_notgrid\n"
"    cmpq $1, %rsi\n"
"    jl .Lgg_oob\n"
"    cmpq $1, %rdx\n"
"    jl .Lgg_oob\n"
"    cmpq %r8, %rdx\n"
"    jg .Lgg_oob\n"
"    movq %rsi, %rax\n"
"    decq %rax\n"
"    imulq %r8, %rax\n"
"    addq %rdx, %rax\n"
"    decq %rax\n"
"    cmpq 0(%rdi), %rax\n"
"    jge .Lgg_oob\n"
"    movq 16(%rdi), %r9\n"
"    movq (%r9,%rax,8), %rax\n"
"    ret\n"
".Lgg_notgrid:\n"
"    pushq %rbp\n    movq %rsp, %rbp\n"
"    movq %rcx, %rsi\n"
"    leaq .Lnotgrid(%rip), %rdi\n"
"    xorl %eax, %eax\n"
"    call printf\n"
"    movl $1, %edi\n"
"    call exit\n"
".Lgg_oob:\n"
"    pushq %rbp\n    movq %rsp, %rbp\n"
"    movq %rsi, %r10\n"
"    movq %rdx, %r11\n"
"    movq 0(%rdi), %rax\n"
"    cqto\n"
"    idivq %r8\n"                       // rax = 행수 (r8 = 열수 그대로)
"    movq %r8, %r9\n"
"    movq %rax, %r8\n"
"    movq %rcx, %rsi\n"
"    movq %r10, %rdx\n"
"    movq %r11, %rcx\n"
"    leaq .Lgoob(%rip), %rdi\n"
"    xorl %eax, %eax\n"
"    call printf\n"
"    movl $1, %edi\n"
"    call exit\n"
"\n__han_gset:\n"                       // rdi=핸들, rsi=행, rdx=열, rcx=값, r8=행번호
"    movq 24(%rdi), %r9\n"
"    testq %r9, %r9\n"
"    je .Lgs_notgrid\n"
"    cmpq $1, %rsi\n"
"    jl .Lgs_oob\n"
"    cmpq $1, %rdx\n"
"    jl .Lgs_oob\n"
"    cmpq %r9, %rdx\n"
"    jg .Lgs_oob\n"
"    movq %rsi, %rax\n"
"    decq %rax\n"
"    imulq %r9, %rax\n"
"    addq %rdx, %rax\n"
"    decq %rax\n"
"    cmpq 0(%rdi), %rax\n"
"    jge .Lgs_oob\n"
"    movq 16(%rdi), %r10\n"
"    movq %rcx, (%r10,%rax,8)\n"
"    ret\n"
".Lgs_notgrid:\n"
"    pushq %rbp\n    movq %rsp, %rbp\n"
"    movq %r8, %rsi\n"
"    leaq .Lnotgrid(%rip), %rdi\n"
"    xorl %eax, %eax\n"
"    call printf\n"
"    movl $1, %edi\n"
"    call exit\n"
".Lgs_oob:\n"
"    pushq %rbp\n    movq %rsp, %rbp\n"
"    movq %rsi, %r10\n"
"    movq %rdx, %r11\n"
"    movq 0(%rdi), %rax\n"
"    cqto\n"
"    idivq %r9\n"                       // rax = 행수 (r9 = 열수 그대로)
"    movq %r8, %rsi\n"
"    movq %r10, %rdx\n"
"    movq %r11, %rcx\n"
"    movq %rax, %r8\n"
"    leaq .Lgoob(%rip), %rdi\n"
"    xorl %eax, %eax\n"
"    call printf\n"
"    movl $1, %edi\n"
"    call exit\n"
"\n__han_grows:\n"                      // rdi=핸들, rsi=행번호
"    movq 24(%rdi), %rcx\n"
"    testq %rcx, %rcx\n"
"    je .Lgr_notgrid\n"
"    movq 0(%rdi), %rax\n"
"    cqto\n"
"    idivq %rcx\n"
"    ret\n"
".Lgr_notgrid:\n"
"    pushq %rbp\n    movq %rsp, %rbp\n"
"    leaq .Lnotgrid(%rip), %rdi\n"
"    xorl %eax, %eax\n"
"    call printf\n"
"    movl $1, %edi\n"
"    call exit\n"
"\n__han_gcols:\n"                      // rdi=핸들, rsi=행번호
"    movq 24(%rdi), %rax\n"
"    testq %rax, %rax\n"
"    je .Lgc_notgrid\n"
"    ret\n"
".Lgc_notgrid:\n"
"    pushq %rbp\n    movq %rsp, %rbp\n"
"    leaq .Lnotgrid(%rip), %rdi\n"
"    xorl %eax, %eax\n"
"    call printf\n"
"    movl $1, %edi\n"
"    call exit\n"
"\n__han_shift:\n"                      // edi = 행번호, rsi = 밀려던 칸 수
"    pushq %rbp\n    movq %rsp, %rbp\n"
"    movq %rsi, %rdx\n"
"    movl %edi, %esi\n"
"    leaq .Lshift(%rip), %rdi\n"
"    xorl %eax, %eax\n"
"    call printf\n"
"    movl $1, %edi\n"
"    call exit\n"
// 문자 <-> 코드값. UTF-8 을 직접 풀고 감는다 (한글도 한 글자 = 코드값 하나).
"\n__han_ord:\n"                        // rdi = 문자, rsi = 행번호 -> 첫 글자의 코드값
"    movzbl (%rdi), %eax\n"
"    testb %al, %al\n"
"    je .Lord_empty\n"
"    cmpb $128, %al\n"
"    jb .Lord_done\n"                   // 0xxxxxxx : 아스키 그대로
"    cmpb $224, %al\n"
"    jae .Lord_3\n"
"    andl $31, %eax\n"                  // 110xxxxx 10xxxxxx
"    shll $6, %eax\n"
"    movzbl 1(%rdi), %ecx\n"
"    andl $63, %ecx\n"
"    orl %ecx, %eax\n"
"    ret\n"
".Lord_3:\n"
"    cmpb $240, %al\n"
"    jae .Lord_4\n"
"    andl $15, %eax\n"                  // 1110xxxx 10xxxxxx 10xxxxxx
"    shll $12, %eax\n"
"    movzbl 1(%rdi), %ecx\n"
"    andl $63, %ecx\n"
"    shll $6, %ecx\n"
"    orl %ecx, %eax\n"
"    movzbl 2(%rdi), %ecx\n"
"    andl $63, %ecx\n"
"    orl %ecx, %eax\n"
"    ret\n"
".Lord_4:\n"
"    andl $7, %eax\n"                   // 11110xxx + 이어붙임 3개
"    shll $18, %eax\n"
"    movzbl 1(%rdi), %ecx\n"
"    andl $63, %ecx\n"
"    shll $12, %ecx\n"
"    orl %ecx, %eax\n"
"    movzbl 2(%rdi), %ecx\n"
"    andl $63, %ecx\n"
"    shll $6, %ecx\n"
"    orl %ecx, %eax\n"
"    movzbl 3(%rdi), %ecx\n"
"    andl $63, %ecx\n"
"    orl %ecx, %eax\n"
".Lord_done:\n"
"    ret\n"
".Lord_empty:\n"
"    pushq %rbp\n    movq %rsp, %rbp\n"
"    leaq .Lordempty(%rip), %rdi\n"
"    xorl %eax, %eax\n"
"    call printf\n"
"    movl $1, %edi\n"
"    call exit\n"
"\n__han_chr:\n"                        // rdi = 코드값, rsi = 행번호 -> 글자 하나
"    pushq %rbp\n    movq %rsp, %rbp\n"
"    subq $32, %rsp\n"
"    movq %rdi, 0(%rsp)\n"
"    movq %rsi, 8(%rsp)\n"
"    cmpq $0, %rdi\n"
"    jl .Lchr_bad\n"
"    cmpq $1114111, %rdi\n"
"    jg .Lchr_bad\n"
"    movl $5, %edi\n"
"    call malloc\n"
"    movq %rax, 16(%rsp)\n"
"    movq 0(%rsp), %rcx\n"
"    movq %rax, %rdx\n"
"    cmpq $127, %rcx\n"
"    jg .Lchr_2\n"
"    movb %cl, (%rdx)\n"
"    movb $0, 1(%rdx)\n"
"    jmp .Lchr_done\n"
".Lchr_2:\n"
"    cmpq $2047, %rcx\n"
"    jg .Lchr_3\n"
"    movq %rcx, %rax\n"
"    shrq $6, %rax\n"
"    orq $192, %rax\n"
"    movb %al, (%rdx)\n"
"    movq %rcx, %rax\n"
"    andq $63, %rax\n"
"    orq $128, %rax\n"
"    movb %al, 1(%rdx)\n"
"    movb $0, 2(%rdx)\n"
"    jmp .Lchr_done\n"
".Lchr_3:\n"
"    cmpq $65535, %rcx\n"
"    jg .Lchr_4\n"
"    movq %rcx, %rax\n"
"    shrq $12, %rax\n"
"    orq $224, %rax\n"
"    movb %al, (%rdx)\n"
"    movq %rcx, %rax\n"
"    shrq $6, %rax\n"
"    andq $63, %rax\n"
"    orq $128, %rax\n"
"    movb %al, 1(%rdx)\n"
"    movq %rcx, %rax\n"
"    andq $63, %rax\n"
"    orq $128, %rax\n"
"    movb %al, 2(%rdx)\n"
"    movb $0, 3(%rdx)\n"
"    jmp .Lchr_done\n"
".Lchr_4:\n"
"    movq %rcx, %rax\n"
"    shrq $18, %rax\n"
"    orq $240, %rax\n"
"    movb %al, (%rdx)\n"
"    movq %rcx, %rax\n"
"    shrq $12, %rax\n"
"    andq $63, %rax\n"
"    orq $128, %rax\n"
"    movb %al, 1(%rdx)\n"
"    movq %rcx, %rax\n"
"    shrq $6, %rax\n"
"    andq $63, %rax\n"
"    orq $128, %rax\n"
"    movb %al, 2(%rdx)\n"
"    movq %rcx, %rax\n"
"    andq $63, %rax\n"
"    orq $128, %rax\n"
"    movb %al, 3(%rdx)\n"
"    movb $0, 4(%rdx)\n"
".Lchr_done:\n"
"    movq 16(%rsp), %rax\n"
"    leave\n    ret\n"
".Lchr_bad:\n"
"    movq 8(%rsp), %rsi\n"
"    movq 0(%rsp), %rdx\n"
"    leaq .Lchrbad(%rip), %rdi\n"
"    xorl %eax, %eax\n"
"    call printf\n"
"    movl $1, %edi\n"
"    call exit\n";
    }

    string run(Node* top, map<string,Func>& fs, map<string, deque<Inst>>& is) {
        funcs = &fs; insts = &is;
        // 검사된 조합만 코드로 낸다. 안 불린 라이브러리 함수는 조합이 없어 자동으로 빠진다.
        for (auto& kv : is)
            for (auto& in : kv.second)
                genFunc(in.label, &fs.at(kv.first), in.body.get());
        genFunc("main", nullptr, top);

        ostringstream o;
        o << "    .section .rodata\n"
             ".Lfmt:\n    .asciz \"%lld\\n\"\n"
             ".Lfmt2:\n    .asciz \"%lld\"\n"
             ".LfmtF:\n    .asciz \"%.15g\\n\"\n"
             ".LfmtG:\n    .asciz \"%.15g\"\n"
             ".LfmtS:\n    .asciz \"%s\"\n"
             ".Lnl:\n    .asciz \"\\r\\n\"\n"
             ".Lempty:\n    .asciz \"\"\n"
             ".Ldiv0:\n    .asciz \"[실행 오류] %d행: 0으로 나눌 수 없음\\n\"\n"
             ".Loob:\n    .asciz \"[실행 오류] %lld행: 목록에 %lld번째는 없음 (개수: %lld)\\n\"\n"
             ".Lsoob:\n    .asciz \"[실행 오류] %lld행: 문자에 %lld번째 글자는 없음 (글자수: %lld)\\n\"\n"
             ".Lnegsqrt:\n    .asciz \"[실행 오류] %d행: 음수의 제곱근은 없음\\n\"\n"
             ".Lrand0:\n    .asciz \"[실행 오류] %lld행: 무작위는 1 이상이어야 하는데 %lld를 넣음\\n\"\n"
             ".Lgoob:\n    .asciz \"[실행 오류] %lld행: 격자에 %lld행 %lld열은 없음 (크기: %lld행 %lld열)\\n\"\n"
             ".Lnotgrid:\n    .asciz \"[실행 오류] %lld행: 격자가 아닌 목록에는 '행 열' 을 쓸 수 없음\\n\"\n"
             ".Lgsize:\n    .asciz \"[실행 오류] %lld행: 격자 크기는 1 이상이어야 하는데 %lld행 %lld열을 넣음\\n\"\n"
             ".Lgpush:\n    .asciz \"[실행 오류] 격자에는 '넣다' 를 쓸 수 없음 (칸 수가 고정)\\n\"\n"
             ".Lshift:\n    .asciz \"[실행 오류] %d행: 비트를 %lld칸 밀 수 없음 (0~63 만 됨)\\n\"\n"
             ".Lordempty:\n    .asciz \"[실행 오류] 빈 문자에는 '코드' 가 없음\\n\"\n"
             ".Lchrbad:\n    .asciz \"[실행 오류] %lld행: %lld 는 글자로 만들 수 없는 코드값\\n\"\n";
        o << rodata.str();
        o << "\n    .bss\n.Lgline:\n    .skip 8\n.Lgcap:\n    .skip 8\n.Lseeded:\n    .skip 4\n";
        o << "\n    .text\n";
        o << runtime();
        o << text.str();
        o << "\n    .section .note.GNU-stack,\"\",@progbits\n";
        return o.str();
    }
};

// ==================== main ====================
static const char* HAN_VERSION = "한랭 0.10";

int main(int argc, char** argv) {
    if (argc >= 2 && (string(argv[1]) == "--version" || string(argv[1]) == "-v")) {
        cout << HAN_VERSION << "\n"; return 0;
    }
    if (argc >= 2 && (string(argv[1]) == "--help" || string(argv[1]) == "-h")) {
        cout << HAN_VERSION << "\n\n"
                "사용법:\n"
                "  hanc 파일.han -o 실행파일   실행파일 만들기\n"
                "  hanc 파일.han               어셈블리를 화면에 뿌리기\n"
                "  han  파일.han               한 번에 컴파일하고 실행\n\n"
                "라이브러리는 부르는 파일 옆, $HAN_LIB, 설치 폴더 순으로 찾는다.\n";
        return 0;
    }
    if (argc < 2) { cerr << "사용법: hanc 파일.han [-o 실행파일]\n       hanc --help\n"; return 1; }
    { // 표준 라이브러리를 찾으려면 컴파일러 자기 위치를 알아야 함
        char buf[PATH_MAX];
        ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n > 0) { buf[n] = 0; hancDir = dirOf(buf); }
        else hancDir = dirOf(canonical(argv[0]));
    }

    Loader ld;
    ld.loaded.insert(canonical(argv[1]));
    vector<Token> toks0 = ld.load(argv[1], argv[1]);
    { Token e; e.t = TK::End; e.s = "<끝>";
      if (!toks0.empty()) { e.file = toks0.back().file; e.line = toks0.back().line; }
      toks0.push_back(e); }

    Resolver res;
    auto toks = res.run(move(toks0));
    Parser ps(move(toks), res.funcNames);
    auto ast = ps.program();
    Checker ck;
    ck.run(ast.get(), ps.funcs);
    string asmOut = Codegen{}.run(ast.get(), ps.funcs, ck.insts);

    if (argc >= 4 && string(argv[2]) == "-o") {
        string sfile = string(argv[3]) + ".s";
        ofstream sf(sfile); sf << asmOut; sf.close();
        string cmd = "gcc -no-pie '" + sfile + "' -o '" + argv[3] + "' -lm";
        if (system(cmd.c_str()) != 0) { cerr << "gcc 실패\n"; return 1; }
        remove(sfile.c_str());
        return 0;
    }
    cout << asmOut;
    return 0;
}
