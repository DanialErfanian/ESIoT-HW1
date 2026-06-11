
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <ctype.h>
#include <strings.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_http_client.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"
#include "psa/crypto.h"

#define UART_PORT            UART_NUM_0
#define UART_BAUDRATE        115200
#define UART_RX_BUF_SIZE     4096
#define MAX_LINE_LEN         512
#define MANIFEST_MAX         4096
#define HTTP_CHUNK           1024

/* Set these before building. The grader hosts update files on your laptop,
 * so the ESP32 must be on the same network and able to reach --host-ip. */
#define LAB_WIFI_SSID        "CHANGE_ME_WIFI"
#define LAB_WIFI_PASS        "CHANGE_ME_PASSWORD"
#define WIFI_CONNECTED_BIT   BIT0

#define DEVICE_MODEL         "ce-40876"
#define NS_SYS               "sys"
#define KEY_VERSION          "ver"

static uint32_t g_current_version = 1;
static EventGroupHandle_t s_wifi_event_group;

typedef struct {
    char device_model[48];
    uint32_t version;
    uint32_t size;
    char sha256[65];
    char firmware_url[256];
    char signature[129];
} manifest_t;

static void uart_write_str(const char *s) {
    uart_write_bytes(UART_PORT, s, strlen(s));
}

static void uart_printf(const char *fmt, ...) {
    char buf[768];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    uart_write_str(buf);
}


