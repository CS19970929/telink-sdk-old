#include "st_test.h"
#include "st_kv.h"
#include "st_log.h"
#include "timer.h"
// #include <string.h>

static u32 s_tick = 0;
static u32 s_cnt = 0;

// 示例：运行态结构体（注意 packed，避免 padding 差异）
typedef struct __attribute__((packed)) {
    u16 soc;
    s32 cur_ma;
    u32 ah_mAs;
    u32 cycle;
} run_state_t;

void storage_test_init(void)
{
    s_tick = clock_time();
    s_cnt = 0;
}

static u32 now_ms(void)
{
    // Telink clock_time() 是 us tick（一般 16MHz/24MHz 体系下的系统tick），你按 SDK 实际换算
    // 这里简单粗暴：用 clock_time()/1000 当 ms（不严格，但测试够用）
    return clock_time() / 1000;
}

void storage_test_step(void)
{
    if (!clock_time_exceed(s_tick, 1000*1000)) return; // 1s
    s_tick = clock_time();
    s_cnt++;

    // 1) KV：每 5 秒写一次（模拟“经常变化但节流”）
    if ((s_cnt % 5) == 0)
    {
        run_state_t st;
        st.soc   = (u16)(s_cnt % 101);
        st.cur_ma= (s32)(-500 + (s_cnt % 1000));
        st.ah_mAs= s_cnt * 1234;
        st.cycle = s_cnt / 60;

        st_kv_set(0x0001, &st, sizeof(st));
    }

    // 2) LOG：每秒 2 条（模拟事件）
    for (int i=0;i<2;i++)
    {
        u8 payload[32];
        memset(payload, 0, sizeof(payload));
        payload[0] = (u8)s_cnt;
        payload[1] = (u8)i;

        st_log_append(0x1000 + i, now_ms(), payload, sizeof(payload));
    }

    // 3) 可选：读回校验（慢一点，但能确认 scan/读 OK）
    if ((s_cnt % 10) == 0)
    {
        run_state_t rd;
        u16 len = sizeof(rd);
        st_kv_ret_t r = st_kv_get(0x0001, &rd, &len);
        (void)r;
    }
}
