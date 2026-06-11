# README - تمرین به‌روزرسانی Firmware روی ESP32-S3

## مشخصات اجرا

* برد: `ESP32-S3`
* مدل دستگاه: `ce-40876`
* IP لپ‌تاپ: `10.114.151.190`
* پورت HTTP: `8000`
* پورت سریال: `/dev/ttyACM0`

خروجی شناسایی Flash:

```text
Chip type: ESP32-S3
Detected flash size: 16MB
MAC: 1c:db:d4:99:55:e8
```

---

## آماده‌سازی فایل Firmware

```bash
mkdir -p files
cp E1_Students/build/CE40876_E2_update.bin files/
```

---

## ساخت Manifest

```bash
python3 tools/make_manifest.py \
  --firmware files/CE40876_E2_update.bin \
  --version 2 \
  --base-url http://10.114.151.190:8000 \
  --model ce-40876 \
  --out files/manifest.json
```

## manifest:

```json
{
  "device_model": "ce-40876",
  "version": 2,
  "size": 901472,
  "sha256": "ab8bdc79a6ede66cb6e5522fc1fe67a6aa5b54b8fdb8911cb978bd41d8ec49ef",
  "firmware_url": "http://10.114.151.190:8000/CE40876_E2_update.bin"
}
```

---

## اجرای سرور HTTP

```bash
cd files
python3 -m http.server 8000
```

آدرس مورد استفاده برای manifest:

```text
http://10.114.151.190:8000/manifest.json
```

---

## دستورات موجود روی برد

طبق firmware فعلی، دستورات پشتیبانی‌شده عبارت‌اند از:

```text
help
version
update_apply <manifest_url>
reset_state CONFIRM
```

---

## اجرای به‌روزرسانی روی برد

در مانیتور سریال (نرخ باود `115200`):

```text
update_apply http://10.114.151.190:8000/manifest.json
```

خروجی قبل از به‌روزرسانی:

```text
> version
device_model=ce-40876
current_version=1
update_state=ready
```

خروجی هنگام به‌روزرسانی موفق:

```text
> update_apply http://10.114.151.190:8000/manifest.json
UPDATE_OK version=2
```

خروجی بعد از به‌روزرسانی موفق:

```text
> version
device_model=ce-40876
current_version=2
update_state=ready
```

---

## بررسی‌هایی که firmware فعلی واقعاً انجام می‌دهد

در نسخه‌ی فعلی `main.c`، تابع `cmd_update_apply` این مراحل را به ترتیب انجام می‌دهد:

1. دانلود manifest از طریق HTTP
2. تجزیه (parse) فیلدهای manifest
3. بررسی برابری `device_model` با `ce-40876`
4. دانلود فایل firmware به‌صورت چانک‌چانک و محاسبه‌ی streaming `SHA-256` و اندازه‌ی واقعی
5. بررسی برابری `size`
6. بررسی برابری `sha256`
7. ذخیره‌ی `version` در NVS و چاپ `UPDATE_OK version=N`

نکته‌ی مهم: فایل firmware روی هیچ پارتیشن فلش نوشته نمی‌شود و اجرا هم نمی‌شود؛
فقط دانلود و هش می‌شود تا اعتبارسنجی شود. تنها چیزی که به‌صورت ماندگار تغییر می‌کند،
مقدار `version` ذخیره‌شده در NVS است.

---

## خروجی‌ها

خطاها:

```text
ERR usage: update_apply <manifest_url>
ERR manifest_download <esp_err>
ERR manifest_parse
ERR device_model_mismatch
ERR firmware_download <esp_err>
ERR size_mismatch expected=<n> actual=<m>
ERR sha256_mismatch
```

---

## رفتار واقعی در سناریوهای تست

