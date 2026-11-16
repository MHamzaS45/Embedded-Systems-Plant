// Minimal ThingSpeak connectivity test for Raspberry Pi Pico W (Pico SDK 2.x).
// Sends field1 = on-chip temperature (real hardware reading), field2 = counter.
// ThingSpeak free tier: min 15 s between updates, so we send every 20 s.
#include <cstdio>
#include <cstring>
#include <algorithm>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/adc.h"
#include "lwip/dns.h"
#include "lwip/tcp.h"
#include "lwip/pbuf.h"

// channel > API Keys > Write API Key

#define WIFI_SSID      "YOUR_SSID"
#define WIFI_PASS      "YOUR_PASSWORD"
#define TS_WRITE_KEY   "C1D44AAF76AHEQFF"     
#define TS_HOST        "api.thingspeak.com"
#define SEND_PERIOD_MS 20000

struct Ctx {
    volatile bool dnsDone = false, dnsOk = false, done = false;
    ip_addr_t ip;
    tcp_pcb* pcb = nullptr;
    char req[300]; u16_t len = 0;
    char resp[256]; size_t n = 0;
};

static void on_dns(const char*, const ip_addr_t* ip, void* arg) {
    auto* c = static_cast<Ctx*>(arg);
    if (ip) { c->ip = *ip; c->dnsOk = true; }
    c->dnsDone = true;
}
static err_t finish(Ctx* c, tcp_pcb* pcb) {
    tcp_arg(pcb, nullptr); tcp_recv(pcb, nullptr); tcp_err(pcb, nullptr);
    c->pcb = nullptr; c->resp[c->n] = 0; c->done = true;
    if (tcp_close(pcb) != ERR_OK) { tcp_abort(pcb); return ERR_ABRT; }
    return ERR_OK;
}
static err_t on_connected(void* arg, tcp_pcb* pcb, err_t err) {
    auto* c = static_cast<Ctx*>(arg);
    if (err != ERR_OK) return finish(c, pcb);
    if (tcp_write(pcb, c->req, c->len, TCP_WRITE_FLAG_COPY) != ERR_OK || tcp_output(pcb) != ERR_OK)
        return finish(c, pcb);
    return ERR_OK;
}
static err_t on_recv(void* arg, tcp_pcb* pcb, pbuf* p, err_t) {
    auto* c = static_cast<Ctx*>(arg);
    if (!p) return finish(c, pcb);                              // server closed: response complete
    size_t room = sizeof(c->resp) - 1 - c->n;
    c->n += pbuf_copy_partial(p, c->resp + c->n, std::min<size_t>(p->tot_len, room), 0);
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}
static void on_err(void* arg, err_t) { auto* c = static_cast<Ctx*>(arg); c->pcb = nullptr; c->done = true; }

static float onboard_temp_c() {
    adc_select_input(4);                                         // internal temperature sensor
    float v = adc_read() * 3.3f / 4096.0f;
    return 27.0f - (v - 0.706f) / 0.001721f;
}

// Returns the ThingSpeak entry id (>0) on success, 0 or negative on failure.
static long send_to_thingspeak(float f1, unsigned f2) {
    static Ctx c;                                                // static: keeps stack small
    c = Ctx();
    c.len = snprintf(c.req, sizeof c.req,
        "GET /update?api_key=%s&field1=%.2f&field2=%u HTTP/1.1\r\n"
        "Host: " TS_HOST "\r\nConnection: close\r\n\r\n", TS_WRITE_KEY, f1, f2);

    // 1) DNS
    cyw43_arch_lwip_begin();
    err_t e = dns_gethostbyname(TS_HOST, &c.ip, on_dns, &c);
    cyw43_arch_lwip_end();
    if (e == ERR_OK) { c.dnsOk = true; c.dnsDone = true; }
    else if (e != ERR_INPROGRESS) { printf("DNS error %d\n", e); return -1; }
    for (int i = 0; i < 500 && !c.dnsDone; i++) sleep_ms(10);
    if (!c.dnsOk) { printf("DNS failed\n"); return -2; }

    // 2) TCP connect to port 80 and send the request
    cyw43_arch_lwip_begin();
    tcp_pcb* pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!pcb) { cyw43_arch_lwip_end(); return -3; }
    c.pcb = pcb;
    tcp_arg(pcb, &c); tcp_err(pcb, on_err); tcp_recv(pcb, on_recv);
    e = tcp_connect(pcb, &c.ip, 80, on_connected);
    if (e != ERR_OK) { tcp_abort(pcb); cyw43_arch_lwip_end(); return -4; }
    cyw43_arch_lwip_end();

    // 3) Wait for the response (max 8 s)
    for (int i = 0; i < 800 && !c.done; i++) sleep_ms(10);
    if (!c.done) {
        cyw43_arch_lwip_begin();
        if (c.pcb) { tcp_arg(c.pcb, nullptr); tcp_recv(c.pcb, nullptr); tcp_err(c.pcb, nullptr); tcp_abort(c.pcb); }
        cyw43_arch_lwip_end();
        printf("Timeout waiting for response\n");
        return -5;
    }

    printf("--- response ---\n%s\n----------------\n", c.resp);
    if (strncmp(c.resp, "HTTP/1.1 200", 12) != 0) return -6;
    const char* body = strstr(c.resp, "\r\n\r\n");
    return body ? atol(body + 4) : -7;                           // body = new entry id, "0" = rejected
}

int main() {
    stdio_init_all();
    sleep_ms(3000);                                              // let USB serial attach
    adc_init(); adc_set_temp_sensor_enabled(true);

    if (cyw43_arch_init()) { printf("Wi-Fi chip init failed\n"); return 1; }
    cyw43_arch_enable_sta_mode();
    printf("Connecting to %s ...\n", WIFI_SSID);
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASS, CYW43_AUTH_WPA2_AES_PSK, 20000)) {
        printf("Wi-Fi connect FAILED (check SSID/password, 2.4 GHz network)\n");
        return 1;
    }
    printf("Wi-Fi connected\n");

    unsigned count = 0;
    while (true) {
        float t = onboard_temp_c();
        long id = send_to_thingspeak(t, ++count);
        if (id > 0) printf("[%u] OK: sent %.2f C, entry id %ld\n", count, t, id);
        else        printf("[%u] FAILED (code %ld)\n", count, id);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, id > 0);      // LED on = last send OK
        sleep_ms(SEND_PERIOD_MS);
    }
}