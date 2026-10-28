// 한랭 VS Code 확장
//   - 저장할 때(와 열 때) hanc 로 검사해서 [오류]·[경고] 를 문제 패널과 밑줄로
//   - ▶ / F5 로 현재 파일 실행 (han 스크립트)
//   - 키워드·내장 값에 마우스를 올리면 짧은 설명
// 빌드 단계 없이 그대로 돌아가게 순수 JS 로 짰다.
'use strict';
const vscode = require('vscode');
const cp = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

// ---------- 도구 찾기 ----------
function findTool(setting, name, docUri) {
    const cfg = vscode.workspace.getConfiguration('hanlang').get(setting);
    if (cfg) return cfg;
    const ws = docUri && vscode.workspace.getWorkspaceFolder(docUri);
    const dirs = [];
    if (ws) dirs.push(ws.uri.fsPath);
    if (docUri) {                                    // 파일이 있는 폴더부터 위로 올라가며
        let d = path.dirname(docUri.fsPath);
        for (let i = 0; i < 6; i++) { dirs.push(d); const up = path.dirname(d); if (up === d) break; d = up; }
    }
    for (const d of dirs) {
        const p = path.join(d, name);
        try { fs.accessSync(p, fs.constants.X_OK); if (fs.statSync(p).isFile()) return p; } catch (e) { }
    }
    return name;                                     // PATH 에서 찾게
}

// ---------- 오류 메시지 읽기 ----------
// hanc 의 출력 꼴:
//   [오류] (파일이름 )N행: 메시지
//     N | 원래 줄
//       |    ^^^^          ← 캐럿. 한글은 화면 두 칸
//     '두배로' 를 3행에서 부름 (인자: 문자)    ← 다형 함수 몸통 에러면 호출 문맥
function isWide(cp) {
    return (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0x1100 && cp <= 0x11FF) || (cp >= 0x3130 && cp <= 0x318F);
}
// 화면 칸 위치 -> 글자 위치 (VS Code 는 UTF-16 단위지만 한글은 BMP 라 글자 하나 = 1)
function screenToChar(text, screenCol) {
    let s = 0, i = 0;
    for (const ch of text) {
        if (s >= screenCol) return i;
        s += isWide(ch.codePointAt(0)) ? 2 : 1;
        i += ch.length;
    }
    return i;
}

function parseOutput(out) {
    const lines = out.split(/\r?\n/);
    const items = [];
    for (let k = 0; k < lines.length; k++) {
        const m = /^\[(오류|경고)\] (?:(.+?) )?(\d+)행: (.*)$/.exec(lines[k]);
        if (!m) continue;
        const it = { severity: m[1], file: m[2] || null, line: parseInt(m[3], 10), msg: m[4], col: 0, width: 0, extra: [] };
        let j = k + 1;
        const src = j < lines.length && /^\s+\d+ \| (.*)$/.exec(lines[j]);
        if (src) {
            const caret = j + 1 < lines.length && /^\s+\| (.*)$/.exec(lines[j + 1]);
            if (caret) {
                const c = caret[1];
                const startScreen = c.indexOf('^');
                const endScreen = c.lastIndexOf('^') + 1;
                if (startScreen >= 0) {
                    it.col = screenToChar(src[1], startScreen);
                    it.width = Math.max(1, screenToChar(src[1], endScreen) - it.col);
                }
                j += 2;
            } else j += 1;
        }
        while (j < lines.length && /^  '/.test(lines[j])) { it.extra.push(lines[j].trim()); j++; }
        items.push(it);
        k = j - 1;
    }
    // 실행 오류처럼 꼴이 다르거나 꼴을 못 읽었으면 통째로 하나
    if (items.length === 0 && out.trim()) items.push({ severity: '오류', file: null, line: 1, msg: out.trim().split('\n')[0], col: 0, width: 0, extra: [] });
    return items;
}

// ---------- 검사 ----------
const collection = vscode.languages.createDiagnosticCollection('hanlang');
const running = new Map();

function check(doc) {
    if (doc.languageId !== 'hanlang' || doc.uri.scheme !== 'file') return;
    if (!vscode.workspace.getConfiguration('hanlang').get('checkOnSave')) return;
    const hanc = findTool('hancPath', 'hanc', doc.uri);
    const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'hanlang-'));
    const exe = path.join(tmp, 'p');
    const key = doc.uri.toString();
    if (running.has(key)) running.get(key).kill();
    const child = cp.execFile(hanc, [doc.uri.fsPath, '-o', exe], { cwd: path.dirname(doc.uri.fsPath), timeout: 20000 },
        (err, stdout, stderr) => {
            running.delete(key);
            fs.rm(tmp, { recursive: true, force: true }, () => { });
            if (err && err.code === 'ENOENT') {
                vscode.window.showWarningMessage(`한랭: 컴파일러 '${hanc}' 를 찾을 수 없음. 설정의 hanlang.hancPath 를 지정하세요.`);
                return;
            }
            const byFile = new Map([[key, []]]);
            for (const it of parseOutput(stderr || '')) {
                let uri = doc.uri;
                if (it.file && path.basename(it.file) !== path.basename(doc.uri.fsPath)) {
                    // 불러온 파일에서 난 에러: 옆 폴더 → 작업 폴더의 lib/ 순으로 찾아본다
                    const cands = [path.join(path.dirname(doc.uri.fsPath), it.file)];
                    const ws = vscode.workspace.getWorkspaceFolder(doc.uri);
                    if (ws) cands.push(path.join(ws.uri.fsPath, 'lib', it.file));
                    cands.push(path.join(path.dirname(hanc), 'lib', it.file));
                    const hit = cands.find(c => fs.existsSync(c));
                    if (hit) uri = vscode.Uri.file(hit);
                    else it.msg = `(${it.file} ${it.line}행) ` + it.msg;
                }
                const line = Math.max(0, it.line - 1);
                const range = new vscode.Range(line, it.col, line, it.col + (it.width || 1000));
                const d = new vscode.Diagnostic(range, it.msg + (it.extra.length ? '\n' + it.extra.join('\n') : ''),
                    it.severity === '경고' ? vscode.DiagnosticSeverity.Warning : vscode.DiagnosticSeverity.Error);
                d.source = '한랭';
                const k = uri.toString();
                if (!byFile.has(k)) byFile.set(k, []);
                byFile.get(k).push(d);
            }
            for (const [k, ds] of byFile) collection.set(vscode.Uri.parse(k), ds);
        });
    running.set(key, child);
}