| سناریو                                   | رفتار firmware فعلی                               |
|------------------------------------------|--------------------------------------------------|
| بسته معتبر با نسخه جدیدتر                | پذیرفته می‌شود → `UPDATE_OK version=N`           |
| فایل باینری تغییر کند ولی hash درست باشد | پذیرفته می‌شود (hash جدید با manifest می‌خواند)  |
| manifest با امضای نامعتبر                | **بررسی نمی‌شود** و پذیرفته می‌شود (آسیب‌پذیری)  |
| نسخه قدیمی‌تر از نسخه ذخیره‌شده          | **رد نمی‌شود** و پذیرفته می‌شود (آسیب‌پذیری rollback) |
| مدل دستگاه اشتباه                        | رد می‌شود → `ERR device_model_mismatch`          |
| hash فایل نادرست                         | رد می‌شود → `ERR sha256_mismatch`                |
| اندازه‌ی فایل نادرست                     | رد می‌شود → `ERR size_mismatch ...`              |

---

## تحلیل امنیتی

بررسی `SHA-256` فقط نشان می‌دهد فایل دانلودشده با مقدار hash داخل manifest یکی است. اما اگر مهاجم بتواند هم فایل باینری و هم manifest را تغییر دهد، می‌تواند hash جدید را هم داخل manifest قرار دهد و دستگاه آن بسته را معتبر تشخیص می‌دهد.

بنابراین hash به‌تنهایی برای اعتماد کافی نیست. برای اعتماد به بسته، خود manifest باید امضای دیجیتال داشته باشد، چون فیلدهای مهمی مثل `version`، `device_model`، `size`، `sha256` و `firmware_url` داخل manifest قرار دارند.

علاوه بر آن، چون نسخه‌ی فعلی هیچ بررسی‌ای روی ترتیب نسخه‌ها انجام نمی‌دهد، حمله‌ی rollback ممکن است: مهاجم می‌تواند یک نسخه‌ی قدیمی‌تر (و احتمالاً آسیب‌پذیر) را دوباره نصب کند.


## آزمایش بسته‌ی تغییریافته (Tampered Package)

هدف این آزمایش بررسی این پرسش است: اگر مهاجم بتواند **هم فایل باینری و هم manifest را با هم تغییر دهد**، آیا دستگاه همچنان بسته را می‌پذیرد؟

### ۱) ساخت یک باینری تغییریافته

یک فایل firmware جعلی می‌سازیم (محتوای آن متفاوت از باینری اصلی است):

```bash
cp E1_Students/build/CE40876_E2_update.bin files/CE40876_E2_update.bin
echo 'MALICIOUS_PAYLOAD' >> files/CE40876_E2_update.bin
```

با این کار هم محتوای فایل و هم `size` آن نسبت به نسخه‌ی اصلی تغییر می‌کند، بنابراین `sha256` نیز کاملاً متفاوت خواهد بود.


در زیر خروجی اجرای عوض کردن فایل باینری بدون عوض کردن hash و size را مشاهده می‌کنید. در تلاش دوم size درست شده است ولی عدم تطابق hash خطا می‌دهد:
```shell
> version
device_model=ce-40876
current_version=1
update_state=ready
>     
> update_apply http://10.114.151.190:8000/manifest.json
UPDATE_OK version=2
> update_apply http://10.114.151.190:8000/manifest.json
ERR size_mismatch expected=901490 actual=901509
> update_apply http://10.114.151.190:8000/manifest.json
ERR sha256_mismatch
> 
```

### ۲) ساخت manifest متناظر با باینری تغییریافته

از آنجا که `make_manifest.py` خودش `size` و `sha256` را از روی فایل واقعی محاسبه می‌کند، manifest جدید **با باینری تغییریافته سازگار (consistent)** خواهد بود:

```bash
python3 tools/make_manifest.py \
  --firmware files/CE40876_E2_update.bin \
  --version 3 \
  --base-url http://10.114.151.190:8000 \
  --model ce-40876 \
  --out files/manifest.json
```

manifest تولیدشده (مقادیر `size` و `sha256` با فایل تغییریافته مطابقت دارند):

```json5
{
  "device_model": "ce-40876",
  "version": 3,
  "size": 901509,
  "sha256": "e34a31871c21d99c1656d92358775d465670d798bad5306a2a0e006a0f126c13",
  "firmware_url": "http://10.114.151.190:8000/CE40876_E2_update.bin" // MALICIOUS CODE INCLUDED
}
```

