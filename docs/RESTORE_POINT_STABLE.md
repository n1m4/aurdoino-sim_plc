# نقطه بازگشت — وضعیت پایدار قبل از تغییرات بزرگ

این سند برای این است که هر وقت خواستید به نسخه‌ای که **پایدار و درست کار می‌کرد** برگردید، بدانید دقیقاً کجا ذخیره شده و چطور برگردید.

## خلاصه

| مورد | مقدار |
|------|--------|
| **نام تگ Git** | `stable-before-major-changes` |
| **کامیت** | `af1d8d158caf87b6d2f525afc661a24c33db1dde` |
| **شاخه در زمان ایجاد تگ** | `MQTT-CL-NEW` |
| **تاریخ ایجاد تگ** | ۱۴۰۴/۰۷/۱۷ (۹ اکتبر ۲۰۲۶) |
| **پیام تگ** | نقطه بازگشت: برنامه پایدار قبل از تغییرات بزرگ (شاخه MQTT-CL-NEW) |

تگ روی همان کامیتی است که در آن working tree تمیز بود (بدون تغییر commit نشده).

---

## دیدن این نقطه (بدون عوض کردن کار فعلی)

```bash
git fetch --tags
git show stable-before-major-changes
```

برای checkout موقت (حالت detached HEAD — مناسب تست و مقایسه):

```bash
git checkout stable-before-major-changes
```

برگشت به شاخه کاری:

```bash
git checkout MQTT-CL-NEW
```

---

## برگشت کامل به این نقطه روی شاخه فعلی

> **هشدار:** همه commitها و تغییرات **بعد از** این نقطه روی شاخه‌ای که reset می‌کنید **حذف می‌شوند** (مگر جایی backup داشته باشید).

```bash
git checkout MQTT-CL-NEW
git reset --hard stable-before-major-changes
```

اگر قبلاً این شاخه را push کرده بودید و reset کردید، برای هم‌سان کردن remote (فقط در صورت اطمینان):

```bash
git push --force-with-lease origin MQTT-CL-NEW
```

---

## ساخت شاخه جدید از این نقطه (بدون از دست دادن کار فعلی)

اگر روی شاخه دیگری تغییرات بزرگ می‌دهید و می‌خواهید یک شاخه «تمیز» از نسخه پایدار داشته باشید:

```bash
git checkout -b restore-stable stable-before-major-changes
```

---

## پشتیبان تگ روی سرور (origin)

تگ به‌صورت پیش‌فرض **فقط روی سیستم لوکال** است. برای نگه‌داشتن روی remote:

```bash
git push origin stable-before-major-changes
```

بعداً روی ماشین دیگر:

```bash
git fetch origin tag stable-before-major-changes
git checkout stable-before-major-changes
```

---

## لیست تگ‌های مرتبط

```bash
git tag -l "stable*"
git log -1 stable-before-major-changes
```

---

## یادداشت

- قبل از شروع refactor بزرگ، این تگ ایجاد شد تا بتوانید به وضعیت کارکرد فعلی برگردید.
- برای commit جدید روی همان خط زمانی، از شاخه `MQTT-CL-NEW` (یا شاخه feature جدید) ادامه دهید؛ تگ ثابت می‌ماند.
