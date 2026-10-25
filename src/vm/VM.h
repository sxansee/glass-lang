#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "../bytecode/Chunk.h"
#include "../bytecode/Obj.h"

namespace glass {

// Стековая VM для скомпилированного байткода. Один общий стек значений
// для операндов и локальных переменных всех фреймов вызова (классическая
// схема, как в JVM/CLR) — раздел 6 истории проекта: "стековая, не
// регистровая — надёжнее для соло-проекта, лучше задокументирована".
//
// С этой фазы — своя куча под mark-and-sweep GC: объекты mold и массивы
// выделяются через 'new' в C++, но время жизни решает GC, а не RAII/подсчёт
// ссылок — это то же самое разделение, которое видно в разделе 4
// grammar.md ("Итоговая архитектура"). Корни для mark-фазы — весь текущий stack_
// (операнды + локальные переменные всех активных фреймов, поскольку это
// единый стек, ничего дополнительного отслеживать не нужно: у языка нет
// глобальных переменных вне функций).
class VM {
public:
    explicit VM(CompiledProgram program);
    ~VM();

    VM(const VM&) = delete;
    VM& operator=(const VM&) = delete;

    // Выполняет main(). Возвращает 0 при успехе. Ошибки времени
    // выполнения не бросаются наружу — собираются в errors().
    int run();

    const std::vector<std::string>& errors() const { return errors_; }
    // Всё, что напечатано через print() за время выполнения — по одной
    // строке на вызов. Используется тестами вместо перехвата stdout.
    const std::vector<std::string>& printedLines() const { return output_; }

    // Дублировать ли print() в std::cout. По умолчанию — да: консольные glassc и REPL показывают вывод сразу.
    // Клиенты C API забирают вывод через printedLines()/glass_get_output и эхо отключают: иначе вывод программы
    // попадает в stdout процесса-хозяина (у DLL на Windows — через её собственный буфер) и путается с его собственным.
    void setEchoOutput(bool echo) { echoOutput_ = echo; }

    // Для тестов GC: сколько объектов сейчас живо в куче (после sweep).
    size_t liveObjectCount() const { return liveObjectCount_; }
    // Принудительный запуск сборки мусора — используется тестами и
    // интринзиком '~gc' (см. Value.cpp/OpCode::Gc).
    void collectGarbage();

    // Немедленная сборка мусора "как ~gc": собирает, пересчитывает порог
    // автоматического сбора (как после обычного сбора) и возвращает,
    // сколько объектов освобождено. Безопасно вызывать между инструкциями —
    // в том числе пока отладчик стоит на паузе: корни — весь stack_ (см.
    // выше), там же лежат и временные значения недовычисленных выражений.
    size_t collectGarbageNow();

    // Запрос остановки выполнения. Единственная часть VM, которую можно
    // вызывать ИЗ ДРУГОГО ПОТОКА, пока основной поток внутри run()/
    // runUntilPause()/step*(): иначе из зависшей программы (while true {})
    // не было бы выхода — IDE зависла бы вместе с ней. VM проверяет флаг
    // перед каждой инструкцией; при запросе завершается как при обычной
    // рантайм-ошибке (errors() получает сообщение "выполнение прервано").
    void requestStop() { stopRequested_.store(true, std::memory_order_relaxed); }

    // -----------------------------------------------------------------
    // Пошаговое выполнение и точки останова — для C API (см. src/capi/),
    // которым в итоге будет пользоваться WPF-дебаггер. run() выше и это —
    // РАЗНЫЕ входные точки в одну и ту же машину: run() просто крутит
    // executeOneInstruction() до конца, не проверяя точки останова;
    // runUntilPause()/stepInto()/stepOver()/stepOut() — то же самое, но с
    // паузами. Смешивать оба входа на одном VM не предполагается (это
    // разные сценарии использования — 'просто запустить' и 'отладить'),
    // но технически безопасно: они делят одно и то же состояние честно.
    // -----------------------------------------------------------------
    enum class RunStatus {
        Finished,             // main() отработал до конца (успешно или с рантайм-ошибкой — см. errors())
        PausedAtBreakpoint,
        PausedAfterStep,
        NotStarted,           // ни run(), ни один из ниже ещё не вызывались
    };

