#ifndef WEB_C
#define WEB_C

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "scan.h"
#include "webstr.h"

#define MIN(x, y) x > y ? y : x
#define TAG "WEB"

#define HTMLMAXLEN 50000
#define BOXMAXLEN 160
// https://www.sitepoint.com/draw-rectangle-html

; // code wont conpile with out this, really don't understand why this is a thing

/* functions */
char *getHTMLRespond();
char *createOnlineBox(uint32_t);
char *createOfflineBox(uint32_t);
char *createPrevOnlineBox(uint32_t);
char *MAC2Char(uint8_t*);
char *getVendorName(const uint8_t *mac);
static char *fetch_vendor_online(const uint8_t *mac);
static char *vendor_from_cache(const uint8_t *mac);
static void mac_to_str(const uint8_t *mac, char out[18]);
static void start_vendor_resolver_task(void);
static void vendor_resolver_task(void *);
static void init_time_once(void);
static char *extract_json_string_value(const char *json, const char *key);


/* Our URI handler function to be called during GET /uri request */
esp_err_t get_handler(httpd_req_t *req)
{
    /* Send a simple response */
    char *respond = getHTMLRespond(); // request html respond
    if (!respond) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    size_t len = strnlen(respond, HTMLMAXLEN);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, respond, len); // send html response to client
    free(respond); // free html respond after sending
    return ESP_OK;
}

