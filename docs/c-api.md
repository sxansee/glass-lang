# C API

`glass_capi` — чистый C-интерфейс для встраивания Glass в другие процессы. Им пользуется Glass Studio (через P/Invoke), но подойдёт любому вызывающему с C ABI. Собирается как `libglass_capi.so` на Linux и `glass_capi.dll` на Windows. Объявления — в [`src/capi/GlassApi.h`](../src/capi/GlassApi.h).

## Модель

`GlassSession*` — непрозрачный handle: внутри одна живая VM. Сессия создаётся из исходного текста, дальше её можно выполнить целиком или вести по шагам.

**Память.** Любая функция, возвращающая `char*`, отдаёт свежевыделенную строку; освобождать её нужно **только** через `glass_free_string` (не `free` и не `delete`: аллокатор может быть другим, особенно на Windows между модулями).

**Потоки.** Вызовы на одной сессии не рассчитаны на конкурентный доступ; разные сессии независимы. Единственное исключение — `glass_request_stop`: его можно вызвать из другого потока, пока основной внутри `glass_run` или шага (так работает кнопка «Стоп»).

**Структурные данные** (стек вызовов, переменные, куча, сведения об объекте) возвращаются JSON-строками.

## Функции

| Функция | Что делает |
|---|---|
| `glass_compile(source, &error)` | разбирает, проверяет и компилирует программу; при успехе возвращает сессию, при ошибке — `NULL` и текст первой ошибки в `error` |
| `glass_session_free(session)` | освобождает сессию и её кучу |
| `glass_free_string(s)` | освобождает строку, полученную от любой функции |
| `glass_run(session)` | выполняет `main` целиком; `0` — без ошибок выполнения, `1` — была ошибка |
| `glass_get_output(session)` | весь вывод `print` с начала выполнения, по строке на вызов. В stdout процесса сессия вывод **не дублирует** (в отличие от консольного `glassc`): забирайте его этой функцией |
| `glass_has_error(session)` | `1`, если была ошибка выполнения или исключение |
| `glass_get_error(session)` | текст последней ошибки или `NULL` |
| `glass_set_breakpoint(session, line)` | ставит точку останова на строке |
| `glass_clear_breakpoint(session, line)` | снимает точку на строке |
| `glass_clear_all_breakpoints(session)` | снимает все точки |
| `glass_run_until_pause(session)` | запускает (или продолжает) до точки останова, конца программы или ошибки |
| `glass_step_into` / `glass_step_over` / `glass_step_out` | шаг с заходом, с обходом, с выходом; возвращают статус |
| `glass_request_stop(session)` | просит остановить выполнение (из любого потока) |
| `glass_current_line(session)` | строка, на которой стоит выполнение, или `-1` |
| `glass_get_call_stack_json(session)` | стек вызовов: `[{"function": "имя", "line": N}, ...]` от текущего кадра к `main` |
| `glass_get_locals_json(session, frame_depth)` | локальные переменные кадра; объекты и массивы — только ссылкой (адресом) |
| `glass_get_heap_json(session)` | снимок всей кучи; у каждого объекта признак `reachable` — достижим ли он от корней сборщика |
| `glass_collect_garbage(session)` | немедленная сборка мусора; возвращает число освобождённых объектов |
| `glass_get_object_info_json(session, address)` | раскладка полей и vtable объекта по адресу из снимка кучи — ровно то, что вернут `~layout` и `~vtable` |

Функции `glass_run_until_pause` и `glass_step_*` возвращают статус из набора `GLASS_STATUS_*` (в заголовке). После `glass_request_stop` сессия считается завершённой: `glass_get_error` вернёт «выполнение прервано».

## Минимальный пример

```c
#include "capi/GlassApi.h"
#include <stdio.h>

int main(void) {
    char* error = NULL;
    GlassSession* session = glass_compile("fn main() {\n    print(\"Привет, Glass!\")\n}\n", &error);
    if (!session) {
        printf("ошибка компиляции: %s\n", error);
        glass_free_string(error);
        return 1;
    }
    glass_run(session);
    char* output = glass_get_output(session);
    printf("%s\n", output);
    glass_free_string(output);
    glass_session_free(session);
    return 0;
}
```

## Со стороны .NET

Проект `Glass.Interop` оборачивает эти функции в класс `GlassSession` (P/Invoke, `SafeHandle`, разбор JSON). Нативная библиотека ищется рядом с приложением; для тестов путь можно задать переменной окружения `GLASS_CAPI_PATH`. Подробнее — [сборка и тесты](building.md) и [архитектура](architecture.md).
