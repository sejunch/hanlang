// 한랭 디버그 어댑터 (VS Code 의 Debug Adapter Protocol 을 직접 구현 — 의존 없음)
//
// 흐름: launch → hanc --디버그 로 임시 실행파일을 만들고 TCP 서버를 연 뒤, VS Code 에 runInTerminal 을 부탁해
//       통합 터미널에서 HAN_DEBUG_PORT=포트 로 프로그램을 띄운다 (그래서 '듣다' 입력도 터미널에서 됨).
//       프로그램의 runtime/debug.c 가 그 포트로 붙어 한 줄짜리 명령을 주고받는다:
//         어댑터 → 프로그램: bp\t경로\t줄 / bpclear\t경로 / run / next / in / out / pause / stack / vars\t틀 / globals / quit
//         프로그램 → 어댑터: hello / stopped\t까닭 / frame\t… / var\t이름\t타입\t값 / end
// vscode 모듈을 쓰지 않아서 node 로 따로 시험할 수 있다 (extension.js 가 DebugAdapterInlineImplementation 으로 감싼다).
'use strict';
const cp = require('child_process');
const fs = require('fs');
const net = require('net');
const os = require('os');
const path = require('path');

const GLOBALS_REF = 1000000;

function realpath(p) { try { return fs.realpathSync(p); } catch (e) { return p; } }

class HanDebugSession {
    constructor(hancPath) {
        this.hanc = hancPath;
        this.seq = 1;
        this.listeners = [];
        this.breakpoints = new Map();      // 실제 경로 → [줄…]
        this.sock = null;
        this.connected = false;
        this.configured = false;
        this.started = false;
        this.stopOnEntry = false;
        this.buf = '';
        this.waiting = null;               // { lines, resolve } — 'end' 까지 모으는 중인 질문
        this.queue = Promise.resolve();
        this.pendingReverse = new Map();
        this.ended = false;
        // vscode.Event 꼴: 듣는 함수를 받아 Disposable 을 돌려준다
        this.onDidSendMessage = (listener) => {
            this.listeners.push(listener);
            return { dispose: () => { this.listeners = this.listeners.filter(l => l !== listener); } };
        };
    }

    // ---------- DAP 보내기 ----------
    send(msg) { msg.seq = this.seq++; for (const l of this.listeners) l(msg); }
    respond(req, body, ok = true, message) {
        this.send({ type: 'response', request_seq: req.seq, success: ok, command: req.command, body: body || {}, message });
    }
    event(name, body) { this.send({ type: 'event', event: name, body: body || {} }); }
    output(text, category = 'console') { this.event('output', { category, output: text }); }
    reverse(command, args) {
        return new Promise(resolve => {
            const seq = this.seq;
            this.pendingReverse.set(seq, resolve);
            this.send({ type: 'request', command, arguments: args });
        });
    }

    // ---------- 프로그램과 주고받기 ----------
    write(line) { if (this.sock) this.sock.write(line + '\n'); }
    ask(line) {                            // 'end' 까지 받은 줄들 (한 번에 하나씩)
        const p = this.queue.then(() => new Promise(resolve => {
            if (!this.sock) { resolve([]); return; }
            this.waiting = { lines: [], resolve };
            this.write(line);
        }));
        this.queue = p.catch(() => { });
        return p;
    }
    onLine(line) {
        if (this.waiting) {
            if (line === 'end') { const w = this.waiting; this.waiting = null; w.resolve(w.lines); }
            else this.waiting.lines.push(line);
            return;
        }
        if (line === 'hello') { this.connected = true; this.maybeStart(); return; }
        if (line.startsWith('stopped\t')) {
            const why = line.slice(8);
            const reason = why === 'breakpoint' ? 'breakpoint' : why === 'pause' ? 'pause' : (this.entryStop ? 'entry' : 'step');
            this.entryStop = false;
            this.event('stopped', { reason, threadId: 1, allThreadsStopped: true });
        }
    }
    maybeStart() {
        if (!this.connected || !this.configured || this.started) return;
        this.started = true;
        for (const [p, lines] of this.breakpoints) for (const l of lines) this.write(`bp\t${p}\t${l}`);
        this.entryStop = this.stopOnEntry;
        this.write(this.stopOnEntry ? 'in' : 'run');
    }
    finish() {
        if (this.ended) return;
        this.ended = true;
        if (this.waiting) { const w = this.waiting; this.waiting = null; w.resolve([]); }
        try { if (this.server) this.server.close(); } catch (e) { }
        try { if (this.exe) fs.unlinkSync(this.exe); } catch (e) { }
        this.event('terminated');
    }

    // ---------- DAP 받기 ----------
    handleMessage(m) {
        if (m.type === 'response') {        // runInTerminal 의 답
            const r = this.pendingReverse.get(m.request_seq);
            if (r) { this.pendingReverse.delete(m.request_seq); r(m); }
            return;
        }
        if (m.type !== 'request') return;
        const f = this['req_' + m.command];
        if (f) Promise.resolve(f.call(this, m, m.arguments || {})).catch(e => this.respond(m, {}, false, String(e)));
        else this.respond(m, {});
    }

