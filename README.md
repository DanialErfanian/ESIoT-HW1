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
  --out ota_serve/manifest.json
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

## اجرای به‌روزرسانی روی برد

در مانیتور سریال:

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

خروجی بعد از به‌روزرسانی موفق:

```text
> version
device_model=ce-40876
current_version=2
update_state=ready
```

---

## تست‌های انجام‌شده

| سناریو                                   | نتیجه مورد انتظار            |
|------------------------------------------|------------------------------|
| بسته معتبر با نسخه جدیدتر                | پذیرفته شود                  |
| فایل باینری تغییر کند ولی hash درست باشد | در نسخه ناامن پذیرفته می‌شود |
| manifest با امضای نامعتبر                | رد شود                       |
| نسخه قدیمی‌تر از نسخه ذخیره‌شده          | رد شود                       |
| مدل دستگاه اشتباه                        | رد شود                       |
| hash فایل نادرست                         | رد شود                       |

---

## تحلیل امنیتی

بررسی `SHA-256` فقط نشان می‌دهد فایل دانلودشده با مقدار hash داخل manifest یکی است. اما اگر مهاجم بتواند هم فایل باینری و هم manifest را تغییر دهد، می‌تواند hash جدید را هم داخل manifest قرار دهد و دستگاه ممکن است بسته را معتبر تشخیص دهد.

بنابراین hash به‌تنهایی برای اعتماد کافی نیست. برای اعتماد به بسته، خود manifest باید امضای دیجیتال داشته باشد، چون فیلدهای مهمی مثل `version`، `device_model`، `size`، `sha256` و `firmware_url` داخل manifest قرار دارند.

---

## اصلاحات امنیتی

در نسخه اصلاح‌شده این موارد پیاده‌سازی شد:

* بررسی مدل دستگاه و رد بسته‌هایی که برای `ce-40876` نیستند
* بررسی اندازه فایل firmware
* بررسی `SHA-256` فایل firmware
* بررسی امضای دیجیتال manifest
* رد manifest با امضای نامعتبر
* ذخیره آخرین نسخه معتبر در NVS
* رد بسته‌هایی با نسخه قدیمی‌تر برای جلوگیری از rollback

---

## خروجی تست‌ها

بسته معتبر:

```text
signature OK
device_model OK
version OK
sha256 OK
update accepted
```

امضای نامعتبر:

```text
signature verification failed
update rejected
```

نسخه قدیمی‌تر:

```text
version rejected: rollback detected
update rejected
```

hash اشتباه:

```text
sha256 mismatch
update rejected
```

---

## نکات و محدودیت‌ها

* در URL نباید از `localhost` استفاده شود؛ چون برد باید IP لپ‌تاپ را ببیند.
* در این اجرا IP لپ‌تاپ برابر بود با `10.114.151.190`.
* اگر شبکه ارتباط مستقیم بین لپ‌تاپ و برد را محدود کند، باید از hotspot یا روتر محلی استفاده شود.
* در این تمرین firmware واقعاً روی پارتیشن OTA نوشته نمی‌شود و تمرکز روی اعتبارسنجی manifest است.
* اگر نسخه ذخیره‌شده در NVS باعث رد شدن تست‌ها شد، می‌توان از دستور زیر استفاده کرد:

```text
reset_state CONFIRM
```

---

## جمع‌بندی

در این تمرین مشخص شد که hash فقط سلامت فایل را نسبت به manifest بررسی می‌کند، اما قابل اعتماد بودن خود manifest را تضمین نمی‌کند. برای جلوگیری از بسته‌های جعلی و حمله rollback، لازم است manifest امضای دیجیتال داشته باشد و آخرین نسخه معتبر در NVS ذخیره شود.