در اینجا مهاجم نه‌تنها باینری را عوض کرده، بلکه `size` و `sha256` داخل manifest را هم به مقادیر جدیدِ همان باینری به‌روزرسانی کرده است؛ یعنی manifest و باینری با هم سازگارند.


### ۳) اجرا روی برد و مستندسازی نتیجه

```text
> update_apply http://10.114.151.190:8000/manifest.json
UPDATE_OK version=3
```

بررسی نسخه پس از اجرا:

```text
> version
device_model=ce-40876
current_version=3
update_state=ready
```

**نتیجه:** دستگاه بسته‌ی تغییریافته را **می‌پذیرد**. زیرا تنها بررسی‌های firmware فعلی (`device_model`، `size`، `sha256`) همگی موفق می‌شوند — `size` و `sha256` داخل manifest دقیقاً با فایل باینریِ تغییریافته مطابقت دارند و فیلد `signature` نادیده گرفته می‌شود.

### ۴) چرا این اتفاق می‌افتد؟

بررسی `SHA-256` فقط تطابق بین «فایل دانلودشده» و «مقدار hash نوشته‌شده در manifest» را می‌سنجد. وقتی مهاجم هر دو را با هم کنترل کند، می‌تواند یک جفت سازگار (باینری حاوی کد مخرب + manifest با hash و size همان باینری) بسازد و بررسی hash همچنان موفق خواهد شد.

---

## SHA-256 دقیقاً چه چیزی را تضمین می‌کند و چه چیزی را نمی‌کند؟

**چیزی که SHA-256 تضمین می‌کند (Integrity نسبت به manifest):**

* یکپارچگی (integrity) فایل نسبت به مقدار hash داخل manifest؛ یعنی فایل در حین انتقال یا روی سرور به‌صورت تصادفی خراب/قطع/تغییر نکرده است.
* اگر فایل باینری تغییر کند ولی مقدار `sha256` در manifest ثابت بماند، بررسی **رد می‌شود** (همان حالت `ERR sha256_mismatch`).
* تشخیص خرابی غیرعمدی (نویز شبکه، فایل ناقص، اشتباه در کپی).

**چیزی که SHA-256 تضمین نمی‌کند (Authenticity / Trust):**

* اصالت و قابل‌اعتماد بودن منبع (authenticity) را تضمین نمی‌کند؛ hash نمی‌گوید چه کسی فایل را ساخته است.
* اگر مهاجم هم باینری و هم manifest را کنترل کند، می‌تواند hash جدید را داخل manifest بنویسد و بررسی hash بی‌اثر می‌شود (همان آزمایش بالا).
* جلوگیری از حمله‌ی rollback را تضمین نمی‌کند؛ نسخه‌ی قدیمی هم می‌تواند hash درست داشته باشد.

به‌طور خلاصه: **SHA-256 یکپارچگی می‌دهد، اما اصالت نمی‌دهد.** برای اصالت لازم است manifest با یک کلید خصوصی **امضای دیجیتال** شود (مثلاً ECDSA P-256 روی فرم canonical فیلدها) و دستگاه امضا را با کلید عمومی معتبر بررسی کند. در آن صورت، مهاجمی که کلید خصوصی را ندارد نمی‌تواند یک manifest سازگار و معتبرِ امضاشده تولید کند، حتی اگر هم باینری و هم manifest را تغییر دهد.

---


## بازگشت به نسخه قدیمی
در این آزمایش مشخص شد firmware فعلی هیچ بررسی‌ای برای جلوگیری از نصب نسخه‌ی قدیمی‌تر انجام نمی‌دهد. با اینکه نسخه‌ی ذخیره‌شده روی دستگاه 2 بود، manifest مربوط به نسخه‌ی 1 پذیرفته شد و مقدار نسخه در NVS به 1 تغییر کرد. این رفتار نشان‌دهنده‌ی آسیب‌پذیری rollback است؛ یعنی مهاجم می‌تواند دستگاه را به نسخه‌ای قدیمی‌تر و احتمالاً آسیب‌پذیر برگرداند. برای جلوگیری از این مشکل، firmware باید فقط نسخه‌هایی را بپذیرد که مقدار version آن‌ها از نسخه‌ی فعلی بزرگ‌تر باشد.