    req_initialize(m) {
        this.respond(m, {
            supportsConfigurationDoneRequest: true,
            supportsTerminateRequest: true,
            supportsSteppingGranularity: false,
        });
        this.event('initialized');
    }

    async req_launch(m, a) {
        const program = a.program;
        if (!program || !fs.existsSync(program)) { this.respond(m, {}, false, `디버그할 파일이 없음: ${program}`); return; }
        this.stopOnEntry = !!a.stopOnEntry;
        this.exe = path.join(os.tmpdir(), `hanlang-debug-${process.pid}-${Date.now()}`);
        const res = await new Promise(resolve => {
            cp.execFile(this.hanc, a.noDebug ? [program, '-o', this.exe] : ['--디버그', program, '-o', this.exe],
                { cwd: path.dirname(program) }, (err, stdout, stderr) => resolve({ err, out: stdout + stderr }));
        });
        if (res.err) {
            this.output(res.out || String(res.err), 'stderr');
            this.respond(m, {}, false, '컴파일 오류 — 문제 패널이나 디버그 콘솔을 보세요');
            this.finish();
            return;
        }
        const env = {};
        if (!a.noDebug) {
            this.server = net.createServer(sock => {
                if (this.sock) { sock.destroy(); return; }
                this.sock = sock;
                sock.setEncoding('utf8');
                sock.on('data', d => {
                    this.buf += d;
                    let i;
                    while ((i = this.buf.indexOf('\n')) >= 0) { const l = this.buf.slice(0, i); this.buf = this.buf.slice(i + 1); this.onLine(l); }
                });
                sock.on('close', () => { this.sock = null; this.finish(); });
                sock.on('error', () => { });
            });
            await new Promise(resolve => this.server.listen(0, '127.0.0.1', resolve));
            env.HAN_DEBUG_PORT = String(this.server.address().port);
        }
        const r = await this.reverse('runInTerminal', {
            kind: 'integrated', title: '한랭', cwd: path.dirname(program), args: [this.exe], env,
        });
        if (!r.success) { this.respond(m, {}, false, '터미널에서 프로그램을 띄울 수 없음'); this.finish(); return; }
        this.respond(m, {});
        if (a.noDebug) {                    // 디버그 없이: 터미널에서 돌기만 (끝나는 걸 알 길이 없어 바로 끝냄)
            setTimeout(() => this.finish(), 500);
        }
    }

    req_setBreakpoints(m, a) {
        const p = realpath(a.source && a.source.path || '');
        const lines = (a.breakpoints || []).map(b => b.line);
        this.breakpoints.set(p, lines);
        if (this.started) { this.write(`bpclear\t${p}`); for (const l of lines) this.write(`bp\t${p}\t${l}`); }
        this.respond(m, { breakpoints: lines.map(line => ({ verified: true, line })) });
    }
    req_setExceptionBreakpoints(m) { this.respond(m, {}); }
    req_configurationDone(m) { this.configured = true; this.respond(m, {}); this.maybeStart(); }
    req_threads(m) { this.respond(m, { threads: [{ id: 1, name: '한랭' }] }); }

    async req_stackTrace(m) {
        const lines = await this.ask('stack');
        const frames = [];
        for (const l of lines) {
            const [, id, name, p, line] = l.split('\t');
            const f = { id: Number(id), name, line: Number(line) || 0, column: 1 };
            if (p) f.source = { name: path.basename(p), path: p };
            frames.push(f);
        }
        this.respond(m, { stackFrames: frames, totalFrames: frames.length });
    }
    req_scopes(m, a) {
        const scopes = [{ name: '변수', variablesReference: a.frameId + 1, expensive: false }];
        scopes.push({ name: '전역 변수', variablesReference: GLOBALS_REF, expensive: false });
        this.respond(m, { scopes });
    }
    async req_variables(m, a) {
        const ref = a.variablesReference;
        const lines = await this.ask(ref === GLOBALS_REF ? 'globals' : `vars\t${ref - 1}`);
        const variables = lines.map(l => {
            const [, name, type, value] = l.split('\t');
            return { name, type, value: value === undefined ? '' : value, variablesReference: 0 };
        });
        this.respond(m, { variables });
    }
    req_continue(m) { this.write('run'); this.respond(m, { allThreadsContinued: true }); }
    req_next(m) { this.write('next'); this.respond(m, {}); }
    req_stepIn(m) { this.write('in'); this.respond(m, {}); }
    req_stepOut(m) { this.write('out'); this.respond(m, {}); }
    req_pause(m) { this.write('pause'); this.respond(m, {}); }
    req_terminate(m) { this.write('quit'); this.respond(m, {}); }
    req_disconnect(m) {
        this.write('quit');
        if (this.sock) this.sock.end();
        this.respond(m, {});
        this.finish();
    }
    dispose() { if (this.sock) this.sock.destroy(); try { if (this.server) this.server.close(); } catch (e) { } }
}

module.exports = { HanDebugSession };
