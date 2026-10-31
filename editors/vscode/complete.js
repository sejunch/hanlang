// 한랭 자동완성 — 무엇을 추천할지 고르는 부분 (vscode 모듈을 안 써서 node 로 따로 시험할 수 있다)
//
// 기호 = `hanc --기호 파일.han` 의 출력 (탭으로 나눈 줄):
//   함수\t별명\t이름\t매개:조사…   묶음\t이름\t칸:타입…   전역\t이름   변수\t시작줄\t끝줄\t이름
// 저장할 때 받아 두고(입력 중에는 파싱 오류가 나기 쉬워서 마지막으로 성공한 것), 그 뒤에 새로 쓴 변수는
// 지금 글에서 'X는 …' 꼴을 훑어서 보탠다.
'use strict';

function parseSymbols(text) {
    const s = { funcs: [], structs: [], globals: [], vars: [] };
    for (const line of (text || '').split('\n')) {
        const f = line.split('\t');
        if (f[0] === '함수' && f.length >= 3) {
            s.funcs.push({ alias: f[1], name: f[2], params: f.slice(3).map(p => { const i = p.lastIndexOf(':'); return { name: p.slice(0, i), josa: p.slice(i + 1) }; }) });
        } else if (f[0] === '묶음' && f.length >= 2) {
            s.structs.push({ name: f[1], fields: f.slice(2).map(p => { const i = p.lastIndexOf(':'); return { name: p.slice(0, i), type: p.slice(i + 1) }; }) });
        } else if (f[0] === '전역' && f.length >= 2) {
            s.globals.push(f[1]);
        } else if (f[0] === '변수' && f.length >= 4) {
            s.vars.push({ from: Number(f[1]), to: Number(f[2]), name: f[3] });
        }
    }
    return s;
}

// 부르는 꼴: 가와 나를 더하다
function callShape(fn) {
    return fn.params.map(p => p.name + p.josa + ' ').join('') + fn.name;
}
// 스니펫: ${1:가}와 ${2:나}를 더하다
function callSnippet(fn, withAlias) {
    let i = 0;
    const args = fn.params.map(p => `\${${++i}:${p.name}}${p.josa} `).join('');
    return args + (withAlias && fn.alias ? fn.alias + '의 ' : '') + fn.name;
}

// 키워드·내장 값 (설명은 extension.js 의 DOCS 를 씀). 의 뒤에서만 쓰는 것은 따로
const KEYWORDS = ['만약', '라면', '아니면', '인 동안', '반복하다', '멈추다', '계속하다', '돌려주다', '종료하다', '말하다', '듣다',
    '바꾸다', '넣다', '빼다', '늘리다', '줄이다', '있나', '불러오다', '전역', '지역', '부르다', '함수', '묶음', '새', '빈', '없음',
    '참', '거짓', '정수', '소수', '문자', '목록', '사전', '평면벡터', '공간벡터', '파일', '열다', '읽기로', '쓰기로', '붙여쓰기로',
    '실행하다', '기다리다', '맞추다', '창', '색칠하다', '사각형그리다', '선긋다', '점찍다', '글쓰다', '보여주다', '다시그리다',
    '삼각형그리다', '그림으로그리다', '누르고있나', '가두다', '이다', '란', '이란', '해보다', '안 되면', '실패하다', '잠그고', '잠그다', '풀다', '따로', '나눠'];
const VALUES = ['입력끝', '명령줄', '실행파일', '지금시각', '눌린키들', '누른키들', '창닫힘', '마우스위치', '마우스움직임', '마우스눌림', '창크기'];
const AFTER_UI = ['개수', '번째', '제곱근', '절댓값', '반올림', '올림', '버림', '무작위', '코드', '글자', '행수', '열수', '열쇠들',
    '환경값', '실제경로', '벡터', '내적', '외적', '성분곱', '길이', '방향', 'x', 'y', 'z'];

// 지금 글에서 새로 만든 변수 ('X는 ' / '전역 X는 ' / '지역 X는 ') — 저장 전이라 기호에 없는 것
function scanAssigned(docText) {
    const out = new Set();
    const re = /(?:^|[{}.;]\s*|\b(?:전역|지역)\s+)([가-힣A-Za-z_][가-힣A-Za-z0-9_]*?)(?:는|은)\s/gm;
    let m;
    while ((m = re.exec(docText))) if (m[1].length > 0) out.add(m[1]);
    return [...out];
}

