#pragma once

// C API для встраивания Glass в другие процессы — в первую очередь в
// WPF-приложение (редактор/дебаггер/визуализация кучи, см. README «Что
// дальше») через P/Invoke, но интерфейс — чистый C, годится для любого
// вызывающего с C ABI.
//
// Собирается на Linux (эта разработка) как .so, на Windows (MSVC) как
// .dll — исходник один и тот же, GLASS_API ниже сама разруливает разницу.
// GlassSession — непрозрачный handle; внутри — один живой glass::VM
// (см. vm/VM.h) с уже готовой к этому моменту пошаговой отладкой
// (breakpoints/stepInto/stepOver/stepOut/locals/callStack/heapObjects) —
// этот файл в основном МАРШАЛИНГ: превращает то, что уже есть в C++, в
// C-совместимые строки/числа, ничего не решает заново.
//
// Владение памятью — одно правило на все функции, возвращающие char*:
// это всегда свежевыделенная строка, вызывающий обязан освободить её
// через glass_free_string (не free()/delete — аллокатор может быть не
// тем же самым, особенно на Windows между модулями).
//
// Структурные данные (стек вызовов, локальные переменные, снимок кучи) —
// в JSON: через границу C нет естественного способа вернуть "список
// записей переменной длины" без ручного маршалинга структур (хрупко,
// особенно со строками внутри) — JSON тут простое, testable и
// единообразное решение, а на стороне C# — System.Text.Json из коробки.
// Формы JSON описаны у каждой функции ниже.
//
// Не потокобезопасно для ОДНОЙ сессии (вызовы на одном GlassSession* не
// рассчитаны на конкурентный доступ из нескольких потоков) — разные
// сессии друг другу не мешают. ЕДИНСТВЕННОЕ исключение — glass_request_stop
// ниже: его можно звать из другого потока, пока основной поток внутри
// glass_run/glass_run_until_pause/glass_step_* (кнопка «Стоп» в IDE).

#ifdef _WIN32
#ifdef GLASS_CAPI_BUILDING
#define GLASS_API __declspec(dllexport)
#else
#define GLASS_API __declspec(dllimport)
#endif
#else
#define GLASS_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GlassSession GlassSession;

// --- Компиляция и жизненный цикл ---

// Лексер -> парсер -> Sema -> BodyChecker -> Compiler (тот же пайплайн,
// что в main.cpp/Repl.cpp, включая пролог с Exception/LayoutInfo/
// VTableInfo/HeapSnapshot — см. prelude/Prelude.h). При успехе — handle
// сессии, готовой к glass_run() или к пошаговой отладке. При ошибке —
// NULL, и, если error_out не NULL, туда кладётся диагностика первой
// найденной ошибки (разбор/типы/байткод) — свежевыделенная строка,
// освобождать через glass_free_string.
GLASS_API GlassSession* glass_compile(const char* source, char** error_out);

// Освобождает сессию и всё, что внутри (включая живую кучу VM). После
// этого вызова session использовать нельзя.
GLASS_API void glass_session_free(GlassSession* session);

// Освобождает строку, полученную от любой из функций ниже.
GLASS_API void glass_free_string(char* s);

// --- Запуск целиком (не для отладки — см. точки останова ниже) ---

// Выполняет main() от начала до конца (или до необработанной
// рантайм-ошибки/исключения). 0 — без ошибок выполнения, 1 — была
// ошибка (см. glass_get_error). Если на этой сессии уже начали
// пошаговую отладку — доработает с текущего места до конца.
GLASS_API int glass_run(GlassSession* session);

// Весь вывод print() с начала выполнения, одна строка на вызов, через
// '\n'. Можно звать в любой момент, в том числе во время пошаговой
// отладки — увидите вывод, накопленный к этому моменту.
GLASS_API char* glass_get_output(GlassSession* session);

// 1, если было исключение/ошибка выполнения, иначе 0.
GLASS_API int glass_has_error(GlassSession* session);
// Текст последней ошибки, или NULL, если ошибки не было.
GLASS_API char* glass_get_error(GlassSession* session);

// --- Точки останова и пошаговое выполнение ---
// Тонкая обёртка над glass::VM (vm/VM.h) — там же полное описание
// семантики каждого шага и почему она именно такая.

GLASS_API void glass_set_breakpoint(GlassSession* session, int line);
GLASS_API void glass_clear_breakpoint(GlassSession* session, int line);
GLASS_API void glass_clear_all_breakpoints(GlassSession* session);

// Статусы, возвращаемые glass_run_until_pause/glass_step_*:
#define GLASS_STATUS_FINISHED 0
#define GLASS_STATUS_PAUSED_AT_BREAKPOINT 1
#define GLASS_STATUS_PAUSED_AFTER_STEP 2

