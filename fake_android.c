#include <stdio.h>
#include <stdarg.h>

// 1. 强行造一个安卓的 __sF 符号（标准输入/输出/错误）
FILE __sF[3];

// 2. 在程序启动时，用 Linux 的文件流覆盖它
__attribute__((constructor))
static void init_sF() {
    __sF[0] = *stdin;
    __sF[1] = *stdout;
    __sF[2] = *stderr;
}

// 3. 伪造安卓底层日志打印
int __android_log_print(int prio, const char* tag, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    return 1;
}

// 4. 伪造安卓资源读取（即使读不到也不让程序崩溃）
void* AAssetManager_fromJava(void* env, void* assetManager) { return 0; }
void* AAssetManager_open(void* mgr, const char* filename, int mode) { return 0; }
void AAsset_close(void* asset) {}
long AAsset_getLength(void* asset) { return 0; }
int AAsset_read(void* asset, void* buf, unsigned long count) { return 0; }