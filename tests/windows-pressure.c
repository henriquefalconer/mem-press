/* Include the actual implementation so tests exercise production policy. */
#define main mem_press_main
#include "../mem-press.c"
#undef main
#include <assert.h>

int main(void) {
    assert(windows_pressure_level(1, FALSE, TRUE, 5) == 1);
    assert(windows_pressure_level(1, FALSE, FALSE, 90) == 2);
    assert(windows_pressure_level(1, TRUE, FALSE, 90) == 3);
    assert(windows_pressure_level(1, TRUE, TRUE, 90) == 3);
    assert(windows_pressure_level(0, FALSE, FALSE, 21) == 1);
    assert(windows_pressure_level(0, FALSE, FALSE, 20) == 2);
    assert(windows_pressure_level(0, FALSE, FALSE, 11) == 2);
    assert(windows_pressure_level(0, FALSE, FALSE, 10) == 3);
    assert(windows_pressure_level(0, FALSE, FALSE, 0) == 3);
    assert(windows_pressure_level(0, FALSE, FALSE, -1) == 0);

    int available = -1;
    assert(read_windows_memory(&available));
    assert(available >= 0 && available <= 100);
    int level = read_windows_pressure(available);
    assert(g_low_memory && g_high_memory);
    BOOL low = FALSE, high = FALSE;
    assert(QueryMemoryResourceNotification(g_low_memory, &low));
    assert(QueryMemoryResourceNotification(g_high_memory, &high));
    assert(level >= 1 && level <= 3);
    /* Do not assert equality across successive live queries: state may change. */
    printf("Native Windows: available=%d%%, sampled level=%d, live low=%d high=%d\n",
           available, level, (int)low, (int)high);
    HANDLE low_handle = g_low_memory, high_handle = g_high_memory;
    read_windows_pressure(available);
    assert(g_low_memory == low_handle && g_high_memory == high_handle);
    cleanup();
    assert(!g_low_memory && !g_high_memory);
    cleanup(); /* teardown is idempotent */
    puts("Windows pressure policy and live API tests passed");
    return 0;
}