// Запускает (при первом вызове на этой сессии) или продолжает с текущей
// точки до ближайшей точки останова, конца программы или ошибки.
GLASS_API int glass_run_until_pause(GlassSession* session);
// "Войти" — включая внутрь вызванной функции, если строка её вызывает.
GLASS_API int glass_step_into(GlassSession* session);
// "Перешагнуть" — вызванная функция выполняется целиком, но без остановки внутри неё.
GLASS_API int glass_step_over(GlassSession* session);
// Доработать текущую функцию до конца и остановиться у вызывающего.
GLASS_API int glass_step_out(GlassSession* session);

// Просит остановить выполнение. Можно звать ИЗ ДРУГОГО ПОТОКА, пока
// основной поток внутри glass_run/glass_run_until_pause/glass_step_* —
// иначе из зависшей программы (while true {}) IDE не смогла бы выйти.
// VM проверяет флаг перед каждой инструкцией и завершает выполнение как
// при рантайм-ошибке: вызов, в котором застрял основной поток, вернётся
// (glass_run — 1, glass_run_until_pause/step_* — GLASS_STATUS_FINISHED), а
// glass_get_error будет содержать "выполнение прервано". Не откатывается —
// сессия после остановки считается завершённой.
GLASS_API void glass_request_stop(GlassSession* session);

// Исходная строка, на которой сейчас стоит выполнение, или -1, если
// выполнение не начато или уже завершилось.
GLASS_API int glass_current_line(GlassSession* session);

// [{"function": "имя", "line": N}, ...] — от самого глубокого (текущего)
// фрейма к main().
GLASS_API char* glass_get_call_stack_json(GlassSession* session);

// [{"name": "x", "value": <значение>}, ...] для фрейма на глубине
// frame_depth (0 — текущий, самый глубокий; см. glass_get_call_stack_json
// за списком доступных глубин). Форма <значения>:
//   число/строка/true|false|null — как есть
//   объект  — {"ref": "АДРЕС", "kind": "object", "class": "ИмяMold'а"}
//   массив  — {"ref": "АДРЕС", "kind": "array", "length": N}
// object/array — только ССЫЛКА (адрес); сами данные объекта/элементов
// массива — в glass_get_heap_json, по этому же адресу. Так строится граф
// (узлы — объекты, рёбра — поля/элементы, ссылающиеся на другой адрес),
// а не разворачивается неограниченно вглубь (циклические ссылки — не
// проблема).
GLASS_API char* glass_get_locals_json(GlassSession* session, int frame_depth);

// {"objects": [
//    {"address": "АДРЕС", "reachable": true, "kind": "object",
//     "class": "ИмяMold'а",
//     "fields": [{"name": "поле", "value": <значение>}, ...]},
//    {"address": "АДРЕС", "reachable": true, "kind": "array",
//     "elements": [<значение>, ...]}
//  ]}
// Снимок ВСЕЙ кучи целиком на этот момент — для визуализации. <значение>
// — та же форма, что у glass_get_locals_json; double в JSON ВСЕГДА с точкой
// или экспонентой (3.0, не 3 — иначе тип не отличить от int), а inf/nan (не
// JSON-числа) приходят как {"kind": "double", "text": "inf"}. "reachable" — достижим ли
// объект от корней GC (весь стек VM, включая временные значения):
// false — ровно то, что освободит следующая сборка мусора.
GLASS_API char* glass_get_heap_json(GlassSession* session);

// Немедленная сборка мусора (как ~gc в языке) — можно между шагами
// отладки: в IDE кнопка «Собрать мусор» показывает, какие объекты исчезают
// из визуализации кучи. Возвращает, сколько объектов освобождено (0 для
// NULL-сессии).
GLASS_API int glass_collect_garbage(GlassSession* session);

// Устройство одного объекта кучи — для инспектора. address — десятичная
// строка, ровно такая, как в "ref"/"address" JSON выше. Форма:
//   объект: {"address": "АДРЕС", "kind": "object", "class": "Имя",
//            "ancestry": ["Имя", "Предок", "Facet", ...],
//            "layout": [{"name": "поле", "offset": N, "size": N,
//                        "type": "int"|"double"|"bool"|"ref"}, ...],
//            "vtable": [{"method": "имя", "implementedBy": "Класс"}, ...]}
//   массив: {"address": "АДРЕС", "kind": "array", "length": N}
//   нет такого живого объекта / address не число / NULL-сессия: null
// layout и vtable — РОВНО то, что вернут ~layout и ~vtable на этом объекте
// (общий код в VM, не повтор логики). Адрес не разыменовывается, пока не
// найден среди живых объектов кучи — произвольная строка безопасна.
GLASS_API char* glass_get_object_info_json(GlassSession* session, const char* address);

#ifdef __cplusplus
}
#endif
