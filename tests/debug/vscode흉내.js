// VS Code 대신 디버그 어댑터(editors/vscode/debug.js)에 DAP 요청을 보내 보는 시험.
// 사용: node vscode흉내.js <debug.js> <프로그램.han> <hanc> <멈춤점 줄> <단계…>
// 멈출 때마다 호출 스택·변수·전역 변수를 찍고 다음 단계(next / stepIn / stepOut / continue)를 보낸다.
// 프로그램 출력은 순서가 들쭉날쭉해서 안 찍는다.
const { HanDebugSession } = require(process.argv[2]);
const cp = require('child_process');
const [prog, hanc, bpLine] = [process.argv[3], process.argv[4], Number(process.argv[5])];
const steps = process.argv.slice(6);
const s = new HanDebugSession(hanc);
let seq = 1; const waiters = new Map();
function req(command, args) {
    const id = seq++;
    return new Promise(r => { waiters.set(id, r); s.handleMessage({ type: 'request', seq: id, command, arguments: args }); });
}
s.onDidSendMessage(async m => {
    if (m.type === 'response') { const w = waiters.get(m.request_seq); if (w) { waiters.delete(m.request_seq); w(m); } return; }
    if (m.type === 'request' && m.command === 'runInTerminal') {
        const child = cp.spawn(m.arguments.args[0], [], { cwd: m.arguments.cwd, env: Object.assign({}, process.env, m.arguments.env), stdio: 'ignore' });
        child.on('error', () => { });
        s.handleMessage({ type: 'response', request_seq: m.seq, success: true, command: 'runInTerminal', body: {} });
        return;
    }
    if (m.type === 'event' && m.event === 'output') { console.log('출력:', m.body.output.trim()); return; }
    if (m.type === 'event' && m.event === 'stopped') {
        const st = await req('stackTrace', { threadId: 1 });
        console.log(`멈춤(${m.body.reason}): ` + st.body.stackFrames.map(f => `${f.name}@${f.line}`).join(' ← '));
        const sc = await req('scopes', { frameId: 0 });
        for (const scope of sc.body.scopes) {
            const v = await req('variables', { variablesReference: scope.variablesReference });
            console.log(`  ${scope.name}: ` + v.body.variables.map(x => `${x.name}(${x.type})=${x.value}`).join(', '));
        }
        const next = steps.shift() || 'continue';
        console.log('  → ' + next);
        await req(next, { threadId: 1 });
        return;
    }
    if (m.type === 'event' && m.event !== 'initialized') console.log('사건: ' + m.event);
});
(async () => {
    await req('initialize', { adapterID: 'hanlang' });
    const launch = req('launch', { program: prog });
    await req('setBreakpoints', { source: { path: prog }, breakpoints: [{ line: bpLine }] });
    await req('configurationDone', {});
    const r = await launch;
    if (!r.success) console.log('launch 실패: ' + r.message);
})();
