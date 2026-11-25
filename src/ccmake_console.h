#ifndef _INCLUDE_CCMAKE_CONSOLE_H_
#define _INCLUDE_CCMAKE_CONSOLE_H_

/* 将 UTF-8 文本按当前控制台编码输出。 */
void ccmake_write_utf8(const char *text);
void ccmake_write_utf8_color(
    const char *text, const char *foreground
);

#endif