// 돌려주는 것: [{label, kind, detail, doc, insert, snippet, filter, sort}]
// kind: 'function' | 'struct' | 'field' | 'variable' | 'keyword' | 'value' | 'file' | 'snippet'
function complete(opts) {
    const { symbols, docText = '', line = 0, linePrefix = '', libFiles = [] } = opts;
    const s = symbols || { funcs: [], structs: [], globals: [], vars: [] };
    const items = [];
    const seen = new Set();
    const add = (it) => { const k = it.kind + '|' + it.label; if (!seen.has(k)) { seen.add(k); items.push(it); } };

    // "수… 를 치는 중이면 라이브러리 파일 이름
    if (/"[^"]*$/.test(linePrefix)) {
        for (const f of libFiles) add({ label: f, kind: 'file', detail: '표준 라이브러리', insert: f, sort: '0' });
        return items;
    }
    // 별명의 … 다음: 그 파일 함수만 (별명은 이미 썼으니 이름만 넣음)
    const aliasM = /([가-힣A-Za-z0-9_]+)의\s+[가-힣A-Za-z0-9_]*$/.exec(linePrefix);
    const aliases = new Set(s.funcs.filter(f => f.alias && f.alias !== '-').map(f => f.alias));
    if (aliasM && aliases.has(aliasM[1])) {
        for (const fn of s.funcs.filter(f => f.alias === aliasM[1])) {
            add({ label: fn.name, kind: 'function', detail: callShape(fn), insert: fn.name, sort: '0' });
        }
        return items;
    }
    // X의 … 다음: 묶음 칸 이름과 의 뒤에 쓰는 내장 값
    if (/의\s+[가-힣A-Za-z0-9_]*$/.test(linePrefix)) {
        for (const st of s.structs) for (const fd of st.fields)
            add({ label: fd.name, kind: 'field', detail: `${st.name} 의 칸 (${fd.type})`, insert: fd.name, sort: '0' });
        for (const w of AFTER_UI) add({ label: w, kind: 'value', insert: w, sort: '1' });
        return items;
    }

    // 변수: 커서가 든 함수의 것, 아니면 맨 위 것 + 전역 + 지금 글에서 새로 만든 것
    const inFunc = s.vars.some(v => v.from > 0 && line >= v.from && line <= v.to);
    for (const v of s.vars) {
        if (inFunc ? (v.from > 0 && line >= v.from && line <= v.to) : v.from === 0)
            add({ label: v.name, kind: 'variable', insert: v.name, sort: '0' });
    }
    for (const g of s.globals) add({ label: g, kind: 'variable', detail: '전역 변수', insert: g, sort: '0' });
    // 저장 뒤에 새로 쓴 이름만 (기호에 이미 있는 이름은 그 범위를 따름, 묶음 칸 선언 '이름은 문자.' 는 빼고)
    const known = new Set([...s.vars.map(v => v.name), ...s.globals]);
    const fields = new Set(s.structs.flatMap(st => st.fields.map(f => f.name)));
    for (const n of scanAssigned(docText)) if (!known.has(n) && !fields.has(n)) add({ label: n, kind: 'variable', insert: n, sort: '0' });

    // 함수: 이름만 넣기 + 부르는 꼴 통째로 넣기 (한랭은 인자를 먼저 쓰고 함수 이름이 맨 뒤라 둘 다)
    for (const fn of s.funcs) {
        if (fn.alias === '-') continue;
        const full = (fn.alias ? fn.alias + '의 ' : '') + fn.name;
        add({ label: full, kind: 'function', detail: callShape(fn), insert: full, filter: fn.name + ' ' + full, sort: '1' });
        if (fn.params.length > 0)
            add({ label: callShape(fn).replace(fn.name, full), kind: 'snippet', detail: '부르는 꼴 통째로', snippet: callSnippet(fn, true), filter: fn.name + ' ' + full, sort: '2' });
    }
    for (const st of s.structs)
        add({ label: st.name, kind: 'struct', detail: '묶음: ' + st.fields.map(f => `${f.name}(${f.type})`).join(', '), insert: st.name, sort: '1' });
    for (const k of KEYWORDS) add({ label: k, kind: 'keyword', insert: k, sort: '3' });
    for (const v of VALUES) add({ label: v, kind: 'value', insert: v, sort: '3' });
    return items;
}

module.exports = { parseSymbols, complete, callShape, callSnippet, scanAssigned };