```shell
> version
device_model=ce-40876
current_version=2
update_state=ready
>     
> update_apply http://10.114.151.190:8000/manifest.json
UPDATE_OK version=1
```

---

## آسیب‌پذیری‌های شناسایی‌شده در نسخه‌ی فعلی

* امضای دیجیتال manifest **اعتبارسنجی نمی‌شود** (فیلد `signature` فقط parse می‌شود و نادیده گرفته می‌شود).
* هیچ بررسی‌ای برای جلوگیری از rollback وجود ندارد؛ هر نسخه‌ای (حتی قدیمی‌تر) پذیرفته می‌شود.

---

## اصلاحات امنیتی پیشنهادی 

برای امن‌کردن این سرویس، موارد زیر پیشنهاد می‌شود:

* بررسی امضای دیجیتال manifest (ECDSA P-256 روی فرم canonical فیلدها) و رد manifest با امضای نامعتبر
* بررسی نسخه و رد بسته‌هایی با نسخه‌ی مساوی/قدیمی‌تر از نسخه‌ی ذخیره‌شده برای جلوگیری از rollback
* استفاده از HTTPS برای دانلود manifest و firmware (البته با وجود امضای دیجیتال روی manifest و چک هنگام بروزرسانی دغدغه اتک‌های http را کمتر داریم ولی همچنان به دلیل محرمانگی شاید نیاز داشته باشیم)

> ساخت manifest امضاشده با اسکریپت موجود ممکن است:
> ```bash
> python3 tools/make_manifest.py \
>   --firmware files/CE40876_E2_update.bin \
>   --version 2 \
>   --base-url http://10.114.151.190:8000 \
>   --model ce-40876 \
>   --key private_key.pem \
>   --out files/manifest.json
> ```
> برای ساخت یک امضای خراب جهت تست، می‌توان از فلگ `--bad-signature` استفاده کرد.

---

## مرحله ۴: اصلاح طراحی و پیاده‌سازی

برای رفع ضعف‌های امنیتی مشاهده‌شده، طراحی به‌روزرسانی باید به‌گونه‌ای تغییر کند که firmware قبل از پذیرش هر بسته، ابتدا اصالت manifest را بررسی کند و سپس از نصب نسخه‌های قدیمی‌تر جلوگیری شود. در این مرحله دو اصلاح اصلی انجام می‌شود:

1. اعتبارسنجی امضای دیجیتال manifest
2. جلوگیری از rollback با بررسی شماره نسخه

ابتدا برای امضای manifest به یک جفت کلید خصوصی و عمومی نیاز داریم. کلید خصوصی فقط در سمت تولیدکننده‌ی manifest نگهداری می‌شود و برای امضا کردن manifest استفاده می‌شود. کلید عمومی داخل firmware قرار می‌گیرد تا دستگاه بتواند صحت امضا را بررسی کند.

تولید کلیدها با دستورهای زیر انجام شد:

```shell
# Private key (keep secret, used by the signer)
openssl ecparam -name prime256v1 -genkey -noout -out priv.pem

# Public key (PEM, just for reference)
openssl ec -in priv.pem -pubout -out pub.pem
```


پس از تولید کلیدها، فایل `make_manifest.py` به‌روزرسانی شد تا بتواند با استفاده از کلید خصوصی، manifest را امضا کند. در نتیجه، فیلدهای مهم manifest مانند `device_model`، `version`، `size`، `sha256` و `firmware_url` دیگر قابل تغییر توسط مهاجم نیستند؛ زیرا هر تغییری در این فیلدها باعث نامعتبر شدن امضا می‌شود.

برای اینکه firmware بتواند امضا را بررسی کند، کلید عمومی باید داخل `main.c` قرار گیرد. برای استخراج کلید عمومی به فرم آرایه‌ی C از کد زیر استفاده شد:

```python
def print_pubkey_c_array(key):
    pub = key.public_key().public_bytes(
        serialization.Encoding.X962,
        serialization.PublicFormat.UncompressedPoint,
    )
    assert len(pub) == 65 and pub[0] == 0x04
    print('static const uint8_t g_manifest_pubkey[65] = {')
    for i in range(0, 65, 8):
        chunk = pub[i:i + 8]
        print('    ' + ' '.join(f'0x{b:02x},' for b in chunk))
    print('};')
```