/* Our URI handler function to be called during POST /uri request */
esp_err_t post_handler(httpd_req_t *req)
{
    /* Destination buffer for content of HTTP POST request.
     * httpd_req_recv() accepts char* only, but content could
     * as well be any binary data (needs type casting).
     * In case of string data, null termination will be absent, and
     * content length would give length of string */
    char content[100];

    /* Truncate if content length larger than the buffer */
    size_t recv_size = MIN(req->content_len, sizeof(content));

    int ret = httpd_req_recv(req, content, recv_size);
    if (ret <= 0) {  /* 0 return value indicates connection closed */
        /* Check if timeout occurred */
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            /* In case of timeout one can choose to retry calling
             * httpd_req_recv(), but to keep it simple, here we
             * respond with an HTTP 408 (Request Timeout) error */
            httpd_resp_send_408(req);
        }
        /* In case of error, returning ESP_FAIL will
         * ensure that the underlying socket is closed */
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Respond POST");
    /* Send a simple response */
    const char resp[] = "URI POST Response";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* URI handler structure for GET /uri */
httpd_uri_t uri_get = {
    .uri      = "/",
    .method   = HTTP_GET,
    .handler  = get_handler,
    .user_ctx = NULL
};

/* URI handler structure for POST /uri */
httpd_uri_t uri_post = {
    .uri      = "/",
    .method   = HTTP_POST,
    .handler  = post_handler,
    .user_ctx = NULL
};

/* Function for starting the webserver */
httpd_handle_t start_webserver(void)
{
    /* Generate default configuration */
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    // Increase stack to avoid httpd task stack overflow when generating HTML
    config.stack_size = 8192; // bytes
    config.lru_purge_enable = true;

    /* Empty handle to esp_http_server */
    httpd_handle_t server = NULL;

    /* Start the httpd server */
    if (httpd_start(&server, &config) == ESP_OK) {
        /* Register URI handlers */
        httpd_register_uri_handler(server, &uri_get);
        httpd_register_uri_handler(server, &uri_post);
        start_vendor_resolver_task();
    }
    /* If server failed to start, handle will be NULL */
    return server;
}

/* Function for stopping the webserver */
void stop_webserver(httpd_handle_t server)
{
    if (server) {
        /* Stop the httpd server */
        httpd_stop(server);
    }
}


// reconstruct html
char * getHTMLRespond(){
    char *respond = calloc(HTMLMAXLEN, sizeof(char));
    if(respond == NULL){
        return NULL;
    }
    // headers
    strlcat(respond, HEAD, HTMLMAXLEN);
    // contents
    for(uint32_t i=0; i<getMaxDevice(); i++){
        char *newBox; // new device box
        if(getDeviceInfos()[i].online == 1) // online
            newBox = createOnlineBox(i);
        else if(getDeviceInfos()[i].online == 0) // offline
            newBox = createOfflineBox(i);
        else if(getDeviceInfos()[i].online == 2) // previous online
            newBox = createPrevOnlineBox(i);
        else
            newBox = NULL;
        if(newBox == NULL) continue; // skip if cannot render

        strlcat(respond, newBox, HTMLMAXLEN); // add into respond
        free(newBox); // free the space
    }
    // tails
    strlcat(respond, TAIL, HTMLMAXLEN);

    // log success message
    ESP_LOGI(TAG, "Successfully created HTML respond");
    return respond;
}

char * createOnlineBox(uint32_t num){
    char *onlineBox = calloc(BOXMAXLEN, sizeof(char)); // create space for storing new box
    if(onlineBox == NULL) return NULL; // check if correct

    // create char ip
    esp_ip4_addr_t ip;
    char char_ip[IP4ADDR_STRLEN_MAX];
    ip.addr = getDeviceInfos()[num].ip; // getip from database
    esp_ip4addr_ntoa(&ip, char_ip, IP4ADDR_STRLEN_MAX);

    // create char MAC
    char *char_mac = MAC2Char(getDeviceInfos()[num].mac);
    if (!char_mac) { free(onlineBox); return NULL; }
    char *vendor = getVendorName(getDeviceInfos()[num].mac);
    if (!vendor) vendor = strdup("Unknown Vendor");
    snprintf(onlineBox, BOXMAXLEN, CHAR_ONLINE_BOX, char_ip, char_mac, vendor);
    free(char_mac);
    free(vendor);
    return onlineBox;
}

char * createOfflineBox(uint32_t num){
    char *offlineBox = calloc(BOXMAXLEN, sizeof(char)); // create space for storing new box
    if(offlineBox == NULL) return NULL; // check if correct

    esp_ip4_addr_t ip;
    char char_ip[IP4ADDR_STRLEN_MAX];
    ip.addr = getDeviceInfos()[num].ip; // getip from database
    esp_ip4addr_ntoa(&ip, char_ip, IP4ADDR_STRLEN_MAX);


    snprintf(offlineBox, BOXMAXLEN, CHAR_OFFLINE_BOX, char_ip);
    return offlineBox;
}

char * createPrevOnlineBox(uint32_t num){
    char *prevOnlineBox = calloc(BOXMAXLEN, sizeof(char)); // create space for storing new box
    if(prevOnlineBox == NULL) return NULL; // check if correct

    // create char ip
    esp_ip4_addr_t ip;
    char char_ip[IP4ADDR_STRLEN_MAX];
    ip.addr = getDeviceInfos()[num].ip; // getip from database
    esp_ip4addr_ntoa(&ip, char_ip, IP4ADDR_STRLEN_MAX);

    // create char MAC
    char *char_mac = MAC2Char(getDeviceInfos()[num].mac);
    if (!char_mac) { free(prevOnlineBox); return NULL; }
    char *vendor = getVendorName(getDeviceInfos()[num].mac);
    if (!vendor) vendor = strdup("Unknown Vendor");
    snprintf(prevOnlineBox, BOXMAXLEN, CHAR_PREVONLINE_BOX, char_ip, char_mac, vendor);
    free(char_mac);
    free(vendor);
    return prevOnlineBox;
}

// turn uint8_t[6] to string
char *MAC2Char(uint8_t *mac){
    char *char_mac = malloc(18*sizeof(char));
    if (!char_mac) return NULL;
    snprintf(char_mac, 18, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return char_mac;
}

// Simple vendor cache and async resolver; online lookup provides actual vendor

// Tiny cache for resolved vendors (by OUI)
typedef struct { uint8_t oui[3]; char vendor[64]; int used; int resolving; int64_t next_retry_us; } vendor_cache_entry_t;
static vendor_cache_entry_t VENDOR_CACHE[24];
static SemaphoreHandle_t s_vendor_cache_mutex;

static int find_cache_slot_for_oui(const uint8_t *mac){
    for (int i=0;i<(int)(sizeof(VENDOR_CACHE)/sizeof(VENDOR_CACHE[0]));++i){
        if (VENDOR_CACHE[i].used && mac[0]==VENDOR_CACHE[i].oui[0] && mac[1]==VENDOR_CACHE[i].oui[1] && mac[2]==VENDOR_CACHE[i].oui[2]) return i;
    }
    for (int i=0;i<(int)(sizeof(VENDOR_CACHE)/sizeof(VENDOR_CACHE[0]));++i){ if (!VENDOR_CACHE[i].used) return i; }
    return 0; // overwrite
}

char *getVendorName(const uint8_t *mac){
    if (!mac) return NULL;

    // Cache lookup
    char *cached = vendor_from_cache(mac);
    if (cached) return cached;

    // Ensure a cache entry exists and schedule background resolve
    if (s_vendor_cache_mutex) xSemaphoreTake(s_vendor_cache_mutex, pdMS_TO_TICKS(50));
    int slot = find_cache_slot_for_oui(mac);
    if (!VENDOR_CACHE[slot].used){
        VENDOR_CACHE[slot].used = 1;
        VENDOR_CACHE[slot].oui[0]=mac[0]; VENDOR_CACHE[slot].oui[1]=mac[1]; VENDOR_CACHE[slot].oui[2]=mac[2];
        VENDOR_CACHE[slot].vendor[0] = '\0';
        VENDOR_CACHE[slot].resolving = 0;
        VENDOR_CACHE[slot].next_retry_us = 0;
    }
    if (s_vendor_cache_mutex) xSemaphoreGive(s_vendor_cache_mutex);

    // Return placeholder while async resolver fills cache
    return strdup("Resolving vendor...");
}


static void mac_to_str(const uint8_t *mac, char out[18]){
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}


static char *vendor_from_cache(const uint8_t *mac){
    for (size_t i=0;i<sizeof(VENDOR_CACHE)/sizeof(VENDOR_CACHE[0]);++i){
        if (VENDOR_CACHE[i].used && mac[0]==VENDOR_CACHE[i].oui[0] && mac[1]==VENDOR_CACHE[i].oui[1] && mac[2]==VENDOR_CACHE[i].oui[2]){
            if (VENDOR_CACHE[i].vendor[0] != '\0') return strdup(VENDOR_CACHE[i].vendor);
            break;
        }
    }
    return NULL;
}


static char *fetch_vendor_online(const uint8_t *mac){
    char mac_str[18];
    mac_to_str(mac, mac_str);
    char url[160];
    snprintf(url, sizeof(url), "https://api.maclookup.app/v2/macs/%s", mac_str);

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 3000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) { ESP_LOGW(TAG, "HTTP client init failed"); return NULL; }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Vendor fetch open failed (%s) for %s", esp_err_to_name(err), url);
        esp_http_client_cleanup(client);
        return NULL;
    }

    int64_t hdrs = esp_http_client_fetch_headers(client);
    if (hdrs < 0) {
        ESP_LOGW(TAG, "Vendor fetch headers failed for %s", url);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return NULL;
    }

    // Read response body (supports chunked)
    char tmp[256];
    size_t cap = 512, len = 0;
    char *buf = malloc(cap);
    if (!buf) { esp_http_client_close(client); esp_http_client_cleanup(client); return NULL; }
    int r;
    while ((r = esp_http_client_read(client, tmp, sizeof(tmp))) > 0) {
        if (len + r + 1 > cap) {
            size_t ncap = cap * 2; if (ncap < len + r + 1) ncap = len + r + 1;
            char *nbuf = realloc(buf, ncap);
            if (!nbuf) { free(buf); esp_http_client_close(client); esp_http_client_cleanup(client); return NULL; }
            buf = nbuf; cap = ncap;
        }
        memcpy(buf + len, tmp, r); len += r; buf[len] = '\0';
    }
    int status = esp_http_client_get_status_code(client);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (status != 200 || len == 0) {
        ESP_LOGW(TAG, "Vendor fetch HTTP %d len %u for %s", status, (unsigned)len, url);
        free(buf);
        return NULL;
    }

    // Check flags and extract company
    bool success_true = (strstr(buf, "\"success\":true") != NULL);
    bool found_true = (strstr(buf, "\"found\":true") != NULL);
    bool found_false = (strstr(buf, "\"found\":false") != NULL);
    char *vendor = NULL;
    if (success_true && found_true) {
        vendor = extract_json_string_value(buf, "company");
        if (vendor) {
            ESP_LOGI(TAG, "Resolved vendor for %s: %s", mac_str, vendor);
        }
    } else if (success_true && found_false) {
        vendor = strdup("Unknown Vendor");
        ESP_LOGI(TAG, "No assignment for %s; using Unknown Vendor", mac_str);
    } else {
        ESP_LOGW(TAG, "Vendor lookup ambiguous for %s", mac_str);
    }
    free(buf);
    return vendor; // may be NULL
}

