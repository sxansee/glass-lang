#include "../util/Utf8.h"
#include "VM.h"

#include <algorithm>
#include <iostream>

namespace glass {
namespace {

// Условный размер поля в байтах для ~layout — показываем то, что ожидал
// бы студент от "настоящей" раскладки (int/double — 8, bool — 1,
// ссылка — 8 на 64-битной системе), а не sizeof(Value) — тот отражает
// накладные расходы std::variant, а не то, чему учит эта фаза диплома.
int fieldKindByteSize(FieldKind kind) {
    switch (kind) {
        case FieldKind::Int: return 8;
        case FieldKind::Double: return 8;
        case FieldKind::Bool: return 1;
        case FieldKind::Reference: return 8;
    }
    return 8;
}

// Максимальная глубина стека вызовов Glass. Без лимита бесконечная рекурсия
// растёт, пока не кончится память (std::bad_alloc → падение всего процесса,
// а в IDE — всей среды). Лимит превращает это в обычную рантайм-ошибку.
constexpr size_t kMaxCallDepth = 10000;

}  // namespace

VM::VM(CompiledProgram program) : program_(std::move(program)) {}

VM::~VM() {
    ObjInstance* cur = allAllocations_;
    while (cur) {
        ObjInstance* next = cur->nextAllocated;
        delete cur;
        cur = next;
    }
    ObjArray* curArr = allAllocationsArrays_;
    while (curArr) {
        ObjArray* next = curArr->nextAllocated;
        delete curArr;
        curArr = next;
    }
}

Value VM::pop() {
    Value v = std::move(stack_.back());
    stack_.pop_back();
    return v;
}

void VM::push(Value v) { stack_.push_back(std::move(v)); }

const Value& VM::peek(size_t distanceFromTop) const { return stack_[stack_.size() - 1 - distanceFromTop]; }

void VM::runtimeError(const Frame& frame, const std::string& message) {
    hadRuntimeError_ = true;
    const Instruction& instr = frame.fn->code[frame.ip];
    errors_.push_back(frame.fn->name + ":" + std::to_string(instr.line) + ": " + message);
}

// ---------------------------------------------------------------------
// Куча и mark-and-sweep GC
// ---------------------------------------------------------------------

ObjInstance* VM::allocateInstance(const ClassInfo& info) {
    auto* obj = new ObjInstance();
    obj->classInfo = &info;
    obj->fields.resize(info.fields.size(), Value{std::monostate{}});
    for (size_t i = 0; i < info.fields.size(); i++) {
        switch (info.fields[i].kind) {
            case FieldKind::Int: obj->fields[i] = Value(static_cast<long long>(0)); break;
            case FieldKind::Double: obj->fields[i] = Value(0.0); break;
            case FieldKind::Bool: obj->fields[i] = Value(false); break;
            case FieldKind::Reference: break;  // остаётся null (monostate)
        }
    }
    obj->nextAllocated = allAllocations_;
    allAllocations_ = obj;
    liveObjectCount_++;
    // ВАЖНО: GC здесь НЕ запускается. В этот момент объект ещё нигде не
    // виден как корень (указатель ещё не попал на стек — это сделает
    // вызывающий код сразу после return) — mark-фаза его не найдёт, и
    // sweep удалит только что созданный объект прямо во время его
    // конструирования. Проверка на GC — обязанность вызывающего, ПОСЛЕ
    // push(Value(obj)) (см. OpCode::NewInstance в run()).
    return obj;
}

ObjArray* VM::allocateArray(std::vector<Value> elements) {
    auto* arr = new ObjArray();
    arr->elements = std::move(elements);
    arr->nextAllocated = allAllocationsArrays_;
    allAllocationsArrays_ = arr;
    liveObjectCount_++;
    // То же требование, что у allocateInstance: указатель ещё нигде не
    // корень — проверка на GC (maybeCollectGarbage) обязана произойти
    // ПОСЛЕ push(Value(arr)) у вызывающего (см. OpCode::NewArray в run()).
    return arr;
}

void VM::markValue(const Value& root) {
    // ИТЕРАТИВНЫЙ обход с явным рабочим списком: рекурсивный переполнял бы
    // стек C++ на длинной цепочке объектов (связный список в сотни тысяч
    // узлов; на Windows — на десятках тысяч). Указатели в work смотрят в
    // fields/elements живых объектов, которые во время маркировки не меняются.
    std::vector<const Value*> work;
    work.push_back(&root);
    while (!work.empty()) {
        const Value* v = work.back();
        work.pop_back();
        if (isObject(*v)) {
            ObjInstance* obj = std::get<ObjInstance*>(*v);
            if (!obj || obj->marked) continue;
            obj->marked = true;
            for (auto& field : obj->fields) work.push_back(&field);
        } else if (isArray(*v)) {
            ObjArray* arr = std::get<ObjArray*>(*v);
            if (!arr || arr->marked) continue;
            arr->marked = true;
            // Элементы массива — тоже в обход: массив объектов (или массив
            // массивов) трассируется корректно, объект, на который ссылается
            // только живой массив, не будет собран как мусор.
            for (auto& elem : arr->elements) work.push_back(&elem);
        }
    }
}

void VM::collectGarbage() {
    for (auto& v : stack_) markValue(v);

    ObjInstance** cur = &allAllocations_;
    while (*cur) {
        if (!(*cur)->marked) {
            ObjInstance* garbage = *cur;
            *cur = garbage->nextAllocated;
            delete garbage;
            liveObjectCount_--;
        } else {
            (*cur)->marked = false;  // сброс для следующего цикла
            cur = &(*cur)->nextAllocated;
        }
    }

    ObjArray** curArr = &allAllocationsArrays_;
    while (*curArr) {
        if (!(*curArr)->marked) {
            ObjArray* garbage = *curArr;
            *curArr = garbage->nextAllocated;
            delete garbage;
            liveObjectCount_--;
        } else {
            (*curArr)->marked = false;
            curArr = &(*curArr)->nextAllocated;
        }
    }
}

void VM::maybeCollectGarbage() {
    if (liveObjectCount_ < gcThreshold_) return;
    collectGarbage();
    gcThreshold_ = liveObjectCount_ * 2 + 32;  // растущий порог — классическая эвристика простых GC
}

size_t VM::collectGarbageNow() {
    size_t before = liveObjectCount_;
    collectGarbage();
    gcThreshold_ = liveObjectCount_ * 2 + 32;  // тот же пересчёт, что после автоматического сбора
    return before - liveObjectCount_;
}

// ---------------------------------------------------------------------
// Раскладка / vtable / инспектор объекта — общий источник правды для
// ~layout, ~vtable (OpCode::Layout/Vtable ниже) и для C API-инспектора.
// ---------------------------------------------------------------------

std::vector<VM::FieldInspection> VM::layoutOf(const ClassInfo& info) const {
    std::vector<FieldInspection> result;
    result.reserve(info.fields.size());
    for (const auto& f : info.fields) {
        std::string type;
        switch (f.kind) {
            case FieldKind::Int: type = "int"; break;
            case FieldKind::Double: type = "double"; break;
            case FieldKind::Bool: type = "bool"; break;
            case FieldKind::Reference: type = "ref"; break;
        }
        result.push_back(FieldInspection{f.name, f.offset, fieldKindByteSize(f.kind), type});
    }
    return result;
}

std::vector<VM::VTableEntryInspection> VM::vtableOf(const ClassInfo& info) const {
    // vtable — unordered_map: сортируем по имени метода, иначе
    // порядок в ~vtable зависел бы от реализации std lib, а не
    // от программы — неповторяемый результат никого не учит.
    std::vector<std::pair<std::string, int>> entries(info.vtable.begin(), info.vtable.end());
    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<VTableEntryInspection> result;
    result.reserve(entries.size());
    for (const auto& [methodName, fnIndex] : entries) {
        // CompiledFunction.name для метода — всегда "Класс.метод"
        // (см. Compiler::compileMethodBody) — так узнаём, чья
        // именно реализация реально вызовется (override/наследование).
        const std::string& fullName = program_.functions[static_cast<size_t>(fnIndex)].name;
        size_t dot = fullName.find('.');
        std::string owner = (dot == std::string::npos) ? fullName : fullName.substr(0, dot);
        result.push_back(VTableEntryInspection{methodName, owner});
    }
    return result;
}

bool VM::inspectObject(uintptr_t address, ObjectInspection& out) const {
    for (ObjInstance* cur = allAllocations_; cur; cur = cur->nextAllocated) {
        if (reinterpret_cast<uintptr_t>(cur) != address) continue;
        out = ObjectInspection{};
        out.address = address;
        out.isArray = false;
        out.className = cur->classInfo->name;
        out.ancestry = cur->classInfo->ancestry;
        out.layout = layoutOf(*cur->classInfo);
        out.vtable = vtableOf(*cur->classInfo);
        return true;
    }
    for (ObjArray* cur = allAllocationsArrays_; cur; cur = cur->nextAllocated) {
        if (reinterpret_cast<uintptr_t>(cur) != address) continue;
        out = ObjectInspection{};
        out.address = address;
        out.isArray = true;
        out.className = "[]";
        out.length = cur->elements.size();
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------
// Пошаговое выполнение и точки останова (для C API — см. src/capi/)
// ---------------------------------------------------------------------

void VM::setBreakpoint(int line) {
    if (!isBreakpointLine(line)) {
        breakpointLines_.push_back(line);
        std::sort(breakpointLines_.begin(), breakpointLines_.end());
    }
}

void VM::clearBreakpoint(int line) {
    breakpointLines_.erase(std::remove(breakpointLines_.begin(), breakpointLines_.end(), line),
                            breakpointLines_.end());
}

void VM::clearAllBreakpoints() { breakpointLines_.clear(); }

bool VM::isBreakpointLine(int line) const {
    return std::find(breakpointLines_.begin(), breakpointLines_.end(), line) != breakpointLines_.end();
}

VM::RunStatus VM::runUntilPause() {
    bool justStarted = false;
    if (!executionStarted_) {
        executionStarted_ = true;
        justStarted = true;
        if (!startExecution()) {
            lastStatus_ = RunStatus::Finished;
            return lastStatus_;
        }
    }
    int startLine = currentLine();
    // Точка останова ровно на самой первой строке — распространённый
    // случай (например, breakpoint на первой строке main() перед run).
    // Обычный отладчик остановился бы тут же, не выполнив её.
    if (justStarted && isBreakpointLine(startLine)) {
        lastStatus_ = RunStatus::PausedAtBreakpoint;
        return lastStatus_;
    }
    // Не проверяем точки останова, пока не уйдём со стартовой строки —
    // иначе, если уже стоим на строке-точке-останова (типичный случай:
    // 'продолжить' после уже сработавшего останова), сработали бы
    // немедленно повторно и не сдвинулись бы с места, особенно если
    // строка компилируется в несколько инструкций подряд.
    bool leftStartLine = false;
    while (true) {
        StepOutcome outcome = executeOneInstruction();
        if (outcome != StepOutcome::Continue) {
            lastStatus_ = RunStatus::Finished;
            return lastStatus_;
        }
        int line = currentLine();
        if (!leftStartLine) {
            if (line == startLine) continue;
            leftStartLine = true;
        }
        if (isUserLine(line) && isBreakpointLine(line)) {
            lastStatus_ = RunStatus::PausedAtBreakpoint;
            return lastStatus_;
        }
    }
}

VM::RunStatus VM::stepInto() {
    if (!executionStarted_) {
        executionStarted_ = true;
        if (!startExecution()) {
            lastStatus_ = RunStatus::Finished;
            return lastStatus_;
        }
        // Первый шаг после старта — просто встать на первую строку
        // main(), ничего ещё не выполнив: так и ведёт себя обычный
        // отладчик при запуске "под шагом" — сначала показывает точку
        // входа, дальше продвигается только по следующей команде.
        lastStatus_ = RunStatus::PausedAfterStep;
        return lastStatus_;
    }
    int startLine = currentLine();
    while (true) {
        StepOutcome outcome = executeOneInstruction();
        if (outcome != StepOutcome::Continue) {
            lastStatus_ = RunStatus::Finished;
            return lastStatus_;
        }
        int line = currentLine();
        if (isUserLine(line) && line != startLine) {
            lastStatus_ = RunStatus::PausedAfterStep;
            return lastStatus_;
        }
    }
}

VM::RunStatus VM::stepOver() {
    if (!executionStarted_) {
        executionStarted_ = true;
        if (!startExecution()) {
            lastStatus_ = RunStatus::Finished;
            return lastStatus_;
        }
        lastStatus_ = RunStatus::PausedAfterStep;  // см. stepInto() — тот же принцип на первом вызове
        return lastStatus_;
    }
    int startLine = currentLine();
    size_t startDepth = frames_.size();
    while (true) {
        StepOutcome outcome = executeOneInstruction();
        if (outcome != StepOutcome::Continue) {
            lastStatus_ = RunStatus::Finished;
            return lastStatus_;
        }
        if (frames_.size() > startDepth) continue;  // ушли глубже — не останавливаемся, ждём возврата
        int line = currentLine();
        if (isUserLine(line) && (frames_.size() < startDepth || line != startLine)) {
            lastStatus_ = RunStatus::PausedAfterStep;
            return lastStatus_;
        }
    }
}

VM::RunStatus VM::stepOut() {
    if (!executionStarted_) {
        executionStarted_ = true;
        if (!startExecution()) {
            lastStatus_ = RunStatus::Finished;
            return lastStatus_;
        }
        lastStatus_ = RunStatus::PausedAfterStep;  // см. stepInto() — тот же принцип на первом вызове
        return lastStatus_;
    }
    size_t startDepth = frames_.size();
    while (true) {
        StepOutcome outcome = executeOneInstruction();
        if (outcome != StepOutcome::Continue) {
            lastStatus_ = RunStatus::Finished;
            return lastStatus_;
        }
        if (frames_.size() < startDepth && isUserLine(currentLine())) {
            lastStatus_ = RunStatus::PausedAfterStep;
            return lastStatus_;
        }
    }
}

int VM::currentLine() const {
    if (frames_.empty()) return -1;
    const Frame& frame = frames_.back();
    if (frame.ip >= frame.fn->code.size()) return -1;
    return frame.fn->code[frame.ip].line;
}

std::vector<VM::StackFrameView> VM::callStack() const {
    std::vector<StackFrameView> result;
    result.reserve(frames_.size());
    for (auto it = frames_.rbegin(); it != frames_.rend(); ++it) {
        int line = (it->ip < it->fn->code.size()) ? it->fn->code[it->ip].line : -1;
        result.push_back(StackFrameView{it->fn->name, line});
    }
    return result;
}

std::vector<VM::LocalVarView> VM::locals(size_t depthFromTop) const {
    std::vector<LocalVarView> result;
    if (depthFromTop >= frames_.size()) return result;
    const Frame& frame = frames_[frames_.size() - 1 - depthFromTop];
    const auto& names = frame.fn->localNames;
    for (size_t slot = 0; slot < names.size() && slot < static_cast<size_t>(frame.fn->localCount); ++slot) {
        if (names[slot].empty()) continue;  // служебные слоты без имени (см. Chunk.h) — не показываем
        size_t idx = frame.stackBase + slot;
        if (idx >= stack_.size()) continue;
        result.push_back(LocalVarView{names[slot], stack_[idx]});
    }
    return result;
}

std::unordered_set<uintptr_t> VM::reachableAddresses() const {
    // Тот же обход, что в markValue(), но НЕ разрушающий (не трогает
    // флаги marked) и итеративный — глубокая цепочка объектов не переполнит
    // стек C++, а сама куча остаётся ровно как была.
    std::unordered_set<uintptr_t> seen;
    std::vector<const Value*> work;
    work.reserve(stack_.size());
    for (const auto& v : stack_) work.push_back(&v);
    while (!work.empty()) {
        const Value* v = work.back();
        work.pop_back();
        if (isObject(*v)) {
            ObjInstance* obj = std::get<ObjInstance*>(*v);
            if (!obj || !seen.insert(reinterpret_cast<uintptr_t>(obj)).second) continue;
            for (const auto& field : obj->fields) work.push_back(&field);
        } else if (isArray(*v)) {
            ObjArray* arr = std::get<ObjArray*>(*v);
            if (!arr || !seen.insert(reinterpret_cast<uintptr_t>(arr)).second) continue;
            for (const auto& element : arr->elements) work.push_back(&element);
        }
    }
    return seen;
}

std::vector<VM::HeapObjectView> VM::heapObjects() const {
    const std::unordered_set<uintptr_t> reachable = reachableAddresses();
    std::vector<HeapObjectView> result;
    for (ObjInstance* cur = allAllocations_; cur; cur = cur->nextAllocated) {
        HeapObjectView view;
        view.address = reinterpret_cast<uintptr_t>(cur);
        view.reachable = reachable.count(view.address) != 0;
        view.isArray = false;
        view.className = cur->classInfo->name;
        for (auto& f : cur->classInfo->fields) {
            view.fields.push_back(LocalVarView{f.name, cur->fields[static_cast<size_t>(f.offset)]});
        }
        result.push_back(std::move(view));
    }
    for (ObjArray* cur = allAllocationsArrays_; cur; cur = cur->nextAllocated) {
        HeapObjectView view;
        view.address = reinterpret_cast<uintptr_t>(cur);
        view.reachable = reachable.count(view.address) != 0;
        view.isArray = true;
        view.className = "[]";
        for (size_t i = 0; i < cur->elements.size(); ++i) {
            view.fields.push_back(LocalVarView{"[" + std::to_string(i) + "]", cur->elements[i]});
        }
        result.push_back(std::move(view));
    }
    return result;
}

// ---------------------------------------------------------------------
// Исключения: поиск обработчика
// ---------------------------------------------------------------------

bool VM::exceptionIsInstanceOf(const Value& v, const std::string& typeName) const {
    if (!isObject(v)) return false;
    ObjInstance* obj = std::get<ObjInstance*>(v);
    if (!obj) return false;
    for (auto& name : obj->classInfo->ancestry) {
        if (name == typeName) return true;
    }
    return false;
}

std::string VM::describeUncaught(const Value& exceptionValue) const {
    if (isObject(exceptionValue) && std::get<ObjInstance*>(exceptionValue) != nullptr) {
        ObjInstance* obj = std::get<ObjInstance*>(exceptionValue);
        const FieldSlot* msgField = obj->classInfo->findField("message");
        if (msgField) {
            return "необработанное исключение (" + obj->classInfo->name +
                   "): " + valueToDisplayString(obj->fields[static_cast<size_t>(msgField->offset)]);
        }
        return "необработанное исключение (" + obj->classInfo->name + ")";
    }
    return "необработанное исключение: " + valueToDisplayString(exceptionValue);
}

void VM::setField(ObjInstance* obj, const ClassInfo& classInfo, const std::string& fieldName, Value value) {
    const FieldSlot* slot = classInfo.findField(fieldName);
    if (!slot) {
        // Может произойти только если prelude/Prelude.h разошёлся с тем,
        // какие поля здесь ожидаются, — баг в самой VM, не во входе
        // пользователя, поэтому не runtimeError (тот — для ошибок
        // пользовательской программы), а немедленное падение с ясной причиной.
        throw std::runtime_error("VM: внутренняя ошибка — у '" + classInfo.name + "' нет поля '" + fieldName + "'");
    }
    obj->fields[static_cast<size_t>(slot->offset)] = std::move(value);
}

bool VM::unwindToHandler(Value exceptionValue) {
    while (!frames_.empty()) {
        Frame& top = frames_.back();
        for (auto& h : top.fn->handlers) {
            if (top.ip < static_cast<size_t>(h.tryStart) || top.ip >= static_cast<size_t>(h.tryEnd)) continue;
            if (h.catchTypeConstant >= 0) {
                const std::string& typeName =
                    *std::get<std::shared_ptr<std::string>>(top.fn->constants[static_cast<size_t>(h.catchTypeConstant)]);
                if (!exceptionIsInstanceOf(exceptionValue, typeName)) continue;
            }
            // Нашли: сбрасываем операнды этого фрейма до границы локалей
            // (то, что копилось на стеке в прерванном вычислении внутри
            // try, больше не нужно — тот же принцип, что при Return).
            stack_.resize(top.stackBase + static_cast<size_t>(top.fn->localCount));
            if (h.catchTypeConstant >= 0) {
                // обычный catch — кладём пойманное значение в его локальный слот
                stack_[top.stackBase + static_cast<size_t>(h.localSlot)] = exceptionValue;
            } else {
                // finally-проброс: значение нужно на стеке — сразу за
                // скомпилированным finally там стоит Throw, который его
                // заново снимет (см. Compiler::visit(TryStmt&))
                push(std::move(exceptionValue));
            }
            top.ip = static_cast<size_t>(h.handlerPC);
            return true;
        }
        // В этом фрейме подходящего обработчика нет — разворачиваем его,
        // как Return, но без возвращаемого значения, и продолжаем поиск в
        // фрейме вызывающего.
        size_t callerBase = top.stackBase;
        frames_.pop_back();  // top отсюда невалиден
        stack_.resize(callerBase);
    }
    return false;
}

// ---------------------------------------------------------------------
// Исполнение
// ---------------------------------------------------------------------

bool VM::startExecution() {
    auto mainIt = program_.functionIndexByName.find("main");
    if (mainIt == program_.functionIndexByName.end()) {
        errors_.push_back("нет функции 'main' в скомпилированной программе");
        hadRuntimeError_ = true;
        return false;
    }
    const CompiledFunction& mainFn = program_.functions[static_cast<size_t>(mainIt->second)];

    frames_.push_back(Frame{&mainFn, 0, 0, 0});
    for (int i = 0; i < mainFn.localCount; i++) stack_.push_back(Value{std::monostate{}});
    return true;
}

int VM::run() {
    // Тот же гвард, что у runUntilPause()/step*() ниже — если на этой VM
    // уже начали пошагово (executionStarted_ true), run() просто
    // доработает С ТЕКУЩЕГО места до конца ("продолжить без остановок"),
    // а не воткнёт второй фрейм main() поверх существующего состояния.
    // Для run()'а из main.cpp/Repl.cpp — где VM всегда свежая — эффекта
    // нет: executionStarted_ и так false.
    if (!executionStarted_) {
        executionStarted_ = true;
        if (!startExecution()) return 1;
    }
    while (true) {
        StepOutcome outcome = executeOneInstruction();
        if (outcome == StepOutcome::Finished) return hadRuntimeError_ ? 1 : 0;
        if (outcome == StepOutcome::Error) return 1;
    }
}

// Выполняет ровно одну инструкцию текущего (верхнего) фрейма — тот же шаг,
// что раньше был одной итерацией цикла внутри run(). Сама коммутация
// опкодов ниже — БЕЗ ИЗМЕНЕНИЙ логики относительно того, что было: просто
// вынесена в отдельный шаг, переиспользуемый run()'ом (гоняет его по
// кругу до конца) и пошаговой отладкой (runUntilPause()/step*() ниже —
// останавливаются между шагами, проверяя точки останова/смену строки).
VM::StepOutcome VM::executeOneInstruction() {
    if (frames_.empty() || hadRuntimeError_) return StepOutcome::Finished;
    if (stopRequested_.load(std::memory_order_relaxed)) {
        // Запрошена остановка из другого потока (см. requestStop()) —
        // завершаемся как при рантайм-ошибке: дальше все циклы выполнения
        // (run/runUntilPause/step*) видят Error и выходят штатно.
        hadRuntimeError_ = true;
        errors_.push_back(frames_.back().fn->name + ":" + std::to_string(currentLine()) +
                          ": выполнение прервано");
        return StepOutcome::Error;
    }
    Frame& frame = frames_.back();
    if (frame.ip >= frame.fn->code.size()) {
        runtimeError(frame, "выполнение вышло за пределы кода функции без Return");
        return StepOutcome::Error;
    }
    const Instruction instr = frame.fn->code[frame.ip];  // копия — frame может стать невалидной ссылкой ниже

    switch (instr.op) {
            case OpCode::PushConst:
                push(frame.fn->constants[static_cast<size_t>(instr.operand)]);
                frame.ip++;
                break;

            case OpCode::Pop:
                pop();
                frame.ip++;
                break;

            case OpCode::Dup:
                push(peek());
                frame.ip++;
                break;

            case OpCode::Dup2: {
                Value a = peek(1);
                Value b = peek(0);
                push(a);
                push(b);
                frame.ip++;
                break;
            }

            case OpCode::LoadLocal:
                push(stack_[frame.stackBase + static_cast<size_t>(instr.operand)]);
                frame.ip++;
                break;

            case OpCode::StoreLocal:
                stack_[frame.stackBase + static_cast<size_t>(instr.operand)] = peek();
                frame.ip++;
                break;

            case OpCode::Add: {
                Value b = pop(), a = pop();
                if (isString(a) || isString(b)) {
                    // Строка с чем угодно: "x = " + 5, 3 + " шт.", "a" + "b" — второй операнд превращается в
                    // строку так же, как в print.
                    push(Value(std::make_shared<std::string>(valueToDisplayString(a) + valueToDisplayString(b))));
                } else if (isArray(a) && isArray(b)) {
                    ObjArray* left = std::get<ObjArray*>(a);
                    ObjArray* right = std::get<ObjArray*>(b);
                    if (!left || !right) {
                        runtimeError(frame, "сложение массивов с null");
                        break;
                    }
                    std::vector<Value> joined = left->elements;
                    joined.insert(joined.end(), right->elements.begin(), right->elements.end());
                    push(Value(allocateArray(std::move(joined))));
                    maybeCollectGarbage();
                } else if (isInt(a) && isInt(b)) {
                    push(Value(std::get<long long>(a) + std::get<long long>(b)));
                } else {
                    push(Value(toDouble(a) + toDouble(b)));
                }
                frame.ip++;
                break;
            }
            case OpCode::Sub: {
                Value b = pop(), a = pop();
                if (isInt(a) && isInt(b)) push(Value(std::get<long long>(a) - std::get<long long>(b)));
                else push(Value(toDouble(a) - toDouble(b)));
                frame.ip++;
                break;
            }
            case OpCode::Mul: {
                Value b = pop(), a = pop();
                if ((isString(a) && isInt(b)) || (isInt(a) && isString(b))) {   // "ab" * 3
                    const std::string& text = *std::get<std::shared_ptr<std::string>>(isString(a) ? a : b);
                    long long times = std::get<long long>(isInt(a) ? a : b);
                    std::string out;
                    if (times > 0 && !text.empty()) {
                        if (static_cast<unsigned long long>(times) * text.size() > 100000000ull) {
                            runtimeError(frame, "результат повторения строки слишком большой");
                            break;
                        }
                        out.reserve(text.size() * static_cast<size_t>(times));
                        for (long long i = 0; i < times; i++) out += text;
                    }
                    push(Value(std::make_shared<std::string>(std::move(out))));
                } else if (isInt(a) && isInt(b)) push(Value(std::get<long long>(a) * std::get<long long>(b)));
                else push(Value(toDouble(a) * toDouble(b)));
                frame.ip++;
                break;
            }
            case OpCode::Div: {
                Value b = pop(), a = pop();
                if (isInt(a) && isInt(b)) {
                    if (std::get<long long>(b) == 0) {
                        runtimeError(frame, "деление на ноль");
                        break;
                    }
                    push(Value(std::get<long long>(a) / std::get<long long>(b)));
                } else {
                    push(Value(toDouble(a) / toDouble(b)));
                }
                frame.ip++;
                break;
            }
            case OpCode::Mod: {
                Value b = pop(), a = pop();
                if (!isInt(a) || !isInt(b)) {
                    runtimeError(frame, "'%' определён только для int");
                    break;
                }
                if (std::get<long long>(b) == 0) {
                    runtimeError(frame, "деление по модулю на ноль");
                    break;
                }
                push(Value(std::get<long long>(a) % std::get<long long>(b)));
                frame.ip++;
                break;
            }
            case OpCode::Neg: {
                Value a = pop();
                if (isInt(a)) push(Value(-std::get<long long>(a)));
                else push(Value(-toDouble(a)));
                frame.ip++;
                break;
            }
            case OpCode::Not: {
                Value a = pop();
                push(Value(!(isBool(a) && std::get<bool>(a))));
                frame.ip++;
                break;
            }

            case OpCode::Eq: {
                Value b = pop(), a = pop();
                push(Value(valuesEqual(a, b)));
                frame.ip++;
                break;
            }
            case OpCode::NotEq: {
                Value b = pop(), a = pop();
                push(Value(!valuesEqual(a, b)));
                frame.ip++;
                break;
            }
            case OpCode::Lt: {
                Value b = pop(), a = pop();
                if (isString(a) && isString(b)) {   // лексикографически: байты UTF-8 упорядочены как символы
                    push(Value(*std::get<std::shared_ptr<std::string>>(a) < *std::get<std::shared_ptr<std::string>>(b)));
                } else {
                    push(Value(toDouble(a) < toDouble(b)));
                }
                frame.ip++;
                break;
            }
            case OpCode::Gt: {
                Value b = pop(), a = pop();
                if (isString(a) && isString(b)) {   // лексикографически: байты UTF-8 упорядочены как символы
                    push(Value(*std::get<std::shared_ptr<std::string>>(a) > *std::get<std::shared_ptr<std::string>>(b)));
                } else {
                    push(Value(toDouble(a) > toDouble(b)));
                }
                frame.ip++;
                break;
            }
            case OpCode::LtEq: {
                Value b = pop(), a = pop();
                if (isString(a) && isString(b)) {   // лексикографически: байты UTF-8 упорядочены как символы
                    push(Value(*std::get<std::shared_ptr<std::string>>(a) <= *std::get<std::shared_ptr<std::string>>(b)));
                } else {
                    push(Value(toDouble(a) <= toDouble(b)));
                }
                frame.ip++;
                break;
            }
            case OpCode::GtEq: {
                Value b = pop(), a = pop();
                if (isString(a) && isString(b)) {   // лексикографически: байты UTF-8 упорядочены как символы
                    push(Value(*std::get<std::shared_ptr<std::string>>(a) >= *std::get<std::shared_ptr<std::string>>(b)));
                } else {
                    push(Value(toDouble(a) >= toDouble(b)));
                }
                frame.ip++;
                break;
            }

            case OpCode::Jump:
                frame.ip = static_cast<size_t>(instr.operand);
                break;

            case OpCode::JumpIfFalse: {
                Value cond = pop();
                bool isFalse = !(isBool(cond) && std::get<bool>(cond));
                frame.ip = isFalse ? static_cast<size_t>(instr.operand) : frame.ip + 1;
                break;
            }
            case OpCode::JumpIfFalseOrPop: {
                bool truthy = isBool(peek()) && std::get<bool>(peek());
                if (!truthy) {
                    frame.ip = static_cast<size_t>(instr.operand);
                } else {
                    pop();
                    frame.ip++;
                }
                break;
            }
            case OpCode::JumpIfTrueOrPop: {
                bool truthy = isBool(peek()) && std::get<bool>(peek());
                if (truthy) {
                    frame.ip = static_cast<size_t>(instr.operand);
                } else {
                    pop();
                    frame.ip++;
                }
                break;
            }

            case OpCode::Call: {
                if (frames_.size() >= kMaxCallDepth) {
                    runtimeError(frame, "переполнение стека вызовов (глубина больше " + std::to_string(kMaxCallDepth) + ")");
                    break;
                }
                const CompiledFunction& callee = program_.functions[static_cast<size_t>(instr.operand)];
                size_t newBase = stack_.size() - static_cast<size_t>(callee.paramCount);
                size_t returnIp = frame.ip + 1;  // считаем ДО push_back — frame может инвалидироваться
                for (int i = callee.paramCount; i < callee.localCount; i++) push(Value{std::monostate{}});
                frames_.push_back(Frame{&callee, returnIp, newBase, 0});
                break;
            }
            case OpCode::CallVirtual: {
                if (frames_.size() >= kMaxCallDepth) {
                    runtimeError(frame, "переполнение стека вызовов (глубина больше " + std::to_string(kMaxCallDepth) + ")");
                    break;
                }
                int argCount = instr.operand2;
                const std::string& methodName =
                    *std::get<std::shared_ptr<std::string>>(frame.fn->constants[static_cast<size_t>(instr.operand)]);
                const Value& receiverVal = stack_[stack_.size() - 1 - static_cast<size_t>(argCount)];
                if (!isObject(receiverVal) || std::get<ObjInstance*>(receiverVal) == nullptr) {
                    runtimeError(frame, "виртуальный вызов '" + methodName + "' на null");
                    break;
                }
                ObjInstance* obj = std::get<ObjInstance*>(receiverVal);
                auto slotIt = obj->classInfo->vtable.find(methodName);
                if (slotIt == obj->classInfo->vtable.end()) {
                    runtimeError(frame, "у класса '" + obj->classInfo->name + "' нет метода '" + methodName + "'");
                    break;
                }
                const CompiledFunction& callee = program_.functions[static_cast<size_t>(slotIt->second)];
                size_t newBase = stack_.size() - static_cast<size_t>(argCount) - 1;  // -1: receiver = this = слот 0
                size_t returnIp = frame.ip + 1;
                for (int i = callee.paramCount; i < callee.localCount; i++) push(Value{std::monostate{}});
                frames_.push_back(Frame{&callee, returnIp, newBase, 0});
                break;
            }
            case OpCode::Return: {
                Value retVal = pop();
                size_t callerBase = frame.stackBase;
                size_t returnIp = frame.returnIp;
                frames_.pop_back();  // frame отсюда невалиден
                stack_.resize(callerBase);
                if (frames_.empty()) {
                    return StepOutcome::Finished;
                }
                frames_.back().ip = returnIp;
                push(std::move(retVal));
                break;
            }

            case OpCode::Throw: {
                Value exceptionValue = pop();
                if (!isObject(exceptionValue) || std::get<ObjInstance*>(exceptionValue) == nullptr) {
                    runtimeError(frame, "throw ожидает объект, не null");
                    break;
                }
                if (!unwindToHandler(exceptionValue)) {
                    // Не std::move выше — exceptionValue ещё нужен для
                    // сообщения об ошибке. unwindToHandler уже мог
                    // развернуть все фреймы в поисках обработчика — frame
                    // здесь больше не валиден, поэтому сообщение строим
                    // без него.
                    hadRuntimeError_ = true;
                    errors_.push_back(describeUncaught(exceptionValue));
                }
                // Ни в случае успеха (frames_.back().ip уже указывает на
                // обработчик), ни в случае неудачи (цикл остановится по
                // hadRuntimeError_) НЕ делаем frame.ip++ — обе ветки уже
                // сами решили, что дальше.
                break;
            }

            case OpCode::NewInstance: {
                const ClassInfo& info = program_.classes[static_cast<size_t>(instr.operand)];
                ObjInstance* obj = allocateInstance(info);
                push(Value(obj));       // объект теперь на стеке — виден как корень
                maybeCollectGarbage();  // безопасно проверять/собирать только теперь
                frame.ip++;
                break;
            }
            case OpCode::LoadField: {
                Value objVal = pop();
                if (!isObject(objVal) || std::get<ObjInstance*>(objVal) == nullptr) {
                    runtimeError(frame, "обращение к полю на null");
                    break;
                }
                ObjInstance* obj = std::get<ObjInstance*>(objVal);
                push(obj->fields[static_cast<size_t>(instr.operand)]);
                frame.ip++;
                break;
            }
            case OpCode::StoreField: {
                Value value = pop();
                Value objVal = pop();
                if (!isObject(objVal) || std::get<ObjInstance*>(objVal) == nullptr) {
                    runtimeError(frame, "присваивание полю на null");
                    break;
                }
                ObjInstance* obj = std::get<ObjInstance*>(objVal);
                obj->fields[static_cast<size_t>(instr.operand)] = value;
                push(std::move(value));  // присваивание — выражение, значение остаётся на стеке
                frame.ip++;
                break;
            }

            case OpCode::NewArray: {
                size_t n = static_cast<size_t>(instr.operand);
                std::vector<Value> elems(stack_.end() - static_cast<std::ptrdiff_t>(n), stack_.end());
                stack_.resize(stack_.size() - n);
                ObjArray* arr = allocateArray(std::move(elems));
                push(Value(arr));       // массив теперь на стеке — виден как корень
                maybeCollectGarbage();  // безопасно проверять/собирать только теперь
                frame.ip++;
                break;
            }
            case OpCode::LoadIndex: {
                Value indexVal = pop();
                Value arrVal = pop();
                long long idx = std::get<long long>(indexVal);  // тип уже гарантирован BodyChecker
                if (isString(arrVal)) {
                    // s[i] — строка из одного СИМВОЛА (не байта); отрицательный индекс — от конца, как в Python
                    const std::string& text = *std::get<std::shared_ptr<std::string>>(arrVal);
                    const bool ascii = utf8::isAscii(text);
                    long long n = ascii ? static_cast<long long>(text.size()) : static_cast<long long>(utf8::length(text));
                    long long real = idx < 0 ? idx + n : idx;
                    if (real < 0 || real >= n) {
                        runtimeError(frame, "индекс " + std::to_string(idx) + " вне границ строки (длина " + std::to_string(n) + ")");
                        break;
                    }
                    if (ascii) {
                        push(Value(std::make_shared<std::string>(1, text[static_cast<size_t>(real)])));
                    } else {
                        auto b = utf8::boundaries(text);
                        push(Value(std::make_shared<std::string>(text.substr(b[static_cast<size_t>(real)],
                                                                             b[static_cast<size_t>(real) + 1] - b[static_cast<size_t>(real)]))));
                    }
                    frame.ip++;
                    break;
                }
                if (!isArray(arrVal) || std::get<ObjArray*>(arrVal) == nullptr) {
                    runtimeError(frame, "индексация на null");
                    break;
                }
                ObjArray* arr = std::get<ObjArray*>(arrVal);
                long long real = idx < 0 ? idx + static_cast<long long>(arr->elements.size()) : idx;   // xs[-1] — последний
                if (real < 0 || static_cast<size_t>(real) >= arr->elements.size()) {
                    runtimeError(frame, "индекс " + std::to_string(idx) + " вне границ массива (длина " +
                                             std::to_string(arr->elements.size()) + ")");
                    break;
                }
                push(arr->elements[static_cast<size_t>(real)]);
                frame.ip++;
                break;
            }
            case OpCode::StoreIndex: {
                Value value = pop();
                Value indexVal = pop();
                Value arrVal = pop();
                if (!isArray(arrVal) || std::get<ObjArray*>(arrVal) == nullptr) {
                    runtimeError(frame, "присваивание элементу на null");
                    break;
                }
                ObjArray* arr = std::get<ObjArray*>(arrVal);
                long long idx = std::get<long long>(indexVal);
                long long real = idx < 0 ? idx + static_cast<long long>(arr->elements.size()) : idx;
                if (real < 0 || static_cast<size_t>(real) >= arr->elements.size()) {
                    runtimeError(frame, "индекс " + std::to_string(idx) + " вне границ массива (длина " +
                                             std::to_string(arr->elements.size()) + ")");
                    break;
                }
                arr->elements[static_cast<size_t>(real)] = value;
                push(std::move(value));  // присваивание — выражение, значение остаётся на стеке
                frame.ip++;
                break;
            }
            case OpCode::Slice:
                if (!doSlice(frame, instr.operand)) break;
                frame.ip++;
                break;

            case OpCode::CallBuiltin:
                if (!callBuiltin(frame, instr.operand, instr.operand2)) break;
                frame.ip++;
                break;

            case OpCode::ArrayLen: {
                Value arrVal = pop();
                if (isString(arrVal)) {
                    const std::string& text = *std::get<std::shared_ptr<std::string>>(arrVal);
                    push(Value(static_cast<long long>(utf8::isAscii(text) ? text.size() : utf8::length(text))));
                    frame.ip++;
                    break;
                }
                if (!isArray(arrVal) || std::get<ObjArray*>(arrVal) == nullptr) {
                    runtimeError(frame, "длина массива на null");
                    break;
                }
                push(Value(static_cast<long long>(std::get<ObjArray*>(arrVal)->elements.size())));
                frame.ip++;
                break;
            }

            case OpCode::Layout: {
                Value targetVal = pop();
                if (!isObject(targetVal) || std::get<ObjInstance*>(targetVal) == nullptr) {
                    runtimeError(frame, "~layout на null");
                    break;
                }
                const ClassInfo& targetClass = *std::get<ObjInstance*>(targetVal)->classInfo;

                std::vector<Value> names, offsets, sizes;
                const auto layout = layoutOf(targetClass);
                names.reserve(layout.size());
                offsets.reserve(layout.size());
                sizes.reserve(layout.size());
                for (const auto& f : layout) {
                    names.push_back(Value(std::make_shared<std::string>(f.name)));
                    offsets.push_back(Value(static_cast<long long>(f.offset)));
                    sizes.push_back(Value(static_cast<long long>(f.size)));
                }
                ObjArray* namesArr = allocateArray(std::move(names));
                ObjArray* offsetsArr = allocateArray(std::move(offsets));
                ObjArray* sizesArr = allocateArray(std::move(sizes));

                const ClassInfo& layoutInfoClass = program_.classes[static_cast<size_t>(instr.operand)];
                ObjInstance* info = allocateInstance(layoutInfoClass);
                push(Value(info));  // на стеке — сам себе корень, дальше можно вызывать GC
                setField(info, layoutInfoClass, "className", Value(std::make_shared<std::string>(targetClass.name)));
                setField(info, layoutInfoClass, "fieldNames", Value(namesArr));
                setField(info, layoutInfoClass, "fieldOffsets", Value(offsetsArr));
                setField(info, layoutInfoClass, "fieldSizes", Value(sizesArr));

                maybeCollectGarbage();
                frame.ip++;
                break;
            }

            case OpCode::Vtable: {
                Value targetVal = pop();
                if (!isObject(targetVal) || std::get<ObjInstance*>(targetVal) == nullptr) {
                    runtimeError(frame, "~vtable на null");
                    break;
                }
                const ClassInfo& targetClass = *std::get<ObjInstance*>(targetVal)->classInfo;

                // Сортировка по имени и вычисление реализующего класса — в
                // vtableOf(): общий код с инспектором C API.
                const auto entries = vtableOf(targetClass);
                std::vector<Value> names, implementors;
                names.reserve(entries.size());
                implementors.reserve(entries.size());
                for (const auto& e : entries) {
                    names.push_back(Value(std::make_shared<std::string>(e.method)));
                    implementors.push_back(Value(std::make_shared<std::string>(e.implementedBy)));
                }
                ObjArray* namesArr = allocateArray(std::move(names));
                ObjArray* implArr = allocateArray(std::move(implementors));

                const ClassInfo& vtableInfoClass = program_.classes[static_cast<size_t>(instr.operand)];
                ObjInstance* info = allocateInstance(vtableInfoClass);
                push(Value(info));
                setField(info, vtableInfoClass, "className", Value(std::make_shared<std::string>(targetClass.name)));
                setField(info, vtableInfoClass, "methodNames", Value(namesArr));
                setField(info, vtableInfoClass, "implementingClasses", Value(implArr));

                maybeCollectGarbage();
                frame.ip++;
                break;
            }

            case OpCode::Address: {
                Value targetVal = pop();
                if (!isObject(targetVal) || std::get<ObjInstance*>(targetVal) == nullptr) {
                    runtimeError(frame, "~address на null");
                    break;
                }
                // grammar.md, раздел 8: тип ~address — int (см. BodyChecker::
                // visit(IntrospectFieldExpr&)) — decimal, не hex, это тот же
                // адрес, что и в "<object @...>" (Value.cpp), просто в виде
                // числа, которым можно оперировать в программе.
                auto address = reinterpret_cast<uintptr_t>(std::get<ObjInstance*>(targetVal));
                push(Value(static_cast<long long>(address)));
                frame.ip++;
                break;
            }

            case OpCode::Heap: {
                // Снимок строим ДО аллокации собственных объектов снимка —
                // иначе HeapSnapshot описывал бы сам себя. Массивы в куче
                // тоже перечисляем — они такие же объекты под тем же GC
                // (см. bytecode/Obj.h), просто без имени класса.
                std::vector<Value> classNames, addresses;
                for (ObjInstance* cur = allAllocations_; cur; cur = cur->nextAllocated) {
                    classNames.push_back(Value(std::make_shared<std::string>(cur->classInfo->name)));
                    addresses.push_back(Value(static_cast<long long>(reinterpret_cast<uintptr_t>(cur))));
                }
                for (ObjArray* cur = allAllocationsArrays_; cur; cur = cur->nextAllocated) {
                    classNames.push_back(Value(std::make_shared<std::string>("[]")));
                    addresses.push_back(Value(static_cast<long long>(reinterpret_cast<uintptr_t>(cur))));
                }
                long long count = static_cast<long long>(classNames.size());
                ObjArray* namesArr = allocateArray(std::move(classNames));
                ObjArray* addrArr = allocateArray(std::move(addresses));

                const ClassInfo& heapSnapshotClass = program_.classes[static_cast<size_t>(instr.operand)];
                ObjInstance* info = allocateInstance(heapSnapshotClass);
                push(Value(info));
                setField(info, heapSnapshotClass, "objectCount", Value(count));
                setField(info, heapSnapshotClass, "classNames", Value(namesArr));
                setField(info, heapSnapshotClass, "addresses", Value(addrArr));

                maybeCollectGarbage();
                frame.ip++;
                break;
            }

            case OpCode::Gc: {
                collectGarbageNow();  // немедленно, не по порогу — тут это и есть весь смысл ~gc
                push(Value(std::monostate{}));  // ~gc: void (см. BodyChecker) — конвенция "void кладёт null", как Print
                frame.ip++;
                break;
            }

            case OpCode::CheckCast: {
                const Value& top = peek();
                if (isObject(top) && std::get<ObjInstance*>(top) != nullptr) {
                    ObjInstance* obj = std::get<ObjInstance*>(top);
                    const std::string& target =
                        *std::get<std::shared_ptr<std::string>>(frame.fn->constants[static_cast<size_t>(instr.operand)]);
                    if (!exceptionIsInstanceOf(top, target)) {   // та же проверка «чем является объект», что и у catch
                        runtimeError(frame, "нельзя привести объект класса '" + obj->classInfo->name + "' к '" + target + "'");
                        break;
                    }
                }
                frame.ip++;
                break;
            }

            case OpCode::ObjToString: {
                const Value& top = peek();
                if (isObject(top) && std::get<ObjInstance*>(top) != nullptr) {
                    ObjInstance* obj = std::get<ObjInstance*>(top);
                    auto slotIt = obj->classInfo->vtable.find("toString");
                    if (slotIt != obj->classInfo->vtable.end()) {
                        const CompiledFunction& callee = program_.functions[static_cast<size_t>(slotIt->second)];
                        if (callee.paramCount == 1) {   // только this: toString() без параметров
                            if (frames_.size() >= kMaxCallDepth) {
                                runtimeError(frame, "переполнение стека вызовов (глубина больше " + std::to_string(kMaxCallDepth) + ")");
                                break;
                            }
                            size_t newBase = stack_.size() - 1;   // receiver = this = слот 0, как у CallVirtual
                            size_t returnIp = frame.ip + 1;
                            for (int i = callee.paramCount; i < callee.localCount; i++) push(Value{std::monostate{}});
                            frames_.push_back(Frame{&callee, returnIp, newBase, 0});
                            break;
                        }
                    }
                }
                frame.ip++;
                break;
            }

            case OpCode::Print: {
                size_t argc = static_cast<size_t>(instr.operand);
                std::vector<Value> printed(stack_.end() - static_cast<std::ptrdiff_t>(argc), stack_.end());
                stack_.resize(stack_.size() - argc);
                std::string text;
                for (size_t i = 0; i < printed.size(); i++) {
                    if (i > 0) text += " ";
                    text += valueToDisplayString(printed[i]);
                }
                if (echoOutput_) std::cout << text << "\n";
                output_.push_back(text);
                push(Value{std::monostate{}});
                frame.ip++;
                break;
            }

            case OpCode::Halt:
                return StepOutcome::Finished;
        }
    return StepOutcome::Continue;
}

} // namespace glass