خروجی این تابع به‌صورت یک آرایه‌ی ۶۵ بایتی در `main.c` قرار می‌گیرد. این آرایه همان کلید عمومی منحنی P-256 است و برای تابع بررسی امضا استفاده می‌شود.

در firmware نیز تابع `verify_manifest_signature` اضافه شد. این تابع امضای موجود در manifest را با استفاده از کلید عمومی بررسی می‌کند. نکته‌ی مهم این است که بررسی امضا باید قبل از اعتماد به هر فیلد manifest انجام شود؛ چون تا زمانی که امضا معتبر نباشد، مقدارهایی مثل نسخه، hash، اندازه و آدرس firmware قابل اعتماد نیستند.

بخش اصلی منطق به‌روزرسانی در `main.c` به شکل زیر اصلاح شد:

```c++
    /* 1) Authenticate the WHOLE manifest first. Once the signature is valid,
     *    every field (model, version, size, hash, url) can be trusted. */
    if (!verify_manifest_signature(&m)) {
        uart_write_str("ERR signature_invalid\r\n");
        return;
    }

    /* 2) Device model must match this device. */
    if (strcmp(m.device_model, DEVICE_MODEL) != 0) {
        uart_write_str("ERR device_model_mismatch\r\n");
        return;
    }

    /* 3) Anti-rollback: reject versions lower than the last valid version. */
    if (m.version <= g_current_version) {
        uart_printf("ERR version_too_old offered=%lu current=%lu\r\n",
                    (unsigned long)m.version, (unsigned long)g_current_version);
        return;
    }

    /* 4) Download the binary, computing size and SHA-256 on the fly. */
    uint8_t actual_hash[32];
    uint32_t actual_size = 0;
    err = http_hash_and_size(m.firmware_url, actual_hash, &actual_size);
    if (err != ESP_OK) {
        uart_printf("ERR firmware_download %s\r\n", esp_err_to_name(err));
        return;
    }

    char actual_hex[65];
    bytes_to_hex(actual_hash, sizeof(actual_hash), actual_hex, sizeof(actual_hex));

    /* 5) Size must match the signed manifest. */
    if (actual_size != m.size) {
        uart_printf("ERR size_mismatch expected=%lu actual=%lu\r\n",
                    (unsigned long)m.size, (unsigned long)actual_size);
        return;
    }

    /* 6) SHA-256 must match the signed manifest. */
    if (strcasecmp(actual_hex, m.sha256) != 0) {
        uart_write_str("ERR sha256_mismatch\r\n");
        return;
    }

    /* 7) All checks passed: store the new last-valid version in NVS. */
    g_current_version = m.version;
    nvs_write_u32_val(KEY_VERSION, g_current_version);
    uart_printf("UPDATE_OK version=%lu\r\n", (unsigned long)g_current_version);
```

با این تغییرات، ترتیب بررسی‌ها امن‌تر شده است. ابتدا manifest احراز اصالت می‌شود، سپس مدل دستگاه و نسخه بررسی می‌شوند، و در نهایت فایل firmware دانلود شده و از نظر اندازه و hash با مقادیر امضاشده داخل manifest مقایسه می‌شود.

در طراحی جدید، اگر مهاجم باینری و manifest را با هم تغییر دهد، دیگر نمی‌تواند بسته‌ی جعلی معتبر بسازد؛ زیرا برای تولید امضای معتبر به کلید خصوصی نیاز دارد. همچنین اگر manifest مربوط به یک نسخه‌ی قدیمی‌تر ارائه شود، firmware آن را با خطای `ERR version_too_old` رد می‌کند. بنابراین دو آسیب‌پذیری اصلی نسخه‌ی قبلی، یعنی نبود احراز اصالت manifest و امکان rollback، در این مرحله برطرف شده‌اند.

## manifests:

