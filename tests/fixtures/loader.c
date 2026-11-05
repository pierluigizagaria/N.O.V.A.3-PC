typedef unsigned int u32;
extern int host_function(int);
extern int host_data;
extern int missing_first(int);
extern int missing_second(int);
extern int pthread_once(int*, void (*)(void));
extern int __cxa_atexit(void (*)(void*), void*, void*);

static int local_data = 41;
int* relative_pointer = &local_data;
int* absolute_pointer = &host_data;
char* absolute_addend_pointer = (char*)&host_data + 3;
int zero_bss[64];
volatile int init_order;
int once_control;
int once_count;

void fixture_init(void) { init_order = 1; }
__attribute__((constructor(101))) static void first_initializer(void) { init_order = init_order * 10 + 2; }
__attribute__((constructor(102))) static void second_initializer(void) { init_order = init_order * 10 + 3; }
__attribute__((noinline)) int relocation_probe(void) {
    return host_function(host_data) + *relative_pointer + *absolute_pointer;
}
__attribute__((visibility("hidden"), noinline)) int file_only_symbol(int a) { return a + 17; }
u32 file_only_address(void) { return (u32)&file_only_symbol; }
u32 sum_seven(u32 a, u32 b, u32 c, u32 d, u32 e, u32 f, u32 g) {
    return a + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7;
}
int trigger_first(void) { volatile int result = missing_first(3); return result + 1; }
int trigger_second(void) { volatile int result = missing_second(4); return result + 2; }
extern void crash_site(void);
void trigger_crash(void) { crash_site(); __asm__ volatile("" ::: "memory"); }
extern void once_callback(void);
int once_probe(void) {
    if (pthread_once(&once_control, once_callback)) return -1;
    if (pthread_once(&once_control, once_callback)) return -2;
    return once_count;
}
static void fixture_destructor(void* argument) { *(int*)argument = 999; }
int register_destructor(void) { return __cxa_atexit(fixture_destructor, &once_count, &local_data); }
