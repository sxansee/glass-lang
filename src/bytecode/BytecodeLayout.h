#pragma once
// Размеры общих структур байткода и номера ключевых опкодов — для сверки раскладки между единицами трансляции
// (см. util/LayoutCheck.h). Номера опкодов нужны потому, что вставка нового опкода в середину перечисления сдвигает
// все следующие: компилятор и VM, собранные по разным версиям Chunk.h, понимали бы один байт по-разному.

#include "Chunk.h"
#include "ClassInfo.h"
#include "Obj.h"
#include "Value.h"
#include "../util/LayoutCheck.h"

#define GLASS_BYTECODE_LAYOUT_VALUES                                                                                    \
    sizeof(::glass::Value), sizeof(::glass::Instruction), sizeof(::glass::ExceptionHandler),                           \
    sizeof(::glass::CompiledFunction), sizeof(::glass::StaticSlot), sizeof(::glass::CompiledProgram),                  \
    sizeof(::glass::FieldSlot), sizeof(::glass::ClassInfo), sizeof(::glass::ObjInstance), sizeof(::glass::ObjArray),   \
    static_cast<unsigned long long>(::glass::OpCode::PushConst), static_cast<unsigned long long>(::glass::OpCode::Call), \
    static_cast<unsigned long long>(::glass::OpCode::StoreField), static_cast<unsigned long long>(::glass::OpCode::LoadStatic), \
    static_cast<unsigned long long>(::glass::OpCode::StoreStatic), static_cast<unsigned long long>(::glass::OpCode::Return), \
    static_cast<unsigned long long>(::glass::OpCode::Halt)

#define GLASS_DEFINE_BYTECODE_LAYOUT(functionName) \
    unsigned long long glass::functionName() { return ::glass::layoutOf({GLASS_BYTECODE_LAYOUT_VALUES}); }
