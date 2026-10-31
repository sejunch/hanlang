/* 한랭 런타임: 소리 대체품 — ALSA 헤더가 없을 때 make 가 깨지지 않게. 소리내다 를 쓰면 실행 오류 */
__attribute__((noreturn)) void __han_fail_msg(long line, const char *msg);
void __han_sound(long *list, long line) { (void)list; __han_fail_msg(line, "이 hanc 는 소리 없이 빌드됨 (ALSA 헤더가 없었음)"); }
void __han_sound_file(const char *path, long line) { (void)path; __han_fail_msg(line, "이 hanc 는 소리 없이 빌드됨 (ALSA 헤더가 없었음)"); }