### 1-manifest_valid.json
```json
{
  "device_model": "ce-40876",
  "version": 5,
  "size": 902256,
  "sha256": "71c1988630257f3191b986b12041c4e801220a16706cf8f873ad14a40e0c777f",
  "firmware_url": "http://10.114.151.190:8000/firmware_valid.bin",
  "signature": "f91ca992eff1e1610630465a42dda0f870a04311ddd36c69b3ddcc1ec38953a98bc449ece870937afc1a0a48cc932464b242bca21da4a8c31dcec6deb2c09f9d"
}
```

### 2-manifest_tampered.json
```json
{
  "device_model": "ce-40876",
  "version": 6,
  "size": 902256,
  "sha256": "71c1988630257f3191b986b12041c4e801220a16706cf8f873ad14a40e0c777f",
  "firmware_url": "http://10.114.151.190:8000/CE40876_E2_update.bin",
  "signature": "4b19e4f752b7923a54f684dbea04251f5fa9ddc487316a63463259d01e2ab73a5be7983eb1e79c6eec1c828e22d41621e242b4f9540ff9a9df4a574af70b1c7c"
}
```

### 3-manifest_old_version.json
```json
{
  "device_model": "ce-40876",
  "version": 1,
  "size": 902256,
  "sha256": "71c1988630257f3191b986b12041c4e801220a16706cf8f873ad14a40e0c777f",
  "firmware_url": "http://10.114.151.190:8000/firmware_valid.bin",
  "signature": "abdc33dd61a52fb873d14330de1d3a72ec9fc61ad8066a0adc116167fc004830c22608567983b9701eaae880de1675c01eb10edeb019aa0d43807dc73945f3a9"
}
```

### 4-manifest_wrong_model.json
```json
{
  "device_model": "EE-40876",
  "version": 5,
  "size": 902256,
  "sha256": "71c1988630257f3191b986b12041c4e801220a16706cf8f873ad14a40e0c777f",
  "firmware_url": "http://10.114.151.190:8000/firmware_valid.bin",
  "signature": "b78618f6494069c6c6914d893728c834bab4b4a4dc693298b307f7cbafe2550edb5b00dd9ca5067d6d66f698714d012fa4fa10865a946905385c66692ce2d7c6"
}
```

### 5-manifest_bad_hash.json
```json
{
  "device_model": "ce-40876",
  "version": 5,
  "size": 902256,
  "sha256": "11c1988630257f3191b986b12041c4e801220a16706cf8f873ad14a40e0c777f",
  "firmware_url": "http://10.114.151.190:8000/firmware_valid.bin",
  "signature": "57112116ccb53172eeacaab4fa0a09afee4b6eaa52aeb8ab4c31d3fcd8be0e6e9b1dcec79b0444c25cc0fab8625f5754878e6846dd06d99a40f142770a66abb8"
}
```

## جمع‌بندی
در این تمرین مشخص شد که بررسی SHA-256 فقط یکپارچگی فایل firmware را نسبت به مقدار داخل manifest تضمین می‌کند و به‌تنهایی برای اعتماد به بسته کافی نیست. در نسخه‌ی اولیه، چون امضای دیجیتال manifest بررسی نمی‌شد، مهاجم می‌توانست هم فایل firmware و هم manifest را تغییر دهد و بسته‌ی جعلی همچنان پذیرفته شود. همچنین به دلیل نبود بررسی نسخه، امکان rollback به نسخه‌ی قدیمی‌تر وجود داشت.
برای رفع این ضعف‌ها، در طراحی جدید ابتدا امضای دیجیتال manifest با کلید عمومی داخل firmware اعتبارسنجی می‌شود و سپس نسخه‌ی پیشنهادی با نسخه‌ی ذخیره‌شده در NVS مقایسه می‌گردد. به این ترتیب، بسته‌های جعلی و نسخه‌های قدیمی‌تر رد می‌شوند و فرآیند به‌روزرسانی امنیت بیشتری پیدا می‌کند.

## محدودیت‌ها
* در این تمرین فایل firmware روی پارتیشن OTA نوشته و اجرا نمی‌شود.
* تمرکز فقط روی اعتبارسنجی manifest، بررسی hash/size، سیاست نسخه و ثبت version در NVS است.
* HTTP استفاده شده و محرمانگی مسیر دانلود را تضمین نمی‌کند؛ اما اصالت manifest با امضای دیجیتال کنترل می‌شود.