    void setBreakpoint(int line);
    void clearBreakpoint(int line);
    void clearAllBreakpoints();

    // Запускает (если ещё не запущено) и продолжает выполнение до
    // ближайшей точки останова, конца программы или ошибки.
    RunStatus runUntilPause();
    // Выполняет до смены текущей исходной строки — включая вход в
    // вызванную функцию (аналог "step into" в обычных отладчиках).
    RunStatus stepInto();
    // Как stepInto, но если шаг входит в более глубокий вызов — доходит
    // до возврата из него, не останавливаясь внутри ("step over").
    RunStatus stepOver();
    // Продолжает до возврата из ТЕКУЩЕГО фрейма и следующей смены строки
    // уже у вызывающего ("step out").
    RunStatus stepOut();

    RunStatus lastStatus() const { return lastStatus_; }
    // -1, если выполнение ещё не начато или уже завершилось.
    int currentLine() const;

    struct StackFrameView {
        std::string functionName;
        int line = 0;
    };
    // От самого глубокого (текущего) фрейма к main().
    std::vector<StackFrameView> callStack() const;

    struct LocalVarView {
        std::string name;
        Value value;
    };
    // Локальные переменные фрейма depthFromTop (0 — текущий, самый
    // глубокий). Слоты без когда-либо присвоенного имени (служебные
    // временные — см. Chunk.h: CompiledFunction::localNames) пропускаются.
    std::vector<LocalVarView> locals(size_t depthFromTop = 0) const;

    struct HeapObjectView {
        uintptr_t address = 0;
        bool isArray = false;
        std::string className;  // для массива — "[]"
        std::vector<LocalVarView> fields;  // для массива — name="[индекс]"
        // Достижим ли объект от корней GC (весь stack_: локали всех фреймов
        // И временные значения недовычисленных выражений) — то есть
        // переживёт ли он следующую сборку. false — это ровно тот мусор,
        // который следующий collectGarbage() освободит. Считается пробным
        // обходом, ничего не освобождая и не трогая флаги маркировки.
        bool reachable = true;
    };
    // Снимок ВСЕЙ кучи целиком — для визуализации (см. src/capi/). Тот же
    // принцип, что у уже готового '~heap' в самом языке (см. Value.cpp),
    // просто с полными значениями полей/элементов, а не только счётчиком.
    std::vector<HeapObjectView> heapObjects() const;

    // Устройство одного объекта кучи — для инспектора в IDE. layout и
    // vtable строятся ТЕМИ ЖЕ функциями (layoutOf/vtableOf), что и ~layout/
    // ~vtable в самом языке: то, что показывает инспектор, совпадает с тем,
    // что напечатает программа, по построению, а не по договорённости.
    struct FieldInspection {
        std::string name;
        int offset = 0;    // номер слота — то же число, что в ~layout
        int size = 0;      // условный размер в байтах — как в ~layout
        std::string type;  // "int" | "double" | "bool" | "ref"
    };
    struct VTableEntryInspection {
        std::string method;
        std::string implementedBy;  // чья реализация реально вызовется (override/наследование)
    };
    struct ObjectInspection {
        uintptr_t address = 0;
        bool isArray = false;
        std::string className;              // для массива — "[]"
        size_t length = 0;                  // для массива — число элементов
        std::vector<std::string> ancestry;  // сам класс, предки и facet'ы (ClassInfo::ancestry)
        std::vector<FieldInspection> layout;
        std::vector<VTableEntryInspection> vtable;
    };
    // false, если по address нет живого объекта кучи. Сам address НЕ
    // разыменовывается, пока не найден в списках аллокаций — произвольное
    // число от вызывающего не может привести к чтению чужой памяти.
    bool inspectObject(uintptr_t address, ObjectInspection& out) const;

private:
    struct Frame {
        const CompiledFunction* fn = nullptr;
        size_t returnIp = 0;   // куда вернуться В ВЫЗЫВАЮЩЕМ фрейме после Return
        size_t stackBase = 0;  // где в общем стеке начинаются локали этого фрейма
        size_t ip = 0;         // текущая инструкция внутри fn->code
    };

    CompiledProgram program_;
    std::vector<Value> stack_;
    std::vector<Frame> frames_;
    std::vector<std::string> errors_;
    std::vector<std::string> output_;
    bool hadRuntimeError_ = false;
    bool echoOutput_ = true;