static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static bool init_wifi_sta(void) {
    if (strcmp(LAB_WIFI_SSID, "CHANGE_ME_WIFI") == 0) {
        uart_write_str("network=not_configured\r\n");
        return false;
    }

    s_wifi_event_group = xEventGroupCreate();
    if (!s_wifi_event_group) {
        uart_write_str("network=event_group_error\r\n");
        return false;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {0};
    snprintf((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", LAB_WIFI_SSID);
    snprintf((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", LAB_WIFI_PASS);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(20000));
    if (bits & WIFI_CONNECTED_BIT) {
        uart_write_str("network=ready\r\n");
        return true;
    }

    uart_write_str("network=connect_timeout\r\n");
    return false;
}

static bool read_line_uart(char *out, size_t max_len) {
    size_t idx = 0;
    while (1) {
        uint8_t c;
        int n = uart_read_bytes(UART_PORT, &c, 1, pdMS_TO_TICKS(100));
        if (n <= 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        if (c == '\r' || c == '\n') {
            out[idx] = '\0';
            uart_write_str("\r\n");
            return true;
        }
        if (c == 0x08 || c == 0x7f) {
            if (idx > 0) {
                idx--;
                uart_write_str("\b \b");
            }
            continue;
        }
        if (isprint((int)c) && idx < max_len - 1) {
            out[idx++] = (char)c;
            uart_write_bytes(UART_PORT, (const char *)&c, 1);
        }
    }
}

static void trim_inplace(char *s) {
    if (!s) return;
    char *start = s;
    while (*start && isspace((unsigned char)*start)) start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) s[--len] = '\0';
}

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static bool hex_to_bytes(const char *hex, uint8_t *out, size_t out_len) {
    if (!hex || strlen(hex) != out_len * 2) return false;
    for (size_t i = 0; i < out_len; i++) {
        int hi = hex_nibble(hex[2*i]);
        int lo = hex_nibble(hex[2*i+1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

static void bytes_to_hex(const uint8_t *in, size_t n, char *out, size_t out_sz) {
    static const char *hex = "0123456789abcdef";
    if (out_sz < n * 2 + 1) return;
    for (size_t i = 0; i < n; i++) {
        out[2*i] = hex[in[i] >> 4];
        out[2*i+1] = hex[in[i] & 0xf];
    }
    out[n*2] = '\0';
}

static bool sha256_buffer(const uint8_t *data, size_t len, uint8_t out32[32]) {
    size_t hash_len = 0;
    psa_status_t st = psa_crypto_init();
    if (st != PSA_SUCCESS) return false;
    st = psa_hash_compute(PSA_ALG_SHA_256, data, len, out32, 32, &hash_len);
    return st == PSA_SUCCESS && hash_len == 32;
}

static esp_err_t http_get_to_buffer(const char *url, char *out, size_t out_sz) {
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 8000,
        .buffer_size = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_FAIL;

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }
    esp_http_client_fetch_headers(client);

    size_t used = 0;
    while (1) {
        int r = esp_http_client_read(client, out + used, out_sz - used - 1);
        if (r < 0) {
            err = ESP_FAIL;
            break;
        }
        if (r == 0) {
            break;
        }
        used += (size_t)r;
        if (used >= out_sz - 1) {
            err = ESP_ERR_NO_MEM;
            break;
        }
    }
    out[used] = '\0';
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return err;
}

static esp_err_t http_hash_and_size(const char *url, uint8_t hash32[32], uint32_t *size_out) {
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 10000,
        .buffer_size = HTTP_CHUNK,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_FAIL;

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }
    esp_http_client_fetch_headers(client);

    psa_status_t st = psa_crypto_init();
    if (st != PSA_SUCCESS) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
    st = psa_hash_setup(&op, PSA_ALG_SHA_256);
    if (st != PSA_SUCCESS) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    uint8_t buf[HTTP_CHUNK];
    uint32_t total = 0;
    while (1) {
        int r = esp_http_client_read(client, (char *)buf, sizeof(buf));
        if (r < 0) {
            psa_hash_abort(&op);
            err = ESP_FAIL;
            break;
        }
        if (r == 0) {
            break;
        }
        total += (uint32_t)r;
        st = psa_hash_update(&op, buf, (size_t)r);
        if (st != PSA_SUCCESS) {
            psa_hash_abort(&op);
            err = ESP_FAIL;
            break;
        }
    }

    if (err == ESP_OK) {
        size_t hash_len = 0;
        st = psa_hash_finish(&op, hash32, 32, &hash_len);
        if (st != PSA_SUCCESS || hash_len != 32) err = ESP_FAIL;
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (err == ESP_OK && size_out) *size_out = total;
    return err;
}

static bool parse_manifest(const char *json, manifest_t *m) {
    memset(m, 0, sizeof(*m));
    cJSON *root = cJSON_Parse(json);
    if (!root) return false;

    cJSON *model = cJSON_GetObjectItem(root, "device_model");
    cJSON *version = cJSON_GetObjectItem(root, "version");
    cJSON *size = cJSON_GetObjectItem(root, "size");
    cJSON *sha = cJSON_GetObjectItem(root, "sha256");
    cJSON *fw = cJSON_GetObjectItem(root, "firmware_url");
    cJSON *sig = cJSON_GetObjectItem(root, "signature");

    bool ok = cJSON_IsString(model) && cJSON_IsNumber(version) &&
              cJSON_IsNumber(size) && cJSON_IsString(sha) && cJSON_IsString(fw);
    if (ok) {
        snprintf(m->device_model, sizeof(m->device_model), "%s", model->valuestring);
        m->version = (uint32_t)version->valuedouble;
        m->size = (uint32_t)size->valuedouble;
        snprintf(m->sha256, sizeof(m->sha256), "%s", sha->valuestring);
        snprintf(m->firmware_url, sizeof(m->firmware_url), "%s", fw->valuestring);
        if (cJSON_IsString(sig)) snprintf(m->signature, sizeof(m->signature), "%s", sig->valuestring);
    }

    cJSON_Delete(root);
    return ok;
}

static esp_err_t nvs_read_u32_default(const char *key, uint32_t def, uint32_t *out) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_SYS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        *out = def;
        return err;
    }
    uint32_t v = def;
    err = nvs_get_u32(h, key, &v);
    nvs_close(h);
    *out = v;
    return err;
}

static esp_err_t nvs_write_u32_val(const char *key, uint32_t v) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_SYS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u32(h, key, v);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

static void load_version(void) {
    uint32_t v = 1;
    nvs_read_u32_default(KEY_VERSION, 1, &v);
    if (v == 0) v = 1;
    g_current_version = v;
}

static void reset_state(void) {
    g_current_version = 1;
    nvs_write_u32_val(KEY_VERSION, g_current_version);
}

static void init_uart(void) {
    uart_config_t cfg = {0};
    cfg.baud_rate = UART_BAUDRATE;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, UART_RX_BUF_SIZE, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

static void init_nvs(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }
    load_version();
}


static void cmd_help(void) {
    uart_write_str(
        "Commands:\r\n"
        "  help\r\n"
        "  version\r\n"
        "  update_apply <manifest_url>\r\n"
        "  reset_state CONFIRM\r\n"
        "\r\n"
    );
}

static void cmd_version(void) {
    uart_printf("device_model=%s\r\n", DEVICE_MODEL);
    uart_printf("current_version=%lu\r\n", (unsigned long)g_current_version);
    uart_write_str("update_state=ready\r\n");
}

static void cmd_update_apply(const char *manifest_url) {
    if (!manifest_url || manifest_url[0] == '\0') {
        uart_write_str("ERR usage: update_apply <manifest_url>\r\n");
        return;
    }

    char manifest_json[MANIFEST_MAX];
    esp_err_t err = http_get_to_buffer(manifest_url, manifest_json, sizeof(manifest_json));
    if (err != ESP_OK) {
        uart_printf("ERR manifest_download %s\r\n", esp_err_to_name(err));
        return;
    }

    manifest_t m;
    if (!parse_manifest(manifest_json, &m)) {
        uart_write_str("ERR manifest_parse\r\n");
        return;
    }

    if (strcmp(m.device_model, DEVICE_MODEL) != 0) {
        uart_write_str("ERR device_model_mismatch\r\n");
        return;
    }

    uint8_t actual_hash[32];
    uint32_t actual_size = 0;
    err = http_hash_and_size(m.firmware_url, actual_hash, &actual_size);
    if (err != ESP_OK) {
        uart_printf("ERR firmware_download %s\r\n", esp_err_to_name(err));
        return;
    }

    char actual_hex[65];
    bytes_to_hex(actual_hash, sizeof(actual_hash), actual_hex, sizeof(actual_hex));

    if (actual_size != m.size) {
        uart_printf("ERR size_mismatch expected=%lu actual=%lu\r\n",
                    (unsigned long)m.size, (unsigned long)actual_size);
        return;
    }

    if (strcasecmp(actual_hex, m.sha256) != 0) {
        uart_write_str("ERR sha256_mismatch\r\n");
        return;
    }

    g_current_version = m.version;
    nvs_write_u32_val(KEY_VERSION, g_current_version);
    uart_printf("UPDATE_OK version=%lu\r\n", (unsigned long)g_current_version);
}

static void handle_command(char *line) {
    trim_inplace(line);
    if (line[0] == '\0') return;

    char *save = NULL;
    char *cmd = strtok_r(line, " \t", &save);
    if (!cmd) return;

    if (strcasecmp(cmd, "help") == 0) { cmd_help(); return; }
    if (strcasecmp(cmd, "version") == 0) { cmd_version(); return; }
    if (strcasecmp(cmd, "update_apply") == 0) {
        char *url = save;
        if (url) trim_inplace(url);
        cmd_update_apply(url);
        return;
    }
    if (strcasecmp(cmd, "reset_state") == 0) {
        char *arg = strtok_r(NULL, " \t", &save);
        if (arg && strcmp(arg, "CONFIRM") == 0) {
            reset_state();
            uart_write_str("OK reset_state\r\n");
        } else {
            uart_write_str("ERR usage: reset_state CONFIRM\r\n");
        }
        return;
    }
    uart_write_str("ERR unknown command\r\n");
}

void app_main(void) {
    esp_log_level_set("*", ESP_LOG_WARN);
    init_uart();
    init_nvs();
    psa_crypto_init();

    uart_write_str("\r\n=== CE-40876 Firmware Update Service ===\r\n");
    init_wifi_sta();
    uart_write_str("Type 'help'.\r\n\r\n");

    char line[MAX_LINE_LEN];
    while (1) {
        uart_write_str("> ");
        if (read_line_uart(line, sizeof(line))) handle_command(line);
    }
}
