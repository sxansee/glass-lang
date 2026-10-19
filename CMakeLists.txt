cmake_minimum_required(VERSION 3.16)
project(Glass VERSION 1.0.0 LANGUAGES CXX)

# Строгие предупреждения: проект собирается без единого (проверено на gcc с -Wall -Wextra).
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
endif()

if(MSVC)
    # Исходники — UTF-8 (русские сообщения компилятора лежат в строковых литералах): без /utf-8 MSVC читает
    # их в ANSI-кодировке системы, и кириллица превращается в мусор.
    add_compile_options(/utf-8)
    # Статическая среда выполнения C++: glass_capi.dll и glassc.exe не требуют «Visual C++ Redistributable»,
    # поэтому zip-релиз самодостаточен.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Нужно всем статическим библиотекам, которые в итоге линкуются в
# glass_capi (SHARED, ниже) — без -fPIC статические объектники нельзя
# встроить в .so/.dll. Разница в размере/скорости для этого проекта не
# важна — проще выставить один раз глобально, чем на каждой цели отдельно.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

add_library(glass_lexer
    src/lexer/Lexer.cpp
)
target_include_directories(glass_lexer PUBLIC src)

add_library(glass_ast
    src/ast/Type.cpp
    src/ast/Expr.cpp
    src/ast/AstPrinter.cpp
)
target_include_directories(glass_ast PUBLIC src)

add_library(glass_parser
    src/parser/Parser.cpp
)
target_include_directories(glass_parser PUBLIC src)
target_link_libraries(glass_parser PUBLIC glass_lexer glass_ast)

add_library(glass_sema
    src/sema/Sema.cpp
    src/sema/BodyChecker.cpp
)
target_include_directories(glass_sema PUBLIC src)
target_link_libraries(glass_sema PUBLIC glass_ast)

add_library(glass_bytecode
    src/bytecode/Value.cpp
)
target_include_directories(glass_bytecode PUBLIC src)

add_library(glass_codegen
    src/codegen/Compiler.cpp
)
target_include_directories(glass_codegen PUBLIC src)
target_link_libraries(glass_codegen PUBLIC glass_ast glass_bytecode)

add_library(glass_vm
    src/vm/VM.cpp src/vm/VMBuiltins.cpp
)
target_include_directories(glass_vm PUBLIC src)
target_link_libraries(glass_vm PUBLIC glass_bytecode)

add_library(glass_repl
    src/repl/Repl.cpp
)
target_include_directories(glass_repl PUBLIC src)
target_link_libraries(glass_repl PUBLIC glass_lexer glass_ast glass_parser glass_sema glass_codegen glass_vm glass_prelude)

add_library(glass_prelude
    src/prelude/Prelude.cpp
)
target_include_directories(glass_prelude PUBLIC src)
target_link_libraries(glass_prelude PUBLIC glass_lexer glass_ast glass_parser)

# Общий C API (extern "C") для встраивания Glass в другой процесс — в
# первую очередь в WPF через P/Invoke (см. README «Что дальше» и
# src/capi/GlassApi.h). SHARED — на Windows это .dll, здесь (сборка и
# тесты ведутся на Linux) — .so; исходник один и тот же.
add_library(glass_capi SHARED
    src/capi/GlassApi.cpp
)
target_include_directories(glass_capi PUBLIC src)
target_link_libraries(glass_capi PRIVATE glass_lexer glass_ast glass_parser glass_sema glass_codegen glass_vm glass_prelude)
target_compile_definitions(glass_capi PRIVATE GLASS_CAPI_BUILDING)

add_executable(glassc src/main.cpp)
target_link_libraries(glassc PRIVATE glass_lexer glass_ast glass_parser glass_sema glass_codegen glass_vm glass_repl glass_prelude)

enable_testing()
add_subdirectory(tests)
