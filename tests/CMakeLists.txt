add_executable(lexer_tests lexer_tests.cpp)
target_link_libraries(lexer_tests PRIVATE glass_lexer)

add_test(NAME lexer_tests COMMAND lexer_tests)

add_executable(parser_tests parser_tests.cpp)
target_link_libraries(parser_tests PRIVATE glass_lexer glass_ast glass_parser)

add_test(NAME parser_tests COMMAND parser_tests)

add_executable(sema_tests sema_tests.cpp)
target_link_libraries(sema_tests PRIVATE glass_lexer glass_ast glass_parser glass_sema)

add_test(NAME sema_tests COMMAND sema_tests)

add_executable(body_checker_tests body_checker_tests.cpp)
target_link_libraries(body_checker_tests PRIVATE glass_lexer glass_ast glass_parser glass_sema)

add_test(NAME body_checker_tests COMMAND body_checker_tests)

add_executable(vm_tests vm_tests.cpp)
target_link_libraries(vm_tests PRIVATE glass_lexer glass_ast glass_parser glass_sema glass_codegen glass_vm)

add_test(NAME vm_tests COMMAND vm_tests)

add_executable(object_tests object_tests.cpp)
target_link_libraries(object_tests PRIVATE glass_lexer glass_ast glass_parser glass_sema glass_codegen glass_vm)

add_test(NAME object_tests COMMAND object_tests)

add_executable(exception_tests exception_tests.cpp)
target_link_libraries(exception_tests PRIVATE glass_lexer glass_ast glass_parser glass_sema glass_codegen glass_vm glass_prelude)

add_test(NAME exception_tests COMMAND exception_tests)

add_executable(introspection_tests introspection_tests.cpp)
target_link_libraries(introspection_tests PRIVATE glass_lexer glass_ast glass_parser glass_sema glass_codegen glass_vm glass_prelude)

add_test(NAME introspection_tests COMMAND introspection_tests)

add_executable(debug_tests debug_tests.cpp)
target_link_libraries(debug_tests PRIVATE glass_lexer glass_ast glass_parser glass_sema glass_codegen glass_vm glass_prelude)

add_test(NAME debug_tests COMMAND debug_tests)

find_package(Threads REQUIRED)  # тест glass_request_stop гоняет glass_run в отдельном потоке
add_executable(capi_tests capi_tests.cpp)
target_link_libraries(capi_tests PRIVATE glass_capi Threads::Threads)

add_test(NAME capi_tests COMMAND capi_tests)

add_executable(language_tests language_tests.cpp)
target_link_libraries(language_tests PRIVATE glass_lexer glass_ast glass_parser glass_sema glass_codegen glass_vm glass_prelude)

add_test(NAME language_tests COMMAND language_tests)