static void init_time_once(void){
    static bool s_time_inited = false;
    if (s_time_inited) return;
    time_t now = 0; struct tm ti = {0};
    time(&now); localtime_r(&now, &ti);
    if (ti.tm_year < (2016-1900)){
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_init();
        for (int i=0;i<5;i++){
            vTaskDelay(pdMS_TO_TICKS(1000));
            time(&now); localtime_r(&now, &ti);
            if (ti.tm_year >= (2016-1900)) break;
        }
    }
    s_time_inited = true;
}

// Naive JSON string extractor for a top-level key
static char *extract_json_string_value(const char *json, const char *key){
    if (!json || !key) return NULL;
    // Find "key"
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(json, needle);
    if (!p) return NULL;
    p = strchr(p + strlen(needle), ':');
    if (!p) return NULL;
    // Skip whitespace
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if(*p != ':') { 
        return NULL; 
    }
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p != '"') return NULL; 
    p++;
    // Copy until unescaped quote
    char *out = malloc(128);
    if (!out) return NULL;
    size_t oi = 0; char c; int esc = 0;
    while ((c = *p++) != '\0'){
        if (esc){
            if (oi+2 >= 128) break; // truncate
            out[oi++] = c; esc = 0; continue;
        }
        if (c == '\\') { esc = 1; continue; }
        if (c == '"') break;
        if (oi+1 >= 128) break;
        out[oi++] = c;
    }
    out[oi] = '\0';
    return out;
}