    std::atomic<bool> stopRequested_{false};  // см. requestStop()

    std::unordered_set<uintptr_t> reachableAddresses() const;
    // Встроенные функции/методы и срезы (VMBuiltins.cpp). false — возникла рантайм-ошибка (уже записана).
    bool callBuiltin(const Frame& frame, int id, int argc);
    bool doSlice(const Frame& frame, int present);

    std::vector<FieldInspection> layoutOf(const ClassInfo& info) const;
    std::vector<VTableEntryInspection> vtableOf(const ClassInfo& info) const;

    ObjInstance* allAllocations_ = nullptr;  // intrusive-список всех живых объектов, для sweep
    ObjArray* allAllocationsArrays_ = nullptr;  // отдельный intrusive-список для массивов —
                                                 // тот же liveObjectCount_ и тот же
                                                 // mark-and-sweep цикл, просто другой C++-тип
                                                 // узла (см. bytecode/Obj.h)
    size_t liveObjectCount_ = 0;
    size_t gcThreshold_ = 64;  // после скольки аллокаций запускать автоматический сбор; растёт после каждого сбора

    void runtimeError(const Frame& frame, const std::string& message);
    Value pop();
    void push(Value v);
    const Value& peek(size_t distanceFromTop = 0) const;

    ObjInstance* allocateInstance(const ClassInfo& info);
    ObjArray* allocateArray(std::vector<Value> elements);
    void maybeCollectGarbage();
    void markValue(const Value& v);

    // Ищет обработчик для exceptionValue, начиная с текущего (верхнего)
    // фрейма и разворачивая фреймы вверх по стеку вызовов, если в них
    // подходящего нет (как Return, но без возвращаемого значения). При
    // успехе сама переводит frames_.back().ip на handlerPC и кладёт
    // исключение куда нужно (локальный слот catch, либо на стек для
    // finally-проброса) — вызывающему коду (OpCode::Throw) дальше ничего
    // донастраивать не нужно. false — необработанное исключение (все
    // фреймы исчерпаны).
    bool unwindToHandler(Value exceptionValue);
    // typeName — имя из catch (например "Exception"); сравнение — по
    // ClassInfo::ancestry пойманного объекта (сам класс/предки/facet'ы).
    bool exceptionIsInstanceOf(const Value& v, const std::string& typeName) const;
    std::string describeUncaught(const Value& exceptionValue) const;

    // Пишет в поле fieldName объекта obj (класс которого — classInfo) —
    // используется обработчиками ~layout/~vtable/~heap (см. VM.cpp), где
    // поля заполняются самой VM напрямую, а не байткодом конструктора.
    // Понятная внутренняя ошибка вместо неопределённого поведения, если
    // пролог (prelude/Prelude.h) вдруг разойдётся с этим кодом.
    void setField(ObjInstance* obj, const ClassInfo& classInfo, const std::string& fieldName, Value value);

    // -----------------------------------------------------------------
    // Общий механизм для run() и пошаговой отладки — см. публичную секцию выше.
    // -----------------------------------------------------------------
    enum class StepOutcome { Continue, Finished, Error };

    bool executionStarted_ = false;
    RunStatus lastStatus_ = RunStatus::NotStarted;
    std::vector<int> breakpointLines_;  // отсортированный список; количество точек останова всегда мало

    // Кладёт main() в frames_/stack_ — один раз на весь жизненный цикл
    // VM. false, если в программе нет main (тогда уже выставлен hadRuntimeError_/errors_).
    bool startExecution();
    // Один шаг — одна инструкция текущего (верхнего) фрейма. Общая для
    // run() (гоняет её по кругу до конца, не глядя на точки останова) и
    // для runUntilPause()/step*() (те же шаги, просто с паузами между).
    StepOutcome executeOneInstruction();
    bool isBreakpointLine(int line) const;
    // Строка пользовательского исходника (нумерация с 1). Код пролога
    // (Exception и др.) имеет неположительные строки — отладчик в нём не
    // останавливается и не показывает его в стеке: «Just My Code».
    static bool isUserLine(int line) { return line > 0; }
};

} // namespace glass
