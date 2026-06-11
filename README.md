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

---

## آسیب‌پذیری‌های شناسایی‌شده در نسخه‌ی فعلی

* امضای دیجیتال manifest **اعتبارسنجی نمی‌شود** (فیلد `signature` فقط parse می‌شود و نادیده گرفته می‌شود).
* هیچ بررسی‌ای برای جلوگیری از rollback وجود ندارد؛ هر نسخه‌ای (حتی قدیمی‌تر) پذیرفته می‌شود.

---

## اصلاحات امنیتی پیشنهادی (هنوز پیاده‌سازی نشده)

برای امن‌کردن این سرویس، موارد زیر پیشنهاد می‌شود:

* بررسی امضای دیجیتال manifest (ECDSA P-256 روی فرم canonical فیلدها) و رد manifest با امضای نامعتبر
* بررسی نسخه و رد بسته‌هایی با نسخه‌ی مساوی/قدیمی‌تر از نسخه‌ی ذخیره‌شده برای جلوگیری از rollback
* استفاده از HTTPS برای دانلود manifest و firmware

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

## نکات و محدودیت‌ها

* در URL نباید از `localhost` استفاده شود؛ چون برد باید IP لپ‌تاپ را ببیند.
* در این اجرا IP لپ‌تاپ برابر بود با `10.114.151.190`.
* اگر شبکه ارتباط مستقیم بین لپ‌تاپ و برد را محدود کند، باید از hotspot یا روتر محلی استفاده شود.
* در این تمرین firmware واقعاً روی پارتیشن OTA نوشته نمی‌شود و تمرکز روی اعتبارسنجی manifest است.
* اگر نسخه‌ی ذخیره‌شده در NVS باعث مزاحمت در تست‌ها شد، می‌توان نسخه را به ۱ بازنشانی کرد:

```text
reset_state CONFIRM
```

---

## جمع‌بندی

در این تمرین مشخص شد که hash فقط سلامت فایل را نسبت به manifest بررسی می‌کند، اما قابل اعتماد بودن خود manifest را تضمین نمی‌کند. نسخه‌ی فعلی firmware تنها `device_model`، `size` و `sha256` را بررسی می‌کند و نه امضای دیجیتال manifest و نه ترتیب نسخه‌ها را. برای جلوگیری از بسته‌های جعلی و حمله‌ی rollback، لازم است manifest امضای دیجیتال داشته باشد و نسخه‌ی جدید فقط در صورتی پذیرفته شود که از نسخه‌ی ذخیره‌شده در NVS بزرگ‌تر باشد.
```