// ---------- 실행 ----------
let terminal = null;
async function run() {
    const ed = vscode.window.activeTextEditor;
    if (!ed || ed.document.languageId !== 'hanlang') return;
    await ed.document.save();
    const han = findTool('hanPath', 'han', ed.document.uri);
    if (!terminal || terminal.exitStatus !== undefined) terminal = vscode.window.createTerminal('한랭');
    terminal.show(true);
    const q = s => "'" + s.replace(/'/g, "'\\''") + "'";
    terminal.sendText(`cd ${q(path.dirname(ed.document.uri.fsPath))} && ${q(han)} ${q(ed.document.uri.fsPath)}`);
}

// ---------- 마우스 올리면 설명 ----------
const DOCS = {
    '만약': '조건 — `만약 (나이 >= 18) 라면 { } 아니면 { }`. 조건은 괄호 필수',
    '라면': '`만약 (조건) 라면 { }` 의 끝맺음', '이라면': '`라면` 과 같음',
    '아니면': '`만약` 의 조건이 거짓일 때 블록',
    '동안': '조건 반복 — `(번호 <= 10) 인 동안 { }`',
    '반복하다': '횟수 반복 — `10번 반복하다 { }`',
    '멈추다': '가장 안쪽 반복을 끝냄', '계속하다': '가장 안쪽 반복의 다음 회차로',
    '돌려주다': '함수에서 값을 돌려줌 — `(가 + 나)를 돌려주다.`',
    '종료하다': '프로그램 전체를 끝냄 — `종료하다.` / `1로 종료하다.` (종료코드)',
    '말하다': '출력 — `"안녕"을 말하다.` 줄바꿈 대신 다른 끝맺음: `x를 끝" " 말하다.`',
    '듣다': '입력 한 줄(항상 문자) — `이름은 듣다.` 숫자로: `(이름을 정수로 바꾸다)`',
    '바꾸다': '타입 바꾸기 — `(x를 정수로 바꾸다)` / 소수로 / 문자로',
    '넣다': '목록 뒤에 넣기 — `점수에 80을 넣다.` (\'에\' 붙은 쪽이 목록)',
    '빼다': '목록: `(점수에서 빼다)` 마지막 / `(점수의 2번째를 빼다)` 그 자리. 사전: `점수표에서 "철수"를 빼다.`',
    '채우다': '타입 있는 격자 — `(3행 4열을 0.0으로 채우다)`',
    '있나': '사전에 열쇠가 있나 — `(점수표에 "영희"가 있나)` → 참/거짓',
    '열다': '파일 블록 — `파일 "a.txt"를 읽기로 열다 { 줄은 듣다. }` (쓰기로 / 붙여쓰기로)',
    '파일': '`파일 X을 읽기로|쓰기로|붙여쓰기로 열다 { } 못 열면 { }`. `파일 오류를 쓰기로 열다` = 표준 에러',
    '불러오다': '라이브러리 — `"수학.han"을 불러오다.` 후 `수학의 최대공약수구하다` 처럼 붙여 부름. 별명: `"표.han"을 데이터로 불러오다.` 골라서: `"수학.han"에서 소수인가를 불러오다.`',
    '란': '함수 정의 — `가와 나를 더하다 란 { }` (부르는 모양 그대로). 묶음: `학생 이란 묶음 { }`',
    '이란': '`란` 과 같음', '묶음': '구조체 — `학생 이란 묶음 { 이름은 문자. 점수는 정수. }`',
    '새': '묶음 하나 만들기 — `(새 학생)`',
    '빈': '타입 있는 빈 목록·사전 — `(빈 문자 목록)`, `(빈 정수 사전)`',
    '없음': '아무 묶음도 아님 — `노드의 왼쪽은 없음이다.` / `((노드의 왼쪽) == 없음)`',
    '참': '정수 1', '거짓': '정수 0',
    '이다': '대입의 끝맺음 — `나이는 17이다.`',
    '번째': '목록·문자의 N번째 (1부터) — `(점수의 1번째)`',
    '개수': '목록·문자·사전의 개수 — `(점수의 개수)`',
    '열쇠들': '사전의 열쇠 목록 (넣은 순서) — `(점수표의 열쇠들)`',
    '제곱근': '`(9의 제곱근)` → 3 (결과는 소수)', '절댓값': '`(-5의 절댓값)` → 5',
    '반올림': '`(3.7의 반올림)` → 4', '올림': '`(3.2의 올림)` → 4', '버림': '`(3.9의 버림)` → 3',
    '무작위': '`(6의 무작위)` → 1~6 중 하나', '코드': '`("가"의 코드)` → 44032', '글자': '`(65의 글자)` → "A"',
    '행수': '격자의 행 수 — `(판의 행수)`', '열수': '격자의 열 수 — `(판의 열수)`',
    '입력끝': '`(입력끝)` — 더 읽을 줄이 없으면 참', '명령줄': '`(명령줄)` — 명령줄 인자 문자 목록',
    '환경값': '`("HOME"의 환경값)` — 환경변수 (없으면 "")', '실제경로': '`(경로의 실제경로)` — 절대 경로 (없는 파일이면 "")',
    '실행파일': '`(실행파일)` — 지금 도는 이 프로그램의 경로',
    '지금시각': '`(지금시각)` — 1970년부터 지난 초 (소수, 밀리초까지)',
    '초': '`2초 기다리다.` 의 초', '기다리다': '멈췄다가 다음 줄로 — `0.5초 기다리다.`',
    '맞추다': '정규식 — `(글에서 "[0-9]+"를 맞추다)` → 맞은 부분들(문자 목록)',
    '창': '창 열기 — `창 640과 480을 열다.` 왼쪽 위가 (0, 0), 아래로 y 가 커짐',
    '색칠하다': '그리는 색 — `"빨강"으로 색칠하다.` (검정 하양 빨강 주황 노랑 초록 파랑 남색 보라 분홍 갈색 회색 연회색 하늘, "#ff8800")',
    '사각형그리다': '채운 사각형 — `x와 y와 너비와 높이를 사각형그리다.`',
    '선긋다': '선 — `x1과 y1과 x2와 y2를 선긋다.`', '점찍다': '점 — `x와 y를 점찍다.`',
    '글쓰다': '글자 (한글 됨) — `"안녕"을 x와 y에 글쓰다.` (y 는 글자 위쪽)',
    '보여주다': '창을 띄우고 닫을 때까지 기다림 (창의 X, Esc, q)',
    '실행하다': '다른 프로그램 실행 — `("ls"를 실행하다)` → 종료코드. 문장으로: `"gcc a.s -o a"를 실행하다.`',
    '정수': '타입 정수 (64비트)', '소수': '타입 소수 (배정밀도)', '문자': '타입 문자 (문자열)',
    '목록': '`정수 목록` 처럼 타입 뒤에 — 목록 타입', '사전': '`정수 사전` 처럼 타입 뒤에 — 값이 그 타입인 사전 (열쇠는 문자)',
};
const PARTICLES = ['이라고', '라고', '으로', '에서', '에게', '를', '을', '는', '은', '이', '가', '의', '에', '로', '와', '과'];

function hover(doc, pos) {
    const r = doc.getWordRangeAtPosition(pos, /[가-힣ㄱ-ㅎㅏ-ㅣA-Za-z_][가-힣ㄱ-ㅎㅏ-ㅣA-Za-z0-9_]*/);
    if (!r) return null;
    const w = doc.getText(r);
    const cands = [w];
    for (const p of PARTICLES) if (w.length > p.length && w.endsWith(p)) cands.push(w.slice(0, -p.length));
    if (w.endsWith('이다') && w.length > 2) cands.push('이다');
    if (w.endsWith('번째')) cands.push('번째');
    for (const c of cands) if (DOCS[c]) return new vscode.Hover(new vscode.MarkdownString(`**${c}** — ${DOCS[c]}`), r);
    return null;
}

function activate(ctx) {
    ctx.subscriptions.push(
        collection,
        vscode.commands.registerCommand('hanlang.run', run),
        vscode.commands.registerCommand('hanlang.check', () => {
            const ed = vscode.window.activeTextEditor;
            if (ed) check(ed.document);
        }),
        vscode.workspace.onDidSaveTextDocument(check),
        vscode.workspace.onDidOpenTextDocument(check),
        vscode.workspace.onDidCloseTextDocument(d => collection.delete(d.uri)),
        vscode.languages.registerHoverProvider('hanlang', { provideHover: hover }),
    );
    for (const d of vscode.workspace.textDocuments) check(d);
}

function deactivate() { for (const c of running.values()) c.kill(); }

module.exports = { activate, deactivate, parseOutput, screenToChar };
