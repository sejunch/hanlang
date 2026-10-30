// VS Code 없이 자동완성(editors/vscode/complete.js)을 시험: hanc --기호 결과로 자리마다 추천을 뽑아 본다
// 사용: node 완성시험.js <complete.js> <hanc> <완성.han>
const { parseSymbols, complete } = require(process.argv[2]);
const cp = require('child_process'), fs = require('fs'), path = require('path');
const [hanc, file] = [process.argv[3], process.argv[4]];
const sym = parseSymbols(cp.execFileSync(hanc, ['--기호', file], { cwd: path.dirname(file) }).toString());
const doc = fs.readFileSync(file, 'utf8') + '\n새변수는 1이다.\n';
const libFiles = ['수학.han', '문자.han', '목록.han'];
function show(title, line, linePrefix, kinds) {
    const its = complete({ symbols: sym, docText: doc, line, linePrefix, libFiles })
        .filter(it => !kinds || kinds.includes(it.kind));
    console.log(`== ${title}`);
    for (const it of its) console.log(`  [${it.kind}] ${it.label}` + (it.detail ? `  — ${it.detail}` : '') + (it.snippet ? `  ⇒ ${it.snippet}` : ''));
}
show('함수 안 (8행) 변수', 8, '    ', ['variable']);
show('맨 위 (12행) 변수', 12, '', ['variable']);
// 라이브러리 함수는 늘 수 있어서 대표 하나만 본다
function has(title, line, linePrefix, label, kind) {
    const its = complete({ symbols: sym, docText: doc, line, linePrefix, libFiles });
    const hit = its.find(it => it.label === label && it.kind === kind);
    console.log(`== ${title}: ${hit ? `[${hit.kind}] ${hit.label}` + (hit.detail ? `  — ${hit.detail}` : '') + (hit.snippet ? `  ⇒ ${hit.snippet}` : '') : '없음'}` +
        (title.includes('뒤') ? `  (전부 ${its.every(it => it.kind === kind) ? kind : '섞임'})` : ''));
}
const mine = complete({ symbols: sym, docText: doc, line: 12, linePrefix: '', libFiles }).filter(it => (it.kind === 'function' || it.kind === 'snippet') && !it.label.includes('의 '));
console.log('== 내 함수·골라 부른 함수');
for (const it of mine) console.log(`  [${it.kind}] ${it.label}` + (it.detail ? `  — ${it.detail}` : '') + (it.snippet ? `  ⇒ ${it.snippet}` : ''));
has('"수학의 " 뒤', 12, '(12와 18을 수학의 ', '최대공약수구하다', 'function');
has('붙여 부르는 라이브러리 함수', 12, '', '수학의 최대공약수구하다', 'function');
has('붙여 부르는 꼴 통째로', 12, '', '가와 나를 수학의 최대공약수구하다', 'snippet');
show('"철수의 " 뒤 (칸·의 뒤 내장 값 중 칸만)', 12, '(철수의 ', ['field']);
show('불러오기 파일 이름', 2, '"목', null);