static void vendor_resolver_task(void *arg){
    init_time_once();
    for(;;){
        deviceInfo *infos = getDeviceInfos();
        uint32_t max = getMaxDevice();
        if (!infos || max==0){ vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
        int resolved = 0;
        for (uint32_t i=0;i<max;i++){
            if (infos[i].ip == 0 && infos[i].online == 0) continue;
            const uint8_t *mac = infos[i].mac;
            if (s_vendor_cache_mutex) xSemaphoreTake(s_vendor_cache_mutex, portMAX_DELAY);
            int slot = find_cache_slot_for_oui(mac);
            vendor_cache_entry_t *e = &VENDOR_CACHE[slot];
            if (!e->used){ e->used=1; e->oui[0]=mac[0]; e->oui[1]=mac[1]; e->oui[2]=mac[2]; e->vendor[0]='\0'; e->resolving=0; e->next_retry_us=0; }
            int have = (e->vendor[0] != '\0');
            int64_t nowus = esp_timer_get_time();
            int can_try = (!e->resolving && nowus >= e->next_retry_us);
            if (s_vendor_cache_mutex) xSemaphoreGive(s_vendor_cache_mutex);
            if (have || !can_try) continue;

            // Mark resolving
            if (s_vendor_cache_mutex) xSemaphoreTake(s_vendor_cache_mutex, portMAX_DELAY);
            e->resolving = 1;
            if (s_vendor_cache_mutex) xSemaphoreGive(s_vendor_cache_mutex);

            ESP_LOGI(TAG, "Resolving vendor for OUI %02X:%02X:%02X...", mac[0], mac[1], mac[2]);
            char *vend = fetch_vendor_online(mac);
            if (s_vendor_cache_mutex) xSemaphoreTake(s_vendor_cache_mutex, portMAX_DELAY);
            if (vend){
                snprintf(e->vendor, sizeof(e->vendor), "%s", vend);
                free(vend);
                e->next_retry_us = nowus + 3600LL*1000000LL; // cache 1h
                ESP_LOGI(TAG, "Cached vendor: %s", e->vendor);
            } else {
                // Show something instead of being stuck on "Resolving vendor..."
                snprintf(e->vendor, sizeof(e->vendor), "%s", "Unknown Vendor");
                e->next_retry_us = nowus + 30000000LL; // retry in 30s; overwrite on success
                ESP_LOGW(TAG, "Vendor resolve failed; caching Unknown Vendor for now");
            }
            e->resolving = 0;
            if (s_vendor_cache_mutex) xSemaphoreGive(s_vendor_cache_mutex);

            if (++resolved >= 2) break; // be gentle each cycle
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

static void start_vendor_resolver_task(void){
    if (!s_vendor_cache_mutex) s_vendor_cache_mutex = xSemaphoreCreateMutex();
    static bool started = false; if (started) return; started = true;
    // TLS handshake can be stack-hungry; give this task a larger stack
    xTaskCreate(vendor_resolver_task, "vend_res", 12288, NULL, tskIDLE_PRIORITY+1, NULL);
}

// MAC Lookup
/* https://maclookup.app/ */



#endif
