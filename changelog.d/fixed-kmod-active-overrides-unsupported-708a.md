_2026-04-20_

## English

Kmod install recommendation no longer falsely pushes users to Zygisk on custom kernels whose `uname -r` lacks the GKI KMI tag (e.g. `android12`, `android13`). The heuristic now matches only the parsed KMI — not the phone's Android OS release, which is an unrelated label — and falls back on kernel series when the KMI is missing: 6.1 / 6.6 / 6.12 each ship a single KMI variant and are still unambiguous; 5.10 and 5.15 each have two candidates, both of which the app now surfaces (primary + alternative), with a dedicated banner when the installed variant fails to load so the user knows to try the other. An active kmod — `/proc/vpnhide_targets` present — also overrides any remaining heuristic-driven warning.

## Русский

Рекомендация установки модуля ядра больше не подталкивает ложно к Zygisk на кастомных ядрах, в `uname -r` которых нет GKI KMI-метки (`android12`, `android13` и т.д.). Эвристика теперь сопоставляет только распарсенный KMI — версия Android OS телефона к этому не относится — и при отсутствии KMI делает fallback по серии ядра: для 6.1, 6.6 и 6.12 существует по одному KMI-варианту, рекомендация однозначна; у 5.10 и 5.15 по два кандидата, приложение теперь показывает оба (основной + альтернативный) и отдельный баннер, когда установленный вариант не загрузился — чтобы было понятно, что нужно попробовать второй. Активный kmod (`/proc/vpnhide_targets` существует) также имеет приоритет над любыми эвристическими предупреждениями.
